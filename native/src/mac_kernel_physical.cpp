// Production physical conduction and viscosity (MAC_KERNEL.md, "Physical
// stages"), and the reserved diffusion stage slot. Conduction solves the
// reference backward-Euler graph for temperature changes (no absolute
// temperature cancellation) and applies each face's heat to both cells.
// Viscosity relaxes the reference rank-one stress modes in multicolour order.
#include "mac_kernel_detail.hpp"

namespace ma::experimental::mac_kernel {
namespace {
std::size_t colour(std::size_t x, std::size_t n, bool periodic) { return periodic && n % 2 == 1 && x + 1 == n ? 2 : x % 2; }
double decay(double x) {
    if (x == 0 || x < 0x1p-53) return x;
    if (x >= 64) return 1;
    return -std::expm1(-x);
}
double conductance(double ka, double kb, double h, double area) {
    if (ka == 0 || kb == 0 || area == 0) return 0;
    return static_cast<double>(static_cast<long double>(area) /
        (static_cast<long double>(.5 * h) / ka + static_cast<long double>(.5 * h) / kb));
}
}

void stage_diffusion(Context&, Fields&, double, Accounts&) {
    // Slot for the gas-diffusion stage being merged on main (mac_diffusion*.cpp).
    // The reference wraps flow intervals in diffusion(dt/2) ... diffusion(dt/2)
    // at the owner level; until a flux-form production diffusion exists, the
    // production policy keeps that wrapper and this slot stays empty.
}

void stage_conduction(Context& c, Fields& f, double dt, Accounts& acc) {
    if (!c.settings.thermal_conduction) return;
    auto& ws = c.ws; const auto& g = c.g; const auto N = g.N; const auto& model = c.model;
    evaluate_cells(c, f, ws.eos);
    const auto& e = ws.eos;
    const auto occupied = [&](std::size_t i) { return e.kind[i] == gas_cell || e.kind[i] == liquid_cell || e.kind[i] == mixed_cell; };
    std::vector<double> k(N, 0);
    auto any_mixed = c.exec.partials<int>([&](std::size_t y0, std::size_t y1, std::size_t) {
        int found = 0;
        for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
            if (!occupied(i)) continue;
            if (e.kind[i] == mixed_cell) found = 1;
            double moles = 0;
            for (const auto s : model.gases) { const double m = f.m[s * N + i]; if (m != 0) moles += m / model.species[s].molar_mass; }
            double num = 0, den = 0;
            for (std::size_t s = 0; s < model.K(); ++s) {
                const double m = f.m[s * N + i]; if (!(m > 0)) continue;
                const auto& sp = model.species[s];
                const double w = sp.gas ? e.Vg[i] * (m / sp.molar_mass) / moles : m / sp.rho0 / (1 + e.p[i] / sp.bulk);
                num += w * sp.conductivity; den += w;
            }
            k[i] = num / den;
            if (!std::isfinite(k[i]) || k[i] < 0) throw std::invalid_argument("Production mixture conductivity is unrepresentable");
        }
        return found;
    });
    bool mixed = false; for (const int v : any_mixed) mixed = mixed || v != 0;
    const double area = g.h * g.depth;
    std::vector<double> gx(g.NX, 0), gy(g.NY, 0);
    c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t blk) {
        for (std::size_t fidx = y0 * g.XW; fidx < y1 * g.XW; ++fidx) {
            const auto l = g.xl[fidx], r = g.xr[fidx];
            if (l != none && r != none && l != r && occupied(l) && occupied(r)) gx[fidx] = conductance(k[l], k[r], g.h, area);
        }
        for (std::size_t fidx = y0 * g.W; fidx < g.block_yrow_end(blk) * g.W; ++fidx) {
            const auto l = g.yl[fidx], r = g.yr[fidx];
            if (l != none && r != none && l != r && occupied(l) && occupied(r)) gy[fidx] = conductance(k[l], k[r], g.h, area);
        }
    });
    ++c.counters.conduction_stages;
    double ops = 0, moved = 0;
    if (!mixed) {
        LinearSystem sys; sys.g = &g; sys.active.assign(N, 0); sys.diag.assign(N, 0);
        sys.wx.resize(g.NX); sys.wy.resize(g.NY);
        std::vector<double> rhs(N, 0), dT, qx(g.NX, 0), qy(g.NY, 0); // q: heat into the left cell
        c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t blk) {
            for (std::size_t fidx = y0 * g.XW; fidx < y1 * g.XW; ++fidx) sys.wx[fidx] = dt * gx[fidx];
            for (std::size_t fidx = y0 * g.W; fidx < g.block_yrow_end(blk) * g.W; ++fidx) sys.wy[fidx] = dt * gy[fidx];
        });
        c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) {
            for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
                if (!occupied(i)) continue;
                sys.active[i] = 1; sys.diag[i] = e.Ceq[i];
                const auto lf = g.left_face(i), rf = g.right_face(i), bf = g.bottom_face(i), tf = g.top_face(i);
                double r = 0;
                if (sys.wx[lf] != 0) r += sys.wx[lf] * (e.T[g.xl[lf]] - e.T[i]);
                if (sys.wx[rf] != 0) r += sys.wx[rf] * (e.T[g.xr[rf]] - e.T[i]);
                if (sys.wy[bf] != 0) r += sys.wy[bf] * (e.T[g.yl[bf]] - e.T[i]);
                if (sys.wy[tf] != 0) r += sys.wy[tf] * (e.T[g.yr[tf]] - e.T[i]);
                rhs[i] = r;
            }
        });
        const auto solved = solve_linear(c, sys, rhs, dT, 1e-13, 0, 4000);
        c.counters.linear += solved.iterations;
        if (!solved.converged) reject("Production conductive solve did not converge");
        c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t blk) {
            for (std::size_t fidx = y0 * g.XW; fidx < y1 * g.XW; ++fidx) if (sys.wx[fidx] != 0) {
                const auto l = g.xl[fidx], r = g.xr[fidx];
                qx[fidx] = sys.wx[fidx] * ((e.T[r] - e.T[l]) + (dT[r] - dT[l]));
            }
            for (std::size_t fidx = y0 * g.W; fidx < g.block_yrow_end(blk) * g.W; ++fidx) if (sys.wy[fidx] != 0) {
                const auto l = g.yl[fidx], r = g.yr[fidx];
                qy[fidx] = sys.wy[fidx] * ((e.T[r] - e.T[l]) + (dT[r] - dT[l]));
            }
        });
        struct Heat { double ops = 0, moved = 0; std::size_t unresolved = 0; };
        for (const auto& o : c.exec.partials<Heat>([&](std::size_t y0, std::size_t y1, std::size_t) {
            Heat o;
            for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
                if (!occupied(i)) continue;
                // Heat into i: +q of faces where i is left, -q where i is right.
                DD q; q.add(qx[g.right_face(i)]); q.add(-qx[g.left_face(i)]); q.add(qy[g.top_face(i)]); q.add(-qy[g.bottom_face(i)]);
                const double h = q.value();
                if (h == 0) continue;
                const double before = f.U[i];
                f.U[i] = before + h;
                if (f.U[i] == before) ++o.unresolved;
                o.ops += std::abs(before) + std::abs(h); o.moved += std::abs(h);
            }
            return o;
        })) { ops += o.ops; moved += o.moved; acc.unresolved += o.unresolved; }
    } else {
        // Complete-EOS pairwise contact solves in the reference order (cell
        // order, right contact then top contact), forward then reverse halves.
        struct Contact { Index a, b; double G; };
        std::vector<Contact> contacts;
        for (std::size_t i = 0; i < N; ++i) {
            if (!occupied(i)) continue;
            const auto rf = g.right_face(i), tf = g.top_face(i);
            if ((i % g.W + 1 < g.W || g.px) && gx[rf] > 0) contacts.push_back({static_cast<Index>(i), g.xr[rf], gx[rf]});
            if ((i / g.W + 1 < g.H || g.py) && gy[tf] > 0) contacts.push_back({static_cast<Index>(i), g.yr[tf], gy[tf]});
        }
        auto& cache = ws.eos;
        const auto stats = [&](Index i) { return Stats{cache.M[i], cache.Cv[i], cache.off[i], cache.nR[i], cache.Nl[i], cache.kind[i]}; };
        const auto exchange = [&](const Contact& ct, double duration) {
            State sa, sb;
            if (!evaluate_state(model, f, N, ct.a, stats(ct.a), f.U[ct.a], g.V, sa) ||
                !evaluate_state(model, f, N, ct.b, stats(ct.b), f.U[ct.b], g.V, sb)) reject("Production conduction left the EOS domain");
            if (sa.T == sb.T) return;
            const bool forward = sa.T < sb.T;
            const Index cold = forward ? ct.a : ct.b, hot = forward ? ct.b : ct.a;
            const State& cs = forward ? sa : sb; const State& hs = forward ? sb : sa;
            const double m = duration * ct.G;
            double q = m * (hs.T - cs.T) / (1 + m * (1 / cs.Ceq + 1 / hs.Ceq));
            for (int it = 0; it < 64; ++it) {
                State cn, hn;
                if (!evaluate_state(model, f, N, cold, stats(cold), f.U[cold] + q, g.V, cn) ||
                    !evaluate_state(model, f, N, hot, stats(hot), f.U[hot] - q, g.V, hn)) { q *= .5; continue; }
                const double residual = q - m * (hn.T - cn.T);
                const double derivative = 1 + m * (1 / cn.Ceq + 1 / hn.Ceq);
                const double next = q - residual / derivative;
                if (std::abs(next - q) <= 4 * eps * std::abs(q)) { q = next; break; }
                q = next;
            }
            const double uc = f.U[cold], uh = f.U[hot];
            f.U[cold] = uc + q; f.U[hot] = uh - q;
            if (q != 0 && (f.U[cold] == uc || f.U[hot] == uh)) ++acc.unresolved;
            ops += std::abs(uc) + std::abs(uh) + 2 * std::abs(q); moved += 2 * std::abs(q);
        };
        if (contacts.size() == 1) exchange(contacts.front(), dt);
        else {
            for (const auto& ct : contacts) exchange(ct, .5 * dt);
            for (auto it = contacts.rbegin(); it != contacts.rend(); ++it) exchange(*it, .5 * dt);
        }
    }
    acc.operands[c.q_energy()] += 2 * ops + moved;
}

