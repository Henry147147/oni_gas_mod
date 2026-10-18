// Production remap (MAC_KERNEL.md, "Transport"): the reference limited-linear
// material reconstruction (selector 1) or donor cell (0), positive primal
// parcels written once per face by their donor and gathered by their target,
// compatible dual J/K fluxes with limited-linear momentum corrections
// (selector 1) applied in the reference order by independent lattice lines, and mixing/wall heat deposited by
// new half-mass shares. Every transfer is a shared face quantity, so each
// constituent, passive and J component is conserved by construction.
#include "mac_kernel_detail.hpp"
#include <chrono>

namespace ma::experimental::mac_kernel {
namespace {
double limited_slope(double a, double b) {
    if (a == 0 || b == 0 || (a < 0) != (b < 0)) return 0;
    return std::abs(a) > std::abs(b) ? b : a;
}
double kinetic(double inertia, double j) { return j == 0 ? 0 : .5 * j * (j / inertia); }
double variance(double mass, double du) { return mass == 0 || du == 0 ? 0 : .5 * mass * du * du; }
// Three-term sum rounded once (the reference's exact correction predictor).
double sum3(double a, double b, double c) { DD s; s.add(a); s.add(b); s.add(c); return s.value(); }

// Reference limit_mac_momentum_pair (linear heat limiter with transported
// work budget), in binary64. Returns false when no admissible limiter exists.
struct Pair { double jl, jr, ql, qr, kl, kr; bool limited = false; };
bool limit_pair(double il, double ir, Pair& p, double requested) {
    if (requested == 0) return true;
    const double ul = p.jl / il, ur = p.jr / ir, difference = ul - ur, vsum = ul + ur;
    const double linear = requested * difference * .5;
    double limiter = 1;
    for (int side = 0; side < 2; ++side) {
        const double coefficient = kinetic(side == 0 ? il : ir, requested);
        const double heat = side == 0 ? p.ql : p.qr;
        const double cost = coefficient - linear;
        if (cost > 0 && heat < cost) limiter = std::min(limiter, heat / cost);
    }
    const double requested_work = vsum * requested * .5;
    if (requested_work != 0) {
        const double available = requested_work > 0 ? p.kl : p.kr;
        if (available - std::abs(requested_work) < 0) limiter = std::min(limiter, available / std::abs(requested_work));
    }
    for (int attempt = 0; attempt <= 64; ++attempt) {
        const double impulse = requested * limiter;
        const double a = impulse * difference * .5;
        const double hl = p.ql + (a - kinetic(il, impulse)), hr = p.qr + (a - kinetic(ir, impulse));
        if (hl >= 0 && hr >= 0) {
            const double flux = vsum * impulse * .5;
            if (p.kl - flux >= 0 && p.kr + flux >= 0) {
                p.jl -= impulse; p.jr += impulse; p.ql = hl; p.qr = hr; p.kl -= flux; p.kr += flux;
                p.limited = limiter < 1;
                return true;
            }
        }
        if (limiter == 0) break;
        limiter = std::nextafter(limiter, 0.);
    }
    return false;
}
}

void transport_after_pressure(Context& c, Fields& f, double dt, Accounts& acc) {
    (void)dt;
    auto& ws = c.ws; const auto& g = c.g; const auto& model = c.model; const auto N = g.N;
    const std::size_t K = model.K(), P = model.P(), F = K + 1 + P; // transported fields
    const std::size_t L = F + 1;                                  // + positive energy proxy
    // Parcel entries are written by the donor of every face with outgoing
    // volume and read only through such faces: no zeroing is needed.
    ws.px.resize(F * g.NX); ws.py.resize(F * g.NY); ws.retained.resize(F * N);
    ws.fx.assign(g.NX, 0); ws.fy.assign(g.NY, 0); ws.theta.resize(N);
    const auto& e = ws.eos;
    const bool linear = c.settings.reconstruction == MacMaterialReconstruction::limited_linear;
    ws.proxy.resize(N);
    c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) {
        for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) ws.proxy[i] = e.kind[i] == empty || e.kind[i] == solid_cell ? 0 : f.U[i] - e.off[i];
    });
    std::size_t backtracks = 0;
    const auto t_reconstruction = std::chrono::steady_clock::now();
    std::vector<const double*> source(L);
    for (std::size_t k = 0; k < K; ++k) source[k] = f.m.data() + k * N;
    source[K] = f.U.data();
    for (std::size_t p = 0; p < P; ++p) source[K + 1 + p] = f.q.data() + p * N;
    source[F] = ws.proxy.data();
    const bool gas_only = model.liquids.empty();
    // ---- reconstruction: each donor writes its outgoing face parcels.
    auto bt = c.exec.partials<std::size_t>([&](std::size_t y0, std::size_t y1, std::size_t) {
        std::size_t local_backtracks = 0;
        std::vector<double> centre(L), sx(L), sy(L), lo(L), hi(L), proto(4 * F), keep(F);
        for (std::size_t row = y0; row < y1; ++row) for (std::size_t col = 0; col < g.W; ++col) {
            const auto i = col + row * g.W;
            if (g.wall[i]) continue;
            const auto lf = g.left_face(i), rf = g.right_face(i), bf = g.bottom_face(i), tf = g.top_face(i);
            struct Edge { int axis; Index face; int dir; double volume, cx, cy, share; };
            Edge edges[4]; int count = 0;
            const double Vi = ws.Vmat[i];
            const double midx = .5 * (ws.qx[lf] + ws.qx[rf]), midy = .5 * (ws.qy[bf] + ws.qy[tf]);
            const auto add = [&](int axis, Index face, int dir, double q) {
                const double volume = std::abs(q), share = volume / Vi;
                const double normal = .5 * dir * (1 - share);
                const double transverse = (axis == 0 ? midy : midx) / Vi * -.5;
                edges[count++] = {axis, face, dir, volume, axis == 0 ? normal : transverse, axis == 0 ? transverse : normal, share};
            };
            if (ws.qx[lf] < 0) add(0, lf, -1, ws.qx[lf]);
            if (ws.qx[rf] > 0) add(0, rf, 1, ws.qx[rf]);
            if (ws.qy[bf] < 0) add(1, bf, -1, ws.qy[bf]);
            if (ws.qy[tf] > 0) add(1, tf, 1, ws.qy[tf]);
            if (count == 0 || e.kind[i] == empty) {
                for (std::size_t fld = 0; fld < F; ++fld) ws.retained[fld * N + i] = source[fld][i];
                for (int n = 0; n < count; ++n) {
                    // Empty space moving out of an empty donor carries nothing.
                    auto& out = edges[n].axis == 0 ? ws.px : ws.py; const auto stride = edges[n].axis == 0 ? g.NX : g.NY;
                    for (std::size_t fld = 0; fld < F; ++fld) out[fld * stride + edges[n].face] = 0;
                }
                ws.theta[i] = 0;
                continue;
            }
            double outgoing = 0; for (int n = 0; n < count; ++n) outgoing += edges[n].volume;
            const double retained_volume = Vi - outgoing;
            // Stencil (walls excluded, empty cells included with zero inventory).
            const auto x = col, y = row;
            Index nb[4] = {none, none, none, none};
            if (g.px || x > 0) { const auto n = (x + g.W - 1) % g.W + y * g.W; if (!g.wall[n]) nb[0] = static_cast<Index>(n); }
            if (g.px || x + 1 < g.W) { const auto n = (x + 1) % g.W + y * g.W; if (!g.wall[n]) nb[1] = static_cast<Index>(n); }
            if (g.py || y > 0) { const auto n = x + ((y + g.H - 1) % g.H) * g.W; if (!g.wall[n]) nb[2] = static_cast<Index>(n); }
            if (g.py || y + 1 < g.H) { const auto n = x + ((y + 1) % g.H) * g.W; if (!g.wall[n]) nb[3] = static_cast<Index>(n); }
            double ratio[4] = {0, 0, 0, 0};
            for (int n = 0; n < 4; ++n) if (nb[n] != none) ratio[n] = Vi / ws.Vmat[nb[n]];
            const bool hx = nb[0] != none && nb[1] != none, hy = nb[2] != none && nb[3] != none;
            double theta = linear ? 1 : 0;
            bool flat = !linear;
            if (linear) {
                for (std::size_t fld = 0; fld < L; ++fld) {
                    const double* src = source[fld];
                    const double cv = src[i];
                    double vals[4] = {cv, cv, cv, cv};
                    double low = cv, high = cv;
                    for (int n = 0; n < 4; ++n) if (nb[n] != none) {
                        const double v = src[nb[n]] * ratio[n];
                        vals[n] = v; low = std::min(low, v); high = std::max(high, v);
                    }
                    centre[fld] = cv; lo[fld] = low; hi[fld] = high;
                    sx[fld] = hx ? .5 * (vals[1] - vals[0]) : 0;
                    sy[fld] = hy ? .5 * (vals[3] - vals[2]) : 0;
                    if (!std::isfinite(sx[fld]) || !std::isfinite(sy[fld]) ||
                        (hx && vals[1] != vals[0] && sx[fld] == 0) || (hy && vals[3] != vals[2] && sy[fld] == 0)) flat = true;
                }
                if (!flat) {
                    // Points: corners, outgoing slab centres, retained centroid.
                    double ptx[9] = {-.5, -.5, .5, .5}, pty[9] = {-.5, .5, -.5, .5}; int pts = 4;
                    double rx = 0, ry = 0;
                    for (int n = 0; n < count; ++n) {
                        const double share = edges[n].volume / retained_volume;
                        ptx[pts] = edges[n].cx; pty[pts] = edges[n].cy; ++pts;
                        rx -= share * edges[n].cx; ry -= share * edges[n].cy;
                    }
                    ptx[pts] = rx; pty[pts] = ry; ++pts;
                    // theta = min of bound/extreme quotients; a quotient is
                    // formed only when it can lower the current value.
                    const auto lower = [&](double numerator, double denominator) {
                        if (numerator < theta * denominator) theta = std::min(theta, numerator / denominator);
                    };
                    for (std::size_t fld = 0; fld < L && !flat; ++fld) {
                        if (sx[fld] == 0 && sy[fld] == 0) continue;
                        double dmax = 0, dmin = 0;
                        for (int n = 0; n < pts; ++n) {
                            const double delta = ptx[n] * sx[fld] + pty[n] * sy[fld];
                            dmax = std::max(dmax, delta); dmin = std::min(dmin, delta);
                        }
                        if (!std::isfinite(dmax) || !std::isfinite(dmin)) { flat = true; break; }
                        if (dmax > 0) lower(hi[fld] - centre[fld], dmax);
                        if (dmin < 0) {
                            lower(centre[fld] - lo[fld], -dmin);
                            if (fld != K && centre[fld] > 0) lower(.5 * centre[fld], -dmin);
                        }
                    }
                }
                if (flat) theta = 0;
                if (!(theta >= 0 && theta <= 1)) theta = 0;
            }
            // Cheap EOS admission: gas-only inventories need nonnegative
            // amounts and positive thermal energy; liquids use the full EOS.
            const auto admissible = [&](const double* fields, double volume) {
                double M = 0, off = 0;
                for (std::size_t k = 0; k < K; ++k) {
                    const double m = fields[k];
                    if (!(m >= 0)) return false;
                    M += m; off += m * model.species[k].offset;
                }
                for (std::size_t p = 0; p < P; ++p) if (!(fields[K + 1 + p] >= 0)) return false;
                if (M == 0) return fields[K] == 0;
                if (!(fields[K] - off > 0)) return false;
                if (gas_only) return true;
                double Nl = 0; for (const auto k : model.liquids) Nl += fields[k] / model.species[k].rho0;
                if (Nl == 0) return true;
                Fields one; one.m.assign(fields, fields + K);
                const auto st0 = cell_stats(model, one, 1, 0);
                State st; return evaluate_state(model, one, 1, 0, st0, fields[K], volume, st);
            };
            // Parcels with EOS admission backtracking.
            bool accepted = false;
            for (int attempt = 0; attempt <= 33; ++attempt) {
                bool ok = true;
                for (std::size_t fld = 0; fld < F; ++fld) keep[fld] = source[fld][i];
                for (int n = 0; n < count && ok; ++n) {
                    double* pr = &proto[static_cast<std::size_t>(n) * F];
                    const double tx = theta * edges[n].cx, ty = theta * edges[n].cy;
                    for (std::size_t fld = 0; fld < F; ++fld)
                        pr[fld] = theta == 0 ? source[fld][i] : source[fld][i] + (tx * sx[fld] + ty * sy[fld]);
                    if (!admissible(pr, Vi)) { ok = false; break; }
                    for (std::size_t fld = 0; fld < F; ++fld) {
                        pr[fld] = pr[fld] * edges[n].share;
                        keep[fld] -= pr[fld];
                    }
                }
                if (ok) {
                    for (std::size_t fld = 0; fld < F; ++fld) if (fld != K && keep[fld] < 0) {
                        // Rounding of an exhausted positive field: move the
                        // deficit into the largest parcel so the donor's
                        // decomposition stays exact and nonnegative.
                        if (keep[fld] < -4 * eps * source[fld][i]) { ok = false; break; }
                        int largest = 0;
                        for (int n = 1; n < count; ++n)
                            if (proto[static_cast<std::size_t>(n) * F + fld] > proto[static_cast<std::size_t>(largest) * F + fld]) largest = n;
                        proto[static_cast<std::size_t>(largest) * F + fld] += keep[fld];
                        keep[fld] = 0;
                    }
                }
                if (ok && !admissible(keep.data(), retained_volume)) ok = false;
                if (ok) { accepted = true; break; }
                if (theta == 0) break;
                ++local_backtracks; theta = attempt < 32 ? theta * .5 : 0;
            }
            if (!accepted) reject("Production reconstruction EOS backtracking exhausted");
            ws.theta[i] = theta;
            for (std::size_t fld = 0; fld < F; ++fld) ws.retained[fld * N + i] = keep[fld];
            for (int n = 0; n < count; ++n) {
                const double* pr = &proto[static_cast<std::size_t>(n) * F];
                auto& out = edges[n].axis == 0 ? ws.px : ws.py;
                const auto stride = edges[n].axis == 0 ? g.NX : g.NY;
                double mass = 0;
                for (std::size_t fld = 0; fld < F; ++fld) out[fld * stride + edges[n].face] = pr[fld];
                for (std::size_t k = 0; k < K; ++k) mass += pr[k];
                // Donor on the left (dir +1) gives a positive flux.
                (edges[n].axis == 0 ? ws.fx : ws.fy)[edges[n].face] = edges[n].dir > 0 ? mass : -mass;
            }
        }
        return local_backtracks;
    });
    for (const auto b : bt) backtracks += b;
    c.counters.reconstruction_backtracks += backtracks;
    if (c.options.collect_stage_timing) c.timing.reconstruction_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_reconstruction).count();
    // ---- primal gather: retained + incoming parcels; masses before/after.
    ws.mass_after.assign(N, 0);
    std::vector<double> sum_ops(F, 0);
    auto ops = c.exec.partials<std::vector<double>>([&](std::size_t y0, std::size_t y1, std::size_t) {
        std::vector<double> local(F, 0);
        for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
            if (g.wall[i]) continue;
            const auto lf = g.left_face(i), rf = g.right_face(i), bf = g.bottom_face(i), tf = g.top_face(i);
            const bool in_l = ws.qx[lf] > 0, in_r = ws.qx[rf] < 0, in_b = ws.qy[bf] > 0, in_t = ws.qy[tf] < 0;
            double M = 0;
            for (std::size_t fld = 0; fld < F; ++fld) {
                double v = ws.retained[fld * N + i];
                double magnitude = std::abs(v);
                if (in_l) { v += ws.px[fld * g.NX + lf]; magnitude += std::abs(ws.px[fld * g.NX + lf]); }
                if (in_r) { v += ws.px[fld * g.NX + rf]; magnitude += std::abs(ws.px[fld * g.NX + rf]); }
                if (in_b) { v += ws.py[fld * g.NY + bf]; magnitude += std::abs(ws.py[fld * g.NY + bf]); }
                if (in_t) { v += ws.py[fld * g.NY + tf]; magnitude += std::abs(ws.py[fld * g.NY + tf]); }
                if (fld != K && v < 0) reject("Production transport produced a negative inventory");
                if (fld < K) { f.m[fld * N + i] = v; M += v; }
                else if (fld == K) f.U[i] = v;
                else f.q[(fld - K - 1) * N + i] = v;
                local[fld] += 2 * magnitude;
            }
            ws.mass_after[i] = M;
        }
        return local;
    });
    ws.stats_valid = false; // masses changed
    for (const auto& part : ops) for (std::size_t fld = 0; fld < F; ++fld) sum_ops[fld] += part[fld];
    for (std::size_t k = 0; k < K; ++k) acc.operands[k] += sum_ops[k];
    for (std::size_t p = 0; p < P; ++p) acc.operands[K + p] += sum_ops[K + 1 + p];
    acc.operands[c.q_energy()] += sum_ops[K];
    const auto t_dual = std::chrono::steady_clock::now();
    // ---- dual transport of J and K for both components.
    face_inertias(c, ws.mass_after, ws.ix1, ws.iy1);
    const bool corrected = c.settings.momentum_reconstruction == MacMomentumReconstruction::limited_linear;
    ws.face_heat_x.assign(g.NX, 0); ws.face_heat_y.assign(g.NY, 0);
    double mixing = 0, wall_heat = 0, wall_x = 0, wall_y = 0, energy_ops = 0, jx_ops = 0, jy_ops = 0;
    std::size_t limited = 0;
    for (int component = 0; component < 2; ++component) {
        const bool xc = component == 0;
        auto& J = xc ? f.jx : f.jy;
        const auto& m0 = xc ? ws.ix : ws.iy;
        const auto& m1 = xc ? ws.ix1 : ws.iy1;
        const auto& fl = xc ? g.xl : g.yl;
        const auto& fr = xc ? g.xr : g.yr;
        const std::size_t NF = xc ? g.NX : g.NY;
        // Dual cells are faces on a (cw x ch) lattice.
        const std::size_t cw = xc ? g.XW : g.W, ch = xc ? g.H : g.YH;
        const bool normal_periodic = xc ? g.px : g.py, transverse_periodic = xc ? g.py : g.px;
        // Normal edges: one per fluid cell, between its two faces of this
        // component. Transverse edges: between faces adjacent across a corner.
        // Edge mass arrays indexed by the "upper" endpoint face:
        //   en[f]: edge (prev_normal(f) -> f), et[f]: edge (prev_transverse(f) -> f).
        auto& en = ws.dual_en; auto& et = ws.dual_et; auto& has_n = ws.dual_has_n; auto& has_t = ws.dual_has_t;
        en.assign(NF, 0); et.assign(NF, 0); has_n.assign(NF, 0); has_t.assign(NF, 0);
        ws.v0.resize(NF);
        const auto normal_prev = [&](std::size_t fidx) -> std::size_t { // face before along the component axis
            const auto x = fidx % cw, y = fidx / cw;
            if (xc) return x > 0 ? fidx - 1 : (normal_periodic ? fidx + cw - 1 : NF);
            return y > 0 ? fidx - cw : (normal_periodic ? fidx + (ch - 1) * cw : NF);
        };
        const auto transverse_prev = [&](std::size_t fidx) -> std::size_t {
            const auto x = fidx % cw, y = fidx / cw;
            if (xc) return y > 0 ? fidx - cw : (transverse_periodic ? fidx + (ch - 1) * cw : NF);
            return x > 0 ? fidx - 1 : (transverse_periodic ? fidx + cw - 1 : NF);
        };
        const auto& Fn = xc ? ws.fx : ws.fy; // primal flux along the component
        const auto& Ft = xc ? ws.fy : ws.fx; // transverse primal flux
        c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t blk) {
            const std::size_t r0 = y0, r1 = xc ? y1 : g.block_yrow_end(blk);
            for (std::size_t row = r0; row < r1; ++row) for (std::size_t col = 0; col < cw; ++col) {
                const auto fidx = col + row * cw;
                ws.v0[fidx] = m0[fidx] > 0 ? J[fidx] / m0[fidx] : 0;
                // Normal edge ending at fidx: the fluid cell between prev and fidx.
                const auto prev = normal_prev(fidx);
                if (prev != NF) {
                    const Index cell = fl[fidx]; // cell to the left/below of fidx
                    if (cell != none) { has_n[fidx] = 1; en[fidx] = .5 * (Fn[prev] + Fn[fidx]); }
                }
                const auto tp = transverse_prev(fidx);
                if (tp != NF) {
                    has_t[fidx] = 1;
                    double a = 0, b = 0;
                    if (xc) {
                        // Corner at x-face (x, y): y faces of row y at cells x-1 and x.
                        const auto x = col, y = row;
                        if (x > 0 || g.px) a = Ft[g.yface((x + g.W - 1) % g.W, y)];
                        if (x < g.W) b = Ft[g.yface(x, y)];
                    } else {
                        const auto x = col, y = row;
                        if (y > 0 || g.py) a = Ft[g.xface(x, (y + g.H - 1) % g.H)];
                        if (y < g.H) b = Ft[g.xface(x, y)];
                    }
                    et[fidx] = .5 * (a + b);
                }
            }
        });
        // One gather pass per dual cell: keep, transported J and K, upwind
        // mixing heat, wall removal, new J and the correction predictors.
        auto& v0 = ws.v0; auto& jsum = ws.jsum; auto& ktr = ws.ktr; auto& keep = ws.keep; auto& mix = ws.mix; auto& wheat = ws.wheat;
        auto& J1 = ws.dual_j1;
        for (auto* v : {&jsum, &ktr, &keep, &mix, &wheat, &J1}) v->resize(NF);
        const auto normal_next = [&](std::size_t fidx) -> std::size_t {
            const auto x = fidx % cw, y = fidx / cw;
            if (xc) return x + 1 < cw ? fidx + 1 : (normal_periodic ? fidx + 1 - cw : NF);
            return y + 1 < ch ? fidx + cw : (normal_periodic ? fidx + cw - NF : NF);
        };
        const auto transverse_next = [&](std::size_t fidx) -> std::size_t {
            const auto x = fidx % cw, y = fidx / cw;
            if (xc) return y + 1 < ch ? fidx + cw : (transverse_periodic ? fidx + cw - NF : NF);
            return x + 1 < cw ? fidx + 1 : (transverse_periodic ? fidx + 1 - cw : NF);
        };
        const auto is_free = [&](std::size_t fidx) { return fidx != NF && fl[fidx] != none && fr[fidx] != none && m0[fidx] > 0; };
        auto& s0 = ws.slope0; auto& s1 = ws.slope1; auto& c0 = ws.cour0; auto& c1 = ws.cour1;
        for (auto* v : {&s0, &s1, &c0, &c1}) v->resize(NF);
        struct GatherOut { int failed = 0; double wall = 0; };
        auto gathered = c.exec.partials<GatherOut>([&](std::size_t y0, std::size_t y1, std::size_t blk) {
            GatherOut o;
            const std::size_t r1 = xc ? y1 : g.block_yrow_end(blk);
            for (std::size_t row = y0; row < r1; ++row) for (std::size_t col = 0; col < cw; ++col) {
                const auto fidx = col + row * cw;
                // Edges touching fidx: (prev -> fidx) stored at fidx, (fidx -> next) stored at next.
                struct Touch { double mass; std::size_t other; bool upper, normal; };
                Touch touches[4]; int n = 0;
                const auto np = normal_prev(fidx), nn = normal_next(fidx), tp = transverse_prev(fidx), tn = transverse_next(fidx);
                if (has_n[fidx]) touches[n++] = {en[fidx], np, true, true};
                if (nn != NF && has_n[nn]) touches[n++] = {en[nn], nn, false, true};
                if (has_t[fidx]) touches[n++] = {et[fidx], tp, true, false};
                if (tn != NF && has_t[tn]) touches[n++] = {et[tn], tn, false, false};
                double out = 0, jin = 0, kin = 0;
                for (int k = 0; k < n; ++k) {
                    const double a = touches[k].mass; if (a == 0) continue;
                    // Positive mass flows from the lower endpoint to the upper one.
                    const bool donor = touches[k].upper ? a < 0 : a > 0;
                    if (donor) { out += std::abs(a); continue; }
                    const auto d = touches[k].other;
                    if (!(m0[d] > 0)) { o.failed = 1; continue; }
                    // Donor velocity and kinetic energy per unit mass.
                    jin += v0[d] * std::abs(a);
                    kin += .5 * v0[d] * v0[d] * std::abs(a);
                }
                const double kp = m0[fidx] - out;
                if (kp < 0 && kp < -4 * eps * m0[fidx]) o.failed = 2;
                const double kf = std::max(kp, 0.);
                keep[fidx] = kf;
                const double js = v0[fidx] * kf + jin;
                jsum[fidx] = js;
                ktr[fidx] = .5 * v0[fidx] * v0[fidx] * kf + kin;
                const double vp = m1[fidx] > 0 ? js / m1[fidx] : 0;
                double heat = variance(kf, v0[fidx] - vp);
                for (int k = 0; k < n; ++k) {
                    const double a = touches[k].mass; if (a == 0) continue;
                    const bool donor = touches[k].upper ? a < 0 : a > 0;
                    if (!donor) heat += variance(std::abs(a), v0[touches[k].other] - vp);
                }
                mix[fidx] = heat;
                const bool fixed = fl[fidx] == none || fr[fidx] == none;
                if (fixed) {
                    if (js != 0 && !(m1[fidx] > 0)) o.failed = 3;
                    wheat[fidx] = m1[fidx] > 0 ? kinetic(m1[fidx], js) : 0;
                    o.wall -= js; J1[fidx] = 0;
                } else { wheat[fidx] = 0; J1[fidx] = js; }
                s0[fidx] = s1[fidx] = c0[fidx] = c1[fidx] = 0;
                if (!corrected || !is_free(fidx)) continue;
                // Lattice x and y neighbours for limited velocity slopes.
                const auto xprev = xc ? np : tp, xnext = xc ? nn : tn, yprev = xc ? tp : np, ynext = xc ? tn : nn;
                if (is_free(xprev) && is_free(xnext)) s0[fidx] = limited_slope(v0[fidx] - v0[xprev], v0[xnext] - v0[fidx]);
                if (is_free(yprev) && is_free(ynext)) s1[fidx] = limited_slope(v0[fidx] - v0[yprev], v0[ynext] - v0[fidx]);
                // Signed centred dual flux per lattice axis.
                double cx = 0, cy = 0;
                for (int k = 0; k < n; ++k) {
                    const bool along_x = xc ? touches[k].normal : !touches[k].normal;
                    (along_x ? cx : cy) += .5 * touches[k].mass;
                }
                c0[fidx] = cx / m0[fidx]; c1[fidx] = cy / m0[fidx];
            }
            return o;
        });
        for (const auto& o : gathered) {
            if (o.failed == 2) reject("Production dual outgoing mass CFL exceeded");
            if (o.failed == 1) reject("Production dual flux has no occupied donor");
            if (o.failed == 3) reject("Production fixed face retains momentum without inertia");
            (xc ? wall_x : wall_y) += o.wall;
        }
        if (corrected) {
            const auto apply_edge = [&](std::size_t fidx, bool normal_edges, std::size_t& local_limited) {
                const auto upper = fidx;
                const auto lower = normal_edges ? normal_prev(fidx) : transverse_prev(fidx);
                const bool along_x = xc ? normal_edges : !normal_edges;
                const double flux = normal_edges ? en[fidx] : et[fidx];
                if (flux == 0 || !is_free(lower) || !is_free(upper)) return;
                if (!(m1[lower] > 0) || !(m1[upper] > 0)) return;
                const auto donor = flux > 0 ? lower : upper, target = flux > 0 ? upper : lower;
                const double slope = along_x ? s0[donor] : s1[donor];
                const double predicted = sum3(slope * (flux > 0 ? .5 : -.5), c0[donor] * s0[donor] * -.5, c1[donor] * s1[donor] * -.5);
                const double difference = v0[target] - v0[donor];
                double bounded = 0;
                if (predicted != 0 && difference != 0 && (predicted < 0) == (difference < 0))
                    bounded = std::abs(predicted) > std::abs(difference) ? difference : predicted;
                const double request = flux * bounded;
                if (request == 0) return;
                Pair p{J1[lower], J1[upper], mix[lower], mix[upper], ktr[lower], ktr[upper]};
                if (!limit_pair(m1[lower], m1[upper], p, request))
                    precision("Production momentum correction has no admissible limiter");
                J1[lower] = p.jl; J1[upper] = p.jr; mix[lower] = p.ql; mix[upper] = p.qr;
                ktr[lower] = p.kl; ktr[upper] = p.kr;
                if (p.limited) ++local_limited;
            };
            // The reference applies normal edges in cell order, then
            // transverse edges. Each kind only chains along one lattice line
            // (rows or columns) and lines are independent, so processing
            // every line sequentially, lines in parallel, reproduces the
            // reference order exactly for any thread count.
            const auto row_pass = [&](std::size_t rows, auto&& line) {
                (void)rows;
                auto counts = c.exec.partials<std::size_t>([&](std::size_t y0, std::size_t y1, std::size_t blk) {
                    std::size_t local = 0;
                    const std::size_t r1 = rows == g.H ? y1 : g.block_yrow_end(blk);
                    for (std::size_t y = y0; y < r1; ++y) line(y, local);
                    return local;
                });
                for (const auto n : counts) limited += n;
            };
            const auto column_pass = [&](std::size_t columns, auto&& line) {
                auto counts = c.exec.column_partials<std::size_t>(columns, [&](std::size_t x0, std::size_t x1, std::size_t) {
                    std::size_t local = 0;
                    for (std::size_t x = x0; x < x1; ++x) line(x, local);
                    return local;
                });
                for (const auto n : counts) limited += n;
            };
            if (xc) {
                row_pass(g.H, [&](std::size_t y, std::size_t& lim) {
                    for (std::size_t x = 0; x < g.W; ++x) if (!g.wall[x + y * g.W]) apply_edge(g.xface((x + 1) % g.XW, y), true, lim);
                });
                column_pass(g.XW, [&](std::size_t x, std::size_t& lim) {
                    for (std::size_t y = g.py ? 0 : 1; y < g.H; ++y) apply_edge(g.xface(x, y), false, lim);
                });
            } else {
                column_pass(g.W, [&](std::size_t x, std::size_t& lim) {
                    for (std::size_t y = 0; y < g.H; ++y) if (!g.wall[x + y * g.W]) apply_edge(g.yface(x, (y + 1) % g.YH), true, lim);
                });
                row_pass(g.YH, [&](std::size_t y, std::size_t& lim) {
                    for (std::size_t x = g.px ? 0 : 1; x < g.W; ++x) apply_edge(g.yface(x, y), false, lim);
                });
            }
        }
        double component_ops = 0;
        for (std::size_t fidx = 0; fidx < NF; ++fidx) {
            J[fidx] = J1[fidx];
            const double heat = mix[fidx] + wheat[fidx];
            (xc ? ws.face_heat_x : ws.face_heat_y)[fidx] = heat;
            mixing += mix[fidx]; wall_heat += wheat[fidx];
            energy_ops += ktr[fidx] + heat + (m1[fidx] > 0 ? kinetic(m1[fidx], J1[fidx]) : 0) + (m0[fidx] > 0 ? kinetic(m0[fidx], jsum[fidx]) : 0);
            component_ops += 2 * std::abs(jsum[fidx]) + std::abs(J1[fidx]);
        }
        (xc ? jx_ops : jy_ops) += component_ops;
    }
    acc.mixing_heat += mixing; acc.wall_heat += wall_heat; acc.wall_x += wall_x; acc.wall_y += wall_y;
    acc.operands[c.q_jx()] += jx_ops; acc.operands[c.q_jy()] += jy_ops;
    c.counters.limited_momentum += limited;
    if (c.options.collect_stage_timing) c.timing.momentum_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_dual).count();
    // ---- heat deposition by new mass shares, plus the gravity adjustment.
    const double gravity = c.settings.gravity_m_per_s2;
    auto unresolved = c.exec.partials<std::size_t>([&](std::size_t y0, std::size_t y1, std::size_t) {
        std::size_t local = 0;
        for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
            if (g.wall[i] || !(ws.mass_after[i] > 0)) continue;
            const double mi = ws.mass_after[i];
            const auto share = [&](double heat, Index a, Index b) {
                if (heat == 0) return 0.;
                const double total = (a == none ? 0 : ws.mass_after[a]) + (b == none ? 0 : ws.mass_after[b]);
                return total > 0 ? heat * mi / total : 0.;
            };
            const auto lf = g.left_face(i), rf = g.right_face(i), bf = g.bottom_face(i), tf = g.top_face(i);
            DD heat;
            heat.add(share(ws.face_heat_x[lf], g.xl[lf], g.xr[lf])); heat.add(share(ws.face_heat_x[rf], g.xl[rf], g.xr[rf]));
            heat.add(share(ws.face_heat_y[bf], g.yl[bf], g.yr[bf])); heat.add(share(ws.face_heat_y[tf], g.yl[tf], g.yr[tf]));
            if (gravity != 0) {
                const auto work = [&](Index face) {
                    const double potential = ws.fy[face] * gravity * g.h;
                    return -(ws.body_work_y[face] + potential);
                };
                heat.add(share(work(bf), g.yl[bf], g.yr[bf])); heat.add(share(work(tf), g.yl[tf], g.yr[tf]));
            }
            const double h = heat.value();
            if (h == 0) continue;
            const double before = f.U[i];
            f.U[i] = before + h;
            if (f.U[i] == before) ++local;
        }
        return local;
    });
    for (const auto n : unresolved) acc.unresolved += n;
    double gravity_ops = 0;
    if (gravity != 0) for (std::size_t fidx = 0; fidx < g.NY; ++fidx)
        gravity_ops += std::abs(ws.body_work_y[fidx]) + std::abs(ws.fy[fidx] * gravity * g.h);
    acc.operands[c.q_energy()] += energy_ops + 2 * gravity_ops;
}
}
