// Production physical conduction and viscosity (MAC_KERNEL.md, "Physical
// stages"), and the reserved diffusion stage slot. Conduction solves the
// reference backward-Euler graph for temperature changes (no absolute
// temperature cancellation) and applies each face's heat to both cells. With
// gas/liquid cells present, Newton relinearizes each against the complete EOS.
// Viscosity relaxes the reference rank-one stress modes in multicolour order.
#include "mac_kernel_detail.hpp"
#include "mac_kernel_heat_entropy.hpp"
#include "mixed_atmospheres/mac_phase_change.hpp"
#include "mixed_atmospheres/thermal.hpp"
#include <numeric>

namespace ma::experimental::mac_kernel {
void PhysicalAccounts::merge(const PhysicalAccounts& other) {
    accepted_substeps += other.accepted_substeps;
    conduction_stages += other.conduction_stages; conduction_contacts += other.conduction_contacts;
    conduction_iterations += other.conduction_iterations; unresolved_conduction_updates += other.unresolved_conduction_updates;
    viscosity_stages += other.viscosity_stages; viscosity_modes += other.viscosity_modes;
    unresolved_viscous_heat_updates += other.unresolved_viscous_heat_updates;
    unresolved_viscous_momentum_updates += other.unresolved_viscous_momentum_updates;
    const auto add = [](MacEnergySum& target, const MacEnergySum& source) {
        for (std::size_t j = 0; j < source.size(); ++j) target.add(source.component(j));
    };
    add(conductive_redistribution, other.conductive_redistribution);
    add(viscous_heat, other.viscous_heat); add(viscous_stored_heat, other.viscous_stored_heat);
    add(viscous_wall_x, other.viscous_wall_x); add(viscous_wall_y, other.viscous_wall_y);
    add(gravity_internal, other.gravity_internal);
    conductive_entropy.add(other.conductive_entropy); add(viscous_entropy, other.viscous_entropy);
}
MacKernelPhysicalReport PhysicalAccounts::report() const {
    MacKernelPhysicalReport result;
    result.accepted_substeps = accepted_substeps;
    result.conduction_stages = conduction_stages; result.conduction_contacts = conduction_contacts;
    result.conduction_iterations = conduction_iterations; result.unresolved_conduction_updates = unresolved_conduction_updates;
    result.conductive_redistribution_j = conductive_redistribution.value(); result.conductive_entropy_j_per_k = conductive_entropy.value();
    result.viscosity_stages = viscosity_stages; result.viscosity_modes = viscosity_modes;
    result.unresolved_viscous_heat_updates = unresolved_viscous_heat_updates;
    result.unresolved_viscous_momentum_updates = unresolved_viscous_momentum_updates;
    result.viscous_heat = viscous_heat.value(); result.viscous_stored_heat = viscous_stored_heat.value();
    result.viscous_wall_impulse_x = viscous_wall_x.value(); result.viscous_wall_impulse_y = viscous_wall_y.value();
    result.viscous_entropy_lower_bound_j_per_k = viscous_entropy.value().project().value;
    result.gravity_internal_change_wide_j = gravity_internal.value();
    return result;
}
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
// Keep the viscosity report's 32-epsilon requested-update resolution test.
// This is a diagnostic count, not a per-stage admission or conservation gate.
bool unresolved_request(double before, double after, double requested) {
    if (requested == 0) return false;
    if (before == after) return true;
    const auto exact_subtraction = [](double a, double b) {
        if (a == 0 || b == 0) return true;
        return std::isnormal(a) && std::isnormal(b) && std::signbit(a) == std::signbit(b) &&
            .5 * std::abs(a) <= std::abs(b) && .5 * std::abs(b) <= std::abs(a);
    };
    const double change = after - before, error = change - requested, bound = std::ldexp(std::abs(requested), -47);
    if (exact_subtraction(after, before) && exact_subtraction(change, requested) && std::isnormal(bound))
        return std::abs(error) > bound;
    MacEnergySum residual; residual.add(after); residual.add(-before); residual.add(-requested);
    const int sign = residual.sign();
    MacEnergySum remaining; remaining.add(MacEnergy::from_double(requested).magnitude().shifted(-47));
    for (std::size_t j = 0; j < residual.size(); ++j)
        remaining.add(sign < 0 ? residual.component(j) : residual.component(j).negated());
    return remaining.sign() < 0;
}

void redistribution(MacEnergySum& total, double before, double old_low, double after, double new_low) {
    DD change; change.add(after); change.add(-before); change.add(new_low); change.add(-old_low);
    const double sign = change.value() < 0 ? -1. : 1.;
    total.add(MacEnergy::from_double(sign * change.hi).shifted(-1));
    total.add(MacEnergy::from_double(sign * change.lo).shifted(-1));
}

double caloric_entropy(double capacity, double temperature, double before, double after) {
    const double change = after - before;
    if (change == 0) return 0;
    const long double relative = static_cast<long double>(change) / capacity / temperature;
    if (!(relative > -1)) throw EosRefinement{};
    const long double factor = relative == 0 ? 1 : std::log1p(relative) / relative;
    const auto value = static_cast<double>((static_cast<long double>(change) / temperature) * factor);
    if (!std::isfinite(value)) throw ArithmeticRefinement{};
    return value;
}
double combine_sum(const std::vector<double>& part) { DD s; for (const double v : part) s.add(v); return s.value(); }
}