void stage_viscosity(Context& c, Fields& f, double seconds, Accounts& acc) {
    const auto& visc = c.settings.viscosity;
    if (!(visc.shear_pa_s > 0) && !(visc.bulk_pa_s > 0)) return;
    auto& ws = c.ws; const auto& g = c.g; const auto N = g.N;
    evaluate_cells(c, f, ws.eos);
    ws.cell_mass = ws.eos.M;
    const double grad = 1 / g.h;
    const double shear_weight = visc.shear_pa_s * g.V;
    const double div_weight = shear_weight / 3 + visc.bulk_pa_s * g.V;
    const auto fixed_x = [&](Index fidx) { return g.xl[fidx] == none || g.xr[fidx] == none; };
    const auto fixed_y = [&](Index fidx) { return g.yl[fidx] == none || g.yr[fidx] == none; };
    struct Dof { int axis; Index face; double gradient; };
    const auto occupied = [&](std::size_t i) { return !g.wall[i] && ws.cell_mass[i] > 0; };
    double heat_total = 0, wall_x = 0, wall_y = 0, ops = 0; std::size_t modes = 0, unresolved = 0;
    // One rank-one mode; returns the heat. Duplicate faces merge gradients.
    const auto relax = [&](Dof* dofs, int count, double weight, double duration, double& wx_out, double& wy_out, double& ops_out) {
        // Merge duplicates (small periodic grids).
        int n = 0; Dof merged[4];
        for (int k = 0; k < count; ++k) {
            bool found = false;
            for (int m = 0; m < n; ++m) if (merged[m].axis == dofs[k].axis && merged[m].face == dofs[k].face) { merged[m].gradient += dofs[k].gradient; found = true; }
            if (!found) merged[n++] = dofs[k];
        }
        double strain = 0, inverse = 0;
        for (int k = 0; k < n; ++k) {
            const auto& d = merged[k];
            const bool fixed = d.axis == 0 ? fixed_x(d.face) : fixed_y(d.face);
            if (fixed || d.gradient == 0) continue;
            const double I = d.axis == 0 ? ws.ix[d.face] : ws.iy[d.face];
            const double J = d.axis == 0 ? f.jx[d.face] : f.jy[d.face];
            strain += d.gradient * (J / I);
            inverse += .5 * (1 / I) * d.gradient * d.gradient * 2;
        }
        if (weight == 0 || strain == 0 || inverse == 0) return 0.;
        const double ldt = weight * inverse * duration;
        const double relaxation = decay(ldt);
        const double impulse = strain * relaxation / inverse;
        const double heat = .5 * (1 / inverse) * strain * strain * decay(2 * ldt);
        for (int k = 0; k < n; ++k) {
            const auto& d = merged[k];
            const double delta = -(d.gradient * impulse);
            const bool fixed = d.axis == 0 ? fixed_x(d.face) : fixed_y(d.face);
            if (fixed) (d.axis == 0 ? wx_out : wy_out) -= delta;
            else {
                auto& J = d.axis == 0 ? f.jx[d.face] : f.jy[d.face];
                ops_out += std::abs(J) + std::abs(delta);
                J += delta;
            }
        }
        return heat;
    };
    const auto deposit = [&](std::size_t cell, double heat, std::size_t& unres) {
        const double before = f.U[cell]; f.U[cell] = before + heat;
        if (heat != 0 && f.U[cell] == before) ++unres;
    };
    for (int sweep = 0; sweep < 2; ++sweep) {
        // Cell modes, then corner shear modes; the reverse sweep reverses all.
        for (int phase = 0; phase < 2; ++phase) {
            const int kind_group = sweep == 0 ? phase : 1 - phase; // 0 = cell modes, 1 = corner modes
            if (kind_group == 1 && !(shear_weight > 0)) continue;
            for (int step = 0; step < 9; ++step) {
                const int colour_index = sweep == 0 ? step : 8 - step;
                const auto cxc = static_cast<std::size_t>(colour_index / 3), cyc = static_cast<std::size_t>(colour_index % 3);
                struct Out { double heat = 0, wx = 0, wy = 0, ops = 0; std::size_t modes = 0, unresolved = 0; };
                auto parts = c.exec.partials<Out>([&](std::size_t y0, std::size_t y1, std::size_t) {
                    Out o;
                    for (std::size_t y = y0; y < y1; ++y) {
                        if (colour(y, g.H, g.py) != cyc) continue;
                        for (std::size_t x = 0; x < g.W; ++x) {
                            if (colour(x, g.W, g.px) != cxc) continue;
                            const auto cell = x + y * g.W;
                            if (kind_group == 0) {
                                if (!occupied(cell)) continue;
                                const auto l = g.xface(x, y), r = g.xface((x + 1) % g.XW, y);
                                const auto b = g.yface(x, y), t = g.yface(x, (y + 1) % g.YH);
                                for (int kk = 0; kk < 2; ++kk) {
                                    const int kind = sweep == 0 ? kk : 1 - kk;
                                    const double weight = kind == 0 ? shear_weight : div_weight;
                                    if (!(weight > 0)) continue;
                                    Dof d[4] = {{0, l, -grad}, {0, r, grad}, {1, b, kind == 0 ? grad : -grad}, {1, t, kind == 0 ? -grad : grad}};
                                    ++o.modes;
                                    const double heat = relax(d, 4, weight, .5 * seconds, o.wx, o.wy, o.ops);
                                    if (heat != 0) { deposit(cell, heat, o.unresolved); o.heat += heat; }
                                }
                            } else {
                                if (!((y > 0 || g.py) && (x > 0 || g.px))) continue;
                                const auto left = (x + g.W - 1) % g.W, bottom = (y + g.H - 1) % g.H;
                                const std::size_t cells[4] = {left + bottom * g.W, x + bottom * g.W, left + y * g.W, x + y * g.W};
                                bool ok = true; for (const auto cc : cells) ok = ok && occupied(cc);
                                if (!ok) continue;
                                Dof d[4] = {{0, g.xface(x, bottom), -grad}, {0, g.xface(x, y), grad}, {1, g.yface(left, y), -grad}, {1, g.yface(x, y), grad}};
                                ++o.modes;
                                const double heat = relax(d, 4, shear_weight, .5 * seconds, o.wx, o.wy, o.ops);
                                if (heat != 0) {
                                    const double share = heat / 4;
                                    for (const auto cc : cells) deposit(cc, share, o.unresolved);
                                    o.heat += heat;
                                }
                            }
                        }
                    }
                    return o;
                });
                for (const auto& o : parts) { heat_total += o.heat; wall_x += o.wx; wall_y += o.wy; ops += o.ops; modes += o.modes; unresolved += o.unresolved; }
            }
        }
    }
    acc.wall_x += wall_x; acc.wall_y += wall_y; acc.unresolved += unresolved;
    acc.operands[c.q_jx()] += ops; acc.operands[c.q_jy()] += ops;
    double u_ops = 0;
    for (const double v : c.exec.partials<double>([&](std::size_t y0, std::size_t y1, std::size_t) {
        double sum = 0; for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) if (!g.wall[i]) sum += std::abs(f.U[i]);
        return sum;
    })) u_ops += v;
    (void)N;
    acc.operands[c.q_energy()] += u_ops + 2 * heat_total;
    c.counters.viscosity_modes += modes;
}
}
