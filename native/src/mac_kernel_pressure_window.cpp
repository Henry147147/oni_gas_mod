// Nonlinear pressure windows with conditional fine-cell evolution. Physical
// face paths remain shared; a masked pressure graph is only a preconditioner.
#include "mac_kernel_pressure_window.hpp"
#include "mac_kernel_detail.hpp"
#include "mac_kernel_time.hpp"
#include <numeric>

namespace ma::experimental::mac_kernel {
namespace {
struct Trace { double p0 = 0, mean = 0, first = 0, du = 0; };
bool anchor_at(const Model& model, const Fields& source, std::size_t N, std::size_t i,
    const Stats& stats, double U, double V, Anchor& result) {
    State state;
    if (!evaluate_state(model, source, N, i, stats, U, V, state)) return false;
    Anchor a; a.kind = stats.kind; a.V0 = V; a.p0 = state.p; a.T0 = state.T; a.Vg0 = state.Vg;
    a.Cv = stats.Cv; a.nR = stats.nR; a.k = stats.nR / stats.Cv; a.comp = state.comp;
    if (a.kind == liquid_cell) {
        std::size_t count = 0, which = 0;
        for (const auto k : model.liquids) if (source.m[k * N + i] != 0) { ++count; which = k; }
        if (count == 1) {
            const auto& sp = model.species[which]; a.single_liquid = true;
            a.K = sp.bulk; a.Nl = source.m[which * N + i] / sp.rho0; a.qmax = sp.maximum_ratio - 1;
        }
    }
    if (!(a.comp > 0) || !std::isfinite(a.comp)) return false;
    result = a; return true;
}
bool trace_at(const Model& model, const Fields& source, std::size_t N, std::size_t i,
    const Anchor& anchor, double a, double b, Trace& result) {
    const double root3 = std::sqrt(3.);
    if (!std::isfinite(a) || !std::isfinite(b) || !(anchor.V0 + a > 0)) return false;
    if (b > 0) {
        const double tau = .5 - (a / b) / (2 * root3);
        if (tau > 0 && tau < 1 && !(anchor.V0 + tau * a + root3 * (tau * tau - tau) * b > 0)) return false;
    }
    const double inner = std::sqrt((3 - 2 * std::sqrt(6. / 5)) / 7), outer = std::sqrt((3 + 2 * std::sqrt(6. / 5)) / 7);
    const std::array<double, 4> nodes{.5 * (1 - outer), .5 * (1 - inner), .5 * (1 + inner), .5 * (1 + outer)};
    const std::array<double, 4> weights{(18 - std::sqrt(30.)) / 72, (18 + std::sqrt(30.)) / 72,
        (18 + std::sqrt(30.)) / 72, (18 - std::sqrt(30.)) / 72};
    Work endpoint;
    if (!isentropic_work(model, source, N, i, anchor, a, endpoint)) return false;
    DD mean, first;
    for (std::size_t k = 0; k < nodes.size(); ++k) {
        const double tau = nodes[k], volume = tau * a + root3 * (tau * tau - tau) * b; Work w;
        if (!isentropic_work(model, source, N, i, anchor, volume, w)) return false;
        const double dp = w.dW + volume * w.derivative;
        mean.add(weights[k] * dp); first.add(weights[k] * root3 * (2 * tau - 1) * dp);
    }
    double X = mean.value(), Y = first.value();
    if (std::abs(a) >= std::abs(b)) { if (a != 0) X = endpoint.dW - (b / a) * Y; }
    else Y = (a / b) * (endpoint.dW - X);
    if (!std::isfinite(X) || !std::isfinite(Y) || !std::isfinite(endpoint.dU)) return false;
    result = {anchor.p0, X, Y, endpoint.dU}; return true;
}
struct Face { Index left = none, right = none, index = 0; bool vertical = false, movable = false; double inertia = 0, body = 0; };
struct MatrixData { std::vector<double> comp, diagonal, boundary, wx, wy; };
void executor(const void* context, std::size_t blocks, interval_detail::BlockFunction fn, const void* job) {
    if (blocks < 32) { for (std::size_t b = 0; b < blocks; ++b) fn(job, b); }
    else static_cast<Pool*>(const_cast<void*>(context))->run(blocks, [&](std::size_t b) { fn(job, b); });
}
}

struct PressureTimeWorkspace::Impl {
    std::unique_ptr<PressureMomentMultigrid> coarse_inverse, fine_inverse, fine_linear_inverse;
    MatrixData coarse_matrix, fine_matrix, fine_linear_matrix;
    struct Run;
};
PressureTimeWorkspace::PressureTimeWorkspace() : impl_(std::make_unique<Impl>()) {}
PressureTimeWorkspace::~PressureTimeWorkspace() = default;
std::size_t PressureTimeWorkspace::storage_bytes() const noexcept {
    std::size_t result = sizeof(Impl);
    for (const auto* inverse : {&impl_->coarse_inverse, &impl_->fine_inverse, &impl_->fine_linear_inverse})
        if (*inverse) result += (*inverse)->storage_bytes();
    for (const auto* matrix : {&impl_->coarse_matrix, &impl_->fine_matrix, &impl_->fine_linear_matrix})
        for (const auto* field : {&matrix->comp, &matrix->diagonal, &matrix->boundary, &matrix->wx, &matrix->wy}) result += field->capacity() * sizeof(double);
    return result;
}

struct PressureTimeWorkspace::Impl::Run {
    Impl& workspace;
    const Model& model; const Fields& source; const MacGrid& grid; const PressureTimeOptions& options; Pool& pool;
    Grid geometry;
    std::vector<Stats> stats;
    std::vector<Anchor> original;
    std::vector<Index> pressure_cells, coarse, fine, coarse_faces, fine_faces;
    std::vector<Index> coarse_position, fine_position;
    std::vector<std::uint8_t> occupied, is_fine;
    std::vector<Face> faces;
    std::vector<double> ra, rb, da, db;
    PressureTimeReport counters;
    std::uint32_t pieces;
    double local_seconds;