void stage_diffusion(Context&, Fields&, double, Accounts&) {
    // Slot for the gas-diffusion stage being merged on main (mac_diffusion*.cpp).
    // The reference wraps flow intervals in diffusion(dt/2) ... diffusion(dt/2)
    // at the owner level; until a flux-form production diffusion exists, the
    // production policy keeps that wrapper and this slot stays empty.
}

void stage_conduction(Context& c, Fields& f, double dt, Accounts& acc) {
    prepare_primary_remainders(f);
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
            if (e.kind[i] == gas_cell)
                k[i] = std::min(k[i], free_molecular_conductivity(e.Cv[i], e.Vg[i], e.T[i], e.M[i] / moles, g.h));
        }
        return found;
    });
    bool mixed = false; for (const int v : any_mixed) mixed = mixed || v != 0;
    const double area = g.h * g.depth;
    // Void phase: a face conducts over the part both cells' material touches.
    std::vector<std::array<double, 4>> aperture; void_apertures(c, f, aperture);
    const auto contact = [&](Index l, Index r, int axis) {
        return aperture.empty() ? 1. : std::min(aperture[l][axis == 0 ? 1 : 3], aperture[r][axis == 0 ? 0 : 2]);
    };
    std::vector<double> gx(g.NX, 0), gy(g.NY, 0);
    c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t blk) {
        for (std::size_t fidx = y0 * g.XW; fidx < y1 * g.XW; ++fidx) {
            const auto l = g.xl[fidx], r = g.xr[fidx];
            if (l != none && r != none && l != r && occupied(l) && occupied(r)) gx[fidx] = conductance(k[l], k[r], g.h, area) * contact(l, r, 0);
        }
        for (std::size_t fidx = y0 * g.W; fidx < g.block_yrow_end(blk) * g.W; ++fidx) {
            const auto l = g.yl[fidx], r = g.yr[fidx];
            if (l != none && r != none && l != r && occupied(l) && occupied(r)) gy[fidx] = conductance(k[l], k[r], g.h, area) * contact(l, r, 1);
        }
    });
    // Sharp-interface boiling (MAC_PHASE_CHANGE.md, "Sharp interfaces"): a
    // boiling cell is held at its saturation temperature, located at its phase
    // boundary. A neighbour conducts through its own half cell plus the boiling
    // cell's layer of the neighbour's phase, (phase volume fraction) h deep.
    const bool sharp = c.sharp_boiling();
    if (sharp) for (auto* v : {&ws.interface_temperature, &ws.interface_heat, &ws.interface_conductance, &ws.interface_slope}) v->assign(N, 0);
    if (sharp) ws.interface_pair.assign(N, 0);
    const bool boiling = sharp && mixed;
    // Phase fractions are of each cell's material volume (void phase).
    const auto volume = [&](Index n) { return material_volume(f, g.V, n); };
    std::vector<double> kgas, kliquid;
    if (boiling) {
        kgas.assign(N, 0); kliquid.assign(N, 0);
        auto failures = c.exec.partials<int>([&](std::size_t y0, std::size_t y1, std::size_t) {
            int failed = 0;
            for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
                if (e.kind[i] != mixed_cell) continue;
                const double held = volume(static_cast<Index>(i)), gas = e.Vg[i], liquid = held - gas;
                if (!(gas >= mac_interface_phase_fraction * held && liquid >= mac_interface_phase_fraction * held)) continue;
                for (std::size_t p = 0; p < c.phase_pairs.size(); ++p) {
                    const auto& pair = c.phase_pairs[p];
                    if (!(f.m[pair.liquid * N + i] > 0)) continue;
                    bool only = true;
                    for (const auto l : model.liquids) if (l != pair.liquid && f.m[l * N + i] != 0) only = false;
                    double moles = 0, weighted = 0;
                    for (const auto s : model.gases) {
                        const double n = f.m[s * N + i] / model.species[s].molar_mass;
                        moles += n; weighted += n * model.species[s].conductivity;
                    }
                    const double vapour = f.m[pair.vapour * N + i] / model.species[pair.vapour].molar_mass;
                    if (!only || !(moles > 0) || vapour < mac_interface_vapour_fraction * moles) break;
                    // The interface touches the gas beside it, and pressure is
                    // continuous across a flat interface: take the mean pressure
                    // of mostly-gas neighbours. A nearly full cell's own pressure
                    // is stiff and would make the interface temperature ring.
                    double beside = 0; int gases = 0;
                    const auto lf = g.left_face(i), rf = g.right_face(i), bf = g.bottom_face(i), tf = g.top_face(i);
                    for (const Index n : {g.xl[lf], g.xr[rf], g.yl[bf], g.yr[tf]}) {
                        if (n == none || n == static_cast<Index>(i)) continue;
                        if (e.kind[n] == gas_cell || (e.kind[n] == mixed_cell && e.Vg[n] >= .5 * volume(n))) { beside += e.p[n]; ++gases; }
                    }
                    const double pressure = gases > 0 ? beside / gases : e.p[i];
                    try {
                        ws.interface_temperature[i] = mac_saturation_temperature(*c.thermodynamics, pair.liquid_profile,
                            pair.vapour_profile, pressure * (vapour / moles), pressure, e.T[i]);
                    } catch (const std::exception&) { failed = 1; break; }
                    ws.interface_pair[i] = static_cast<std::uint8_t>(p + 1);
                    // Clausius-Clapeyron dT_sat/dp at the cell's vapour fraction:
                    // r T^2 / (L p), the pressure stage's implicit coupling.
                    const double t = ws.interface_temperature[i], r = experimental::reference_gas_constant / model.species[pair.vapour].molar_mass;
                    ws.interface_slope[i] = r * t * t / (mac_latent_heat(*c.thermodynamics, pair.liquid_profile, pair.vapour_profile, t, pressure) * pressure);
                    kgas[i] = weighted / moles; kliquid[i] = model.species[pair.liquid].conductivity;
                    break;
                }
            }
            return failed;
        });
        for (const int failed : failures) if (failed) reject("Production interface saturation temperature did not converge");
        // Transport can leave a thin film on both sides of a face: a mostly-gas
        // cell with a liquid film beside a mostly-liquid cell with a gas film.
        // Across such a face only the cell with the larger minority phase holds
        // the interface (ties: the lower index); side-by-side cells all hold it.
        const auto minority = [&](Index i) { return std::min(e.Vg[i], volume(i) - e.Vg[i]); };
        const auto across = [&](Index a, Index b) {
            if (a == none || b == none || a == b || ws.interface_pair[a] == 0 || ws.interface_pair[b] == 0) return false;
            return (e.Vg[a] >= .5 * volume(a)) != (e.Vg[b] >= .5 * volume(b)) &&
                (minority(b) > minority(a) || (minority(b) == minority(a) && b < a));
        };
        // An interface borders the other phase: a mostly-liquid cell holds it
        // only beside a mostly-gas cell, and the reverse. Trace gas smeared
        // into a liquid's interior is left to the equilibrium relaxation.
        const auto gaseous = [&](Index n) { return e.kind[n] == gas_cell || (e.kind[n] == mixed_cell && e.Vg[n] >= .5 * volume(n)); };
        const auto liquid = [&](Index n) { return e.kind[n] == liquid_cell || (e.kind[n] == mixed_cell && e.Vg[n] < .5 * volume(n)); };
        std::vector<std::uint8_t> released(N, 0);
        c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) {
            for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
                if (ws.interface_pair[i] == 0) continue;
                const auto lf = g.left_face(i), rf = g.right_face(i), bf = g.bottom_face(i), tf = g.top_face(i);
                const auto self = static_cast<Index>(i);
                const bool mostly_gas = e.Vg[i] >= .5 * volume(self);
                bool borders = false;
                for (const Index n : {g.xl[lf], g.xr[rf], g.yl[bf], g.yr[tf]})
                    if (n != none && n != self && (mostly_gas ? liquid(n) : gaseous(n))) borders = true;
                if (!borders || across(self, g.xl[lf]) || across(self, g.xr[rf]) || across(self, g.yl[bf]) || across(self, g.yr[tf])) released[i] = 1;
            }
        });
        for (std::size_t i = 0; i < N; ++i) if (released[i]) ws.interface_pair[i] = 0;
        const auto layered = [&](std::size_t fidx, Index l, Index r, std::vector<double>& faces) {
            if (faces[fidx] == 0) return; // also every exterior face
            const bool pl = ws.interface_pair[l] != 0, pr = ws.interface_pair[r] != 0;
            if (pl == pr) return;
            const auto boundary = pl ? l : r, other = pl ? r : l;
            const bool gas_side = e.kind[other] == gas_cell || (e.kind[other] == mixed_cell && e.Vg[other] >= .5 * volume(other));
            const double depth = g.h * (gas_side ? e.Vg[boundary] / volume(boundary) : 1 - e.Vg[boundary] / volume(boundary));
            const double inner = gas_side ? kgas[boundary] : kliquid[boundary];
            if (inner == 0 && depth > 0) { faces[fidx] = 0; return; }
            const long double resistance = static_cast<long double>(.5 * g.h) / k[other] +
                (depth > 0 ? static_cast<long double>(depth) / inner : 0.L);
            faces[fidx] = static_cast<double>(static_cast<long double>(area) / resistance);
        };
        c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t blk) {
            for (std::size_t fidx = y0 * g.XW; fidx < y1 * g.XW; ++fidx) layered(fidx, g.xl[fidx], g.xr[fidx], gx);
            for (std::size_t fidx = y0 * g.W; fidx < g.block_yrow_end(blk) * g.W; ++fidx) layered(fidx, g.yl[fidx], g.yr[fidx], gy);
        });
    }
    const auto pinned = [&](std::size_t i) { return boiling && ws.interface_pair[i] != 0; };
    // Each held interface's integrated conductance dt sum(G), the heat its
    // temperature drives per kelvin (local, an upper bound on the graph's).
    if (boiling) c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) {
        for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) if (pinned(i))
            ws.interface_conductance[i] = dt * (gx[g.left_face(i)] + gx[g.right_face(i)] + gy[g.bottom_face(i)] + gy[g.top_face(i)]);
    });
    ++c.counters.conduction_stages;
    ++acc.physical.conduction_stages;
    for (const auto* conductances : {&gx, &gy})
        for (const auto value : *conductances) if (value > 0) ++acc.physical.conduction_contacts;
    double ops = 0, moved = 0;
    LinearSystem sys; sys.g = &g; sys.active.assign(N, 0); sys.diag.assign(N, 0);
    sys.wx.resize(g.NX); sys.wy.resize(g.NY);
    std::vector<double> rhs(N, 0), dT, qx(g.NX, 0), qy(g.NY, 0); // q: heat into the left cell
    c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t blk) {
        for (std::size_t fidx = y0 * g.XW; fidx < y1 * g.XW; ++fidx) sys.wx[fidx] = dt * gx[fidx];
        for (std::size_t fidx = y0 * g.W; fidx < g.block_yrow_end(blk) * g.W; ++fidx) sys.wy[fidx] = dt * gy[fidx];
    });
    // Mixed cells (MAC_HEAT.md, "Mixed cells"): U(T) at fixed volume is
    // nonlinear there. Newton relinearizes each mixed cell at its current
    // state as C_k (T - start), start = T_k - Q_k / C_k; other cells keep
    // start = T^n, and a held boiling cell its interface temperature. Each
    // correction is one graph solve, warm-started.
    std::vector<double> start;
    if (mixed) start = e.T;
    if (boiling) for (std::size_t i = 0; i < N; ++i) if (pinned(i)) start[i] = ws.interface_temperature[i];
    const auto& base = mixed ? start : e.T;
    c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) {
        for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
            if (!occupied(i) || pinned(i)) continue;
            sys.active[i] = 1; sys.diag[i] = e.Ceq[i];
            const auto lf = g.left_face(i), rf = g.right_face(i), bf = g.bottom_face(i), tf = g.top_face(i);
            double r = 0;
            if (sys.wx[lf] != 0) r += sys.wx[lf] * (base[g.xl[lf]] - base[i]);
            if (sys.wx[rf] != 0) r += sys.wx[rf] * (base[g.xr[rf]] - base[i]);
            if (sys.wy[bf] != 0) r += sys.wy[bf] * (base[g.yl[bf]] - base[i]);
            if (sys.wy[tf] != 0) r += sys.wy[tf] * (base[g.yr[tf]] - base[i]);
            rhs[i] = r;
        }
    });
    // Temperatures equal within their rounding across every contact are
    // already equilibrium: heat driven by an ulp of temperature is noise.
    const auto isothermal = [&] {
        for (const auto& part : c.exec.partials<int>([&](std::size_t y0, std::size_t y1, std::size_t blk) {
            for (std::size_t fidx = y0 * g.XW; fidx < y1 * g.XW; ++fidx) if (sys.wx[fidx] != 0) {
                const double a = base[g.xl[fidx]], b = base[g.xr[fidx]];
                if (std::abs(a - b) > 64 * eps * (a + b)) return 0;
            }
            for (std::size_t fidx = y0 * g.W; fidx < g.block_yrow_end(blk) * g.W; ++fidx) if (sys.wy[fidx] != 0) {
                const double a = base[g.yl[fidx]], b = base[g.yr[fidx]];
                if (std::abs(a - b) > 64 * eps * (a + b)) return 0;
            }
            return 1;
        })) if (part == 0) return false;
        return true;
    };
    const bool equilibrium = mixed && isothermal();
    if (!equilibrium) {
        const auto solved = solve_linear(c, sys, rhs, dT, 1e-13, 0, 4000, true);
        c.counters.linear += solved.iterations;
        acc.physical.conduction_iterations += solved.iterations;
        if (!solved.converged) reject("Production conductive solve did not converge");
    }
    const auto face_heats = [&] {
        c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t blk) {
            for (std::size_t fidx = y0 * g.XW; fidx < y1 * g.XW; ++fidx) if (sys.wx[fidx] != 0) {
                const auto l = g.xl[fidx], r = g.xr[fidx];
                qx[fidx] = sys.wx[fidx] * ((base[r] - base[l]) + (dT[r] - dT[l]));
            }
            for (std::size_t fidx = y0 * g.W; fidx < g.block_yrow_end(blk) * g.W; ++fidx) if (sys.wy[fidx] != 0) {
                const auto l = g.yl[fidx], r = g.yr[fidx];
                qy[fidx] = sys.wy[fidx] * ((base[r] - base[l]) + (dT[r] - dT[l]));
            }
        });
    };
    // Heat into i: +q of faces where i is left, -q where i is right.
    const auto cell_heat = [&](std::size_t i) {
        DD q; q.add(qx[g.right_face(i)]); q.add(-qx[g.left_face(i)]); q.add(qy[g.top_face(i)]); q.add(-qy[g.bottom_face(i)]);
        return q.value();
    };
    if (!equilibrium) face_heats();
    const auto stats = [&](std::size_t i) { return Stats{e.M[i], e.Cv[i], e.off[i], e.nR[i], e.Nl[i], e.kind[i]}; };
    if (mixed && !equilibrium) {
        const double scale = std::sqrt(combine_sum(c.exec.partials<double>([&](std::size_t y0, std::size_t y1, std::size_t) {
            double s = 0; for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) s += rhs[i] * rhs[i]; return s;
        })));
        std::vector<double> correction;
        double previous = std::numeric_limits<double>::infinity();
        for (int newton = 1;; ++newton) {
            // Relinearize the mixed cells at U^n + Q; mismatch is the EOS
            // temperature minus the linear model's start + Q / C_k.
            struct Step { double worst = 0; bool outside = false; };
            Step step;
            for (const auto& o : c.exec.partials<Step>([&](std::size_t y0, std::size_t y1, std::size_t) {
                Step o;
                for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
                    if (e.kind[i] != mixed_cell || pinned(i)) continue;
                    const double heat = cell_heat(i), energy = f.U[i] + heat;
                    State state;
                    if (!evaluate_state(model, f, N, i, stats(i), energy, material_volume(f, g.V, i), state) || !(state.Ceq > 0)) { o.outside = true; continue; }
                    const double mismatch = state.T - (start[i] + heat / sys.diag[i]);
                    const double bound = 128 * eps * (state.T + std::abs(energy) / state.Ceq);
                    o.worst = std::max(o.worst, std::abs(mismatch) / bound);
                    rhs[i] = state.T - heat / state.Ceq; dT[i] -= rhs[i] - start[i]; start[i] = rhs[i]; sys.diag[i] = state.Ceq;
                }
                return o;
            })) { step.worst = std::max(step.worst, o.worst); step.outside = step.outside || o.outside; }
            if (step.outside) reject("Production conduction left the EOS domain");
            // At the arithmetic floor a further correction only re-rounds.
            if (step.worst <= 1 || (newton > 2 && step.worst <= 64 && step.worst >= .5 * previous)) break;
            if (newton == 32) reject("Production mixed conduction did not converge");
            previous = step.worst;
            // Residual of (C_k + L) x = -L start at the current x = dT.
            c.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) {
                for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
                    if (!occupied(i) || pinned(i)) { rhs[i] = 0; continue; }
                    const auto lf = g.left_face(i), rf = g.right_face(i), bf = g.bottom_face(i), tf = g.top_face(i);
                    const double own = start[i] + dT[i];
                    double r = -sys.diag[i] * dT[i];
                    if (sys.wx[lf] != 0) r += sys.wx[lf] * ((start[g.xl[lf]] + dT[g.xl[lf]]) - own);
                    if (sys.wx[rf] != 0) r += sys.wx[rf] * ((start[g.xr[rf]] + dT[g.xr[rf]]) - own);
                    if (sys.wy[bf] != 0) r += sys.wy[bf] * ((start[g.yl[bf]] + dT[g.yl[bf]]) - own);
                    if (sys.wy[tf] != 0) r += sys.wy[tf] * ((start[g.yr[tf]] + dT[g.yr[tf]]) - own);
                    rhs[i] = r;
                }
            });
            const auto corrected = solve_linear(c, sys, rhs, correction, 0, 1e-13 * scale, 4000, true);
            c.counters.linear += corrected.iterations;
            acc.physical.conduction_iterations += corrected.iterations;
            if (!corrected.converged) reject("Production conductive solve did not converge");
            for (std::size_t i = 0; i < N; ++i) dT[i] += correction[i];
            face_heats();
        }
    }
    struct Heat { double ops = 0, moved = 0; std::size_t unresolved = 0; MacEnergySum redistributed; DD entropy; EnergyRounding energy; };
    for (const auto& o : c.exec.partials<Heat>([&](std::size_t y0, std::size_t y1, std::size_t) {
        Heat o;
        for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
            if (!occupied(i)) continue;
            const double h = cell_heat(i);
            if (pinned(i)) ws.interface_heat[i] = h;
            if (h == 0) continue;
            const double before = f.U[i], low = f.U_remainder[i];
            if (add_internal_energy(f, i, h, o.energy)) ++o.unresolved;
            o.ops += std::abs(before) + std::abs(h); o.moved += std::abs(h);
            redistribution(o.redistributed, before, low, f.U[i], f.U_remainder[i]);
            if (e.kind[i] != mixed_cell) {
                const double entropy = caloric_entropy(e.Cv[i], e.T[i], before, f.U[i]);
                if (c.options.collect_conduction_entropy) {
                    o.entropy.add(entropy);
                    o.entropy.add(f.U_remainder[i] / (e.T[i] + (f.U[i] - before) / e.Cv[i]) - low / e.T[i]);
                }
                continue;
            }
            const double change = f.U[i] - before;
            const auto temperature = [&](double x) {
                const double energy = heat_path_energy(before, f.U[i], x);
                State state;
                if (!evaluate_state(model, f, N, i, stats(i), energy, material_volume(f, g.V, i), state)) throw EosRefinement{};
                return state.T;
            };
            const double after = temperature(1);
            if (c.options.collect_conduction_entropy) {
                const long double allowance = 128 * static_cast<long double>(eps) *
                    (std::abs(before) / e.T[i] + std::abs(f.U[i]) / after + e.Cv[i]);
                o.entropy.add(mixed_cell_entropy(temperature, change, allowance));
                o.entropy.add(f.U_remainder[i] / after - low / e.T[i]);
            }
        }
        return o;
    })) {
        ops += o.ops; moved += o.moved; acc.unresolved += o.unresolved;
        acc.physical.unresolved_conduction_updates += o.unresolved;
        for (std::size_t j = 0; j < o.redistributed.size(); ++j) acc.physical.conductive_redistribution.add(o.redistributed.component(j));
        acc.physical.conductive_entropy.add(o.entropy); acc.energy.merge(o.energy);
    }
    acc.operands[c.q_energy()] += 2 * ops + moved;
}

