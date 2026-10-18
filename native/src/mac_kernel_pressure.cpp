// Production pressure step (MAC_KERNEL.md, "Pressure"): the reference
// implicit EOS pressure work with midpoint face sweeps, solved for per-cell
// volume changes in the increment formulation W = p0 + dW(dV). Node pressure
// differences gather (p0_l - p0_r) + (dW_l - dW_r), so no absolute-pressure
// cancellation floor limits the Newton root. Linear systems use PCG with a
// Jacobi or aggregation-multigrid preconditioner.
#include "mac_kernel_detail.hpp"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace ma::experimental::mac_kernel {
namespace {
double combine(const std::vector<double>& part) { DD s; for (const double v : part) s.add(v); return s.value(); }
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
// ---------------------------------------------------------------- multigrid
// Pairwise 2x2 aggregation with Galerkin coarse operators (piecewise-constant
// prolongation), symmetric damped-Jacobi smoothing. Levels are built from the
// fine system for each solve; every operation is a fixed-order gather.
struct Level {
    std::size_t W = 0, H = 0, N = 0;
    bool px = false, py = false;
    std::vector<double> diag, wx, wy, inv; // wx: right-neighbour weight of cell (x,y); wy: top-neighbour weight
    std::vector<std::uint8_t> active;
    std::vector<double> x, b, r;
};
struct Hierarchy { std::vector<Level> levels; };
void build_hierarchy(const Context& c, const LinearSystem& sys, Hierarchy& h) {
    const auto& g = c.g;
    h.levels.clear();
    Level fine; fine.W = g.W; fine.H = g.H; fine.N = g.N; fine.px = g.px; fine.py = g.py;
    fine.diag.assign(fine.N, 0); fine.wx.assign(fine.N, 0); fine.wy.assign(fine.N, 0); fine.active.assign(fine.N, 0);
    for (std::size_t i = 0; i < g.N; ++i) {
        fine.active[i] = sys.active[i];
        if (!sys.active[i]) continue;
        const auto r = g.right_face(i), t = g.top_face(i);
        fine.wx[i] = (g.px || i % g.W + 1 < g.W) ? sys.wx[r] : 0;
        fine.wy[i] = (g.py || i / g.W + 1 < g.H) ? sys.wy[t] : 0;
        const auto l = g.left_face(i), b = g.bottom_face(i);
        // Diagonal of A = diag + sum of incident weights.
        fine.diag[i] = sys.diag[i] + sys.wx[l] + sys.wx[r] + sys.wy[b] + sys.wy[t];
    }
    h.levels.push_back(std::move(fine));
    while (h.levels.back().N > 64 && h.levels.size() < 24) {
        const auto& f = h.levels.back();
        if (f.W < 2 && f.H < 2) break;
        Level q; q.W = (f.W + 1) / 2; q.H = (f.H + 1) / 2; q.N = q.W * q.H;
        q.px = f.px && f.W % 2 == 0; q.py = f.py && f.H % 2 == 0;
        q.diag.assign(q.N, 0); q.wx.assign(q.N, 0); q.wy.assign(q.N, 0); q.active.assign(q.N, 0);
        const auto parent = [&](std::size_t x, std::size_t y) { return (x / 2) + (y / 2) * q.W; };
        for (std::size_t y = 0; y < f.H; ++y) for (std::size_t x = 0; x < f.W; ++x) {
            const auto i = x + y * f.W; if (!f.active[i]) continue;
            const auto P = parent(x, y); q.active[P] = 1; q.diag[P] += f.diag[i];
            // Right neighbour.
            if (f.wx[i] != 0) {
                const auto nx = (x + 1) % f.W, j = nx + y * f.W;
                const auto Q = parent(nx, y);
                if (Q == P) q.diag[P] -= 2 * f.wx[i];
                else {
                    // Coarse right link of P is to the cell at P+1 (or wrap).
                    const bool right = (x / 2 + 1) % q.W == nx / 2;
                    if (right && (q.px || x / 2 + 1 < q.W)) q.wx[P] += f.wx[i];
                    else { // Periodic fine wrap without a periodic coarse grid: keep as diagonal only.
                        q.diag[P] -= f.wx[i]; q.diag[Q] -= 0; (void)j;
                    }
                }
            }
            if (f.wy[i] != 0) {
                const auto ny = (y + 1) % f.H;
                const auto Q = parent(x, ny);
                if (Q == P) q.diag[P] -= 2 * f.wy[i];
                else {
                    const bool up = (y / 2 + 1) % q.H == ny / 2;
                    if (up && (q.py || y / 2 + 1 < q.H)) q.wy[P] += f.wy[i];
                    else q.diag[P] -= f.wy[i];
                }
            }
        }
        h.levels.push_back(std::move(q));
    }
    for (auto& L : h.levels) {
        L.inv.assign(L.N, 0);
        for (std::size_t i = 0; i < L.N; ++i) if (L.active[i] && L.diag[i] > 0) L.inv[i] = 1 / L.diag[i];
        L.x.assign(L.N, 0); L.b.assign(L.N, 0); L.r.assign(L.N, 0);
    }
}
// Residual r = b - A x on a level (A from diag/off-diagonal weights).
void level_residual(const Level& L, const std::vector<double>& x, const std::vector<double>& b, std::vector<double>& r) {
    for (std::size_t y = 0; y < L.H; ++y) for (std::size_t xx = 0; xx < L.W; ++xx) {
        const auto i = xx + y * L.W; if (!L.active[i]) { r[i] = 0; continue; }
        double ax = L.diag[i] * x[i];
        if (L.wx[i] != 0) ax -= L.wx[i] * x[(xx + 1) % L.W + y * L.W];
        if (L.wy[i] != 0) ax -= L.wy[i] * x[xx + ((y + 1) % L.H) * L.W];
        const auto left = xx > 0 ? i - 1 : (L.px ? i + L.W - 1 : i);
        if (left != i && L.wx[left] != 0) ax -= L.wx[left] * x[left];
        const auto below = y > 0 ? i - L.W : (L.py ? i + (L.H - 1) * L.W : i);
        if (below != i && L.wy[below] != 0) ax -= L.wy[below] * x[below];
        r[i] = b[i] - ax;
    }
}
void smooth(Level& L, int sweeps) {
    constexpr double omega = .8;
    for (int s = 0; s < sweeps; ++s) {
        level_residual(L, L.x, L.b, L.r);
        for (std::size_t i = 0; i < L.N; ++i) if (L.active[i]) L.x[i] += omega * L.inv[i] * L.r[i];
    }
}
void vcycle(Hierarchy& h, std::size_t level) {
    auto& L = h.levels[level];
    if (level + 1 == h.levels.size()) { smooth(L, 40); return; }
    smooth(L, 2);
    level_residual(L, L.x, L.b, L.r);
    auto& Q = h.levels[level + 1];
    std::fill(Q.b.begin(), Q.b.end(), 0.); std::fill(Q.x.begin(), Q.x.end(), 0.);
    for (std::size_t y = 0; y < L.H; ++y) for (std::size_t x = 0; x < L.W; ++x) {
        const auto i = x + y * L.W; if (L.active[i]) Q.b[(x / 2) + (y / 2) * Q.W] += L.r[i];
    }
    vcycle(h, level + 1);
    for (std::size_t y = 0; y < L.H; ++y) for (std::size_t x = 0; x < L.W; ++x) {
        const auto i = x + y * L.W; if (L.active[i]) L.x[i] += Q.x[(x / 2) + (y / 2) * Q.W];
    }
    smooth(L, 2);
}
}
LinearSolveResult solve_linear(Context& c, const LinearSystem& sys, const std::vector<double>& b, std::vector<double>& x,
    double rtol, double atol, std::size_t maximum) {
    auto& ws = c.ws; const auto& g = c.g; const auto N = g.N;
    for (auto* v : {&ws.lr, &ws.lz, &ws.lp, &ws.lq, &ws.lAp, &ws.ldiag}) v->resize(N);
    x.assign(N, 0);
    const bool multigrid = c.options.preconditioner == MacKernelPreconditioner::multigrid && g.N > 256;
    static thread_local Hierarchy hierarchy;
    if (multigrid) build_hierarchy(c, sys, hierarchy);
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
        auto& L = hierarchy.levels.front();
        std::copy(ws.lr.begin(), ws.lr.end(), L.b.begin()); std::fill(L.x.begin(), L.x.end(), 0.);
        vcycle(hierarchy, 0);
        std::copy(L.x.begin(), L.x.end(), ws.lz.begin());
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
    for (std::size_t it = 1; it <= maximum; ++it) {
        // p = z + beta p_old (double buffered) fused with A p and p.Ap.
        double pAp = 0;
        if (it == 1) apply(c, sys, ws.lp, ws.lAp, &ws.lp, &pAp);
        else {
            c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) {
                for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) ws.lq[i] = ws.lz[i] + beta * ws.lp[i];
            });
            ws.lp.swap(ws.lq);
            apply(c, sys, ws.lp, ws.lAp, &ws.lp, &pAp);
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
    return result;
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
            if (s.kind == gas_cell) {
                const double A = f.U[i] - s.off;
                if (!(A > 0)) reject("Production kernel cell left its EOS domain");
                const double T = A / s.Cv, p = s.nR * T / g.V;
                e.T[i] = T; e.p[i] = p; e.Vg[i] = g.V; e.Eel[i] = 0; e.Ceq[i] = s.Cv; e.comp[i] = s.Cv / (s.Cv + s.nR) * g.V / p; e.lcomp[i] = 0;
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
            if (!evaluate_state(c.model, f, g.N, i, s, f.U[i], g.V, st)) reject("Production kernel cell left its EOS domain");
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
    ws.anchor.resize(N);
    for (auto* v : {&ws.dV, &ws.dW, &ws.cc, &ws.dU, &ws.bdiag}) v->assign(N, 0);
    for (auto* v : {&ws.res, &ws.tdV, &ws.tdW, &ws.tcc, &ws.tres, &ws.Vmat, &ws.tdU}) v->resize(N);
    for (auto* v : {&ws.dp0x, &ws.wx, &ws.qx}) v->assign(g.NX, 0);
    for (auto* v : {&ws.dp0y, &ws.wy, &ws.qy, &ws.body_work_y}) v->assign(g.NY, 0);
    ws.sx.resize(g.NX); ws.sy.resize(g.NY);
    const auto occupied = [&](Index i) { return i != none && (e.kind[i] == gas_cell || e.kind[i] == liquid_cell || e.kind[i] == mixed_cell); };
    const auto p0 = [&](Index i) { return occupied(i) ? e.p[i] : 0.; };
    // Anchors.
    c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) {
        for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
            auto& a = ws.anchor[i]; a = Anchor{}; a.kind = e.kind[i];
            if (e.kind[i] == empty || e.kind[i] == solid_cell) continue;
            a.V0 = g.V; a.p0 = e.p[i]; a.T0 = e.T[i]; a.Vg0 = e.Vg[i]; a.Cv = e.Cv[i]; a.nR = e.nR[i];
            a.k = a.nR / a.Cv; a.comp = e.comp[i];
            if (a.kind == liquid_cell) {
                std::size_t count = 0, which = 0;
                for (const auto k : c.model.liquids) if (f.m[k * N + i] != 0) { ++count; which = k; }
                if (count == 1) {
                    const auto& sp = c.model.species[which];
                    a.single_liquid = true; a.K = sp.bulk; a.Nl = f.m[which * N + i] / sp.rho0; a.qmax = sp.maximum_ratio - 1;
                }
            }
            ws.cc[i] = 2 * a.comp; // -1/W'(0)
        }
    });
    // Anchor pressure differences and link coefficients per face, then the
    // boundary diagonals (faces to empty cells) gathered per cell.
    const auto weight = [&](double inertia) { return .5 * dt * A * (dt * A / inertia); };
    const auto face_setup = [&](const std::vector<Index>& fl, const std::vector<Index>& fr, const std::vector<double>& I,
        std::vector<double>& dp0, std::vector<double>& w, std::size_t fidx) {
        const auto l = fl[fidx], r = fr[fidx];
        if (I[fidx] > 0) dp0[fidx] = p0(l) - p0(r);
        if (l != none && r != none && I[fidx] > 0 && occupied(l) && occupied(r)) w[fidx] = weight(I[fidx]);
    };
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
    // Midpoint sweep of a movable face with work-pressure increments:
    // swept = A dt (u0 + dt (A dp + I a) / (2 I)) = c0 + c1 (dW_l - dW_r),
    // c0 = A dt u0 + (A dt^2 / (2 I)) (A dp0 + I a), c1 = A^2 dt^2 / (2 I).
    // J and I are fixed during the root, so both coefficients are formed once.
    const auto movable_x = [&](std::size_t fidx) { return g.xl[fidx] != none && g.xr[fidx] != none && ws.ix[fidx] > 0; };
    const auto movable_y = [&](std::size_t fidx) { return g.yl[fidx] != none && g.yr[fidx] != none && ws.iy[fidx] > 0; };
    for (auto* v : {&ws.sx, &ws.keep}) v->assign(g.NX, 0);   // sx: c0, keep: c1 (x faces)
    for (auto* v : {&ws.sy, &ws.jsum}) v->assign(g.NY, 0);   // sy: c0, jsum: c1 (y faces)
    const auto coefficients = [&](double J, double I, double dp0, double accel, double& c0, double& c1) {
        const double half = A * dt * dt / (2 * I);
        c0 = A * dt * (J / I) + half * (A * dp0 + I * accel);
        c1 = half * A;
    };
    c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t blk) {
        for (std::size_t row = y0; row < y1; ++row) for (std::size_t col = 0; col < g.XW; ++col) {
            const auto fidx = g.xface(col, row);
            if (movable_x(fidx)) coefficients(f.jx[fidx], ws.ix[fidx], ws.dp0x[fidx], 0, ws.sx[fidx], ws.keep[fidx]);
        }
        for (std::size_t row = y0; row < g.block_yrow_end(blk); ++row) for (std::size_t col = 0; col < g.W; ++col) {
            const auto fidx = g.yface(col, row);
            if (movable_y(fidx)) coefficients(f.jy[fidx], ws.iy[fidx], ws.dp0y[fidx], -gravity, ws.sy[fidx], ws.jsum[fidx]);
        }
    });
    const auto& c0x = ws.sx; const auto& c1x = ws.keep; const auto& c0y = ws.sy; const auto& c1y = ws.jsum;
    // Residual evaluation for volume changes dv; fills dw, cc, res; returns
    // the max relative residual, or +inf if a trial left the EOS domain.
    const auto evaluate = [&](const std::vector<double>& dv, std::vector<double>& dw, std::vector<double>& cc,
        std::vector<double>& res, std::vector<double>& du) {
        auto bad = c.exec.partials<int>([&](std::size_t y0, std::size_t y1, std::size_t) {
            int failed = 0;
            for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
                if (!occupied(static_cast<Index>(i))) { dw[i] = 0; continue; }
                Work w;
                if (!isentropic_work(c.model, f, N, i, ws.anchor[i], dv[i], w)) { failed = 1; continue; }
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
                    res[i] = ((dv[i] + sl) - sr) + (sb - st);
                    norm = std::max(norm, std::abs(res[i]) / (g.V + dv[i]));
                }
            }
            return norm;
        });
        double norm = 0; for (const double v : norms) norm = std::max(norm, v);
        return norm;
    };
    lap(c.timing.eos_ms, t_mark);
    const double tolerance = 16 * eps;
    double norm = evaluate(ws.dV, ws.dW, ws.cc, ws.res, ws.dU);
    if (!std::isfinite(norm)) reject("Production pressure anchor left its EOS domain");
    LinearSystem sys; sys.g = &g; sys.wx = ws.wx; sys.wy = ws.wy;
    sys.active.assign(N, 0); sys.diag.assign(N, 0);
    c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) {
        for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) sys.active[i] = occupied(static_cast<Index>(i)) ? 1 : 0;
    });
    std::size_t newton = 0;
    const std::size_t linear_limit = c.options.preconditioner == MacKernelPreconditioner::multigrid ? 400 : 4000;
    while (norm > tolerance) {
        if (newton == 80) reject("Production pressure root did not converge; retry smaller timestep");
        c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) {
            for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) sys.diag[i] = sys.active[i] ? ws.cc[i] + ws.bdiag[i] : 0;
        });
        // Inexact Newton: the linear error only needs to stay below the
        // quadratically convergent nonlinear residual (relative tolerance
        // ~0.1 * norm), with an absolute floor at a fraction of the target.
        const double rtol = std::clamp(.1 * norm, 1e-13, 1e-3);
        const auto solved = solve_linear(c, sys, ws.res, ws.sol, rtol, .05 * tolerance * g.V, linear_limit);
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
        if (std::getenv("MA_KERNEL_DEBUG")) std::fprintf(stderr, "  newton %zu norm %.3e linear %zu res %.3e conv %d accepted %d\n", newton, norm, solved.iterations, solved.residual, solved.converged ? 1 : 0, accepted ? 1 : 0);
        if (!accepted) reject("Production pressure line search failed; retry smaller timestep");
        ++newton;
    }
    c.counters.newton += newton;
    // Publish the root: canonical J, midpoint sweeps, U and material volume.
    const auto dWfinal = [&](Index i) { return occupied(i) ? ws.dW[i] : 0.; };
    struct Publish { double wall_x = 0, wall_y = 0, body = 0, energy = 0, jx = 0, jy = 0, closure = 0; std::size_t unresolved = 0; int failed = 0; };
    auto published = c.exec.partials<Publish>([&](std::size_t y0, std::size_t y1, std::size_t blk) {
        Publish o;
        for (std::size_t row = y0; row < y1; ++row) for (std::size_t col = 0; col < g.XW; ++col) {
            const auto fidx = g.xface(col, row);
            if (!(ws.ix[fidx] > 0)) continue;
            const double I = ws.ix[fidx], J0 = f.jx[fidx];
            const double dp = ws.dp0x[fidx] + (dWfinal(g.xl[fidx]) - dWfinal(g.xr[fidx]));
            const double impulse = dt * A * dp;
            o.jx += std::abs(J0) + std::abs(impulse);
            if (!movable_x(fidx)) { o.wall_x -= impulse; continue; }
            const double J1 = J0 + impulse;
            f.jx[fidx] = J1; o.jx += std::abs(J1);
            ws.qx[fidx] = .5 * (J0 + J1) / I * dt * A;
            o.energy += .5 * J0 * (J0 / I) + .5 * J1 * (J1 / I);
        }
        for (std::size_t row = y0; row < g.block_yrow_end(blk); ++row) for (std::size_t col = 0; col < g.W; ++col) {
            const auto fidx = g.yface(col, row);
            if (!(ws.iy[fidx] > 0)) continue;
            const double I = ws.iy[fidx], J0 = f.jy[fidx];
            const double dp = ws.dp0y[fidx] + (dWfinal(g.yl[fidx]) - dWfinal(g.yr[fidx]));
            const double gimp = -I * gravity * dt;
            const double impulse = gravity == 0 ? dt * A * dp : dt * (A * dp + (-I * gravity));
            o.body += gimp;
            o.jy += std::abs(J0) + std::abs(impulse) + std::abs(gimp);
            if (!movable_y(fidx)) { o.wall_y -= impulse; continue; }
            const double J1 = J0 + impulse;
            f.jy[fidx] = J1; o.jy += std::abs(J1);
            ws.qy[fidx] = .5 * (J0 + J1) / I * dt * A;
            if (gravity != 0) ws.body_work_y[fidx] = gimp * (.5 * (J0 + J1) / I);
            o.energy += .5 * J0 * (J0 / I) + .5 * J1 * (J1 / I) + std::abs(ws.body_work_y[fidx]);
        }
        return o;
    });
    // Material volumes, U and the closure diagnostic after every sweep exists.
    auto volumes = c.exec.partials<Publish>([&](std::size_t y0, std::size_t y1, std::size_t) {
        Publish o;
        for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
            if (g.wall[i]) continue;
            // Geometric volume change: right/top faces moving outward expand the cell.
            DD sweeps; sweeps.add(ws.qx[g.right_face(i)]); sweeps.add(-ws.qx[g.left_face(i)]);
            sweeps.add(ws.qy[g.top_face(i)]); sweeps.add(-ws.qy[g.bottom_face(i)]);
            if (!occupied(static_cast<Index>(i))) {
                DD v; v.add(g.V); v.add(sweeps.hi); v.add(sweeps.lo);
                ws.Vmat[i] = v.value();
                if (!(ws.Vmat[i] > 0)) o.failed = 1;
                continue;
            }
            const double before = f.U[i];
            f.U[i] = before + ws.dU[i];
            o.energy += std::abs(before) + std::abs(ws.dU[i]);
            if (ws.dU[i] != 0 && f.U[i] == before) ++o.unresolved;
            ws.Vmat[i] = g.V + ws.dV[i];
            DD r; r.add(ws.dV[i]); r.add(-sweeps.hi); r.add(-sweeps.lo);
            o.closure = std::max(o.closure, std::abs(r.value()) / g.V);
        }
        return o;
    });
    double energy_ops = 0, jx_ops = 0, jy_ops = 0, closure = 0;
    for (const auto& o : published) { acc.wall_x += o.wall_x; acc.wall_y += o.wall_y; acc.body_y += o.body; energy_ops += o.energy; jx_ops += o.jx; jy_ops += o.jy; }
    for (const auto& o : volumes) {
        if (o.failed) reject("Production swept vacuum volume exhausted");
        energy_ops += o.energy; acc.unresolved += o.unresolved; closure = std::max(closure, o.closure);
    }
    acc.operands[c.q_jx()] += jx_ops; acc.operands[c.q_jy()] += jy_ops;
    acc.operands[c.q_energy()] += energy_ops;
    c.counters.maximum_volume_closure = std::max(c.counters.maximum_volume_closure, closure);
    // Material-volume CFL.
    const double limit = c.settings.maximum_outgoing_volume_fraction;
    auto cfl = c.exec.partials<int>([&](std::size_t y0, std::size_t y1, std::size_t) {
        int exceeded = 0;
        for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
            if (g.wall[i]) continue;
            double out = 0;
            const auto l = g.left_face(i), r = g.right_face(i), b = g.bottom_face(i), t = g.top_face(i);
            if (ws.qx[l] < 0) out -= ws.qx[l];
            if (ws.qx[r] > 0) out += ws.qx[r];
            if (ws.qy[b] < 0) out -= ws.qy[b];
            if (ws.qy[t] > 0) out += ws.qy[t];
            if (out > ws.Vmat[i] * limit) exceeded = 1;
        }
        return exceeded;
    });
    for (const int v : cfl) if (v) reject("Production material-volume CFL exceeded");
    lap(c.timing.pressure_ms, t_mark);
    transport_after_pressure(c, f, dt, acc);
    lap(c.timing.transport_ms, t_mark);
}
}
