// Production pressure step (MAC_KERNEL.md, "Pressure"): the reference
// implicit EOS pressure work with midpoint face sweeps, solved for per-cell
// volume changes in the increment formulation W = p0 + dW(dV). Node pressure
// differences gather (p0_l - p0_r) + (dW_l - dW_r), so no absolute-pressure
// cancellation floor limits the Newton root. Linear systems use PCG with a
// Jacobi or geometric-multigrid preconditioner.
#include "mac_kernel_detail.hpp"
#include "mixed_atmospheres/mac_phase_change.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <numeric>
#include <span>
#include <utility>

namespace ma::experimental::mac_kernel {
namespace {
struct Sweep { double value = 0; MacEnergy full; };
Sweep midpoint_sweep(double j0, double j1, double inertia, double dt, double area) {
    // Preserve the ordinary operation order. Only a flagged intermediate
    // constructs a range value; its sign must survive even if q projects to 0.
    const double middle = .5 * (j0 + j1), velocity = middle / inertia;
    const double timed = velocity * dt, value = timed * area;
    if ((j0 == 0 && j1 == 0) || (std::isnormal(middle) && std::isnormal(velocity) &&
        std::isnormal(timed) && std::isnormal(value))) return {value, {}};
    MacEnergySum sum; sum.add(j0); sum.add(j1);
    const auto full = sum.value().shifted(-1).shared(1, inertia).multiplied(dt).multiplied(area);
    if (full.exponent() > 1024) throw ArithmeticRefinement{};
    const auto stored = full.project();
    // A representable sweep lost before the final product can affect the
    // pressure root itself: replay that interval with the strict arithmetic.
    if (std::isnormal(stored.value)) {
        if (stored.value != value) throw ArithmeticRefinement{};
        return {value, {}};
    }
    return {stored.value, full};
}
double combine(const std::vector<double>& part) { DD s; for (const double v : part) s.add(v); return s.value(); }
// q = z + beta p computed on the fly (for the cell and its neighbours) and
// stored for the cell, fused with A q and q.Aq: one pass per PCG direction.
void apply_direction(const Context& c, const LinearSystem& sys, const std::vector<double>& z, const std::vector<double>& p, double beta,
    std::vector<double>& q, std::vector<double>& y, double& dot) {
    const auto& g = c.g;
    const double* zv = z.data(); const double* pv = p.data();
    const double* wx = sys.wx.data(); const double* wy = sys.wy.data(); const double* d = sys.diag.data();
    const auto dir = [&](std::size_t j) { return zv[j] + beta * pv[j]; };
    auto part = c.exec.partials<double>([&](std::size_t y0, std::size_t y1, std::size_t) {
        double local = 0;
        for (std::size_t row = y0; row < y1; ++row) {
            const std::size_t base = row * g.W, xb = row * g.XW;
            const std::size_t down = row > 0 ? base - g.W : base + (g.H - 1) * g.W;
            const std::size_t up = row + 1 < g.H ? base + g.W : base - (g.H - 1) * g.W;
            const std::size_t yb = row * g.W, yt = ((row + 1) % g.YH) * g.W;
            for (std::size_t col = 0; col < g.W; ++col) {
                const auto i = base + col;
                const double xi = dir(i);
                if (!sys.active[i]) { y[i] = 0; q[i] = xi; continue; }
                const auto left = col > 0 ? i - 1 : i + g.W - 1, right = col + 1 < g.W ? i + 1 : i + 1 - g.W;
                const double wl = wx[xb + col], wr = wx[xb + (col + 1) % g.XW], wb = wy[yb + col], wt = wy[yt + col];
                const double value = d[i] * xi + wl * (xi - dir(left)) + wr * (xi - dir(right)) + wb * (xi - dir(down + col)) + wt * (xi - dir(up + col));
                q[i] = xi; y[i] = value; local += xi * value;
            }
        }
        return local;
    });
    dot = combine(part);
}
void apply(const Context& c, const LinearSystem& sys, const std::vector<double>& x, std::vector<double>& y, std::vector<double>* dot_with,
    double* dot) {
    const auto& g = c.g;
    const double* xv = x.data(); const double* wx = sys.wx.data(); const double* wy = sys.wy.data(); const double* d = sys.diag.data();
    auto part = c.exec.partials<double>([&](std::size_t y0, std::size_t y1, std::size_t) {
        double local = 0;
        for (std::size_t row = y0; row < y1; ++row) {
            // Direct structured neighbours; uncoupled faces carry zero weight,
            // so the wrapped index of a nonperiodic boundary contributes 0.
            const std::size_t base = row * g.W, xb = row * g.XW;
            const std::size_t down = row > 0 ? base - g.W : base + (g.H - 1) * g.W;
            const std::size_t up = row + 1 < g.H ? base + g.W : base - (g.H - 1) * g.W;
            const std::size_t yb = row * g.W, yt = ((row + 1) % g.YH) * g.W;
            for (std::size_t col = 0; col < g.W; ++col) {
                const auto i = base + col;
                if (!sys.active[i]) { y[i] = 0; continue; }
                const double xi = xv[i];
                const auto left = col > 0 ? i - 1 : i + g.W - 1, right = col + 1 < g.W ? i + 1 : i + 1 - g.W;
                const double wl = wx[xb + col], wr = wx[xb + (col + 1) % g.XW], wb = wy[yb + col], wt = wy[yt + col];
                const double value = d[i] * xi + wl * (xi - xv[left]) + wr * (xi - xv[right]) + wb * (xi - xv[down + col]) + wt * (xi - xv[up + col]);
                y[i] = value;
                if (dot_with) local += (*dot_with)[i] * value;
            }
        }
        return local;
    });
    if (dot) *dot = combine(part);
}
}
// A cached hierarchy for exactly this active pressure geometry. Empty-cell
// zero-traction faces enter the fine diagonal, never a coarse physical link.
static bool prepare_multigrid(Context& c, const LinearSystem& sys) {
    auto& ws = c.ws;
    MacGrid active_grid = c.source_grid;
    for (std::size_t i = 0; i < sys.active.size(); ++i) active_grid.walls[i] = sys.active[i] ? 0 : 1;
    const auto same = [&](const MacGrid& old) {
        return old.width == active_grid.width && old.height == active_grid.height &&
            old.spacing_m == active_grid.spacing_m && old.depth_m == active_grid.depth_m &&
            old.periodic_x == active_grid.periodic_x && old.periodic_y == active_grid.periodic_y && old.walls == active_grid.walls;
    };
    if (!ws.pressure_topology || !same(ws.pressure_topology->grid())) {
        ws.pressure_topology = std::make_shared<const interval_detail::PressureMultigridTopology>(active_grid);
        ws.pressure_multigrid = std::make_unique<interval_detail::PressureMultigrid>(ws.pressure_topology);
        ++c.counters.topology_builds;
    }
    ws.pressure_multigrid->set_executor([](const void* context, std::size_t blocks, interval_detail::BlockFunction function, const void* job_context) {
        // Small coarse levels stay serial; the block and row order is unchanged.
        const auto job = [&](std::size_t block) { function(job_context, block); };
        if (blocks < 32) { for (std::size_t b = 0; b < blocks; ++b) job(b); }
        else static_cast<Pool*>(const_cast<void*>(context))->run(blocks, job);
    }, &c.exec.pool);
    c.counters.multigrid_levels = std::max(c.counters.multigrid_levels, ws.pressure_topology->levels().size());
    if (!ws.pressure_multigrid->prepare(sys.diag, sys.wx, sys.wy)) {
        ++c.counters.multigrid_fallbacks; return false;
    }
    ++c.counters.multigrid_solves; return true;
}
namespace {
// Disconnected pressure graphs have independent equations. Give each component
// its own Krylov stream: a stiff liquid pocket must not make every gas cavern
// perform its iterations. Components and their row/face orders depend only on
// the operator; each worker owns one solve and disjoint output cells.
// Components are numbered in order of their lowest cell. Each lists its cells
// in index order, then its internal faces and boundary couplings in face
// order (x faces, then y faces), as slices of flat arrays.
struct ComponentLayout {
    std::vector<std::size_t> cells, cell_start, face_start, boundary_start;
    std::vector<PressureFace> faces;
    std::vector<std::pair<std::size_t, double>> boundary;
    [[nodiscard]] std::size_t size() const { return cell_start.empty() ? 0 : cell_start.size() - 1; }
};
ComponentLayout component_layout(const Context& c, const LinearSystem& sys) {
    const auto& g = c.g; const auto N = g.N;
    std::vector<std::size_t> nodes(N, N), cells;
    std::vector<std::pair<std::size_t, double>> boundary;
    for (std::size_t i = 0; i < N; ++i) if (sys.active[i]) {
        nodes[i] = cells.size(); cells.push_back(i);
    }
    std::vector<PressureFace> faces;
    const auto face = [&](Index l, Index r, double weight) {
        if (weight == 0) return;
        const auto a = l == none ? N : nodes[l], bb = r == none ? N : nodes[r];
        if (a != N && bb != N) faces.push_back({a, bb, weight});
        else if (a != N) boundary.emplace_back(a, weight);
        else if (bb != N) boundary.emplace_back(bb, weight);
    };
    for (std::size_t fidx = 0; fidx < g.NX; ++fidx) face(g.xl[fidx], g.xr[fidx], sys.wx[fidx]);
    for (std::size_t fidx = 0; fidx < g.NY; ++fidx) face(g.yl[fidx], g.yr[fidx], sys.wy[fidx]);
    const auto M = cells.size();
    std::vector<std::size_t> parent(M); std::iota(parent.begin(), parent.end(), 0);
    const auto root = [&](std::size_t i) {
        while (parent[i] != i) { parent[i] = parent[parent[i]]; i = parent[i]; }
        return i;
    };
    for (const auto& edge : faces) {
        const auto l = root(edge.left), r = root(edge.right);
        if (l != r) parent[r] = l;
    }
    ComponentLayout layout;
    // which[i]: component of node i; local[i]: its rank there. The start
    // arrays first hold counts, then (by prefix sums) slice offsets.
    std::vector<std::size_t> labels(M, M), which(M), local(M);
    auto& cell_start = layout.cell_start;
    for (std::size_t i = 0; i < M; ++i) {
        const auto r = root(i);
        if (labels[r] == M) { labels[r] = cell_start.size(); cell_start.push_back(0); }
        which[i] = labels[r]; local[i] = cell_start[which[i]]++;
    }
    const auto parts = cell_start.size();
    const auto offsets = [&](std::vector<std::size_t>& start) {
        std::size_t total = 0;
        for (auto& v : start) total += std::exchange(v, total);
        start.push_back(total);
    };
    offsets(cell_start);
    layout.cells.resize(M);
    for (std::size_t i = 0; i < M; ++i) layout.cells[cell_start[which[i]] + local[i]] = cells[i];
    // Faces and couplings keep their face order inside each component.
    const auto place = [&](const auto& items, std::vector<std::size_t>& start, auto& out, const auto& node, const auto& entry) {
        start.assign(parts, 0);
        for (const auto& item : items) ++start[which[node(item)]];
        offsets(start);
        std::vector<std::size_t> next(start.begin(), start.end() - 1);
        out.resize(items.size());
        for (const auto& item : items) out[next[which[node(item)]]++] = entry(item);
    };
    place(faces, layout.face_start, layout.faces, [](const PressureFace& e) { return e.left; },
        [&](const PressureFace& e) { return PressureFace{local[e.left], local[e.right], e.conductance}; });
    place(boundary, layout.boundary_start, layout.boundary, [](const auto& b) { return b.first; },
        [&](const auto& b) { return std::pair<std::size_t, double>{local[b.first], b.second}; });
    return layout;
}
// Fixed reduction blocks of a parallel component solve. A solve of at most
// this many cells reproduces the serial component solve exactly.
constexpr std::size_t parallel_reduction_rows = 1024;
// Rows of one large component in IC sweep tasks: bands of whole grid rows
// with about equal cell counts, cut into column chunks. A row's lower
// neighbours (left and below) are earlier in its task or in a task of a
// lower or equal band and chunk. The tasks only schedule rows; the
// arithmetic of every row is the serial factor's.
PressureParallelPlan sweep_plan(const Grid& g, std::span<const std::size_t> cells, unsigned workers) {
    PressureParallelPlan plan;
    const auto count = cells.size();
    std::size_t x0 = g.W, x1 = 0;
    for (const auto cell : cells) { x0 = std::min(x0, cell % g.W); x1 = std::max(x1, cell % g.W); }
    const auto y0 = cells.front() / g.W, rows = cells.back() / g.W - y0 + 1, width = x1 - x0 + 1;
    plan.bands = std::max<std::size_t>(1, std::min<std::size_t>(workers, rows));
    plan.chunks = std::clamp<std::size_t>(width / 8, 1, 64);
    std::vector<std::size_t> band_of_row(rows, 0);
    {
        std::size_t band = 0, seen = 0, k = 0;
        for (std::size_t r = 0; r < rows; ++r) {
            band_of_row[r] = band;
            for (; k < count && cells[k] / g.W == y0 + r; ++k) ++seen;
            if (band + 1 < plan.bands && seen * plan.bands >= (band + 1) * count) ++band;
        }
    }
    const auto task = [&](std::size_t cell) {
        return band_of_row[cell / g.W - y0] * plan.chunks + (cell % g.W - x0) * plan.chunks / width;
    };
    plan.start.assign(plan.bands * plan.chunks + 1, 0);
    for (const auto cell : cells) ++plan.start[task(cell) + 1];
    for (std::size_t t = 0; t + 1 < plan.start.size(); ++t) plan.start[t + 1] += plan.start[t];
    plan.order.resize(count);
    auto cursor = plan.start;
    for (std::size_t k = 0; k < count; ++k) plan.order[cursor[task(cells[k])]++] = k;
    plan.reduction_rows = parallel_reduction_rows;
    return plan;
}
LinearSolveResult solve_component_ic(Context& c, const LinearSystem& sys, const std::vector<double>& b,
    std::vector<double>& x, double rtol, double atol, std::size_t maximum, const ComponentLayout* cached = nullptr,
    std::size_t parallel_rows = 0) {
    const auto transient = cached ? ComponentLayout{} : component_layout(c, sys);
    const auto& layout = cached ? *cached : transient;
    const auto parts = layout.size();
    const auto N = c.g.N;
    struct Solved { LinearSolveResult linear; bool ic = false, fallback = false; };
    std::vector<Solved> results(parts); x.assign(N, 0);
    // Sum of squared component residual limits is at most half the squared
    // original global limit, including when relative and absolute terms mix.
    const double local_atol = parts == 0 ? atol : .5 * atol / std::sqrt(static_cast<double>(parts));
    // Components of at least parallel_rows cells run after the others, one at
    // a time across the pool; the lowest failing component's error is raised.
    std::vector<std::size_t> serial, parallel;
    for (std::size_t index = 0; index < parts; ++index)
        (parallel_rows && layout.cell_start[index + 1] - layout.cell_start[index] >= parallel_rows ? parallel : serial).push_back(index);
    std::vector<std::exception_ptr> errors(parts);
    const auto solve_one = [&](std::size_t index, const PressureParallelPlan* plan) {
        const auto first = layout.cell_start[index], last = layout.cell_start[index + 1];
        const auto face_first = layout.face_start[index], face_last = layout.face_start[index + 1];
        std::vector<double> diagonal, rhs; diagonal.reserve(last - first); rhs.reserve(last - first);
        for (auto j = first; j < last; ++j) { diagonal.push_back(sys.diag[layout.cells[j]]); rhs.push_back(b[layout.cells[j]]); }
        for (auto j = layout.boundary_start[index]; j < layout.boundary_start[index + 1]; ++j)
            diagonal[layout.boundary[j].first] += layout.boundary[j].second;
        // A positive isolated scalar with zero RHS has the exact zero root.
        // No reciprocal is needed, including when 1/diagonal overflows.
        if (c.settings.game_profile==MacGameProfile::base_game_floor && face_first == face_last &&
            diagonal.size()==1 && std::isfinite(diagonal[0]) && diagonal[0]>0 && rhs[0]==0 &&
            std::isfinite(rtol) && rtol>=0 && std::isfinite(local_atol) && local_atol>=0 &&
            (.5*rtol>0 || local_atol>0) && maximum>0) {
            results[index]={{0,0,true},false,false}; return;
        }
        PressureSystem matrix(std::move(diagonal),
            std::vector<PressureFace>(layout.faces.begin() + static_cast<std::ptrdiff_t>(face_first),
                layout.faces.begin() + static_cast<std::ptrdiff_t>(face_last)),
            PressurePreconditioner::incomplete_cholesky);
        PressureSolveResult solved;
        try { solved = matrix.solve(rhs, std::vector<double>(rhs.size(), 0), .5 * rtol, local_atol, maximum, plan); }
        catch (const std::runtime_error&) { throw ComponentPressureSolveFailure{}; }
        for (auto j = first; j < last; ++j) x[layout.cells[j]] = solved.pressure[j - first];
        results[index] = {{solved.iterations, solved.residual_norm, true},
            solved.preconditioner_used == PressurePreconditioner::incomplete_cholesky, solved.preconditioner_fallback};
    };
    if (parallel.empty()) c.exec.pool.run(parts, [&](std::size_t index) { solve_one(index, nullptr); });
    else {
        c.exec.pool.run(serial.size(), [&](std::size_t k) {
            try { solve_one(serial[k], nullptr); } catch (...) { errors[serial[k]] = std::current_exception(); }
        });
        struct Executor { Pool* pool; };
        Executor executor{&c.exec.pool};
        for (const auto index : parallel) {
            const auto first = layout.cell_start[index], last = layout.cell_start[index + 1];
            auto plan = sweep_plan(c.g, std::span(layout.cells).subspan(first, last - first), c.exec.pool.size());
            plan.executor_context = &executor;
            plan.executor = [](const void* context, std::size_t count, PressureParallelPlan::Task task, const void* job) {
                static_cast<const Executor*>(context)->pool->run(count, [task, job](std::size_t t) { task(job, t); });
            };
            try { solve_one(index, &plan); } catch (...) { errors[index] = std::current_exception(); }
        }
        for (const auto& error : errors) if (error) std::rethrow_exception(error);
    }
    LinearSolveResult result; result.converged = true; DD squared;
    c.counters.maximum_linear_components = std::max(c.counters.maximum_linear_components, parts);
    for (const auto& solved : results) {
        result.iterations += solved.linear.iterations; squared.add(solved.linear.residual * solved.linear.residual);
        c.counters.incomplete_cholesky_solves += solved.ic ? 1 : 0;
        c.counters.incomplete_cholesky_fallbacks += solved.fallback ? 1 : 0;
    }
    result.residual = std::sqrt(squared.value()); return result;
}
}
LinearSolveResult solve_components(Context& c, const LinearSystem& sys, const std::vector<double>& b, std::vector<double>& x,
    double rtol, double atol, std::size_t maximum, std::size_t parallel_rows) {
    if (c.settings.preconditioner != PressurePreconditioner::incomplete_cholesky || parallel_rows == 0)
        throw std::invalid_argument("Parallel component solves need the IC preconditioner and a size");
    return solve_component_ic(c, sys, b, x, rtol, atol, maximum, nullptr, parallel_rows);
}

// ComponentSolveCache: the structure of component_layout, PressureSystem,
// IncompleteCholesky and the plan reductions for one active pattern.
struct ComponentSolveCache::Impl {
    struct Face { std::size_t left, right, source; bool x; };
    struct Component {
        std::vector<std::size_t> cells;
        std::vector<Face> faces;                       // component face order
        std::vector<std::size_t> incident_offsets;     // row -> faces, face order
        std::vector<std::size_t> incident;             // 2 * face + (row is right)
        std::vector<std::size_t> lower_offsets, lower_columns; // IC(0) lower rows, by column
        std::vector<std::size_t> lower_faces_offsets, lower_faces; // faces combined into each entry
        std::vector<std::size_t> upper_offsets, upper_rows, upper_entries; // gather of the backward sweep
        bool degree_ok = true;
    };
    std::vector<std::uint8_t> active, open_x, open_y;
    std::size_t parallel_rows = 0;
    bool valid = false, boundary = false;
    std::vector<Component> components;
    // Values, rebuilt every solve.
    std::vector<double> compliance, conductance, inverse_diagonal, rhs, lower_values, inverse_pivots;
    std::vector<double> pressure, residual, z, direction, product, next;
};
ComponentSolveCache::ComponentSolveCache() : impl(std::make_unique<Impl>()) {}
ComponentSolveCache::~ComponentSolveCache() = default;
ComponentSolveCache::ComponentSolveCache(ComponentSolveCache&&) noexcept = default;
ComponentSolveCache& ComponentSolveCache::operator=(ComponentSolveCache&&) noexcept = default;
namespace {
using CacheImpl = ComponentSolveCache::Impl;
// Rebuilds the structure exactly as component_layout, PressureSystem and
// IncompleteCholesky::factor order it.
void rebuild_structure(CacheImpl& cache, const Context& c, const LinearSystem& sys) {
    const auto& g = c.g; const auto N = g.N;
    cache.components.clear(); cache.boundary = false;
    std::vector<std::size_t> nodes(N, N), cells;
    for (std::size_t i = 0; i < N; ++i) if (sys.active[i]) { nodes[i] = cells.size(); cells.push_back(i); }
    struct Edge { std::size_t left, right, source; bool x; };
    std::vector<Edge> faces;
    const auto face = [&](bool x, std::size_t f) {
        const auto l = (x ? g.xl : g.yl)[f], r = (x ? g.xr : g.yr)[f];
        if (!((x ? sys.wx : sys.wy)[f] != 0)) return;
        const auto a = l == none ? N : nodes[l], b = r == none ? N : nodes[r];
        if (a != N && b != N) faces.push_back({a, b, f, x});
        else if (a != N || b != N) cache.boundary = true;
    };
    for (std::size_t f = 0; f < g.NX; ++f) face(true, f);
    for (std::size_t f = 0; f < g.NY; ++f) face(false, f);
    if (cache.boundary) return;
    const auto M = cells.size();
    std::vector<std::size_t> parent(M); std::iota(parent.begin(), parent.end(), 0);
    const auto root = [&](std::size_t i) {
        while (parent[i] != i) { parent[i] = parent[parent[i]]; i = parent[i]; }
        return i;
    };
    for (const auto& edge : faces) {
        const auto l = root(edge.left), r = root(edge.right);
        if (l != r) parent[r] = l;
    }
    std::vector<std::size_t> labels(M, M), which(M), local(M);
    for (std::size_t i = 0; i < M; ++i) {
        const auto r = root(i);
        if (labels[r] == M) { labels[r] = cache.components.size(); cache.components.emplace_back(); }
        which[i] = labels[r]; local[i] = cache.components[which[i]].cells.size();
        cache.components[which[i]].cells.push_back(cells[i]);
    }
    for (const auto& edge : faces) {
        auto& component = cache.components[which[edge.left]];
        component.faces.push_back({local[edge.left], local[edge.right], edge.source, edge.x});
    }
    for (auto& component : cache.components) {
        const auto count = component.cells.size();
        auto& io = component.incident_offsets; io.assign(count + 1, 0);
        for (const auto& f : component.faces) { ++io[f.left + 1]; ++io[f.right + 1]; }
        for (std::size_t i = 0; i < count; ++i) io[i + 1] += io[i];
        component.incident.resize(2 * component.faces.size());
        {
            auto cursor = io;
            for (std::size_t f = 0; f < component.faces.size(); ++f) {
                component.incident[cursor[component.faces[f].left]++] = 2 * f;
                component.incident[cursor[component.faces[f].right]++] = 2 * f + 1;
            }
        }
        // IC(0) lower rows: faces with positive conductance (all faces here),
        // grouped by row = max endpoint in face order, sorted by column,
        // parallel faces combined in that order.
        constexpr std::size_t maximum_lower_degree = 64;
        std::vector<std::size_t> offsets(count + 1, 0);
        for (const auto& f : component.faces) ++offsets[std::max(f.left, f.right) + 1];
        for (std::size_t row = 0; row < count; ++row) {
            if (offsets[row + 1] > maximum_lower_degree) component.degree_ok = false;
            offsets[row + 1] += offsets[row];
        }
        std::vector<std::pair<std::size_t, std::size_t>> entries(component.faces.size()); // column, face
        {
            auto cursor = offsets;
            for (std::size_t f = 0; f < component.faces.size(); ++f) {
                const auto& edge = component.faces[f];
                entries[cursor[std::max(edge.left, edge.right)]++] = {std::min(edge.left, edge.right), f};
            }
        }
        component.lower_offsets.assign(count + 1, 0); component.lower_columns.clear();
        component.lower_faces_offsets.assign(1, 0); component.lower_faces.clear();
        for (std::size_t row = 0; row < count; ++row) {
            const auto begin = offsets[row], end = offsets[row + 1];
            // std::sort of IncompleteCholesky::factor: equal columns keep no
            // defined order there; only their sum (in sorted order) is used,
            // so parallel faces are summed in the order std::sort leaves.
            std::sort(entries.begin() + static_cast<std::ptrdiff_t>(begin), entries.begin() + static_cast<std::ptrdiff_t>(end),
                [](const auto& a, const auto& b) { return a.first < b.first; });
            for (auto k = begin; k < end; ++k) {
                if (k > begin && entries[k - 1].first == entries[k].first) component.lower_faces.push_back(entries[k].second);
                else {
                    if (k > begin) component.lower_faces_offsets.push_back(component.lower_faces.size());
                    component.lower_columns.push_back(entries[k].first);
                    component.lower_faces.push_back(entries[k].second);
                }
            }
            if (end > begin) component.lower_faces_offsets.push_back(component.lower_faces.size());
            component.lower_offsets[row + 1] = component.lower_columns.size();
        }
        // Backward gather: rows referencing each column, in descending row order.
        const auto nnz = component.lower_columns.size();
        component.upper_offsets.assign(count + 1, 0);
        for (std::size_t k = 0; k < nnz; ++k) ++component.upper_offsets[component.lower_columns[k] + 1];
        for (std::size_t row = 0; row < count; ++row) component.upper_offsets[row + 1] += component.upper_offsets[row];
        component.upper_rows.resize(nnz); component.upper_entries.resize(nnz);
        auto cursor = component.upper_offsets;
        for (auto row = count; row-- > 0;)
            for (auto k = component.lower_offsets[row]; k < component.lower_offsets[row + 1]; ++k) {
                const auto at = cursor[component.lower_columns[k]]++;
                component.upper_rows[at] = row; component.upper_entries[at] = k;
            }
    }
}
double finite_reduction(long double value) {
    const double result = static_cast<double>(value);
    require_finite(result, "pressure reduction");
    return result;
}
// One component's PCG, as PressureSystem::solve (block == count) or its
// plan form (block == plan rows): only the reduction blocks differ.
PressureSolveResult solve_cached_component(CacheImpl& cache, const CacheImpl::Component& component,
    std::size_t first, double relative, double absolute, std::size_t maximum, std::size_t block) {
    const auto count = component.cells.size();
    const auto* C = cache.compliance.data() + first;
    const auto* G = cache.conductance.data();
    const auto* D = cache.inverse_diagonal.data() + first;
    const auto* B = cache.rhs.data() + first;
    for (std::size_t i = 0; i < count; ++i) require_finite(B[i], "pressure vector value");
    require_finite(relative, "relative pressure tolerance");
    require_finite(absolute, "absolute pressure tolerance");
    if (relative < 0 || absolute < 0 || (relative == 0 && absolute == 0) || maximum == 0)
        throw std::invalid_argument("Invalid pressure solve limits");
    const auto blocks = (count + block - 1) / block;
    const auto reduce = [&](const auto& body) {
        long double total = 0;
        for (std::size_t q = 0; q < blocks; ++q) {
            long double sum = 0;
            const auto end = std::min(count, (q + 1) * block);
            for (auto i = q * block; i < end; ++i) sum += body(i);
            total += sum;
        }
        return finite_reduction(total);
    };
    const auto row_product = [&](std::size_t i, const double* input) {
        double value = C[i] * input[i];
        for (auto k = component.incident_offsets[i]; k < component.incident_offsets[i + 1]; ++k) {
            const auto& face = component.faces[component.incident[k] / 2];
            const double flux = G[component.incident[k] / 2] * (input[face.left] - input[face.right]);
            if (component.incident[k] % 2 == 0) value += flux; else value -= flux;
        }
        return value;
    };
    auto& pressure = cache.pressure; auto& residual = cache.residual; auto& z = cache.z;
    auto& direction = cache.direction; auto& product = cache.product;
    pressure.assign(count, 0); residual.resize(count); z.resize(count); direction.resize(count); product.resize(count);
    PressureSolveResult result{{}, 0, 0};
    const double threshold = std::max(absolute, relative * std::sqrt(reduce([&](std::size_t i) { return static_cast<long double>(B[i]) * B[i]; })));
    require_finite(threshold, "pressure convergence threshold");
    result.residual_threshold = threshold;
    const auto true_residual = [&] {
        result.residual_norm = std::sqrt(reduce([&](std::size_t i) {
            product[i] = row_product(i, pressure.data());
            residual[i] = B[i] - product[i];
            return static_cast<long double>(residual[i]) * residual[i];
        }));
    };
    const auto finish = [&] { result.pressure.assign(pressure.begin(), pressure.end()); return result; };
    true_residual();
    if (result.residual_norm <= threshold) return finish();
    // IncompleteCholesky::factor on the cached sparsity.
    bool ic = component.degree_ok;
    if (ic) {
        auto& L = cache.lower_values; auto& P = cache.inverse_pivots;
        L.resize(component.lower_columns.size()); P.resize(count);
        for (std::size_t k = 0; k < L.size(); ++k) {
            double value = 0; bool started = false;
            for (auto q = component.lower_faces_offsets[k]; q < component.lower_faces_offsets[k + 1]; ++q) {
                const double entry = -G[component.lower_faces[q]];
                if (!started) { value = entry; started = true; } else value += entry;
            }
            L[k] = value;
        }
        const auto& offsets = component.lower_offsets; const auto& columns = component.lower_columns;
        for (std::size_t row = 0; row < count && ic; ++row) {
            const double diagonal = 1 / D[row];
            double pivot = diagonal;
            for (auto k = offsets[row]; k < offsets[row + 1]; ++k) {
                double value = L[k];
                auto left = offsets[row], right = offsets[columns[k]];
                const auto right_end = offsets[columns[k] + 1];
                while (left < k && right < right_end) {
                    if (columns[left] < columns[right]) ++left;
                    else if (columns[right] < columns[left]) ++right;
                    else { value -= L[left] * L[right] / P[columns[left]]; ++left; ++right; }
                }
                L[k] = value * P[columns[k]];
                pivot -= L[k] * value;
            }
            if (!std::isfinite(pivot) || pivot <= diagonal * 1e-12) { ic = false; break; }
            P[row] = 1 / pivot;
            if (!std::isfinite(P[row])) ic = false;
        }
    }
    if (ic) result.preconditioner_used = PressurePreconditioner::incomplete_cholesky;
    else result.preconditioner_fallback = true;
    const auto precondition = [&] {
        if (!ic) { for (std::size_t i = 0; i < count; ++i) z[i] = D[i] * residual[i]; return; }
        const auto& L = cache.lower_values; const auto& P = cache.inverse_pivots;
        const auto& offsets = component.lower_offsets; const auto& columns = component.lower_columns;
        for (std::size_t row = 0; row < count; ++row) {
            double value = residual[row];
            for (auto k = offsets[row]; k < offsets[row + 1]; ++k) value -= L[k] * z[columns[k]];
            z[row] = value;
        }
        for (auto row = count; row-- > 0;) {
            double value = z[row] * P[row];
            for (auto e = component.upper_offsets[row]; e < component.upper_offsets[row + 1]; ++e)
                value -= L[component.upper_entries[e]] * z[component.upper_rows[e]];
            z[row] = value;
        }
    };
    bool restart = true; double beta = 0;
    precondition();
    double previous = reduce([&](std::size_t i) { return static_cast<long double>(residual[i]) * z[i]; });
    while (result.iterations < maximum) {
        for (std::size_t i = 0; i < count; ++i) direction[i] = restart ? z[i] : z[i] + beta * direction[i];
        const double denominator = reduce([&](std::size_t i) {
            product[i] = row_product(i, direction.data());
            return static_cast<long double>(direction[i]) * product[i];
        });
        if (denominator <= 0 || previous <= 0) throw std::runtime_error("Pressure solve lost positive definiteness");
        const double alpha = previous / denominator;
        require_finite(alpha, "pressure solve step");
        const double rr = reduce([&](std::size_t i) {
            pressure[i] += alpha * direction[i];
            residual[i] -= alpha * product[i];
            return static_cast<long double>(residual[i]) * residual[i];
        });
        ++result.iterations;
        result.residual_norm = std::sqrt(rr);
        if (result.residual_norm <= threshold) {
            true_residual();
            if (result.residual_norm <= threshold) return finish();
            precondition();
            restart = true;
            previous = reduce([&](std::size_t i) { return static_cast<long double>(residual[i]) * z[i]; });
            continue;
        }
        precondition();
        const double following = reduce([&](std::size_t i) { return static_cast<long double>(residual[i]) * z[i]; });
        beta = following / previous;
        require_finite(beta, "pressure solve recurrence");
        restart = false;
        previous = following;
    }
    throw std::runtime_error("Pressure solve did not converge; no state published");
}
}
LinearSolveResult solve_components_cached(Context& c, ComponentSolveCache& handle, const LinearSystem& sys,
    const std::vector<double>& b, std::vector<double>& x, double rtol, double atol, std::size_t maximum,
    std::size_t parallel_rows) {
    if (c.exec.pool.size() != 1 || c.settings.game_profile == MacGameProfile::base_game_floor)
        return solve_components(c, sys, b, x, rtol, atol, maximum, parallel_rows);
    if (c.settings.preconditioner != PressurePreconditioner::incomplete_cholesky || parallel_rows == 0)
        throw std::invalid_argument("Parallel component solves need the IC preconditioner and a size");
    auto& cache = *handle.impl;
    const auto& g = c.g;
    bool same = cache.valid && cache.parallel_rows == parallel_rows && cache.active.size() == g.N &&
        cache.open_x.size() == g.NX && cache.open_y.size() == g.NY;
    for (std::size_t i = 0; same && i < g.N; ++i) same = cache.active[i] == sys.active[i];
    for (std::size_t f = 0; same && f < g.NX; ++f) same = cache.open_x[f] == (sys.wx[f] != 0);
    for (std::size_t f = 0; same && f < g.NY; ++f) same = cache.open_y[f] == (sys.wy[f] != 0);
    if (!same) {
        cache.valid = false;
        cache.active.assign(sys.active.begin(), sys.active.end());
        cache.open_x.resize(g.NX); cache.open_y.resize(g.NY);
        for (std::size_t f = 0; f < g.NX; ++f) cache.open_x[f] = sys.wx[f] != 0;
        for (std::size_t f = 0; f < g.NY; ++f) cache.open_y[f] = sys.wy[f] != 0;
        cache.parallel_rows = parallel_rows;
        rebuild_structure(cache, c, sys);
        cache.valid = true;
    }
    // Couplings to an inactive cell (none in the game kernel's systems) keep
    // the general path.
    if (cache.boundary) return solve_components(c, sys, b, x, rtol, atol, maximum, parallel_rows);
    const auto parts = cache.components.size();
    x.assign(g.N, 0);
    const double local_atol = parts == 0 ? atol : .5 * atol / std::sqrt(static_cast<double>(parts));
    LinearSolveResult result; result.converged = true; DD squared;
    c.counters.maximum_linear_components = std::max(c.counters.maximum_linear_components, parts);
    for (const auto& component : cache.components) {
        // PressureSystem: compliance, then each face's conductance on both
        // rows in face order; the inverse diagonal is the reciprocal.
        const auto count = component.cells.size();
        cache.compliance.resize(count); cache.rhs.resize(count); cache.inverse_diagonal.resize(count);
        cache.conductance.resize(component.faces.size());
        for (std::size_t k = 0; k < count; ++k) {
            const double value = sys.diag[component.cells[k]];
            require_finite(value, "pressure compliance");
            if (value < 0) throw std::invalid_argument("Negative pressure compliance");
            cache.compliance[k] = value; cache.inverse_diagonal[k] = value; cache.rhs[k] = b[component.cells[k]];
        }
        bool anchored = false;
        for (std::size_t k = 0; k < count; ++k) anchored = anchored || cache.compliance[k] > 0;
        for (std::size_t f = 0; f < component.faces.size(); ++f) {
            const auto& face = component.faces[f];
            const double value = (face.x ? sys.wx : sys.wy)[face.source];
            require_finite(value, "pressure face conductance");
            if (value < 0) throw std::invalid_argument("Invalid pressure face");
            cache.conductance[f] = value;
            cache.inverse_diagonal[face.left] += value;
            cache.inverse_diagonal[face.right] += value;
        }
        if (!anchored) throw std::invalid_argument("Pressure component has no compressibility or pressure reference");
        for (std::size_t k = 0; k < count; ++k) {
            require_finite(cache.inverse_diagonal[k], "pressure diagonal");
            cache.inverse_diagonal[k] = 1 / cache.inverse_diagonal[k];
            require_finite(cache.inverse_diagonal[k], "pressure preconditioner");
        }
        PressureSolveResult solved;
        const auto block = count >= parallel_rows ? parallel_reduction_rows : std::max<std::size_t>(count, 1);
        try { solved = solve_cached_component(cache, component, 0, .5 * rtol, local_atol, maximum, block); }
        catch (const std::runtime_error&) { throw ComponentPressureSolveFailure{}; }
        for (std::size_t k = 0; k < count; ++k) x[component.cells[k]] = solved.pressure[k];
        result.iterations += solved.iterations; squared.add(solved.residual_norm * solved.residual_norm);
        c.counters.incomplete_cholesky_solves += solved.preconditioner_used == PressurePreconditioner::incomplete_cholesky ? 1 : 0;
        c.counters.incomplete_cholesky_fallbacks += solved.preconditioner_fallback ? 1 : 0;
    }
    result.residual = std::sqrt(squared.value()); return result;
}
LinearSolveResult solve_linear(Context& c, const LinearSystem& sys, const std::vector<double>& b, std::vector<double>& x,
    double rtol, double atol, std::size_t maximum, bool try_jacobi) {
    if (c.settings.preconditioner == PressurePreconditioner::incomplete_cholesky && !(try_jacobi && c.g.N >= 256))
        return solve_component_ic(c, sys, b, x, rtol, atol, maximum);
    auto& ws = c.ws; const auto& g = c.g; const auto N = g.N;
    for (auto* v : {&ws.lr, &ws.lz, &ws.lp, &ws.lq, &ws.lAp, &ws.ldiag}) v->resize(N);
    x.assign(N, 0);
    // Heat-capacity-dominated conduction usually needs only a few Jacobi-PCG
    // iterations. Avoid building a hierarchy (and evicting pressure topology)
    // for it. A bounded probe retains the full geometric solve for stiff heat
    // systems, from the same zero correction and at the original tolerance.
    const bool probe = try_jacobi && c.settings.preconditioner != PressurePreconditioner::jacobi && g.N >= 256;
    const bool multigrid = !probe && c.settings.preconditioner == PressurePreconditioner::automatic && g.N >= 256 && prepare_multigrid(c, sys);
    // Jacobi: inverse of the full operator diagonal.
    c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) {
        for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
            if (!sys.active[i]) { ws.ldiag[i] = 0; continue; }
            const double d = sys.diag[i] + sys.wx[g.left_face(i)] + sys.wx[g.right_face(i)] + sys.wy[g.bottom_face(i)] + sys.wy[g.top_face(i)];
            ws.ldiag[i] = 1 / d;
        }
    });
    const auto precondition = [&] {
        if (!multigrid) {
            c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) {
                for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) ws.lz[i] = ws.lr[i] * ws.ldiag[i];
            });
            return;
        }
        if (!ws.pressure_multigrid->apply(ws.lr, ws.lz)) reject("Production multigrid application failed");
        ++c.counters.multigrid_cycles;
    };
    const auto dot = [&](const std::vector<double>& a, const std::vector<double>& bb) {
        return combine(c.exec.partials<double>([&](std::size_t y0, std::size_t y1, std::size_t) {
            double s = 0; for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) s += a[i] * bb[i]; return s;
        }));
    };
    std::copy(b.begin(), b.end(), ws.lr.begin());
    const double bb = dot(ws.lr, ws.lr);
    const double threshold = std::max(atol, rtol * std::sqrt(bb));
    LinearSolveResult result; result.residual = std::sqrt(bb);
    if (!(result.residual > threshold)) { result.converged = true; return result; }
    precondition();
    std::copy(ws.lz.begin(), ws.lz.end(), ws.lp.begin());
    double rz = dot(ws.lr, ws.lz);
    double beta = 0;
    for (std::size_t it = 1; it <= (probe ? std::min<std::size_t>(32, maximum) : maximum); ++it) {
        // p = z + beta p_old (double buffered) fused with A p and p.Ap.
        double pAp = 0;
        if (it == 1) apply(c, sys, ws.lp, ws.lAp, &ws.lp, &pAp);
        else {
            apply_direction(c, sys, ws.lz, ws.lp, beta, ws.lq, ws.lAp, pAp);
            ws.lp.swap(ws.lq);
        }
        if (!(pAp > 0)) break;
        const double alpha = rz / pAp;
        if (!multigrid) {
            // x, r update fused with the Jacobi preconditioner and both dots.
            double rr = 0, rz1 = 0;
            auto parts = c.exec.partials<std::array<double, 2>>([&](std::size_t y0, std::size_t y1, std::size_t) {
                std::array<double, 2> s{0, 0};
                for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
                    x[i] += alpha * ws.lp[i];
                    const double r = ws.lr[i] - alpha * ws.lAp[i];
                    ws.lr[i] = r; const double z = r * ws.ldiag[i]; ws.lz[i] = z;
                    s[0] += r * r; s[1] += r * z;
                }
                return s;
            });
            DD sum_rr, sum_rz; for (const auto& v : parts) { sum_rr.add(v[0]); sum_rz.add(v[1]); }
            rr = sum_rr.value(); rz1 = sum_rz.value();
            result.iterations = it; result.residual = std::sqrt(rr);
            if (result.residual <= threshold) { result.converged = true; break; }
            beta = rz1 / rz; rz = rz1;
            continue;
        }
        const auto rr = combine(c.exec.partials<double>([&](std::size_t y0, std::size_t y1, std::size_t) {
            double s = 0;
            for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
                x[i] += alpha * ws.lp[i]; ws.lr[i] -= alpha * ws.lAp[i]; s += ws.lr[i] * ws.lr[i];
            }
            return s;
        }));
        result.iterations = it; result.residual = std::sqrt(rr);
        if (result.residual <= threshold) { result.converged = true; break; }
        precondition();
        const double rz1 = dot(ws.lr, ws.lz); beta = rz1 / rz; rz = rz1;
    }
    if (probe && !result.converged) {
        auto geometric = solve_linear(c, sys, b, x, rtol, atol, maximum);
        geometric.iterations += result.iterations;
        return geometric;
    }
    return result;
}