void stage_viscosity(Context& c, Fields& f, double seconds, Accounts& acc) {
    prepare_primary_remainders(f);
    const auto& visc = c.settings.viscosity;
    if (!(visc.shear_pa_s > 0) && !(visc.bulk_pa_s > 0)) return;
    ++acc.physical.viscosity_stages;
    auto& ws = c.ws; const auto& g = c.g; const auto N = g.N;
    const auto& model = c.model;
    evaluate_cells(c, f, ws.eos);
    ws.cell_mass = ws.eos.M;
    const double grad = 1 / g.h;
    const double shear_weight = visc.shear_pa_s * g.V;
    const double div_weight = shear_weight / 3 + visc.bulk_pa_s * g.V;
    const auto fixed_x = [&](Index fidx) { return g.xl[fidx] == none || g.xr[fidx] == none || g.closed(false, fidx); };
    const auto fixed_y = [&](Index fidx) { return g.yl[fidx] == none || g.yr[fidx] == none || g.closed(true, fidx); };
    struct Dof { int axis; Index face; double gradient; };
    const auto occupied = [&](std::size_t i) { return !g.wall[i] && ws.cell_mass[i] > 0; };
    // Void phase: a partly filled cell's modes act on its material only.
    const auto filled = [&](std::size_t i) { return f.Vo.empty() ? 1. : f.Vo[i] / g.V; };
    double heat_total = 0, wall_x = 0, wall_y = 0, ops = 0; std::size_t modes = 0, unresolved = 0;
    // One rank-one mode; returns the heat. Duplicate faces merge gradients.
    const auto relax = [&](Dof* dofs, int count, double weight, double duration, double& wx_out, double& wy_out,
        double& ops_out, std::size_t& unresolved_momentum) {
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
                const double before = J;
                J += delta;
                if (unresolved_request(before, J, delta)) ++unresolved_momentum;
            }
        }
        return heat;
    };
    const auto deposit = [&](std::size_t cell, double heat, std::size_t& unres, std::size_t& unresolved_resolution,
        MacEnergySum& stored, MacEnergySum& entropy, EnergyRounding& energy) {
        const double before = f.U[cell], low = f.U_remainder[cell];
        if (add_internal_energy(f, cell, heat, energy)) ++unres;
        DD actual; actual.add(f.U[cell]); actual.add(-before); actual.add(f.U_remainder[cell]); actual.add(-low);
        const double change = actual.value();
        if (unresolved_request(0, change, heat)) ++unresolved_resolution;
        if (change == 0) return;
        stored.add(actual.hi); stored.add(actual.lo);
        const auto& e = ws.eos;
        const Stats stats{e.M[cell], e.Cv[cell], e.off[cell], e.nR[cell], e.Nl[cell], e.kind[cell]};
        State after;
        if (!evaluate_state(model, f, N, cell, stats, f.U[cell], material_volume(f, g.V, cell), after)) throw EosRefinement{};
        const double contribution = change / after.T;
        if (std::isnormal(contribution)) entropy.add(contribution);
        else entropy.add(MacEnergy::from_double(change).shared(1, after.T));
    };
    for (int sweep = 0; sweep < 2; ++sweep) {
        // Cell modes, then corner shear modes; the reverse sweep reverses all.
        for (int phase = 0; phase < 2; ++phase) {
            const int kind_group = sweep == 0 ? phase : 1 - phase; // 0 = cell modes, 1 = corner modes
            if (kind_group == 1 && !(shear_weight > 0)) continue;
            for (int step = 0; step < 9; ++step) {
                const int colour_index = sweep == 0 ? step : 8 - step;
                const auto cxc = static_cast<std::size_t>(colour_index / 3), cyc = static_cast<std::size_t>(colour_index % 3);
                struct Out {
                    double heat = 0, wx = 0, wy = 0, ops = 0;
                    std::size_t modes = 0, unresolved = 0, unresolved_resolution = 0, unresolved_momentum = 0;
                    MacEnergySum stored, entropy; EnergyRounding energy;
                };
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
                                    const double weight = (kind == 0 ? shear_weight : div_weight) * filled(cell);
                                    if (!(weight > 0)) continue;
                                    Dof d[4] = {{0, l, -grad}, {0, r, grad}, {1, b, kind == 0 ? grad : -grad}, {1, t, kind == 0 ? -grad : grad}};
                                    ++o.modes;
                                    const double heat = relax(d, 4, weight, .5 * seconds, o.wx, o.wy, o.ops, o.unresolved_momentum);
                                    if (heat != 0) { deposit(cell, heat, o.unresolved, o.unresolved_resolution, o.stored, o.entropy, o.energy); o.heat += heat; }
                                }
                            } else {
                                if (!((y > 0 || g.py) && (x > 0 || g.px))) continue;
                                const auto left = (x + g.W - 1) % g.W, bottom = (y + g.H - 1) % g.H;
                                const std::size_t cells[4] = {left + bottom * g.W, x + bottom * g.W, left + y * g.W, x + y * g.W};
                                bool ok = true; for (const auto cc : cells) ok = ok && occupied(cc);
                                if (!ok) continue;
                                Dof d[4] = {{0, g.xface(x, bottom), -grad}, {0, g.xface(x, y), grad}, {1, g.yface(left, y), -grad}, {1, g.yface(x, y), grad}};
                                ++o.modes;
                                double share_filled = 1; for (const auto cc : cells) share_filled = std::min(share_filled, filled(cc));
                                const double heat = relax(d, 4, shear_weight * share_filled, .5 * seconds, o.wx, o.wy, o.ops, o.unresolved_momentum);
                                if (heat != 0) {
                                    const double share = heat / 4;
                                    for (const auto cc : cells) deposit(cc, share, o.unresolved, o.unresolved_resolution, o.stored, o.entropy, o.energy);
                                    o.heat += heat;
                                }
                            }
                        }
                    }
                    return o;
                });
                for (const auto& o : parts) {
                    heat_total += o.heat; wall_x += o.wx; wall_y += o.wy; ops += o.ops; modes += o.modes; unresolved += o.unresolved;
                    acc.physical.unresolved_viscous_momentum_updates += o.unresolved_momentum;
                    acc.physical.unresolved_viscous_heat_updates += o.unresolved_resolution; acc.energy.merge(o.energy);
                    for (std::size_t j = 0; j < o.stored.size(); ++j) acc.physical.viscous_stored_heat.add(o.stored.component(j));
                    for (std::size_t j = 0; j < o.entropy.size(); ++j) acc.physical.viscous_entropy.add(o.entropy.component(j));
                }
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
    acc.physical.viscosity_modes += modes;
    acc.physical.viscous_heat.add(heat_total); acc.physical.viscous_wall_x.add(wall_x); acc.physical.viscous_wall_y.add(wall_y);
}
}