    Run(Impl& cache, const Model& m, const Fields& f, const MacGrid& g, std::span<const std::uint8_t> mask,
        const PressureTimeOptions& opt, Pool& workers)
        : workspace(cache), model(m), source(f), grid(g), options(opt), pool(workers), geometry(g) {
        const auto N = geometry.N;
        require(mask.size() == N && source.U.size() == N && source.m.size() == model.K() * N &&
            source.jx.size() == geometry.NX && source.jy.size() == geometry.NY &&
            opt.seconds > 0 && std::isfinite(opt.seconds) && std::isfinite(opt.gravity) && opt.fine_level <= 20 &&
            opt.maximum_outgoing_fraction > 0 && opt.maximum_outgoing_fraction <= .45, "Invalid pressure time window");
        require(opt.gravity == 0 || !grid.periodic_y, "Constant pressure-window gravity requires nonperiodic vertical boundaries");
        pieces = std::uint32_t{1} << opt.fine_level; local_seconds = std::ldexp(opt.seconds, -static_cast<int>(opt.fine_level));
        require(std::isnormal(local_seconds), "Pressure time window requires a normal local duration");
        stats.resize(N); original.resize(N); occupied.resize(N); is_fine.resize(N);
        for (const auto mass : source.m) require(mass >= 0 && std::isfinite(mass), "Invalid pressure time mass");
        for (const auto energy : source.U) require(std::isfinite(energy), "Invalid pressure time energy");
        for (const auto* momentum : {&source.jx, &source.jy}) for (const auto value : *momentum)
            require(std::isfinite(value), "Invalid pressure time momentum");
        for (std::size_t i = 0; i < N; ++i) {
            require(mask[i] <= 1, "Invalid pressure time class");
            if (geometry.wall[i]) {
                require(source.U[i] == 0, "Pressure time solid contains energy");
                for (std::size_t species = 0; species < model.K(); ++species)
                    require(source.m[species * N + i] == 0, "Pressure time solid contains fluid");
                continue;
            }
            stats[i] = cell_stats(model, source, N, i); occupied[i] = stats[i].kind != empty ? 1 : 0;
            if (!occupied[i]) { require(source.U[i] == 0, "Pressure time vacuum contains energy"); continue; }
            if (!anchor_at(model, source, N, i, stats[i], source.U[i], geometry.V, original[i]))
                reject("Pressure time source is outside its normal EOS domain");
            pressure_cells.push_back(static_cast<Index>(i));
            is_fine[i] = mask[i]; (is_fine[i] ? fine : coarse).push_back(static_cast<Index>(i));
        }
        require(fine.size() <= options.cell_interval_budget / pieces &&
            coarse.size() <= options.cell_interval_budget - fine.size() * pieces, "Pressure time cell-interval budget exhausted");
        counters.coarse_cells = coarse.size(); counters.fine_cells = fine.size(); counters.cell_intervals = coarse.size() + fine.size() * pieces;
        coarse_position.assign(N, none); fine_position.assign(N, none);
        for (std::size_t j = 0; j < coarse.size(); ++j) coarse_position[coarse[j]] = static_cast<Index>(j);
        for (std::size_t j = 0; j < fine.size(); ++j) fine_position[fine[j]] = static_cast<Index>(j);
        faces.reserve(geometry.NX + geometry.NY);
        const auto append = [&](Index l, Index r, Index index, bool vertical) {
            const double ml = l == none ? 0 : stats[l].M, mr = r == none ? 0 : stats[r].M;
            const double inertia = .5 * ml + .5 * mr;
            if ((ml > 0 && .5 * ml == 0) || (mr > 0 && .5 * mr == 0) || !std::isfinite(inertia))
                throw ArithmeticRefinement{};
            const bool moving = l != none && r != none && inertia > 0;
            require(moving || (vertical ? source.jy : source.jx)[index] == 0, "Pressure time fixed face has momentum");
            const auto face = static_cast<Index>(faces.size());
            faces.push_back({l, r, index, vertical, moving, inertia, vertical ? -inertia * options.gravity : 0});
            if (inertia == 0) return;
            ((l != none && is_fine[l]) || (r != none && is_fine[r]) ? fine_faces : coarse_faces).push_back(face);
        };
        require(geometry.NX + geometry.NY < none, "Pressure time face count exceeds its index range");
        for (std::size_t j = 0; j < geometry.NX; ++j) append(geometry.xl[j], geometry.xr[j], static_cast<Index>(j), false);
        for (std::size_t j = 0; j < geometry.NY; ++j) append(geometry.yl[j], geometry.yr[j], static_cast<Index>(j), true);
        ra.resize(N); rb.resize(N); da.resize(N); db.resize(N);
    }