void solve_pressure_moments_linear(Context& c, const LinearSystem& sys, const std::vector<double>& compliance,
    const std::vector<double>& ra, const std::vector<double>& rb, std::vector<double>& da, std::vector<double>& db,
    double rtol, double atol) {
    const auto& g = c.g; const auto N = g.N;
    auto& scratch = c.ws.moments->linear;
    for (auto& v : scratch) v.resize(N);
    auto& ba = scratch[0]; auto& bb = scratch[1];
    auto& r = scratch[2]; auto& z = scratch[3]; auto& p = scratch[4]; auto& Ap = scratch[5];
    auto& Zp = scratch[6]; auto& ZZp = scratch[7]; auto& pre = scratch[8]; auto& intermediate = scratch[9];
    const bool multigrid = c.settings.preconditioner == PressurePreconditioner::automatic && N >= 256 && prepare_multigrid(c, sys);
    const auto apply_Z = [&](const std::vector<double>& v, std::vector<double>& out) {
        const auto pressure = [&](Index i) { return i != none && sys.active[i] ? v[i] / compliance[i] : 0.; };
        c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) {
            for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
                if (!sys.active[i]) { out[i] = 0; continue; }
                const auto l = g.left_face(i), rr = g.right_face(i), b = g.bottom_face(i), t = g.top_face(i);
                const double own = pressure(static_cast<Index>(i));
                out[i] = (sys.diag[i] - compliance[i]) * own +
                    sys.wx[l] * (own - pressure(g.xl[l])) + sys.wx[rr] * (own - pressure(g.xr[rr])) +
                    sys.wy[b] * (own - pressure(g.yl[b])) + sys.wy[t] * (own - pressure(g.yr[t]));
            }
        });
    };
    const auto dot = [&](const std::vector<double>& a, const std::vector<double>& b) {
        return combine(c.exec.partials<double>([&](std::size_t y0, std::size_t y1, std::size_t) {
            double s = 0;
            for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) if (sys.active[i]) s += a[i] * (b[i] / compliance[i]);
            return s;
        }));
    };
    // Z = dt² H C / 12, H the positive pressure graph operator and C the
    // inverse compliance. The block Jacobian is [[I+2Z,-sqrt(3)Z],
    // [sqrt(3)Z,I-Z]]. Its Schur polynomial S=I+Z+Z² is positive in the
    // C-weighted inner product. (I+Z)² is spectrally equivalent, with
    // eigenvalue ratio in [3/4,1]. Both factors use the SAME fixed V-cycle.
    apply_Z(ra, Zp); apply_Z(rb, ZZp);
    const double root3 = std::sqrt(3.);
    c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) {
        for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
            ba[i] = ra[i] - Zp[i] + root3 * ZZp[i];
            bb[i] = rb[i] - root3 * Zp[i] + 2 * ZZp[i];
        }
    });
    const double weights = combine(c.exec.partials<double>([&](std::size_t y0, std::size_t y1, std::size_t) {
        double s = 0; for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) if (sys.active[i]) s += 1 / compliance[i]; return s;
    }));
    const auto precondition = [&] {
        if (multigrid) {
            if (!c.ws.pressure_multigrid->apply(r, pre)) reject("Two-moment multigrid application failed");
            c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) {
                for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) intermediate[i] = sys.active[i] ? compliance[i] * pre[i] : 0;
            });
            if (!c.ws.pressure_multigrid->apply(intermediate, pre)) reject("Two-moment multigrid application failed");
            c.counters.multigrid_cycles += 2;
            c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) {
                for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) z[i] = sys.active[i] ? compliance[i] * pre[i] : 0;
            });
        } else c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) {
            for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
                if (!sys.active[i]) { z[i] = 0; continue; }
                const double d = sys.diag[i] + sys.wx[g.left_face(i)] + sys.wx[g.right_face(i)] +
                    sys.wy[g.bottom_face(i)] + sys.wy[g.top_face(i)];
                const double inverse = compliance[i] / d;
                z[i] = (r[i] * inverse) * inverse;
            }
        });
    };
    const auto solve = [&](const std::vector<double>& rhs, std::vector<double>& out) {
        out.assign(N, 0); std::copy(rhs.begin(), rhs.end(), r.begin());
        const double threshold = std::max(atol * std::sqrt(weights), rtol * std::sqrt(dot(r, r)));
        if (std::sqrt(dot(r, r)) <= threshold) return;
        precondition(); std::copy(z.begin(), z.end(), p.begin());
        double rz = dot(r, z);
        for (std::size_t iteration = 0; iteration < 4000; ++iteration) {
            apply_Z(p, Zp); apply_Z(Zp, ZZp);
            c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) {
                for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) Ap[i] = p[i] + Zp[i] + ZZp[i];
            });
            const double pAp = dot(p, Ap);
            if (!(pAp > 0) || !std::isfinite(pAp)) reject("Two-moment Schur operator lost positivity");
            const double alpha = rz / pAp;
            c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) {
                for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) { out[i] += alpha * p[i]; r[i] -= alpha * Ap[i]; }
            });
            ++c.counters.linear;
            if (std::sqrt(dot(r, r)) <= threshold) return;
            precondition(); const double next = dot(r, z), beta = next / rz; rz = next;
            c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) {
                for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) p[i] = z[i] + beta * p[i];
            });
        }
        reject("Two-moment Schur solve did not converge");
    };
    solve(ba, da); solve(bb, db);
}

