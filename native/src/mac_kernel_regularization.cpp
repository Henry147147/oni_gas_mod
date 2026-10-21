// Production compression regularization (modes 1/2) and shock-heat
// regularization (modes 1/2/3) (MAC_KERNEL.md, "Regularization"). The pair
// laws are the reference ones (mac_compression.cpp, mac_shock_heat.cpp); the
// forward/reverse Gauss-Seidel sweeps keep the reference order exactly: a
// sweep only chains along grid lines, so independent lines run in parallel.
#include "mac_kernel_detail.hpp"

namespace ma::experimental::mac_kernel {
namespace {
// (1 - exp(-x)) / x and 1 - exp(-x); short series below 1e-3 (truncation
// below 2^-60 relative), expm1 otherwise, reference limits at the ends.
double phi(double x) {
    if (x == 0 || x < 0x1p-53) return 1;
    if (x >= 64) return 1 / x;
    if (x < 1e-3) return 1 - x * (1. / 2 - x * (1. / 6 - x * (1. / 24 - x * (1. / 120 - x * (1. / 720)))));
    return -std::expm1(-x) / x;
}
double fraction(double x) {
    if (x == 0) return 0;
    if (x < 0x1p-53) return x;
    if (x >= 64) return 1;
    if (x < 1e-3) return x * (1 - x * (1. / 2 - x * (1. / 6 - x * (1. / 24 - x * (1. / 120 - x * (1. / 720))))));
    return -std::expm1(-x);
}
bool occupied(std::uint8_t kind) { return kind == gas_cell || kind == liquid_cell || kind == mixed_cell; }
// Frozen half-sound speed (acoustic laws) and per-axis pressure sensors,
// from the stage's incoming state (reference freeze_mac_compression_coefficients).
void compression_coefficients(Context& c, const Fields& f, bool acoustic) {
    auto& ws = c.ws; const auto& g = c.g; const auto& e = ws.eos; const auto N = g.N;
    for (auto* v : {&ws.speed, &ws.pressure, &ws.sensor0, &ws.sensor1}) v->resize(N);
    ws.closing.resize(2 * N);
    c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) {
        for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
            if (!acoustic || !occupied(e.kind[i])) { ws.speed[i] = 0; ws.pressure[i] = 0; continue; }
            const double sound = material_volume(f, g.V, i) / std::sqrt(e.M[i] * e.comp[i]);
            if (!std::isfinite(sound) || !(sound > 0)) precision("Production compression sound speed cannot be represented");
            ws.speed[i] = .5 * sound; ws.pressure[i] = e.p[i];
        }
    });
    if (!acoustic) return;
    // Neighbour along an axis; boundaries and solids supply the cell itself.
    const auto neighbour = [&](std::size_t cell, std::size_t x, std::size_t y, int axis, bool forward) {
        if (axis == 0) {
            if (!g.px && (forward ? x + 1 == g.W : x == 0)) return cell;
            const auto n = forward ? (x + 1 == g.W ? cell + 1 - g.W : cell + 1) : (x == 0 ? cell + g.W - 1 : cell - 1);
            return g.wall[n] ? cell : n;
        }
        if (!g.py && (forward ? y + 1 == g.H : y == 0)) return cell;
        const auto n = forward ? (y + 1 == g.H ? cell + g.W - g.N : cell + g.W) : (y == 0 ? cell + g.N - g.W : cell - g.W);
        return g.wall[n] ? cell : n;
    };
    c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) {
        for (std::size_t y = y0; y < y1; ++y) for (std::size_t x = 0; x < g.W; ++x) for (int axis = 0; axis < 2; ++axis) {
            const auto i = x + y * g.W;
            double& out = ws.closing[static_cast<std::size_t>(axis) * N + i];
            if (g.wall[i]) { out = 0; continue; }
            const double l = ws.pressure[neighbour(i, x, y, axis, false)], m = ws.pressure[i], r = ws.pressure[neighbour(i, x, y, axis, true)];
            const double d = l + 2 * m + r;
            out = d == 0 ? 0 : std::abs((l + r) - 2 * m) / d;
        }
    });
    c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) {
        for (std::size_t y = y0; y < y1; ++y) for (std::size_t x = 0; x < g.W; ++x) for (int axis = 0; axis < 2; ++axis) {
            const auto i = x + y * g.W;
            auto& sensor = axis == 0 ? ws.sensor0 : ws.sensor1;
            if (g.wall[i]) { sensor[i] = 0; continue; }
            const double* curvature = ws.closing.data() + static_cast<std::size_t>(axis) * N;
            sensor[i] = std::max({curvature[i], curvature[neighbour(i, x, y, axis, false)], curvature[neighbour(i, x, y, axis, true)]});
        }
    });
}
double linear_speed(const Workspace& ws, int axis, std::size_t i) { return ws.speed[i] * (axis == 0 ? ws.sensor0[i] : ws.sensor1[i]); }
}