    static constexpr double tolerance = 16 * eps;
    static constexpr double k = 0.28867513459481288225;
    template<class F> void rows(std::size_t n, const F& fn) {
        const auto job = [&](std::size_t block) {
            for (auto j = block * 256; j < std::min(n, block * 256 + 256); ++j) fn(j);
        };
        const auto blocks = (n + 255) / 256;
        if (blocks < 32) for (std::size_t b = 0; b < blocks; ++b) job(b);
        else pool.run(blocks, job);
    }
    double dot(const std::vector<double>& a, const std::vector<double>& b) {
        std::vector<DD> sums((a.size() + 255) / 256);
        const auto partial = [&](std::size_t block) {
            // Independent DD chains shorten the dependency path without
            // changing the fixed blocks or their worker-independent merge.
            std::array<DD, 8> lanes{};
            const auto begin = block * 256, end = std::min(a.size(), begin + 256);
            auto j = begin;
            for (; end - j >= lanes.size(); j += lanes.size())
                for (std::size_t lane = 0; lane < lanes.size(); ++lane) lanes[lane].add(a[j + lane] * b[j + lane]);
            for (; j < end; ++j) lanes[(j - begin) % lanes.size()].add(a[j] * b[j]);
            for (const auto& lane : lanes) sums[block].add(lane);
        };
        if (sums.size() < 32) for (std::size_t block = 0; block < sums.size(); ++block) partial(block);
        else pool.run(sums.size(), partial);
        DD sum; for (const auto& part : sums) sum.add(part); return sum.value();
    }
    double norm(const std::vector<double>& a) {
        double scale = 0; for (const auto value : a) { if (!std::isfinite(value)) return std::numeric_limits<double>::infinity(); scale = std::max(scale, std::abs(value)); }
        if (scale == 0 || !std::isfinite(scale)) return scale;
        std::vector<double> v(a.size()); rows(a.size(), [&](std::size_t i) { v[i] = a[i] / scale; });
        return scale * std::sqrt(dot(v, v));
    }
    // Restarted right-preconditioned FGMRES. Each restart certifies the actual
    // operator residual; the Hessenberg estimate alone never publishes a root.
    template<class Apply, class Precondition>
    std::vector<double> krylov(const std::vector<double>& rhs, const Apply& apply, const Precondition& precondition,
        double relative, double absolute, bool outer) {
        constexpr std::size_t restart = 24, maximum = 384;
        const auto n = rhs.size(); std::vector<double> x(n), residual = rhs;
        const double target = std::max(absolute, relative * norm(rhs));
        for (std::size_t total = 0; total < maximum;) {
            const double beta = norm(residual);
            if (!std::isfinite(beta)) reject("Pressure window Krylov residual is nonfinite");
            if (beta <= target) return x;
            std::vector<std::vector<double>> v(1, residual), z;
            for (auto& value : v[0]) value /= beta;
            std::array<std::array<double, restart>, restart + 1> H{};
            std::array<double, restart> cs{}, sn{}; std::array<double, restart + 1> g{}; g[0] = beta;
            std::size_t columns = 0;
            for (; columns < restart && total < maximum; ++columns) {
                z.push_back(precondition(v[columns])); auto w = apply(z.back());
                // Two deterministic modified Gram-Schmidt passes avoid loss
                // of orthogonality at the small constitutive closure target.
                for (unsigned pass = 0; pass < 2; ++pass) for (std::size_t j = 0; j <= columns; ++j) {
                    const double h = dot(v[j], w); H[j][columns] += h;
                    rows(n, [&](std::size_t i) { w[i] -= h * v[j][i]; });
                }
                H[columns + 1][columns] = norm(w); const double next = H[columns + 1][columns];
                if (!std::isfinite(next)) reject("Pressure window Krylov direction is nonfinite");
                if (next > 0) { for (auto& value : w) value /= next; v.push_back(std::move(w)); }
                for (std::size_t j = 0; j < columns; ++j) {
                    const double h = cs[j] * H[j][columns] + sn[j] * H[j + 1][columns];
                    H[j + 1][columns] = -sn[j] * H[j][columns] + cs[j] * H[j + 1][columns]; H[j][columns] = h;
                }
                const double divisor = std::hypot(H[columns][columns], H[columns + 1][columns]);
                if (!(divisor > 0) || !std::isfinite(divisor)) reject("Pressure window Krylov breakdown");
                cs[columns] = H[columns][columns] / divisor; sn[columns] = H[columns + 1][columns] / divisor;
                H[columns][columns] = divisor; H[columns + 1][columns] = 0;
                g[columns + 1] = -sn[columns] * g[columns]; g[columns] *= cs[columns];
                ++total; if (outer) ++counters.outer_linear; else ++counters.fine_linear;
                if (std::abs(g[columns + 1]) <= target || next == 0) { ++columns; break; }
            }
            std::vector<double> y(columns);
            for (std::size_t i = columns; i-- > 0;) {
                DD value; value.add(g[i]); for (std::size_t j = i + 1; j < columns; ++j) value.add(-H[i][j] * y[j]);
                y[i] = value.value() / H[i][i];
            }
            rows(n, [&](std::size_t i) { DD value; value.add(x[i]); for (std::size_t j = 0; j < columns; ++j) value.add(y[j] * z[j][i]); x[i] = value.value(); });
            residual = apply(x); rows(n, [&](std::size_t i) { residual[i] = rhs[i] - residual[i]; });
        }
        reject("Pressure window Krylov budget exhausted");
    }
    void prepare(std::unique_ptr<PressureMomentMultigrid>& inverse, MatrixData& matrix,
        const std::vector<Index>& active, const std::vector<Anchor>& anchors, double seconds) {
        MacGrid graph = grid; std::fill(graph.walls.begin(), graph.walls.end(), 1);
        for (const auto i : active) graph.walls[i] = 0;
        const auto same = [&](const MacGrid& old) { return old.width == graph.width && old.height == graph.height &&
            old.spacing_m == graph.spacing_m && old.depth_m == graph.depth_m && old.periodic_x == graph.periodic_x &&
            old.periodic_y == graph.periodic_y && old.walls == graph.walls; };
        if (!inverse || !same(inverse->topology().grid())) inverse = std::make_unique<PressureMomentMultigrid>(
            std::make_shared<const interval_detail::PressureMultigridTopology>(graph));
        inverse->set_executor(executor, &pool);
        matrix.comp.assign(geometry.N, 0); matrix.diagonal.assign(geometry.N, 0); matrix.boundary.assign(geometry.N, 0);
        matrix.wx.assign(geometry.NX, 0); matrix.wy.assign(geometry.NY, 0);
        for (const auto i : active) matrix.comp[i] = matrix.diagonal[i] = anchors[i].comp;
        for (const auto& face : faces) if (face.movable) {
            const bool left = !graph.walls[face.left], right = !graph.walls[face.right];
            if (!left && !right) continue;
            const double timed = seconds * geometry.A, weight = (timed / face.inertia) * timed / 12;
            if (!std::isnormal(weight)) throw ArithmeticRefinement{};
            if (left && right) (face.vertical ? matrix.wy : matrix.wx)[face.index] = weight;
            else matrix.boundary[left ? face.left : face.right] += weight;
        }
        for (const auto i : active) matrix.diagonal[i] += matrix.boundary[i];
        if (!inverse->prepare(matrix.comp, matrix.diagonal, matrix.wx, matrix.wy))
            reject("Pressure window multigrid coefficients are outside their normal domain");
    }
    void refresh_fine(const std::vector<Anchor>& anchors) {
        auto& matrix = workspace.fine_matrix;
        for (const auto i : fine) { matrix.comp[i] = anchors[i].comp; matrix.diagonal[i] = anchors[i].comp + matrix.boundary[i]; }
        if (!workspace.fine_inverse->prepare(matrix.comp, matrix.diagonal, matrix.wx, matrix.wy))
            reject("Pressure window local coefficients are outside their normal domain");
    }
    std::vector<double> inverse_pair(PressureMomentMultigrid& inverse, const std::vector<Index>& active,
        const std::vector<double>& rhs, bool solve) {
        const auto n = active.size();
        if (&inverse == workspace.coarse_inverse.get()) for (const auto i : fine) ra[i] = rb[i] = 0;
        for (std::size_t j = 0; j < n; ++j) { ra[active[j]] = rhs[j]; rb[active[j]] = rhs[n + j]; }
        bool done = false;
        if (solve) {
            const auto result = inverse.solve(ra, rb, da, db, 1e-12, 0, 512, false);
            counters.fine_linear += result.iterations; counters.cycles += result.cycles; done = result.converged;
        } else { done = inverse.precondition_pair(ra, rb, da, db, false); ++counters.cycles; }
        if (!done) reject("Pressure window coupled inverse did not converge");
        std::vector<double> result(2 * n);
        for (std::size_t j = 0; j < n; ++j) { result[j] = da[active[j]]; result[n + j] = db[active[j]]; }
        return result;
    }
    struct FaceValue { TimeFaceFlux flux; double pressure_force = 0; DD pressure; };
    FaceValue face_value(const Face& face, double momentum, const std::vector<Trace>& traces,
        double phi, double ratio, unsigned level, bool linear = false) const {
        const auto value = [&](Index i) {
            if (i == none || !occupied[i]) return std::array<double, 3>{};
            const auto& t = traces[i]; const bool local = is_fine[i] != 0;
            return std::array<double, 3>{t.p0, t.mean + (local ? 0 : phi * t.first), (local ? 1 : ratio) * t.first};
        };
        const auto l = value(face.left), r = value(face.right);
        DD pressure_parts; pressure_parts.add(l[0]); pressure_parts.add(-r[0]); pressure_parts.add(l[1]); pressure_parts.add(-r[1]);
        const double pressure = geometry.A * ((l[0] - r[0]) + (l[1] - r[1])), first = geometry.A * (l[2] - r[2]);
        if (!std::isfinite(pressure) || !std::isfinite(first)) throw ArithmeticRefinement{};
        if (!face.movable) return {{}, pressure, pressure_parts};
        return {integrate_time_face(momentum, pressure + (linear ? 0 : face.body), first, face.inertia, geometry.A, options.seconds, level), pressure, pressure_parts};
    }
    struct Register {
        DD sum; std::optional<MacEnergySum> wide;
        void add(double value, const MacEnergy* full = nullptr) {
            if (full && !wide) { wide.emplace(); wide->add(sum.hi); wide->add(sum.lo); }
            if (wide) wide->add(full ? *full : MacEnergy::from_double(value));
            else sum.add(value);
        }
        [[nodiscard]] double value() const { return wide ? wide->value().project().value : sum.value(); }
    };
    struct Evaluation {
        std::vector<double> residual, volume, energy, momentum;
        std::vector<Register> flux;
        std::vector<DD> body_work;
        DD wall_x, wall_y, body;
        std::size_t unresolved = 0;
        double closure = 0;
    };
    // All authoritative candidates are local to one residual evaluation. A
    // rejected outer trial cannot alter its source, cache or accepted counters.
    Evaluation evaluate(const std::vector<double>& x) {
        ++counters.residual_evaluations;
        const auto N = geometry.N, nc = coarse.size(), nf = fine.size();
        Evaluation state; state.volume.assign(N, geometry.V); state.energy = source.U;
        state.momentum = source.jx; state.momentum.insert(state.momentum.end(), source.jy.begin(), source.jy.end());
        state.flux.resize(faces.size()); state.body_work.resize(geometry.NY);
        std::vector<Trace> traces(N); std::vector<Anchor> anchors = original;
        std::vector<DD> coarse_a(nc), coarse_b(nc);
        struct WallPiece { DD x, y, body; };
        const auto coarse_add = [&](const Face& face, double q, double b) {
            if (face.left != none && coarse_position[face.left] != none) {
                const auto j = coarse_position[face.left]; coarse_a[j].add(q); coarse_b[j].add(b);
            }
            if (face.right != none && coarse_position[face.right] != none) {
                const auto j = coarse_position[face.right]; coarse_a[j].add(-q); coarse_b[j].add(-b);
            }
        };
        const auto finish_wall = [&](const WallPiece& wall, unsigned level) {
            const double h = std::ldexp(options.seconds, -static_cast<int>(level));
            state.wall_x.add((wall.x.value() * h) * geometry.A);
            state.wall_y.add((wall.y.value() * h) * geometry.A); state.wall_y.add(wall.body.value() * h);
        };
        const auto accept_face = [&](Index index, const FaceValue& value, unsigned level, WallPiece& wall) {
            const auto& face = faces[index]; const double h = std::ldexp(options.seconds, -static_cast<int>(level));
            const double impulse = h * (value.pressure_force + face.body);
            if (face.vertical) state.body.add(h * face.body);
            if (!face.movable) {
                auto& reaction = face.vertical ? wall.y : wall.x;
                reaction.add(-value.pressure.hi); reaction.add(-value.pressure.lo);
                if (face.vertical) wall.body.add(-face.body);
                return;
            }
            const auto& f = value.flux;
            if (impulse != 0 && f.momentum == state.momentum[index]) ++state.unresolved;
            state.momentum[index] = f.momentum;
            state.flux[index].add(f.volume, f.full ? &f.full->volume : nullptr);
            if (face.vertical && face.body != 0) state.body_work[face.index].add(face.body * (f.volume / geometry.A));
        };
        for (std::size_t j = 0; j < nc; ++j) {
            const auto i = coarse[j];
            if (!trace_at(model, source, N, i, original[i], x[j], x[nc + j], traces[i]))
                reject("Pressure window coarse path leaves its EOS domain");
        }
        WallPiece coarse_wall;
        for (const auto index : coarse_faces) {
            const auto value = face_value(faces[index], state.momentum[index], traces, 0, 1, 0);
            coarse_add(faces[index], value.flux.volume, value.flux.first_volume); accept_face(index, value, 0, coarse_wall);
        }
        finish_wall(coarse_wall, 0);
        std::vector<double> z(2 * nf), candidate(2 * nf);
        struct Local { std::vector<double> residual; std::vector<FaceValue> faces; double closure = 0; };
        for (std::uint32_t tick = 0; nf != 0 && tick < pieces; ++tick) {
            const auto basis = time_mortar_piece(0, options.fine_level, tick).left;
            for (const auto i : fine) if (!anchor_at(model, source, N, i, stats[i], state.energy[i], state.volume[i], anchors[i]))
                reject("Pressure window fine state leaves its EOS domain");
            refresh_fine(anchors);
            std::fill(z.begin(), z.end(), 0);
            const auto local = [&](const std::vector<double>& trial) {
                for (std::size_t j = 0; j < nf; ++j) if (!trace_at(model, source, N, fine[j], anchors[fine[j]], trial[j], trial[nf + j], traces[fine[j]]))
                    reject("Pressure window fine path leaves its EOS domain");
                Local out; out.residual = trial; out.faces.reserve(fine_faces.size());
                std::vector<DD> a(nf), b(nf);
                for (const auto index : fine_faces) {
                    const auto& face = faces[index]; const auto f = face_value(face, state.momentum[index], traces,
                        basis.midpoint_p1, basis.relative_duration, options.fine_level);
                    out.faces.push_back(f);
                    if (face.left != none && fine_position[face.left] != none) {
                        const auto j = fine_position[face.left]; a[j].add(f.flux.volume); b[j].add(f.flux.first_volume);
                    }
                    if (face.right != none && fine_position[face.right] != none) {
                        const auto j = fine_position[face.right]; a[j].add(-f.flux.volume); b[j].add(-f.flux.first_volume);
                    }
                }
                for (std::size_t j = 0; j < nf; ++j) {
                    out.residual[j] -= a[j].value(); out.residual[nf + j] -= b[j].value();
                    out.closure = std::max({out.closure, std::abs(out.residual[j]) / geometry.V, std::abs(out.residual[nf + j]) / geometry.V});
                }
                return out;
            };
            auto current = local(z);
            for (std::size_t iteration = 0; current.closure > tolerance; ++iteration) {
                if (iteration == 24) reject("Pressure window fine Newton budget exhausted");
                const auto correction = inverse_pair(*workspace.fine_inverse, fine, current.residual, true);
                bool accepted = false; double fraction = 1;
                for (unsigned search = 0; search < 40; ++search, fraction *= .5) {
                    for (std::size_t j = 0; j < z.size(); ++j) candidate[j] = z[j] - fraction * correction[j];
                    try {
                        auto next = local(candidate);
                        if (next.closure <= tolerance || next.closure < current.closure * (1 - 1e-4 * fraction)) {
                            z.swap(candidate); current = std::move(next); accepted = true; break;
                        }
                    } catch (const std::domain_error&) { /* bounded EOS-domain line search */ }
                }
                if (!accepted) reject("Pressure window fine line search failed");
                ++counters.fine_newton;
            }
            // Re-form the accepted traces after any rejected line-search trial.
            for (std::size_t j = 0; j < nf; ++j) {
                const auto i = fine[j];
                if (!trace_at(model, source, N, i, anchors[i], z[j], z[nf + j], traces[i])) reject("Lost accepted fine pressure path");
                const auto before = state.energy[i]; state.energy[i] += traces[i].du; state.volume[i] += z[j];
                if (traces[i].du != 0 && state.energy[i] == before) ++state.unresolved;
            }
            state.closure = std::max(state.closure, current.closure);
            WallPiece fine_wall;
            for (std::size_t j = 0; j < fine_faces.size(); ++j) {
                const auto index = fine_faces[j]; const auto& value = current.faces[j];
                const auto first = time_cell_first_moment(basis, value.flux.volume, value.flux.first_volume);
                coarse_add(faces[index], value.flux.volume, first.value); accept_face(index, value, options.fine_level, fine_wall);
            }
            finish_wall(fine_wall, options.fine_level);
        }
        state.residual.resize(2 * nc);
        for (std::size_t j = 0; j < nc; ++j) {
            const auto i = coarse[j]; state.residual[j] = x[j] - coarse_a[j].value(); state.residual[nc + j] = x[nc + j] - coarse_b[j].value();
            state.closure = std::max({state.closure, std::abs(state.residual[j]) / geometry.V, std::abs(state.residual[nc + j]) / geometry.V});
            const auto before = state.energy[i]; state.energy[i] += traces[i].du; state.volume[i] += x[j];
            if (traces[i].du != 0 && state.energy[i] == before) ++state.unresolved;
        }
        return state;
    }
    // Frozen-source acoustic Schur action. Fine volume and momentum responses
    // stream through the same time grid; no finite-difference residuals or
    // monolithic space-time matrix are formed. Nonlinear EOS trials still
    // certify the actual root, so this is a chord Jacobian, not an EOS repair.
    std::vector<double> action(const std::vector<double>& x) {
        const auto N = geometry.N, nc = coarse.size(), nf = fine.size();
        for (const auto i : fine) ra[i] = rb[i] = 0;
        for (std::size_t j = 0; j < nc; ++j) { ra[coarse[j]] = x[j]; rb[coarse[j]] = x[nc + j]; }
        if (!workspace.coarse_inverse->apply_pair(ra, rb, da, db, false)) reject("Flagged global pressure operator");
        std::vector<double> result(2 * nc);
        for (std::size_t j = 0; j < nc; ++j) { result[j] = da[coarse[j]]; result[nc + j] = db[coarse[j]]; }
        if (nf == 0) return result;
        std::vector<Trace> traces(N); std::vector<double> volume(N), momentum(faces.size());
        std::vector<DD> qa(nc), qb(nc);
        const auto add_coarse = [&](const Face& face, double a, double b) {
            if (face.left != none && coarse_position[face.left] != none) {
                const auto j = coarse_position[face.left]; qa[j].add(a); qb[j].add(b);
            }
            if (face.right != none && coarse_position[face.right] != none) {
                const auto j = coarse_position[face.right]; qa[j].add(-a); qb[j].add(-b);
            }
        };
        for (std::size_t j = 0; j < nc; ++j) {
            const auto i = coarse[j]; const double C = 1 / original[i].comp;
            traces[i] = {0, -.5 * C * x[j] + k * C * x[nc + j], -k * C * x[j], 0};
        }
        // Remove macro traces at coarse/fine interfaces before streaming
        // their actual fine-grid response. Internal coarse faces stay in A*x.
        for (const auto index : fine_faces) {
            const auto& edge = faces[index];
            if (!((edge.left != none && coarse_position[edge.left] != none) || (edge.right != none && coarse_position[edge.right] != none))) continue;
            const auto f = face_value(edge, 0, traces, 0, 1, 0, true);
            add_coarse(edge, -f.flux.volume, -f.flux.first_volume);
        }
        std::vector<double> rhs(2 * nf);
        for (std::uint32_t tick = 0; nf != 0 && tick < pieces; ++tick) {
            const auto basis = time_mortar_piece(0, options.fine_level, tick).left;
            for (const auto i : fine) traces[i] = {-volume[i] / original[i].comp, 0, 0, 0};
            std::vector<DD> a(nf), b(nf);
            for (const auto index : fine_faces) {
                const auto& face = faces[index]; const auto f = face_value(face, momentum[index], traces,
                    basis.midpoint_p1, basis.relative_duration, options.fine_level, true);
                if (face.left != none && fine_position[face.left] != none) {
                    const auto j = fine_position[face.left]; a[j].add(f.flux.volume); b[j].add(f.flux.first_volume);
                }
                if (face.right != none && fine_position[face.right] != none) {
                    const auto j = fine_position[face.right]; a[j].add(-f.flux.volume); b[j].add(-f.flux.first_volume);
                }
            }
            for (std::size_t j = 0; j < nf; ++j) { rhs[j] = a[j].value(); rhs[nf + j] = b[j].value(); }
            const auto solution = inverse_pair(*workspace.fine_linear_inverse, fine, rhs, true);
            for (std::size_t j = 0; j < nf; ++j) {
                const auto i = fine[j]; const double C = 1 / original[i].comp;
                traces[i].mean = -.5 * C * solution[j] + k * C * solution[nf + j]; traces[i].first = -k * C * solution[j];
                volume[i] += solution[j];
            }
            for (const auto index : fine_faces) {
                const auto f = face_value(faces[index], momentum[index], traces, basis.midpoint_p1, basis.relative_duration, options.fine_level, true);
                momentum[index] = f.flux.momentum;
                const auto first = time_cell_first_moment(basis, f.flux.volume, f.flux.first_volume);
                add_coarse(faces[index], f.flux.volume, first.value);
            }
        }
        for (std::size_t j = 0; j < nc; ++j) { result[j] -= qa[j].value(); result[nc + j] -= qb[j].value(); }
        return result;
    }
    PressureTimeResult advance() {
        const auto N = geometry.N;
        if (!coarse.empty()) prepare(workspace.coarse_inverse, workspace.coarse_matrix, pressure_cells, original, options.seconds);
        if (!fine.empty()) {
            prepare(workspace.fine_linear_inverse, workspace.fine_linear_matrix, fine, original, local_seconds);
            prepare(workspace.fine_inverse, workspace.fine_matrix, fine, original, local_seconds);
        }
        std::vector<double> x(2 * coarse.size()), candidate(x.size()); auto current = evaluate(x);
        for (std::size_t iteration = 0; current.closure > tolerance; ++iteration) {
            if (iteration == 24 || coarse.empty()) reject("Pressure window coarse Newton budget exhausted");
            const auto correction = krylov(current.residual, [&](const auto& v) { return action(v); },
                [&](const auto& v) { return inverse_pair(*workspace.coarse_inverse, coarse, v, false); },
                std::clamp(.1 * current.closure, 1e-13, 1e-3), .05 * tolerance * geometry.V, true);
            double fraction = 1; bool accepted = false;
            for (unsigned search = 0; search < 40; ++search, fraction *= .5) {
                for (std::size_t j = 0; j < x.size(); ++j) candidate[j] = x[j] - fraction * correction[j];
                try {
                    auto next = evaluate(candidate);
                    if (next.closure <= tolerance || next.closure < current.closure * (1 - 1e-4 * fraction)) {
                        x.swap(candidate); current = std::move(next); accepted = true; break;
                    }
                } catch (const std::domain_error&) { /* bounded EOS-domain line search */ }
            }
            if (!accepted) reject("Pressure window coarse line search failed");
            ++counters.coarse_newton;
        }
        // Certify accumulated displacement, including empty material cells,
        // and the existing macro-remap CFL. This pressure-only component does
        // not certify subcycled transport across a reversing face path.
        std::vector<DD> divergence(N); std::vector<double> outgoing(N);
        for (std::size_t j = 0; j < faces.size(); ++j) {
            const auto& face = faces[j]; const double q = current.flux[j].value();
            if (face.left != none) { divergence[face.left].add(q); outgoing[face.left] += std::max(q, 0.); }
            if (face.right != none) { divergence[face.right].add(-q); outgoing[face.right] += std::max(-q, 0.); }
        }
        for (std::size_t i = 0; i < N; ++i) if (!geometry.wall[i]) {
            if (!occupied[i]) current.volume[i] += divergence[i].value();
            DD closure; closure.add(current.volume[i]); closure.add(-geometry.V); closure.add(-divergence[i].hi); closure.add(-divergence[i].lo);
            const double defect = std::abs(closure.value()) / geometry.V;
            if (defect > 128 * eps) reject("Pressure window accumulated geometric closure failed");
            current.closure = std::max(current.closure, defect);
            if (!(current.volume[i] > 0) || outgoing[i] > current.volume[i] * options.maximum_outgoing_fraction)
                reject("Pressure window material-volume CFL exceeded");
            State endpoint;
            if (occupied[i] && !evaluate_state(model, source, N, i, stats[i], current.energy[i], current.volume[i], endpoint))
                reject("Pressure window endpoint leaves its EOS domain");
        }
        PressureTimeResult result;
        result.energy = std::move(current.energy); result.volume = std::move(current.volume);
        result.momentum_x.assign(current.momentum.begin(), current.momentum.begin() + static_cast<std::ptrdiff_t>(geometry.NX));
        result.momentum_y.assign(current.momentum.begin() + static_cast<std::ptrdiff_t>(geometry.NX), current.momentum.end());
        result.qx.resize(geometry.NX); result.qy.resize(geometry.NY); result.body_work_y.resize(geometry.NY);
        for (std::size_t j = 0; j < faces.size(); ++j) {
            const auto& face = faces[j]; const auto& flux = current.flux[j];
            (face.vertical ? result.qy : result.qx)[face.index] = flux.value();
            if (flux.wide) (face.vertical ? result.full_qy : result.full_qx).emplace_back(face.index, flux.wide->value());
        }
        for (std::size_t j = 0; j < geometry.NY; ++j) result.body_work_y[j] = current.body_work[j].value();
        result.wall_x = MacEnergy::from_double(current.wall_x.value()); result.wall_y = MacEnergy::from_double(current.wall_y.value());
        result.body_y = MacEnergy::from_double(current.body.value());
        counters.maximum_closure = current.closure; counters.unresolved_updates = current.unresolved; result.report = counters;
        return result;
    }
};
PressureTimeResult advance_pressure_time_window(const Model& model, const Fields& fields, const MacGrid& grid,
    std::span<const std::uint8_t> mask, const PressureTimeOptions& options, Pool& pool, PressureTimeWorkspace& workspace) {
    require_mac_energy_environment();
    PressureTimeWorkspace::Impl::Run run(*workspace.impl_, model, fields, grid, mask, options, pool);
    return run.advance();
}
}