void face_inertias(const Context& c, const std::vector<double>& mass, std::vector<double>& ix, std::vector<double>& iy) {
    const auto& g = c.g; ix.resize(g.NX); iy.resize(g.NY);
    c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t b) {
        for (std::size_t row = y0; row < y1; ++row) for (std::size_t col = 0; col < g.XW; ++col) {
            const auto f = g.xface(col, row);
            ix[f] = (g.xl[f] == none ? 0 : .5 * mass[g.xl[f]]) + (g.xr[f] == none ? 0 : .5 * mass[g.xr[f]]);
        }
        for (std::size_t row = y0; row < g.block_yrow_end(b); ++row) for (std::size_t col = 0; col < g.W; ++col) {
            const auto f = g.yface(col, row);
            iy[f] = (g.yl[f] == none ? 0 : .5 * mass[g.yl[f]]) + (g.yr[f] == none ? 0 : .5 * mass[g.yr[f]]);
        }
    });
}
void cell_masses(const Context& c, const Fields& f, std::vector<double>& out) {
    const auto& g = c.g; out.resize(g.N);
    c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) {
        for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
            double m = 0; for (std::size_t k = 0; k < c.model.K(); ++k) m += f.m[k * g.N + i];
            out[i] = m;
        }
    });
}
void evaluate_cells(Context& c, const Fields& f, EosCache& e) {
    const auto& g = c.g;
    auto& ws = c.ws;
    if (e.kind.size() != g.N) { e.resize(g.N); ws.stats_valid = false; }
    const bool refresh = !ws.stats_valid;
    c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) {
        for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
            if (g.wall[i]) { e.kind[i] = solid_cell; continue; }
            Stats s;
            if (refresh) {
                s = cell_stats(c.model, f, g.N, i);
                e.M[i] = s.M; e.Cv[i] = s.Cv; e.off[i] = s.off; e.nR[i] = s.nR; e.Nl[i] = s.Nl; e.kind[i] = s.kind;
            } else s = Stats{e.M[i], e.Cv[i], e.off[i], e.nR[i], e.Nl[i], e.kind[i]};
            if (s.kind == empty) { if (f.U[i] != 0) reject("Production kernel empty cell has internal energy"); continue; }
            const double V = material_volume(f, g.V, i);
            if (s.kind == gas_cell) {
                const double A = f.U[i] - s.off;
                if (!(A > 0)) reject("Production kernel cell left its EOS domain");
                const double T = A / s.Cv, p = s.nR * T / V;
                e.T[i] = T; e.p[i] = p; e.Vg[i] = V; e.Eel[i] = 0; e.Ceq[i] = s.Cv; e.comp[i] = s.Cv / (s.Cv + s.nR) * V / p; e.lcomp[i] = 0;
                continue;
            }
            if (s.kind == liquid_cell && !refresh) {
                // Zero thermal expansion: pressure, elastic energy and
                // compliance depend only on masses and volume.
                const double T = (f.U[i] - s.off - e.Eel[i]) / s.Cv;
                if (!(T > 0)) reject("Production kernel cell left its EOS domain");
                e.T[i] = T;
                continue;
            }
            State st;
            if (!evaluate_state(c.model, f, g.N, i, s, f.U[i], V, st)) reject("Production kernel cell left its EOS domain");
            e.T[i] = st.T; e.p[i] = st.p; e.Vg[i] = st.Vg; e.Eel[i] = st.Eel; e.Ceq[i] = st.Ceq; e.comp[i] = st.comp; e.lcomp[i] = st.liquid_comp;
        }
    });
    if (refresh) {
        // Face inertias depend only on masses: refresh them with the statistics.
        face_inertias(c, e.M, ws.ix, ws.iy);
        ws.inv_ix.resize(g.NX); ws.inv_iy.resize(g.NY);
        c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t blk) {
            for (std::size_t fidx = y0 * g.XW; fidx < y1 * g.XW; ++fidx) ws.inv_ix[fidx] = ws.ix[fidx] > 0 ? 1 / ws.ix[fidx] : 0;
            for (std::size_t fidx = y0 * g.W; fidx < g.block_yrow_end(blk) * g.W; ++fidx) ws.inv_iy[fidx] = ws.iy[fidx] > 0 ? 1 / ws.iy[fidx] : 0;
        });
    }
    ws.stats_valid = true;
    c.counters.eos += g.N;
}