void stage_compression(Context& c, Fields& f, double dt, Accounts& acc) {
    prepare_primary_remainders(f);
    const auto mode = c.settings.compression_viscosity;
    if (mode != MacCompressionViscosity::quadratic && mode != MacCompressionViscosity::acoustic_quadratic) return;
    auto& ws = c.ws; const auto& g = c.g;
    evaluate_cells(c, f, ws.eos);
    const auto& e = ws.eos;
    compression_coefficients(c, f, mode == MacCompressionViscosity::acoustic_quadratic);
    double heat_total = 0, wall_x = 0, wall_y = 0;
    std::size_t events = 0, unresolved = 0;
    // Reference order: per axis, a forward then a reverse sweep over cells in
    // index order. x pairs chain only along rows and y pairs only along
    // columns, so each line runs sequentially and lines run in parallel.
    struct Out { double heat = 0, wall = 0, ops = 0, u = 0; std::size_t events = 0, unresolved = 0; EnergyRounding energy; };
    const double half = .5 * dt;
    for (int axis = 0; axis < 2; ++axis) {
        auto& J = axis == 0 ? f.jx : f.jy;
        const auto& inv = axis == 0 ? ws.inv_ix : ws.inv_iy;
        const auto& fl = axis == 0 ? g.xl : g.yl; const auto& fr = axis == 0 ? g.xr : g.yr;
        const auto pair = [&](std::size_t i, Out& o) {
            if (!(e.M[i] > 0) || g.wall[i]) return;
            const auto l = axis == 0 ? g.left_face(i) : g.bottom_face(i);
            const auto r = axis == 0 ? g.right_face(i) : g.top_face(i);
            const double closing = J[l] * inv[l] - J[r] * inv[r];
            if (!(closing > 0)) return;
            const bool fixl = fl[l] == none || fr[l] == none || g.closed(axis != 0, l), fixr = fl[r] == none || fr[r] == none || g.closed(axis != 0, r);
            const double B = e.M[i] * half / g.h;
            if (B == 0) return;
            const double a = (fixl ? 0 : inv[l]) + (fixr ? 0 : inv[r]);
            if (!(a > 0)) return;
            const double rel = B * closing * a;
            const double lin = linear_speed(ws, axis, i);
            double d;
            if (lin == 0) d = B * closing * closing / (1 + rel);
            else {
                const double ph = phi(B * lin * a);
                d = B * closing * (lin + closing) * ph / (1 + rel * ph);
            }
            const double mid = closing - d * a * .5;
            if (!(mid > 0)) {
                // Only a physically negligible closing speed can lose its
                // positive midpoint to binary64 rounding: skip it, counted.
                if (closing < 0x1p-500) { ++o.unresolved; return; }
                precision("Production compression relaxation lost its positive midpoint closing speed");
            }
            const double Q = d * mid;
            o.ops += std::abs(J[l]) + std::abs(J[r]) + 2 * std::abs(d);
            if (!fixl) J[l] -= d; else o.wall += d;
            if (!fixr) J[r] += d; else o.wall -= d;
            const double before = f.U[i];
            if (add_internal_energy(f, i, Q, o.energy)) ++o.unresolved;
            o.heat += Q; o.u += std::abs(before); ++o.events;
        };
        for (int sweep = 0; sweep < 2; ++sweep) {
            std::vector<Out> parts;
            if (axis == 0) parts = c.exec.partials<Out>([&](std::size_t y0, std::size_t y1, std::size_t) {
                Out o;
                for (std::size_t y = y0; y < y1; ++y) for (std::size_t n = 0; n < g.W; ++n)
                    pair((sweep == 0 ? n : g.W - 1 - n) + y * g.W, o);
                return o;
            });
            else parts = c.exec.column_partials<Out>(g.W, [&](std::size_t x0, std::size_t x1, std::size_t) {
                Out o;
                for (std::size_t x = x0; x < x1; ++x) for (std::size_t n = 0; n < g.H; ++n)
                    pair(x + (sweep == 0 ? n : g.H - 1 - n) * g.W, o);
                return o;
            });
            for (const auto& o : parts) {
                acc.energy.merge(o.energy);
                heat_total += o.heat; (axis == 0 ? wall_x : wall_y) += o.wall; events += o.events; unresolved += o.unresolved;
                acc.operands[axis == 0 ? c.q_jx() : c.q_jy()] += o.ops;
                acc.operands[c.q_energy()] += 2 * o.u;
            }
        }
    }
    acc.operands[c.q_energy()] += 2 * heat_total;
    acc.compression_heat += heat_total; acc.wall_x += wall_x; acc.wall_y += wall_y; acc.unresolved += unresolved;
    c.counters.compression_events += events;
}

