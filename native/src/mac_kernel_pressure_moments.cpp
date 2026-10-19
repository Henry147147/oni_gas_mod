// Two-moment pressure work, MAC_KERNEL.md. The material-volume path is
// V(tau)=V0+a*tau+sqrt(3)*b*(tau²-tau). Its pressure trace is p0+X+P1*Y.
// One coefficient is formed from exact isentropic endpoint work, giving
// a*X+b*Y=a*dW(a). This is a constitutive work identity, not an endpoint repair.
// Shared face traces supply both J and the time-integrated material sweep.
#include "mac_kernel_detail.hpp"
#include <chrono>
#include <cstdio>

namespace ma::experimental::mac_kernel {
namespace {
struct MomentSweep { double value = 0; MacEnergy full; bool corrected = false; };
MomentSweep moment_sweep(double j0, double j1, double f1, double inertia, double dt, double area, bool allow_correction) {
    const double middle = .5 * (j0 + j1), correction = (std::sqrt(3.) / 6) * dt * f1;
    const double average = middle - correction, velocity = average / inertia;
    const double timed = velocity * dt, value = timed * area;
    if ((j0 == 0 && j1 == 0 && f1 == 0) ||
        ((std::isnormal(middle) || (middle == 0 && j0 == -j1)) && (f1 == 0 || std::isnormal(correction)) &&
         (std::isnormal(average) || (average == 0 && middle == correction)) &&
         (average == 0 || (std::isnormal(velocity) && std::isnormal(timed) && std::isnormal(value))))) return {value, {}};
    MacEnergySum j; j.add(j0); j.add(j1);
    MacEnergySum average_full; average_full.add(j.value().shifted(-1));
    average_full.add(MacEnergy::from_double(f1).multiplied(dt).multiplied(std::sqrt(3.) / 6).negated());
    const auto full = average_full.value().shared(1, inertia).multiplied(dt).multiplied(area);
    if (full.exponent() > 1024) throw ArithmeticRefinement{};
    const auto stored = full.project();
    if (std::isnormal(stored.value)) {
        if (stored.value != value) {
            if (allow_correction) return {stored.value, {}, true};
            throw ArithmeticRefinement{};
        }
        return {value, {}};
    }
    return {stored.value, full};
}
}

void stage_pressure_transport_moments(Context& c, Fields& f, double dt, Accounts& acc) {
    const auto& g = c.g; auto& ws = c.ws; const auto N = g.N;
    auto mark = std::chrono::steady_clock::now();
    const auto lap = [&](double& slot) {
        if (!c.options.collect_stage_timing) return;
        const auto now = std::chrono::steady_clock::now();
        slot += std::chrono::duration<double, std::milli>(now - mark).count(); mark = now;
    };
    evaluate_cells(c, f, ws.eos); const auto& e = ws.eos;
    if (!ws.moments) ws.moments = std::make_unique<PressureMomentWorkspace>();
    auto& m = *ws.moments; m.accepted.resize(N); m.trial.resize(N);
    auto& current = m.accepted; auto& trial = m.trial;
    current.a.assign(N, 0); current.b.assign(N, 0);
    ws.anchor.resize(N); ws.Vmat.resize(N);
    ws.qx.assign(g.NX, 0); ws.qy.assign(g.NY, 0); ws.body_work_y.assign(g.NY, 0);
    ws.full_qx.reset(g.NX); ws.full_qy.reset(g.NY);
    ws.dp0x.assign(g.NX, 0); ws.dp0y.assign(g.NY, 0);
    ws.wx.assign(g.NX, 0); ws.wy.assign(g.NY, 0);
    ws.cc.assign(N, 0); ws.bdiag.assign(N, 0);
    const double root3 = std::sqrt(3.), k = root3 / 6, gravity = c.settings.gravity_m_per_s2;
    const auto occupied = [&](Index i) { return i != none && !g.wall[i] && e.kind[i] != empty; };
    const auto pressure0 = [&](Index i) { return occupied(i) ? e.p[i] : 0.; };
    c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) {
        for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
            auto& a = ws.anchor[i]; a = {}; a.kind = e.kind[i];
            if (!occupied(static_cast<Index>(i))) continue;
            a.V0 = g.V; a.p0 = e.p[i]; a.T0 = e.T[i]; a.Vg0 = e.Vg[i]; a.Cv = e.Cv[i]; a.nR = e.nR[i];
            a.k = a.nR / a.Cv; a.comp = e.comp[i]; ws.cc[i] = a.comp;
            if (a.kind == liquid_cell) {
                std::size_t count = 0, which = 0;
                for (const auto j : c.model.liquids) if (f.m[j * N + i] != 0) { ++count; which = j; }
                if (count == 1) {
                    const auto& sp = c.model.species[which]; a.single_liquid = true;
                    a.K = sp.bulk; a.Nl = f.m[which * N + i] / sp.rho0; a.qmax = sp.maximum_ratio - 1;
                }
            }
        }
    });
    const auto weight = [&](double I) { return (dt * g.A) * (dt * g.A / I) / 12; };
    const auto setup_face = [&](Index l, Index r, double I, double& dp, double& w) {
        if (I > 0) dp = pressure0(l) - pressure0(r);
        if (occupied(l) && occupied(r) && I > 0) w = weight(I);
    };
    c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t block) {
        for (auto j = y0 * g.XW; j < y1 * g.XW; ++j) setup_face(g.xl[j], g.xr[j], ws.ix[j], ws.dp0x[j], ws.wx[j]);
        for (auto j = y0 * g.W; j < g.block_yrow_end(block) * g.W; ++j) setup_face(g.yl[j], g.yr[j], ws.iy[j], ws.dp0y[j], ws.wy[j]);
    });
    LinearSystem sys; sys.g = &g; sys.active.assign(N, 0); sys.diag.assign(N, 0); sys.wx = ws.wx; sys.wy = ws.wy;
    c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) {
        for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
            if (!occupied(static_cast<Index>(i))) continue;
            const auto l = g.left_face(i), r = g.right_face(i), b = g.bottom_face(i), t = g.top_face(i);
            const auto vacuum = [&](Index other, double I) { return other != none && !occupied(other) && I > 0 ? weight(I) : 0.; };
            ws.bdiag[i] = vacuum(g.xl[l], ws.ix[l]) + vacuum(g.xr[r], ws.ix[r]) + vacuum(g.yl[b], ws.iy[b]) + vacuum(g.yr[t], ws.iy[t]);
            sys.active[i] = 1; sys.diag[i] = ws.cc[i] + ws.bdiag[i];
        }
    });
    const double inner = std::sqrt((3 - 2 * std::sqrt(6. / 5)) / 7), outer = std::sqrt((3 + 2 * std::sqrt(6. / 5)) / 7);
    const std::array<double, 4> nodes{.5 * (1 - outer), .5 * (1 - inner), .5 * (1 + inner), .5 * (1 + outer)};
    const std::array<double, 4> weights{(18 - std::sqrt(30.)) / 72, (18 + std::sqrt(30.)) / 72,
        (18 + std::sqrt(30.)) / 72, (18 - std::sqrt(30.)) / 72};
    const auto trace = [&](const auto& state, Index i, bool first) { return occupied(i) ? (first ? state.first[i] : state.mean[i]) : 0.; };
    const auto force = [&](const auto& state, Index l, Index r, double dp0, double I, bool vertical, bool first) {
        if (first) return g.A * (trace(state, l, true) - trace(state, r, true));
        return g.A * (dp0 + (trace(state, l, false) - trace(state, r, false))) - (vertical ? I * gravity : 0.);
    };
    const auto movable = [](Index l, Index r, double I) { return l != none && r != none && I > 0; };
    const auto evaluate = [&](PressureMomentWorkspace::State& state) {
        auto errors = c.exec.partials<int>([&](std::size_t y0, std::size_t y1, std::size_t) {
            int bad = 0;
            for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
                state.mean[i] = 0; state.first[i] = 0; state.du[i] = 0;
                if (!occupied(static_cast<Index>(i))) continue;
                DD mean, first; Work endpoint;
                if (!isentropic_work(c.model, f, N, i, ws.anchor[i], state.a[i], endpoint)) { bad = 1; continue; }
                for (std::size_t j = 0; j < nodes.size(); ++j) {
                    const double tau = nodes[j], dv = tau * state.a[i] + root3 * (tau * tau - tau) * state.b[i];
                    Work w;
                    if (!isentropic_work(c.model, f, N, i, ws.anchor[i], dv, w)) { bad = 1; continue; }
                    // p(V0+dv)-p0 from the stable secant increment and its
                    // derivative, including the series branch (p1 is unused).
                    const double dp = w.dW + dv * w.derivative;
                    mean.add(weights[j] * dp); first.add(weights[j] * root3 * (2 * tau - 1) * dp);
                }
                double X = mean.value(), Y = first.value();
                const double a = state.a[i], b = state.b[i];
                if (std::abs(a) >= std::abs(b)) { if (a != 0) X = endpoint.dW - (b / a) * Y; }
                else Y = (a / b) * (endpoint.dW - X);
                if (!std::isfinite(X) || !std::isfinite(Y)) bad = 1;
                state.mean[i] = X; state.first[i] = Y; state.du[i] = endpoint.dU;
            }
            return bad;
        });
        c.counters.isentropic += 5 * N;
        for (const auto bad : errors) if (bad) return std::numeric_limits<double>::infinity();
        // Both moments use the SAME face time trace and inertia. A face's
        // integrated sweep is then reused by both material endpoints.
        c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t block) {
            for (auto j = y0 * g.XW; j < y1 * g.XW; ++j) {
                if (!movable(g.xl[j], g.xr[j], ws.ix[j])) { ws.qx[j] = 0; continue; }
                const double f0 = force(state, g.xl[j], g.xr[j], ws.dp0x[j], ws.ix[j], false, false);
                const double f1 = force(state, g.xl[j], g.xr[j], 0, ws.ix[j], false, true);
                ws.qx[j] = dt * g.A * ((f.jx[j] + dt * (.5 * f0 - k * f1)) / ws.ix[j]);
            }
            for (auto j = y0 * g.W; j < g.block_yrow_end(block) * g.W; ++j) {
                if (!movable(g.yl[j], g.yr[j], ws.iy[j])) { ws.qy[j] = 0; continue; }
                const double f0 = force(state, g.yl[j], g.yr[j], ws.dp0y[j], ws.iy[j], true, false);
                const double f1 = force(state, g.yl[j], g.yr[j], 0, ws.iy[j], true, true);
                ws.qy[j] = dt * g.A * ((f.jy[j] + dt * (.5 * f0 - k * f1)) / ws.iy[j]);
            }
        });
        const auto acceleration_sweep = [&](Index l, Index r, double dp0, double I, bool vertical) {
            return movable(l, r, I) ? dt * dt * k * g.A * (force(state, l, r, dp0, I, vertical, false) / I) : 0.;
        };
        const auto norms = c.exec.partials<double>([&](std::size_t y0, std::size_t y1, std::size_t) {
            double norm = 0;
            for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
                state.ra[i] = 0; state.rb[i] = 0;
                if (!occupied(static_cast<Index>(i))) continue;
                const auto l = g.left_face(i), r = g.right_face(i), b = g.bottom_face(i), t = g.top_face(i);
                state.ra[i] = ((state.a[i] + ws.qx[l]) - ws.qx[r]) + (ws.qy[b] - ws.qy[t]);
                state.rb[i] = state.b[i] - ((acceleration_sweep(g.xl[r], g.xr[r], ws.dp0x[r], ws.ix[r], false) -
                    acceleration_sweep(g.xl[l], g.xr[l], ws.dp0x[l], ws.ix[l], false)) +
                    (acceleration_sweep(g.yl[t], g.yr[t], ws.dp0y[t], ws.iy[t], true) -
                    acceleration_sweep(g.yl[b], g.yr[b], ws.dp0y[b], ws.iy[b], true)));
                norm = std::max({norm, std::abs(state.ra[i]) / (g.V + state.a[i]), std::abs(state.rb[i]) / (g.V + state.a[i])});
            }
            return norm;
        });
        double norm = 0; for (const auto value : norms) norm = std::max(norm, value); return norm;
    };
    lap(c.timing.eos_ms);
    constexpr double tolerance = 16 * eps;
    double norm = evaluate(current);
    if (kernel_debug()) std::fprintf(stderr, "moment anchor dt %.17g norm %.3e\n", dt, norm);
    if (!std::isfinite(norm)) reject("Two-moment pressure anchor left its EOS domain");
    for (std::size_t iteration = 0; norm > tolerance; ++iteration) {
        if (iteration == 80) reject("Two-moment pressure root did not converge");
        const double rtol = std::clamp(.1 * norm, 1e-13, 1e-3), atol = .025 * tolerance * g.V;
        if (c.settings.pressure_integration == MacPressureIntegration::two_moment_coupled &&
            c.settings.preconditioner == PressurePreconditioner::automatic)
            solve_pressure_moments_coupled(c, sys, ws.cc, current.ra, current.rb, ws.sol, ws.tdV, rtol, atol, iteration == 0);
        else solve_pressure_moments_linear(c, sys, ws.cc, current.ra, current.rb, ws.sol, ws.tdV, rtol, atol);
        bool accepted = false; double fraction = 1;
        for (std::size_t attempt = 0; attempt < 48; ++attempt) {
            c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) {
                for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
                    trial.a[i] = current.a[i] - fraction * ws.sol[i]; trial.b[i] = current.b[i] - fraction * ws.tdV[i];
                }
            });
            const double next = evaluate(trial);
            if (std::isfinite(next) && (next <= tolerance || next < norm * (1 - 1e-4 * fraction))) {
                std::swap(current, trial); norm = next; accepted = true; break;
            }
            ++c.counters.line_search; fraction *= .5;
        }
        if (kernel_debug()) std::fprintf(stderr, "moment root %zu norm %.3e accepted %d\n", iteration, norm, accepted ? 1 : 0);
        if (!accepted) reject("Two-moment pressure line search failed");
        ++c.counters.newton;
    }
    struct Published {
        DD wall_x, wall_y, fixed_body;
        double body = 0, energy = 0, jx = 0, jy = 0;
        bool corrected = false;
        std::vector<std::pair<Index, MacEnergy>> tiny_x, tiny_y;
    };
    const auto published = c.exec.partials<Published>([&](std::size_t y0, std::size_t y1, std::size_t block) {
        Published out;
        const auto face = [&](Index j, Index l, Index r, double I, double dp0, bool vertical, std::vector<double>& J, std::vector<double>& q) {
            if (!(I > 0)) return;
            const double f0 = force(current, l, r, dp0, I, vertical, false), f1 = force(current, l, r, 0, I, vertical, true);
            const double impulse = dt * f0, body = vertical ? -I * gravity * dt : 0;
            auto& ops = vertical ? out.jy : out.jx; ops += std::abs(J[j]) + std::abs(impulse) + std::abs(body);
            out.body += body;
            if (!movable(l, r, I)) {
                auto& wall = vertical ? out.wall_y : out.wall_x;
                wall.add(-dp0); wall.add(-trace(current, l, false)); wall.add(trace(current, r, false));
                if (vertical) out.fixed_body.add(-body);
                return;
            }
            const double J0 = J[j], J1 = J0 + impulse; J[j] = J1; ops += std::abs(J1);
            const auto sweep = moment_sweep(J0, J1, f1, I, dt, g.A,
                c.settings.pressure_integration == MacPressureIntegration::two_moment_coupled); q[j] = sweep.value;
            out.corrected = out.corrected || sweep.corrected;
            if (!sweep.full.is_zero()) (vertical ? out.tiny_y : out.tiny_x).emplace_back(j, sweep.full);
            if (vertical && gravity != 0) ws.body_work_y[j] = body * ((.5 * (J0 + J1) - k * dt * f1) / I);
            out.energy += .5 * J0 * (J0 / I) + .5 * J1 * (J1 / I) + (vertical ? std::abs(ws.body_work_y[j]) : 0);
        };
        for (auto j = y0 * g.XW; j < y1 * g.XW; ++j) face(static_cast<Index>(j), g.xl[j], g.xr[j], ws.ix[j], ws.dp0x[j], false, f.jx, ws.qx);
        for (auto j = y0 * g.W; j < g.block_yrow_end(block) * g.W; ++j) face(static_cast<Index>(j), g.yl[j], g.yr[j], ws.iy[j], ws.dp0y[j], true, f.jy, ws.qy);
        return out;
    });
    DD wall_x, wall_y, fixed_body; bool corrected = false;
    for (const auto& out : published) {
        wall_x.add(out.wall_x); wall_y.add(out.wall_y); fixed_body.add(out.fixed_body);
        corrected = corrected || out.corrected;
        acc.body_y += out.body; acc.operands[c.q_jx()] += out.jx; acc.operands[c.q_jy()] += out.jy; acc.operands[c.q_energy()] += out.energy;
        for (const auto& [j, value] : out.tiny_x) ws.full_qx.set(g.NX, j, value);
        for (const auto& [j, value] : out.tiny_y) ws.full_qy.set(g.NY, j, value);
    }
    acc.wall_x += wall_x.value() * dt * g.A;
    DD wy; wy.add(wall_y.value() * dt * g.A); wy.add(fixed_body); acc.wall_y += wy.value();
    struct Volumes { double energy = 0, closure = 0; std::size_t unresolved = 0; bool invalid = false; };
    const auto volumes = c.exec.partials<Volumes>([&](std::size_t y0, std::size_t y1, std::size_t) {
        Volumes out;
        for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
            if (g.wall[i]) continue;
            const auto l = g.left_face(i), r = g.right_face(i), b = g.bottom_face(i), t = g.top_face(i);
            DD swept; swept.add(ws.qx[r]); swept.add(-ws.qx[l]); swept.add(ws.qy[t]); swept.add(-ws.qy[b]);
            if (occupied(static_cast<Index>(i))) {
                const double before = f.U[i]; f.U[i] += current.du[i];
                if (current.du[i] != 0 && f.U[i] == before) ++out.unresolved;
                out.energy += std::abs(before) + std::abs(current.du[i]); ws.Vmat[i] = g.V + current.a[i];
                DD residual; residual.add(current.a[i]); residual.add(-swept.hi); residual.add(-swept.lo);
                out.closure = std::max(out.closure, std::abs(residual.value()) / g.V);
            } else { DD volume; volume.add(g.V); volume.add(swept); ws.Vmat[i] = volume.value(); }
            const double outgoing = std::max(0., -ws.qx[l]) + std::max(0., ws.qx[r]) + std::max(0., -ws.qy[b]) + std::max(0., ws.qy[t]);
            if (!(ws.Vmat[i] > 0) || outgoing > ws.Vmat[i] * c.settings.maximum_outgoing_volume_fraction) out.invalid = true;
        }
        return out;
    });
    for (const auto& out : volumes) {
        if (out.invalid) reject("Two-moment material-volume CFL exceeded");
        if (corrected && out.closure > tolerance)
            reject("Corrected normal sweeps require pressure-root refinement");
        acc.unresolved += out.unresolved; acc.operands[c.q_energy()] += out.energy;
        c.counters.maximum_volume_closure = std::max(c.counters.maximum_volume_closure, out.closure);
    }
    lap(c.timing.pressure_ms); transport_after_pressure(c, f, dt, acc); lap(c.timing.transport_ms);
}
}