void stage_pressure_transport(Context& c, Fields& f, double dt, Accounts& acc) {
    prepare_primary_remainders(f);
    auto& ws = c.ws; const auto& g = c.g; const auto N = g.N;
    const auto t_start = std::chrono::steady_clock::now();
    const auto lap = [&](double& slot, std::chrono::steady_clock::time_point& t) {
        if (!c.options.collect_stage_timing) return;
        const auto now = std::chrono::steady_clock::now();
        slot += std::chrono::duration<double, std::milli>(now - t).count(); t = now;
    };
    auto t_mark = t_start;
    const double A = g.A, gravity = c.settings.gravity_m_per_s2;
    evaluate_cells(c, f, ws.eos);
    auto& e = ws.eos;
    // Sharp interfaces weigh y faces by their PLIC half-cell masses; the
    // transport stage time-centres the weight (MAC_PRESSURE.md).
    const bool sharp_gravity = gravity != 0 && c.settings.reconstruction == MacMaterialReconstruction::interface_plic;
    if (sharp_gravity) interface_gravity_weights(c, f, true, ws.gravity_weight_start);
    // Void phase without sharp weights: a resting liquid surface moves its top
    // face's half of its weight to its bottom face (MAC_VACUUM.md, "Liquid surfaces").
    bool rested = false;
    if (gravity != 0 && !sharp_gravity && !f.Vo.empty()) {
        ws.gravity_weight_start = ws.iy;
        for (std::size_t i = 0; i < N; ++i) if (resting_surface(c, f, e.M, i)) {
            const double half = .5 * e.M[i];
            ws.gravity_weight_start[g.top_face(i)] -= half; ws.gravity_weight_start[g.bottom_face(i)] += half; rested = true;
        }
    }
    const auto weight_y = [&](std::size_t fidx) { return sharp_gravity || rested ? ws.gravity_weight_start[fidx] : ws.iy[fidx]; };
    ws.anchor.resize(N);
    for (auto* v : {&ws.dV, &ws.dW, &ws.cc, &ws.dU, &ws.bdiag}) v->assign(N, 0);
    for (auto* v : {&ws.res, &ws.tdV, &ws.tdW, &ws.tcc, &ws.tres, &ws.Vmat, &ws.tdU}) v->resize(N);
    for (auto* v : {&ws.dp0x, &ws.wx, &ws.qx}) v->assign(g.NX, 0);
    for (auto* v : {&ws.dp0y, &ws.wy, &ws.qy, &ws.body_work_y}) v->assign(g.NY, 0);
    ws.full_qx.reset(g.NX); ws.full_qy.reset(g.NY);
    ws.sx.resize(g.NX); ws.sy.resize(g.NY);
    // Void phase (MAC_VACUUM.md, "Liquid surfaces"): a liquid-only cell holding
    // void sits at zero pressure, its liquid at zero-pressure density. It is no
    // pressure unknown, but its mass still loads its faces.
    const bool voids = c.settings.vacuum == MacVacuumRepresentation::void_phase;
    ws.surface.assign(N, 0); ws.void0.assign(N, 0);
    if (voids && !f.Vo.empty()) for (std::size_t i = 0; i < N; ++i) ws.surface[i] = e.kind[i] == liquid_cell && f.Vo[i] < g.V;
    const auto occupied = [&](Index i) {
        return i != none && (e.kind[i] == gas_cell || e.kind[i] == liquid_cell || e.kind[i] == mixed_cell) && !ws.surface[i];
    };
    const auto p0 = [&](Index i) { return occupied(i) ? e.p[i] : 0.; };
    // Sharp-interface boiling (MAC_PHASE_CHANGE.md, "Sharp interfaces"): a
    // boiling cell converts the heat conducted to its interface, Q / L, at
    // constant pressure at the end of this stage. Its unknown is the total
    // volume change D = x + s: x along the cell's isentrope from its state
    // before that heat, then the vapour volume s at the endpoint pressure p1.
    // The face force uses the mean work pressure (W(x) + p1 s) / D, so U + K
    // closes exactly.
    const bool boiling = c.sharp_boiling() && ws.interface_heat.size() == N;
    constexpr double R = experimental::reference_gas_constant;
    // The converted mass at the endpoint pressure change dp1: the interface
    // temperature follows T_sat(p0 + dp1), so the conducted heat changes by
    // -G dT_sat/dp dp1 (implicit; held at the start pressure, the coupling is
    // an explicit loop whose gain grows like dt/h).
    const auto interface_mass = [&](std::size_t i, double dp1) {
        const auto& pair = c.phase_pairs[ws.interface_pair[i] - 1];
        const double heat = ws.interface_heat[i] - ws.interface_conductance[i] * ws.interface_slope[i] * dp1;
        return std::clamp(heat / ws.interface_latent[i], -f.m[pair.vapour * N + i], f.m[pair.liquid * N + i]);
    };
    // Specific volume change of the conversion at p1 and the interface temperature.
    const auto converted_volume = [&](std::size_t i, double p1) {
        const auto& pair = c.phase_pairs[ws.interface_pair[i] - 1];
        const auto& sv = c.model.species[pair.vapour]; const auto& sl = c.model.species[pair.liquid];
        return R * ws.interface_temperature[i] / (sv.molar_mass * p1) - 1 / (sl.rho0 * (1 + p1 / sl.bulk));
    };
    // A boiling cell's isentrope starts from its state before the conducted
    // heat: that heat is converted, not held, so it must not raise the start
    // pressure (in a mostly-gas cell it would by hundreds of pascals). The
    // heat is still in U; the endpoint takes it back as latent heat.
    if (boiling) {
        for (auto* v : {&ws.interface_latent, &ws.interface_mass, &ws.interface_volume}) v->assign(N, 0);
        c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) {
            for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
                if (ws.interface_pair[i] == 0 || ws.interface_heat[i] == 0 || e.kind[i] != mixed_cell) continue;
                const auto& pair = c.phase_pairs[ws.interface_pair[i] - 1];
                State before;
                if (!evaluate_state(c.model, f, N, i, Stats{e.M[i], e.Cv[i], e.off[i], e.nR[i], e.Nl[i], e.kind[i]},
                        f.U[i] - ws.interface_heat[i], material_volume(f, g.V, i), before)) continue;
                // At the cell's own temperature, so conversion leaves it unchanged.
                const double latent = mac_latent_heat(*c.thermodynamics, pair.liquid_profile, pair.vapour_profile, before.T, before.p);
                if (!(latent > 0)) continue;
                e.T[i] = before.T; e.p[i] = before.p; e.Vg[i] = before.Vg; e.Eel[i] = before.Eel; e.Ceq[i] = before.Ceq; e.comp[i] = before.comp;
                ws.interface_latent[i] = latent;
                ws.interface_mass[i] = interface_mass(i, 0);
                ws.interface_volume[i] = ws.interface_mass[i] * converted_volume(i, e.p[i]);
            }
        });
    }
    if (boiling) for (std::size_t i = 0; i < N; ++i) if (ws.interface_latent[i] != 0) ws.dV[i] = ws.interface_volume[i]; // root starts at x = 0
    const auto weight = [&](double inertia) { return .5 * dt * A * (dt * A / inertia); };
    const auto face_setup = [&](const std::vector<Index>& fl, const std::vector<Index>& fr, const std::vector<double>& I,
        std::vector<double>& dp0, std::vector<double>& w, std::size_t fidx) {
        const auto l = fl[fidx], r = fr[fidx];
        if (I[fidx] > 0) dp0[fidx] = p0(l) - p0(r);
        if (l != none && r != none && I[fidx] > 0 && occupied(l) && occupied(r) && !g.closed(&fl == &g.yl, fidx)) w[fidx] = weight(I[fidx]);
    };
    // Midpoint sweep of a movable face with work-pressure increments:
    // swept = A dt (u0 + dt (A dp + I a) / (2 I)) = c0 + c1 (dW_l - dW_r),
    // c0 = A dt u0 + (A dt^2 / (2 I)) (A dp0 + I a), c1 = A^2 dt^2 / (2 I).
    // J and I are fixed during the root, so both coefficients are formed once.
    const auto movable_x = [&](std::size_t fidx) { return g.xl[fidx] != none && g.xr[fidx] != none && ws.ix[fidx] > 0 && !g.closed(false, fidx); };
    const auto movable_y = [&](std::size_t fidx) { return g.yl[fidx] != none && g.yr[fidx] != none && ws.iy[fidx] > 0 && !g.closed(true, fidx); };
    const auto coefficients = [&](double J, double I, double dp0, double accel, double& c0, double& c1, double W) {
        const double half = A * dt * dt / (2 * I);
        c0 = A * dt * (J / I) + half * (A * dp0 + W * accel);
        c1 = half * A;
    };
    const auto& c0x = ws.sx; const auto& c1x = ws.keep; const auto& c0y = ws.sy; const auto& c1y = ws.jsum;
    const auto interface_work = [&](std::size_t i, double D, Work& out) {
        if (ws.anchor[i].kind != mixed_cell || D == 0) return false;
        const auto& pair = c.phase_pairs[ws.interface_pair[i] - 1];
        const double vapour_mass = f.m[pair.vapour * N + i], liquid_mass = f.m[pair.liquid * N + i];
        const double molar = c.model.species[pair.vapour].molar_mass;
        // At x: the isentrope, the converted mass and vapour volume, and
        // dD/dx = 1 + (ds/dp1) dp1/dx with dp1/dx by a one-sided difference
        // (the secant law is too stiff a tangent once the vapour outgrows the
        // cell's gas).
        struct At { Work iso; double mass = 0, s = 0, stiffness = 0, stretch = 0; };
        const auto at = [&](double x, At& a) {
            Work nearby; const double step = std::max(1e-6 * std::abs(x), 1e-9 * g.V);
            const bool robust = c.settings.subcell_void == MacSubcellVoidPolicy::expand_unresolved_gas;
            if (!isentropic_work(c.model, f, N, i, ws.anchor[i], x, a.iso, robust) ||
                !isentropic_work(c.model, f, N, i, ws.anchor[i], x - step, nearby, robust)) return false;
            a.mass = interface_mass(i, a.iso.dp1); a.s = a.mass * converted_volume(i, a.iso.p1);
            a.stiffness = (a.iso.dp1 - nearby.dp1) / step;
            const double free = a.mass > -vapour_mass && a.mass < liquid_mass ? 1. : 0.;
            const double ds = -a.mass * R * ws.interface_temperature[i] / (molar * a.iso.p1 * a.iso.p1) -
                free * converted_volume(i, a.iso.p1) * ws.interface_conductance[i] * ws.interface_slope[i] / ws.interface_latent[i];
            a.stretch = 1 + ds * a.stiffness;
            return a.stiffness < 0 && a.stretch > 0 && std::isfinite(a.s);
        };
        // x + s(x) = D by Newton: the implicit coupling makes s far more
        // sensitive to x than x itself, so a fixed point would diverge.
        double x = D - ws.interface_volume[i];
        At a;
        for (int it = 0;; ++it) {
            if (it == 64 || !at(x, a)) return false;
            const double residual = (x + a.s) - D;
            if (std::abs(residual) <= 4 * eps * (std::abs(D) + std::abs(a.s))) break;
            const double next = x - residual / a.stretch;
            if (next == x) break;
            x = next;
        }
        ws.interface_volume[i] = a.s; ws.interface_mass[i] = a.mass;
        const double p1 = a.iso.p1, s = a.s;
        out.dU = a.iso.dU - p1 * s;
        // Mean work pressure minus p0, in increments: (dW x + dp1 s) / D.
        out.dW = (a.iso.dW * x + a.iso.dp1 * s) / D;
        out.derivative = a.stiffness / a.stretch;
        out.p1 = p1;
        return std::isfinite(out.dW) && std::isfinite(out.dU) && out.derivative < 0;
    };
    // Residual evaluation for volume changes dv; fills dw, cc, res; returns
    // the max relative residual, or +inf if a trial left the EOS domain.
    const auto evaluate = [&](const std::vector<double>& dv, std::vector<double>& dw, std::vector<double>& cc,
        std::vector<double>& res, std::vector<double>& du) {
        auto bad = c.exec.partials<int>([&](std::size_t y0, std::size_t y1, std::size_t) {
            int failed = 0;
            for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
                if (!occupied(static_cast<Index>(i))) { dw[i] = 0; continue; }
                Work w;
                const bool source = boiling && ws.interface_latent[i] != 0;
                if (!(source ? interface_work(i, dv[i], w) : isentropic_work(c.model, f, N, i, ws.anchor[i], dv[i], w,
                    c.settings.subcell_void == MacSubcellVoidPolicy::expand_unresolved_gas))) { failed = 1; continue; }
                dw[i] = w.dW; cc[i] = -1 / w.derivative; du[i] = w.dU;
                if (!std::isfinite(cc[i]) || !(cc[i] > 0)) failed = 1;
            }
            return failed;
        });
        c.counters.isentropic += N;
        for (const int b : bad) if (b) return std::numeric_limits<double>::infinity();
        auto norms = c.exec.partials<double>([&](std::size_t y0, std::size_t y1, std::size_t) {
            double norm = 0;
            for (std::size_t row = y0; row < y1; ++row) {
                const std::size_t base = row * g.W, xb = row * g.XW;
                const std::size_t down = row > 0 ? base - g.W : base + (g.H - 1) * g.W;
                const std::size_t up = row + 1 < g.H ? base + g.W : base - (g.H - 1) * g.W;
                const std::size_t yb = row * g.W, yt = ((row + 1) % g.YH) * g.W;
                for (std::size_t col = 0; col < g.W; ++col) {
                    const auto i = base + col;
                    if (!occupied(static_cast<Index>(i))) { res[i] = 0; continue; }
                    const auto left = col > 0 ? i - 1 : i + g.W - 1, right = col + 1 < g.W ? i + 1 : i + 1 - g.W;
                    const auto lf = xb + col, rf = xb + (col + 1) % g.XW, bf = yb + col, tf = yt + col;
                    // Uncoupled faces have zero coefficients; empty or solid
                    // neighbours have zero increments (dw = 0).
                    const double di = dw[i];
                    const double sl = c0x[lf] + c1x[lf] * (dw[left] - di);
                    const double sr = c0x[rf] + c1x[rf] * (di - dw[right]);
                    const double sb = c0y[bf] + c1y[bf] * (dw[down + col] - di);
                    const double st = c0y[tf] + c1y[tf] * (di - dw[up + col]);
                    // Left/bottom faces moving +: the cell shrinks by their sweep.
                    res[i] = (((dv[i] - ws.void0[i]) + sl) - sr) + (sb - st);
                    // Relative to the swept cell, not its material volume: face
                    // sweep rounding scales with the grid cell.
                    norm = std::max(norm, std::abs(res[i]) / (g.V + dv[i]));
                }
            }
            return norm;
        });
        double norm = 0; for (const double v : norms) norm = std::max(norm, v);
        return norm;
    };
    const double tolerance = 16 * eps;
    // The void phase settles which liquid cells hold void by an active set: a
    // surface cell squeezed past its void joins the solve, its material volume
    // change offset by that void; a liquid cell pulled into tension opens one.
    for (int pass = 0;; ++pass) {
        // Anchors.
        c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) {
            for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
                auto& a = ws.anchor[i]; a = Anchor{}; a.kind = e.kind[i];
                if (e.kind[i] == empty || e.kind[i] == solid_cell) continue;
                a.V0 = material_volume(f, g.V, i); a.p0 = e.p[i]; a.T0 = e.T[i]; a.Vg0 = e.Vg[i]; a.Cv = e.Cv[i]; a.nR = e.nR[i];
                a.k = a.nR / a.Cv; a.comp = e.comp[i]; a.tension = voids && a.kind == liquid_cell;
                if (a.kind == liquid_cell) {
                    std::size_t count = 0, which = 0;
                    for (const auto k : c.model.liquids) if (f.m[k * N + i] != 0) { ++count; which = k; }
                    if (count == 1) {
                        const auto& sp = c.model.species[which];
                        a.single_liquid = true; a.K = sp.bulk; a.Nl = f.m[which * N + i] / sp.rho0; a.qmax = sp.maximum_ratio - 1;
                    } else if (a.tension) {
                        // Several liquids: the softest bounds the tension range.
                        a.K = std::numeric_limits<double>::infinity();
                        for (const auto k : c.model.liquids) if (f.m[k * N + i] != 0) a.K = std::min(a.K, c.model.species[k].bulk);
                    }
                }
                ws.cc[i] = 2 * a.comp; // -1/W'(0)
            }
        });
        // Anchor pressure differences and link coefficients per face, then the
        // boundary diagonals (faces to empty cells) gathered per cell.
        std::fill(ws.wx.begin(), ws.wx.end(), 0.); std::fill(ws.wy.begin(), ws.wy.end(), 0.);
        c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t blk) {
            for (std::size_t row = y0; row < y1; ++row) for (std::size_t col = 0; col < g.XW; ++col)
                face_setup(g.xl, g.xr, ws.ix, ws.dp0x, ws.wx, g.xface(col, row));
            for (std::size_t row = y0; row < g.block_yrow_end(blk); ++row) for (std::size_t col = 0; col < g.W; ++col)
                face_setup(g.yl, g.yr, ws.iy, ws.dp0y, ws.wy, g.yface(col, row));
        });
        c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) {
            for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
                if (!occupied(static_cast<Index>(i))) continue;
                double d = 0;
                const auto empty_side = [&](Index other, double inertia) { if (other != none && !occupied(other) && inertia > 0) d += weight(inertia); };
                const auto lf = g.left_face(i), rf = g.right_face(i), bf = g.bottom_face(i), tf = g.top_face(i);
                empty_side(g.xl[lf], ws.ix[lf]); empty_side(g.xr[rf], ws.ix[rf]);
                empty_side(g.yl[bf], ws.iy[bf]); empty_side(g.yr[tf], ws.iy[tf]);
                ws.bdiag[i] = d;
            }
        });
        for (auto* v : {&ws.sx, &ws.keep}) v->assign(g.NX, 0);   // sx: c0, keep: c1 (x faces)
        for (auto* v : {&ws.sy, &ws.jsum}) v->assign(g.NY, 0);   // sy: c0, jsum: c1 (y faces)
        c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t blk) {
            for (std::size_t row = y0; row < y1; ++row) for (std::size_t col = 0; col < g.XW; ++col) {
                const auto fidx = g.xface(col, row);
                if (movable_x(fidx)) coefficients(f.jx[fidx], ws.ix[fidx], ws.dp0x[fidx], 0, ws.sx[fidx], ws.keep[fidx], ws.ix[fidx]);
            }
            for (std::size_t row = y0; row < g.block_yrow_end(blk); ++row) for (std::size_t col = 0; col < g.W; ++col) {
                const auto fidx = g.yface(col, row);
                if (movable_y(fidx)) coefficients(f.jy[fidx], ws.iy[fidx], ws.dp0y[fidx], -gravity, ws.sy[fidx], ws.jsum[fidx], weight_y(fidx));
            }
        });
        lap(c.timing.eos_ms, t_mark);
        double norm = evaluate(ws.dV, ws.dW, ws.cc, ws.res, ws.dU);
        if (!std::isfinite(norm)) reject("Production pressure anchor left its EOS domain");
        LinearSystem sys; sys.g = &g; sys.wx = ws.wx; sys.wy = ws.wy;
        sys.active.assign(N, 0); sys.diag.assign(N, 0);
        c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) {
            for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) sys.active[i] = occupied(static_cast<Index>(i)) ? 1 : 0;
        });
        // The active cells and face weights stay fixed throughout this root.
        // Reuse their ordered graph; only the nonlinear diagonal and RHS vary.
        const auto layout = norm > tolerance && c.settings.preconditioner == PressurePreconditioner::incomplete_cholesky ?
            std::make_unique<ComponentLayout>(component_layout(c, sys)) : nullptr;
        std::size_t newton = 0;
        const std::size_t linear_limit = 4000;
        while (norm > tolerance) {
            if (newton == 80) reject("Production pressure root did not converge; retry smaller timestep");
            c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) {
                for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) sys.diag[i] = sys.active[i] ? ws.cc[i] + ws.bdiag[i] : 0;
            });
            // Inexact Newton: the linear error only needs to stay below the
            // quadratically convergent nonlinear residual (relative tolerance
            // ~0.1 * norm), with an absolute floor at a fraction of the target.
            const double rtol = std::clamp(.1 * norm, 1e-13, 1e-3), previous = norm;
            const auto solved = layout ? solve_component_ic(c, sys, ws.res, ws.sol, rtol, .05 * tolerance * g.V, linear_limit, layout.get()) :
                solve_linear(c, sys, ws.res, ws.sol, rtol, .05 * tolerance * g.V, linear_limit);
            c.counters.linear += solved.iterations;
            double fraction = 1; bool accepted = false;
            for (int attempt = 0; attempt < 48; ++attempt) {
                c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) {
                    for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) ws.tdV[i] = sys.active[i] ? ws.dV[i] - fraction * ws.cc[i] * ws.sol[i] : 0;
                });
                const double next = evaluate(ws.tdV, ws.tdW, ws.tcc, ws.tres, ws.tdU);
                if (std::isfinite(next) && (next <= tolerance || next < norm * (1 - 1e-4 * fraction))) {
                    ws.dV.swap(ws.tdV); ws.dW.swap(ws.tdW); ws.cc.swap(ws.tcc); ws.res.swap(ws.tres); ws.dU.swap(ws.tdU);
                    norm = next; accepted = true; break;
                }
                ++c.counters.line_search; fraction *= .5;
            }
            if (kernel_debug()) std::fprintf(stderr, "  newton %zu norm %.3e linear %zu res %.3e conv %d accepted %d\n", newton, norm, solved.iterations, solved.residual, solved.converged ? 1 : 0, accepted ? 1 : 0);
            // A residual within 256 times the closure target that no step can
            // reduce is rounding noise of the stiff liquid/gas coupling: the
            // root is converged. Larger stalls still retry smaller.
            if (!accepted && norm <= 256 * tolerance) break;
            if (!accepted) reject("Production pressure line search failed; retry smaller timestep");
            ++newton;
            // Within that band a step that does not halve the residual only
            // re-rounds it (one step from 1e-13 at quadratic convergence would
            // reach far below the floor): the root is converged.
            if (norm <= 256 * tolerance && norm > .5 * previous) break;
        }
        c.counters.newton += newton;
        if (!voids) break;
        const auto increment = [&](Index n) { return n != none && occupied(n) ? ws.dW[n] : 0.; };
        bool changed = false;
        for (std::size_t i = 0; i < N; ++i) {
            if (g.wall[i] || e.kind[i] != liquid_cell) continue;
            const auto cell = static_cast<Index>(i);
            if (ws.surface[i]) {
                const auto lf = g.left_face(i), rf = g.right_face(i), bf = g.bottom_face(i), tf = g.top_face(i);
                const double sl = c0x[lf] + c1x[lf] * (increment(g.xl[lf]) - increment(cell)), sr = c0x[rf] + c1x[rf] * (increment(cell) - increment(g.xr[rf]));
                const double sb = c0y[bf] + c1y[bf] * (increment(g.yl[bf]) - increment(cell)), st = c0y[tf] + c1y[tf] * (increment(cell) - increment(g.yr[tf]));
                if (g.V + ((sr - sl) + (st - sb)) < e.Nl[i]) { ws.surface[i] = 0; ws.void0[i] = g.V - material_volume(f, g.V, i); changed = true; }
            } else {
                Work w;
                if (!isentropic_work(c.model, f, N, i, ws.anchor[i], ws.dV[i], w)) reject("Production void-phase liquid left its EOS domain");
                if (w.p1 < -64 * eps * (ws.anchor[i].V0 / ws.anchor[i].comp)) { ws.surface[i] = 1; ws.void0[i] = 0; changed = true; }
            }
        }
        if (!changed) break;
        if (pass == 8) reject("Production void-phase surface set did not settle; retry smaller timestep");
        ws.dV.assign(N, 0);
        if (boiling) for (std::size_t i = 0; i < N; ++i) if (ws.interface_latent[i] != 0) ws.dV[i] = ws.interface_volume[i];
    }
    // Publish the root: canonical J, midpoint sweeps, U and material volume.
    const auto dWfinal = [&](Index i) { return occupied(i) ? ws.dW[i] : 0.; };
    struct Publish {
        DD pressure_wall_x, pressure_wall_y, fixed_body;
        PrimaryRounding primary_x, primary_y;
        EnergyRounding energy_rounding;
        double body = 0, energy = 0, jx = 0, jy = 0, closure = 0;
        std::size_t unresolved = 0; int failed = 0;
        std::vector<std::pair<Index, MacEnergy>> tiny_x, tiny_y;
    };
    auto published = c.exec.partials<Publish>([&](std::size_t y0, std::size_t y1, std::size_t blk) {
        Publish o;
        for (std::size_t row = y0; row < y1; ++row) for (std::size_t col = 0; col < g.XW; ++col) {
            const auto fidx = g.xface(col, row);
            if (!(ws.ix[fidx] > 0)) continue;
            const double I = ws.ix[fidx], J0 = f.jx[fidx];
            const double dp = ws.dp0x[fidx] + (dWfinal(g.xl[fidx]) - dWfinal(g.xr[fidx]));
            const double impulse = dt * A * dp;
            o.jx += std::abs(J0) + std::abs(impulse);
            if (!movable_x(fidx)) {
                // Sum pressure components before the common dt*A product.
                // Opposite wall pressures can cancel exactly even when each
                // large absolute pressure would round away its increment.
                o.pressure_wall_x.add(-ws.dp0x[fidx]);
                o.pressure_wall_x.add(-dWfinal(g.xl[fidx])); o.pressure_wall_x.add(dWfinal(g.xr[fidx]));
                continue;
            }
            const double J1 = J0 + impulse;
            o.primary_x.addition(J0, impulse, J1);
            f.jx[fidx] = J1; o.jx += std::abs(J1);
            const auto sweep = midpoint_sweep(J0, J1, I, dt, A); ws.qx[fidx] = sweep.value;
            if (!sweep.full.is_zero()) o.tiny_x.emplace_back(fidx, sweep.full);
            o.energy += .5 * J0 * (J0 / I) + .5 * J1 * (J1 / I);
        }
        for (std::size_t row = y0; row < g.block_yrow_end(blk); ++row) for (std::size_t col = 0; col < g.W; ++col) {
            const auto fidx = g.yface(col, row);
            if (!(ws.iy[fidx] > 0)) continue;
            const double I = ws.iy[fidx], J0 = f.jy[fidx];
            const double dp = ws.dp0y[fidx] + (dWfinal(g.yl[fidx]) - dWfinal(g.yr[fidx]));
            const double W = weight_y(fidx);
            const double gimp = -W * gravity * dt;
            const double impulse = gravity == 0 ? dt * A * dp : dt * (A * dp + (-W * gravity));
            o.body += gimp;
            o.jy += std::abs(J0) + std::abs(impulse) + std::abs(gimp);
            if (!movable_y(fidx)) {
                o.pressure_wall_y.add(-ws.dp0y[fidx]);
                o.pressure_wall_y.add(-dWfinal(g.yl[fidx])); o.pressure_wall_y.add(dWfinal(g.yr[fidx]));
                o.fixed_body.add(-gimp);
                continue;
            }
            const double J1 = J0 + impulse;
            o.primary_y.addition(J0, impulse, J1);
            f.jy[fidx] = J1; o.jy += std::abs(J1);
            const auto sweep = midpoint_sweep(J0, J1, I, dt, A); ws.qy[fidx] = sweep.value;
            if (!sweep.full.is_zero()) o.tiny_y.emplace_back(fidx, sweep.full);
            if (gravity != 0) ws.body_work_y[fidx] = gimp * (.5 * (J0 + J1) / I);
            o.energy += .5 * J0 * (J0 / I) + .5 * J1 * (J1 / I) + std::abs(ws.body_work_y[fidx]);
        }
        return o;
    });
    // Material volumes, U and the closure diagnostic after every sweep exists.
    // Void phase: a cell's swept volume holds its material volume and its
    // unchanged void (the face sweeps move the material); transport reads it.
    if (voids) ws.Vswept.resize(N);
    auto volumes = c.exec.partials<Publish>([&](std::size_t y0, std::size_t y1, std::size_t) {
        Publish o;
        for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
            if (g.wall[i]) continue;
            // Geometric volume change: right/top faces moving outward expand the cell.
            DD sweeps; sweeps.add(ws.qx[g.right_face(i)]); sweeps.add(-ws.qx[g.left_face(i)]);
            sweeps.add(ws.qy[g.top_face(i)]); sweeps.add(-ws.qy[g.bottom_face(i)]);
            DD swept; swept.add(g.V); swept.add(sweeps.hi); swept.add(sweeps.lo);
            if (ws.surface[i]) {
                // A liquid surface keeps its zero-pressure volume; the sweeps move its void.
                ws.Vmat[i] = e.Nl[i]; ws.Vswept[i] = std::max(swept.value(), e.Nl[i]);
                continue;
            }
            if (!occupied(static_cast<Index>(i))) {
                ws.Vmat[i] = swept.value();
                if (voids) ws.Vswept[i] = ws.Vmat[i];
                if (!(ws.Vmat[i] > 0)) o.failed = 1;
                continue;
            }
            const double before = f.U[i];
            double pressure_work = ws.dU[i];
            // Closing an initial liquid void changes material volume without
            // sweeping that void through a face. Pair U with the geometric
            // pressure work used by canonical J/K, not with that volume change.
            if (c.settings.compatible_void_pressure_work && ws.void0[i] != 0)
                pressure_work = -(p0(static_cast<Index>(i)) + ws.dW[i]) * sweeps.value();
            if (add_internal_energy(f, i, pressure_work, o.energy_rounding)) ++o.unresolved;
            o.energy += std::abs(before) + std::abs(pressure_work);

            ws.Vmat[i] = ws.anchor[i].V0 + ws.dV[i];
            if (voids) ws.Vswept[i] = !f.Vo.empty() && f.Vo[i] < g.V ? std::max(swept.value(), ws.Vmat[i]) : ws.Vmat[i];
            DD r; r.add(ws.dV[i]); r.add(-ws.void0[i]); r.add(-sweeps.hi); r.add(-sweeps.lo);
            o.closure = std::max(o.closure, std::abs(r.value()) / g.V);
        }
        return o;
    });
    double energy_ops = 0, jx_ops = 0, jy_ops = 0, closure = 0;
    DD pressure_wall_x, pressure_wall_y, fixed_body;
    for (const auto& o : published) {
        for (const auto& [face, value] : o.tiny_x) ws.full_qx.set(g.NX, face, value);
        for (const auto& [face, value] : o.tiny_y) ws.full_qy.set(g.NY, face, value);
        pressure_wall_x.add(o.pressure_wall_x); pressure_wall_y.add(o.pressure_wall_y); fixed_body.add(o.fixed_body);
        acc.body_y += o.body; energy_ops += o.energy; jx_ops += o.jx; jy_ops += o.jy;
        acc.primary[c.q_jx()].merge(o.primary_x); acc.primary[c.q_jy()].merge(o.primary_y);
    }
    acc.wall_x += pressure_wall_x.value() * dt * A;
    DD wall_y; wall_y.add(pressure_wall_y.value() * dt * A); wall_y.add(fixed_body);
    acc.wall_y += wall_y.value();
    for (const auto& o : volumes) {
        if (o.failed) reject("Production swept vacuum volume exhausted");
        acc.energy.merge(o.energy_rounding);
        energy_ops += o.energy; acc.unresolved += o.unresolved; closure = std::max(closure, o.closure);
    }
    acc.operands[c.q_jx()] += jx_ops; acc.operands[c.q_jy()] += jy_ops;
    acc.operands[c.q_energy()] += energy_ops;
    c.counters.maximum_volume_closure = std::max(c.counters.maximum_volume_closure, closure);
    // Each boiling cell's converted masses at the accepted root; the interval
    // audit takes the moved mass as each species' external change, and each
    // write's rounding.
    if (boiling) for (std::size_t i = 0; i < N; ++i) {
        if (ws.interface_latent[i] == 0 || e.kind[i] != mixed_cell) continue;
        Work accepted;
        if (!interface_work(i, ws.dV[i], accepted)) reject("Production interface conversion left its EOS domain");
        const double dm = ws.interface_mass[i];
        if (dm == 0) continue;
        const auto& pair = c.phase_pairs[ws.interface_pair[i] - 1];
        for (const auto& [k, change] : {std::pair{pair.liquid, -dm}, std::pair{pair.vapour, dm}}) {
            double& m = f.m[k * N + i]; const double before = m; m = before + change;
            acc.inventory[k].addition(before, change, m); acc.conversion[k].add(change);
            acc.operands[k] += std::abs(before) + std::abs(change);
        }
        (dm > 0 ? acc.evaporated : acc.condensed) += std::abs(dm); ++acc.interface_conversions;
        const auto st = cell_stats(c.model, f, N, i);
        e.M[i] = st.M; e.Cv[i] = st.Cv; e.off[i] = st.off; e.nR[i] = st.nR; e.Nl[i] = st.Nl; e.kind[i] = st.kind;
    }
    // Material-volume CFL.
    const double limit = c.settings.maximum_outgoing_volume_fraction;
    struct Cfl { int exceeded = 0; double ratio = 0; };
    auto cfl = c.exec.partials<Cfl>([&](std::size_t y0, std::size_t y1, std::size_t) {
        Cfl v;
        for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
            if (g.wall[i]) continue;
            double out = 0;
            const auto l = g.left_face(i), r = g.right_face(i), b = g.bottom_face(i), t = g.top_face(i);
            if (ws.qx[l] < 0) out -= ws.qx[l];
            if (ws.qx[r] > 0) out += ws.qx[r];
            if (ws.qy[b] < 0) out -= ws.qy[b];
            if (ws.qy[t] > 0) out += ws.qy[t];
            const double allowed = (voids ? ws.Vswept : ws.Vmat)[i] * limit;
            if (out > allowed) v.exceeded = 1;
            if (out > 0 && allowed > 0) v.ratio = std::max(v.ratio, out / allowed);
        }
        return v;
    });
    for (const auto& v : cfl) ws.cfl_ratio = std::max(ws.cfl_ratio, v.ratio);
    for (const auto& v : cfl) if (v.exceeded) reject("Production material-volume CFL exceeded");
    lap(c.timing.pressure_ms, t_mark);
    transport_after_pressure(c, f, dt, acc);
    lap(c.timing.transport_ms, t_mark);
}
}