void stage_shock_heat(Context& c, Fields& f, double dt, Accounts& acc) {
    prepare_primary_remainders(f);
    const auto mode = c.settings.shock_heat;
    if (mode == MacShockHeatRegularization::none) return;
    const bool sensed = mode != MacShockHeatRegularization::compression_activated;
    const bool series = mode == MacShockHeatRegularization::pressure_sensed_series;
    auto& ws = c.ws; const auto& g = c.g; const auto N = g.N;
    evaluate_cells(c, f, ws.eos);
    auto& e = ws.eos;
    if (sensed) compression_coefficients(c, f, true);
    ws.cp.resize(N); ws.diffusion.resize(2 * N); ws.heat.resize(2 * N);
    // Per cell: Cp (sensed laws) and, per axis, closing and diffusion speeds.
    c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) {
        for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
            ws.cp[i] = occupied(e.kind[i]) ? e.Cv[i] + (e.Vg[i] > 0 ? e.p[i] * e.Vg[i] / e.T[i] : 0) : 0;
            for (int axis = 0; axis < 2; ++axis) {
                const auto k = static_cast<std::size_t>(axis) * N + i;
                ws.heat[k] = 0; ws.diffusion[k] = 0; // heat[]: closing speed
                if (g.wall[i] || !(e.M[i] > 0)) continue;
                const auto& J = axis == 0 ? f.jx : f.jy; const auto& inv = axis == 0 ? ws.inv_ix : ws.inv_iy;
                const auto l = axis == 0 ? g.left_face(i) : g.bottom_face(i), r = axis == 0 ? g.right_face(i) : g.top_face(i);
                const double ul = J[l] * inv[l], ur = J[r] * inv[r];
                const double jump = ul - ur;
                if (!(jump > 0)) continue;
                ws.heat[k] = jump;
                // The acoustic term ramps in with the compression (MAC_HEAT.md,
                // "Face conductance laws"): rest and rounding-level motion move nothing.
                ws.diffusion[k] = sensed ? jump + linear_speed(ws, axis, i) * (jump / (jump + .02 * ws.speed[i])) : jump;
            }
        }
    });
    // Frozen integrated conductances per face and, for two constant-capacity
    // cells (capacities fixed during the stage), the relaxation fraction.
    auto& gx = ws.sx; auto& gy = ws.sy; auto& fxr = ws.qx; auto& fyr = ws.qy;
    gx.assign(g.NX, 0); gy.assign(g.NY, 0); fxr.assign(g.NX, -1); fyr.assign(g.NY, -1);
    const auto link = [&](int axis, Index l, Index r, double& gg, double& frac) {
        if (l == none || r == none || l == r || !(e.M[l] > 0) || !(e.M[r] > 0)) return;
        // A shock's numerical heat flux joins gas cells only (MAC_HEAT.md,
        // "Phase interfaces"): liquids barely heat in compression.
        if (e.kind[l] != gas_cell || e.kind[r] != gas_cell) return;
        const double* diffusion = ws.diffusion.data() + static_cast<std::size_t>(axis) * N;
        const double speed = std::max(diffusion[l], diffusion[r]);
        if (speed == 0) return;
        double value;
        if (series) {
            if (diffusion[l] == 0 || diffusion[r] == 0) return;
            const double Gl = ws.cp[l] * diffusion[l] / g.h, Gr = ws.cp[r] * diffusion[r] / g.h;
            value = Gl * Gr / (Gl + Gr) * 2 * (.5 * dt);
        } else {
            double capacity;
            if (sensed) capacity = ws.cp[l] * ws.cp[r] / (ws.cp[l] + ws.cp[r]) * 2;
            else { const double lo = std::min(e.Ceq[l], e.Ceq[r]), hi = std::max(e.Ceq[l], e.Ceq[r]); capacity = lo * 2 / (1 + lo / hi); }
            value = speed * (.5 * dt) / g.h * capacity;
        }
        gg = value;
        if (e.kind[l] != mixed_cell && e.kind[r] != mixed_cell) frac = fraction(value / e.Ceq[l] + value / e.Ceq[r]);
    };
    c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t blk) {
        for (std::size_t row = y0; row < y1; ++row) for (std::size_t x = 0; x < g.XW; ++x) {
            const auto fidx = g.xface(x, row); link(0, g.xl[fidx], g.xr[fidx], gx[fidx], fxr[fidx]);
        }
        for (std::size_t row = y0; row < g.block_yrow_end(blk); ++row) for (std::size_t x = 0; x < g.W; ++x) {
            const auto fidx = g.yface(x, row); link(1, g.yl[fidx], g.yr[fidx], gy[fidx], fyr[fidx]);
        }
    });
    double transferred = 0; std::size_t events = 0, unresolved = 0;
    const auto relax = [&](Index l, Index r, double gdt, double frac, double& moved, std::size_t& count, std::size_t& unres, EnergyRounding& energy) {
        const double Tl = e.T[l], Tr = e.T[r];
        if (gdt == 0 || Tl == Tr) return;
        const bool forward = Tl > Tr;
        const Index hot = forward ? l : r, cold = forward ? r : l;
        const bool constant = e.kind[hot] != mixed_cell && e.kind[cold] != mixed_cell;
        if (!(frac >= 0)) frac = fraction(gdt / e.Ceq[l] + gdt / e.Ceq[r]);
        double qeq;
        if (constant) {
            const double lo = std::min(e.Ceq[hot], e.Ceq[cold]), hi = std::max(e.Ceq[hot], e.Ceq[cold]);
            qeq = lo * 2 / (1 + lo / hi) * .5 * (e.T[hot] - e.T[cold]);
        } else {
            // Common-temperature energy partition by bisection on the full EOS.
            const auto sh = Stats{e.M[hot], e.Cv[hot], e.off[hot], e.nR[hot], e.Nl[hot], e.kind[hot]};
            const auto sc = Stats{e.M[cold], e.Cv[cold], e.off[cold], e.nR[cold], e.Nl[cold], e.kind[cold]};
            const double ch = e.Cv[hot] + (e.Vg[hot] > 0 ? e.p[hot] * e.Vg[hot] / e.T[hot] : 0);
            const double cc = e.Cv[cold] + (e.Vg[cold] > 0 ? e.p[cold] * e.Vg[cold] / e.T[cold] : 0);
            double lo = 0, hi = std::min(ch, cc) * (1 + 0x1p-45) * (e.T[hot] - e.T[cold]);
            qeq = 0;
            for (int it = 0; it < 128; ++it) {
                const double mid = lo + (hi - lo) / 2;
                State a, b;
                if (!evaluate_state(c.model, f, N, hot, sh, f.U[hot] - mid, material_volume(f, g.V, hot), a) ||
                    !evaluate_state(c.model, f, N, cold, sc, f.U[cold] + mid, material_volume(f, g.V, cold), b)) { hi = mid; continue; }
                qeq = mid;
                if (std::abs(a.T - b.T) <= 128 * eps * (a.T + b.T) || mid == lo || mid == hi) break;
                if (a.T > b.T) lo = mid; else hi = mid;
            }
        }
        const double q = qeq * frac;
        if (q == 0) return;
        if (add_internal_energy(f, hot, -q, energy)) ++unres;
        if (add_internal_energy(f, cold, q, energy)) ++unres;
        for (const Index i : {hot, cold}) {
            if (e.kind[i] != mixed_cell) {
                // Constant capacity at fixed volume: only temperature moves.
                e.T[i] = (f.U[i] - e.off[i] - e.Eel[i]) / e.Cv[i];
                if (!(e.T[i] > 0)) reject("Production shock heat left the EOS domain");
                continue;
            }
            const auto s = Stats{e.M[i], e.Cv[i], e.off[i], e.nR[i], e.Nl[i], e.kind[i]};
            State st;
            if (!evaluate_state(c.model, f, N, i, s, f.U[i], material_volume(f, g.V, i), st)) reject("Production shock heat left the EOS domain");
            e.T[i] = st.T; e.p[i] = st.p; e.Vg[i] = st.Vg; e.Eel[i] = st.Eel; e.Ceq[i] = st.Ceq; e.comp[i] = st.comp;
        }
        moved += q; ++count;
    };
    // Reference order: x links in face order, then y links; the reverse sweep
    // reverses the whole list. x links chain along rows, y links along
    // columns: lines run sequentially, in parallel with each other.
    struct Out { double moved = 0; std::size_t events = 0, unresolved = 0; EnergyRounding energy; };
    const auto collect = [&](const std::vector<Out>& parts) {
        for (const auto& o : parts) { transferred += o.moved; events += o.events; unresolved += o.unresolved; acc.energy.merge(o.energy); }
    };
    const auto x_links = [&](bool reverse) {
        collect(c.exec.partials<Out>([&](std::size_t y0, std::size_t y1, std::size_t) {
            Out o;
            for (std::size_t y = y0; y < y1; ++y) for (std::size_t n = 0; n < g.XW; ++n) {
                const auto fidx = g.xface(reverse ? g.XW - 1 - n : n, y);
                if (gx[fidx] != 0) relax(g.xl[fidx], g.xr[fidx], gx[fidx], fxr[fidx], o.moved, o.events, o.unresolved, o.energy);
            }
            return o;
        }));
    };
    const auto y_links = [&](bool reverse) {
        collect(c.exec.column_partials<Out>(g.W, [&](std::size_t x0, std::size_t x1, std::size_t) {
            Out o;
            for (std::size_t x = x0; x < x1; ++x) for (std::size_t n = 0; n < g.YH; ++n) {
                const auto fidx = g.yface(x, reverse ? g.YH - 1 - n : n);
                if (gy[fidx] != 0) relax(g.yl[fidx], g.yr[fidx], gy[fidx], fyr[fidx], o.moved, o.events, o.unresolved, o.energy);
            }
            return o;
        }));
    };
    x_links(false); y_links(false); y_links(true); x_links(true);
    double u_ops = 0;
    for (const double v : c.exec.partials<double>([&](std::size_t y0, std::size_t y1, std::size_t) {
        double sum = 0; for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) if (occupied(e.kind[i])) sum += std::abs(f.U[i]);
        return sum;
    })) u_ops += v;
    acc.operands[c.q_energy()] += 2 * u_ops + 2 * transferred;
    acc.shock_heat += transferred; acc.unresolved += unresolved;
    c.counters.shock_events += events;
}
}
