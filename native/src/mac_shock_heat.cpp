#include "mixed_atmospheres/mac_shock_heat.hpp"
#include "mixed_atmospheres/fluid_thermodynamics.hpp"
#include "mac_material_entropy_detail.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>

namespace ma::experimental {
namespace {
constexpr auto none = std::numeric_limits<std::size_t>::max();
void require(bool ok, const char* why) { if (!ok) throw std::invalid_argument(why); }
[[noreturn]] void precision(const char* why) { throw FluidFatalError(FluidFatalCode::numerical_precision, why); }
MacEnergy sum(MacEnergy a, MacEnergy b) { MacEnergySum s; s.add(a); s.add(b); return s.value(); }
bool same(MacEnergy a, MacEnergy b) { return a.mantissa() == b.mantissa() && a.exponent() == b.exponent(); }
bool interval(double value, double lo, double hi) {
    MacEnergyAccount a;
    if (value < lo) { a.add(value); a.add(-lo); }
    if (value > hi) { a.add(value); a.add(-hi); }
    return a.within_bound();
}
MacEnergy harmonic_capacity(double a, double b) {
    const auto lo = std::min(a, b), hi = std::max(a, b);
    return MacEnergy::from_double(lo).multiplied(2).shared(1, 1 + lo / hi);
}
MacEnergy relaxation_fraction(MacEnergy x) {
    if (x.is_zero()) return {};
    // For x < 2^-53, relative truncation error in 1-exp(-x) < 2^-54.
    // Keep the full x rather than projecting a positive subrange value to zero.
    if (x.exponent() <= MacExponent(-53)) return x;
    // exp(-64) is far below half a binary64 ulp at one.
    if (x.exponent() >= MacExponent(7)) return MacEnergy::from_double(1);
    return MacEnergy::from_double(-std::expm1(-x.require_representable()));
}
bool constant_capacity(const ReferenceEquilibriumState& s) { return !s.has_gas || s.liquid_volume_m3 == 0; }
MacEnergy capacity_upper(const ReferenceEquilibriumState& s) {
    // At fixed V, Cv <= Ceff <= Cv+nR for this zero-expansion elastic EOS.
    // The outward allowance covers only construction of a root bracket.
    MacEnergySum capacity; capacity.add(s.intrinsic_heat_capacity_j_per_k);
    if (s.has_gas) capacity.add(MacEnergy::from_double(s.gas_pressure_pa).multiplied(s.gas_volume_m3).shared(1, s.temperature_k));
    return sum(capacity.value(), capacity.value().shifted(-45));
}
MacEnergy root_temperature_bound(double uh, double uc, double nh, double nc, MacEnergy q,
    const ReferenceEquilibriumState& a, const ReferenceEquilibriumState& b, double th, double tc) {
    MacEnergySum bound; bound.add(MacEnergy::from_double(th).shifted(-45)); bound.add(MacEnergy::from_double(tc).shifted(-45));
    MacEnergySum hot, cold;
    for (auto v : {std::abs(uh), std::abs(nh)}) hot.add(v);
    for (auto v : {std::abs(uc), std::abs(nc)}) cold.add(v);
    hot.add(q.magnitude()); cold.add(q.magnitude());
    bound.add(hot.value().shared(1, a.intrinsic_heat_capacity_j_per_k).shifted(-45));
    bound.add(cold.value().shared(1, b.intrinsic_heat_capacity_j_per_k).shifted(-45));
    return bound.value();
}
MacEnergy equilibrium_transfer(const FluidThermodynamics& model, const MixtureInventory& hot,
    const MixtureInventory& cold, double vh, double vc, const ReferenceEquilibriumState& a,
    const ReferenceEquilibriumState& b, std::size_t& iterations, std::size_t& rejected,
    double& final_hot_t, double& final_cold_t, MacEnergy& final_bound,
    const MacStoredRemainder& hot_remainder, const MacStoredRemainder& cold_remainder) {
    if (constant_capacity(a) && constant_capacity(b))
        return harmonic_capacity(a.equilibrium_heat_capacity_at_cell_volume_j_per_k,
            b.equilibrium_heat_capacity_at_cell_volume_j_per_k).shifted(-1).multiplied(a.temperature_k - b.temperature_k);
    const auto ch = capacity_upper(a), cc = capacity_upper(b);
    MacEnergy lo, hi = (mac_scaled_difference(ch, cc).mantissa() < 0 ? ch : cc).multiplied(a.temperature_k - b.temperature_k);
    for (std::size_t i = 0; i < 128; ++i) {
        ++iterations; const auto mid = sum(lo, hi).shifted(-1);
        MacMaterialCell hc{hot,{},hot_remainder},ccell{cold,{},cold_remainder};
        (void)add_mac_cell_internal_energy(hc,mid.negated());(void)add_mac_cell_internal_energy(ccell,mid);
        const auto& h=hc.mixture;const auto& c=ccell.mixture;
        ReferenceEquilibriumState next_hot, next_cold;
        try { next_hot = model.evaluate(h, vh); next_cold = model.evaluate(c, vc); }
        catch (const ReferenceError& e) {
            // Increasing positive heat only cools hot and heats cold. A trial
            // outside either endpoint's thermal/compression domain is an upper
            // inadmissible heat bound, not an instruction to extrapolate it.
            if (e.code() != ReferenceErrorCode::temperature_outside_model &&
                e.code() != ReferenceErrorCode::compression_outside_model &&
                e.code() != ReferenceErrorCode::insufficient_thermal_energy) throw;
            if (same(mid, lo) || same(mid, hi)) precision("Shock-heat equilibrium is outside its valid thermal domains");
            ++rejected; hi = mid; continue;
        }
        const auto th = next_hot.temperature_k, tc = next_cold.temperature_k;
        MacEnergyAccount temperature; temperature.add(th); temperature.add(-tc);
        const auto allowance = root_temperature_bound(hot.energy, cold.energy, h.energy, c.energy, mid, a, b, th, tc);
        MacEnergySum remaining; remaining.add(allowance); remaining.add(temperature.residual().negated());
        // A broader storage certificate is used only on the cooling side;
        // actual final temperatures may never cross equilibrium under it.
        if (temperature.within_bound() || (th >= tc && remaining.sign() >= 0)) {
            final_hot_t = th; final_cold_t = tc; final_bound = allowance; return mid;
        }
        if (same(mid, lo) || same(mid, hi)) precision("Shock-heat equilibrium lacks a resolved thermal partition");
        if (th > tc) lo = mid; else hi = mid;
    }
    precision("Shock-heat equilibrium exhausted its bracket iterations");
}
struct EntropyCell { const MixtureInventory* source; const MixtureInventory* endpoint; double volume; MacStoredRemainder source_remainder{},endpoint_remainder{}; };
struct EntropyReading {
    double temperature_k,gas_volume_m3,liquid_volume_m3,elastic_energy_j;
    EntropyReading(const ReferenceEquilibriumState& state):temperature_k(state.temperature_k),gas_volume_m3(state.gas_volume_m3),
        liquid_volume_m3(state.liquid_volume_m3),elastic_energy_j(state.elastic_energy_j){}
};
void entropy_check(const FluidThermodynamics& model, std::span<const EntropyCell> cells,
    const std::vector<EntropyReading>& before, const std::vector<EntropyReading>& after,
    double& change, double& allowance) {
    MacEnergySum entropy, bound;
    for (std::size_t i = 0; i < before.size(); ++i) {
        const auto& source = *cells[i].source; const auto& endpoint = *cells[i].endpoint;
        // At fixed masses and volume an identical stored U has exactly zero
        // change. Otherwise use the actual caloric increment, avoiding the
        // subtraction of nearly equal absolute EOS entropy readings.
        if (source.energy != endpoint.energy || source.masses != endpoint.masses)
            entropy.add(entropy_detail::change(model, source, cells[i].volume, before[i], endpoint, cells[i].volume, after[i]));
        entropy.add(mac_stored_remainder_value(cells[i].endpoint_remainder).shared(1,after[i].temperature_k));
        entropy.add(mac_stored_remainder_value(cells[i].source_remainder).shared(1,before[i].temperature_k).negated());
        bound.add(MacEnergy::from_double(std::abs(source.energy)).shared(1, before[i].temperature_k).shifted(-45));
        bound.add(MacEnergy::from_double(std::abs(endpoint.energy)).shared(1, after[i].temperature_k).shifted(-45));
    }
    MacEnergySum lower = bound;
    for (std::size_t i = 0; i < entropy.size(); ++i) lower.add(entropy.component(i));
    if (lower.sign() < 0) precision("Shock heat reduces actual stored-state entropy");
    change = entropy.value().project().value; allowance = bound.value().project().value;
}
struct Link { std::size_t axis, face, left, right; MacScaled closing; MacEnergy integrated; MacScaled diffusion; };
MacShockHeatPair relax_pair(const FluidThermodynamics& model, const MixtureInventory& left,
    const MixtureInventory& right, double vl, double vr, MacEnergy gdt,
    ReferenceEquilibriumState& a,ReferenceEquilibriumState& b,
    const MacStoredRemainder& left_remainder={},const MacStoredRemainder& right_remainder={}) {
    require_mac_energy_environment();
    require(canonical_mac_primary(left.energy,left_remainder) && canonical_mac_primary(right.energy,right_remainder),
        "Noncanonical shock-heat primary-U input");
    require(std::isfinite(vl) && std::isfinite(vr) && vl > 0 && vr > 0 && gdt.mantissa() >= 0, "Invalid shock-heat geometry/conductance");
    MacShockHeatPair p; p.before_left_j = p.after_left_j = left.energy; p.before_right_j = p.after_right_j = right.energy;
    p.after_left_remainder_j=left_remainder;p.after_right_remainder_j=right_remainder;
    p.temperature_left_k = p.after_temperature_left_k = a.temperature_k; p.temperature_right_k = p.after_temperature_right_k = b.temperature_k;
    p.capacity_left_j_per_k = a.equilibrium_heat_capacity_at_cell_volume_j_per_k;
    p.capacity_right_j_per_k = b.equilibrium_heat_capacity_at_cell_volume_j_per_k; p.integrated_conductance = gdt;
    require(std::isfinite(p.capacity_left_j_per_k) && std::isfinite(p.capacity_right_j_per_k) &&
        p.capacity_left_j_per_k > 0 && p.capacity_right_j_per_k > 0, "Shock heat requires positive equilibrium capacities");
    if (gdt.is_zero() || a.temperature_k == b.temperature_k) return p;
    p.relaxation_number = sum(gdt.shared(1, p.capacity_left_j_per_k), gdt.shared(1, p.capacity_right_j_per_k));
    p.fraction = relaxation_fraction(p.relaxation_number);
    const bool forward = a.temperature_k > b.temperature_k;
    p.equilibrium_transfer = forward ? equilibrium_transfer(model, left, right, vl, vr, a, b, p.equilibrium_iterations,
        p.rejected_equilibrium_trials, p.equilibrium_temperature_left_k, p.equilibrium_temperature_right_k, p.equilibrium_temperature_bound,left_remainder,right_remainder) :
        equilibrium_transfer(model, right, left, vr, vl, b, a, p.equilibrium_iterations,
        p.rejected_equilibrium_trials, p.equilibrium_temperature_right_k, p.equilibrium_temperature_left_k, p.equilibrium_temperature_bound,right_remainder,left_remainder).negated();
    p.equilibrium_temperature_residual = sum(MacEnergy::from_double(p.equilibrium_temperature_left_k),
        MacEnergy::from_double(p.equilibrium_temperature_right_k).negated());
    p.transfer = p.equilibrium_transfer.shared(p.fraction, MacEnergy::from_double(1));
    if (p.transfer.is_zero()) precision("Shock heat lost a positive full-range transfer");
    const MacMaterialCell left_cell{left,{},left_remainder},right_cell{right,{},right_remainder};
    auto lc=left_cell,rc=right_cell;
    (void)add_mac_cell_internal_energy(lc,p.transfer.negated(),&p.primary_energy);(void)add_mac_cell_internal_energy(rc,p.transfer,&p.primary_energy);
    p.after_left_j=lc.mixture.energy;p.after_right_j=rc.mixture.energy;
    p.after_left_remainder_j=lc.energy_remainder_j;p.after_right_remainder_j=rc.energy_remainder_j;
    const auto& l=lc.mixture;const auto& r=rc.mixture;
    auto nl = std::bit_cast<std::uint64_t>(l.energy)==std::bit_cast<std::uint64_t>(left.energy) ? a : model.evaluate(l, vl);
    auto nr = std::bit_cast<std::uint64_t>(r.energy)==std::bit_cast<std::uint64_t>(right.energy) ? b : model.evaluate(r, vr);
    p.after_temperature_left_k = nl.temperature_k; p.after_temperature_right_k = nr.temperature_k;
    const auto lo = std::min(a.temperature_k, b.temperature_k), hi = std::max(a.temperature_k, b.temperature_k);
    if (!interval(nl.temperature_k, lo, hi) || !interval(nr.temperature_k, lo, hi) ||
        !interval(forward ? nl.temperature_k : nr.temperature_k, forward ? nr.temperature_k : nl.temperature_k, hi))
        precision("Shock heat exceeded its monotone thermal interval");
    MacEnergyAccount balance; account_mac_cell_internal_energy(balance,lc);account_mac_cell_internal_energy(balance,rc);account_mac_cell_internal_energy(balance,left_cell,-1);account_mac_cell_internal_energy(balance,right_cell,-1);
    if (!balance.within_bound()) precision("Shock-heat actual U account exceeds its operand bound");
    p.energy_residual = balance.residual(); p.energy_bound = balance.bound();
    p.unresolved_updates = static_cast<std::size_t>(!mac_cell_internal_energy_resolved(left_cell,lc,p.transfer.negated())) +
        static_cast<std::size_t>(!mac_cell_internal_energy_resolved(right_cell,rc,p.transfer));
    const std::array<EntropyCell, 2> entropy_cells{{{&left, &l, vl,left_remainder,lc.energy_remainder_j}, {&right, &r, vr,right_remainder,rc.energy_remainder_j}}};
    entropy_check(model, entropy_cells, {a, b}, {nl, nr},
        p.entropy_change_j_per_k, p.entropy_roundoff_j_per_k);
    a=std::move(nl);b=std::move(nr);
    return p;
}
}
MacShockHeatPair relax_mac_shock_heat_pair(const FluidThermodynamics& model,const MixtureInventory& left,
    const MixtureInventory& right,double vl,double vr,MacEnergy gdt,MacStoredRemainder left_remainder,MacStoredRemainder right_remainder) {
    require_mac_energy_environment();
    require(canonical_mac_primary(left.energy,left_remainder) && canonical_mac_primary(right.energy,right_remainder),
        "Noncanonical shock-heat primary-U input");
    require(std::isfinite(vl) && std::isfinite(vr) && vl>0 && vr>0 && gdt.mantissa()>=0,"Invalid shock-heat geometry/conductance");
    auto a=model.evaluate(left,vl),b=model.evaluate(right,vr);
    return relax_pair(model,left,right,vl,vr,gdt,a,b,left_remainder,right_remainder);
}
MacShockHeatAdvance advance_mac_shock_heat(const SpeciesRegistry& registry, const FluidThermodynamics& model,
    const MacTransportState& source, double dt, MacShockHeatLaw law) {
    require_mac_energy_environment(); require(std::isfinite(dt) && dt > 0 && dt <= 1 && .5 * dt > 0, "Invalid shock-heat timestep");
    require(law == MacShockHeatLaw::closing_velocity || law == MacShockHeatLaw::pressure_sensed_viscosity ||
        law == MacShockHeatLaw::pressure_sensed_series || law == MacShockHeatLaw::monotone_pressure_sensed, "Invalid shock-heat law");
    const bool monotone = law == MacShockHeatLaw::monotone_pressure_sensed;
    const bool sensed = law != MacShockHeatLaw::closing_velocity;
    const auto inertia = mac_face_inertias(registry, source, model); const auto& g = source.grid;
    const auto jx = mac_field_values(source.momentum_x, source.full_momentum_x), jy = mac_field_values(source.momentum_y, source.full_momentum_y);
    require(jx.size() == inertia.x.size() && jy.size() == inertia.y.size(), "Shock-heat momentum shape differs");
    MacShockHeatAdvance result{source, {}}; std::vector<EntropyReading> initial;
    // Only U changes in this stage. A successful pair publishes its validated
    // EOS endpoints with the same U update; masses, volume and model stay fixed.
    // The cache is local to this call and cannot survive a material/boundary edit.
    std::vector<ReferenceEquilibriumState> current;
    std::vector<std::size_t> current_index(source.cells.size(),none);
    result.report.law = law;
    if (sensed) {
        result.report.compression_coefficients = freeze_mac_compression_coefficients(registry, model, source,
            monotone ? MacCompressionLaw::monotone_pressure_sensed_acoustic : MacCompressionLaw::pressure_sensed_acoustic);
        result.report.frozen_conductance_capacity_j_per_k.resize(source.cells.size());
    }
    result.report.frozen_capacity_j_per_k.resize(source.cells.size());
    for (std::size_t i = 0; i < source.cells.size(); ++i) if (!g.walls[i] && source.cells[i].mixture.mass() > 0) {
        current_index[i]=current.size();current.push_back(model.evaluate(source.cells[i].mixture,mac_material_volume(source,i)));
        initial.emplace_back(current.back());
        result.report.frozen_capacity_j_per_k[i] = current.back().equilibrium_heat_capacity_at_cell_volume_j_per_k;
        if (sensed) {
            const auto& e = current.back(); MacEnergySum cp; cp.add(e.intrinsic_heat_capacity_j_per_k);
            if (e.has_gas) cp.add(MacEnergy::from_double(e.gas_pressure_pa).multiplied(e.gas_volume_m3).shared(1, e.temperature_k));
            result.report.frozen_conductance_capacity_j_per_k[i] = cp.value();
        }
    }
    std::vector<Link> links;
    for (std::size_t axis = 0; axis < 2; ++axis) {
        const auto& j = axis == 0 ? jx : jy; const auto& m = axis == 0 ? inertia.x : inertia.y;
        const auto width = axis == 0 ? g.x_width() : g.width, height = axis == 0 ? g.height : g.y_height();
        std::vector<MacScaled> velocity(j.size()), closing(source.cells.size()), diffusion(source.cells.size());
        for (std::size_t f = 0; f < j.size(); ++f) {
            static_cast<void>(MacEnergy::kinetic(m[f], j[f]).project());
            if (m[f] > 0) velocity[f] = j[f].shared(1, m[f]);
        }
        for (std::size_t i = 0; i < source.cells.size(); ++i) if (!g.walls[i] && source.cells[i].mixture.mass() > 0) {
            const auto x = i % g.width, y = i / g.width;
            const auto l = axis == 0 ? g.x_face(x, y) : g.y_face(x, y);
            const auto r = axis == 0 ? g.x_face((x + 1) % g.x_width(), y) : g.y_face(x, (y + 1) % g.y_height());
            const auto jump = mac_scaled_difference(velocity[l], velocity[r]); if (jump.mantissa() > 0) closing[i] = jump;
            diffusion[i] = closing[i];
            if (monotone && !closing[i].is_zero()) {
                // Matched to q = rho v ((1 - phi^2) v + (1 - phi) a).
                const double phi = result.report.compression_coefficients.monotone_limiter(axis, i);
                diffusion[i] = phi >= 1 ? MacScaled{} : phi == 0 ? sum(closing[i], result.report.compression_coefficients.linear_speed(axis, i)) :
                    sum(closing[i].multiplied(1 - phi * phi), result.report.compression_coefficients.linear_speed(axis, i).multiplied(1 - phi));
            } else if (sensed && !closing[i].is_zero())
                diffusion[i] = sum(closing[i], result.report.compression_coefficients.linear_speed(axis, i));
        }
        for (std::size_t y = 0; y < height; ++y) for (std::size_t x = 0; x < width; ++x) {
            const auto f = axis == 0 ? g.x_face(x, y) : g.y_face(x, y);
            auto l = none, r = none;
            if (axis == 0) {
                if (x > 0 || g.periodic_x) l = (x + g.width - 1) % g.width + y * g.width;
                if (x < g.width) r = x + y * g.width;
            } else {
                if (y > 0 || g.periodic_y) l = x + ((y + g.height - 1) % g.height) * g.width;
                if (y < g.height) r = x + y * g.width;
            }
            if (l != none && g.walls[l]) l = none;
            if (r != none && g.walls[r]) r = none;
            if (l == none || r == none) { require(j[f].is_zero(), "Fixed shock-heat face has normal momentum"); continue; }
            if (current_index[l] == none || current_index[r] == none) continue;
            if (l == r) continue;
            // A shock's numerical heat flux joins cells of one phase class
            // (gas, liquid or gas/liquid): it never crosses an interface
            // between them (MAC_HEAT.md, "Phase interfaces").
            const auto phase = [&](std::size_t i) {
                const auto& s = current[current_index[i]];
                return s.has_gas ? (s.liquid_volume_m3 > 0 ? 2 : 0) : 1;
            };
            if (phase(l) != phase(r)) continue;
            const auto w = mac_scaled_difference(closing[l], closing[r]).mantissa() >= 0 ? closing[l] : closing[r];
            auto speed = mac_scaled_difference(diffusion[l], diffusion[r]).mantissa() >= 0 ? diffusion[l] : diffusion[r];
            if (speed.is_zero()) continue;
            auto capacity = harmonic_capacity(result.report.frozen_capacity_j_per_k[l], result.report.frozen_capacity_j_per_k[r]);
            if (sensed) {
                const auto a = result.report.frozen_conductance_capacity_j_per_k[l], b = result.report.frozen_conductance_capacity_j_per_k[r];
                const auto denominator = sum(a, b);
                capacity = a.shared(b, denominator).multiplied(2);
            }
            MacEnergy integrated;
            if (law == MacShockHeatLaw::pressure_sensed_series) {
                if (diffusion[l].is_zero() || diffusion[r].is_zero()) continue;
                const auto gl = result.report.frozen_conductance_capacity_j_per_k[l]
                    .shared(diffusion[l], MacEnergy::from_double(1)).shared(1, g.spacing_m);
                const auto gr = result.report.frozen_conductance_capacity_j_per_k[r]
                    .shared(diffusion[r], MacEnergy::from_double(1)).shared(1, g.spacing_m);
                // Continuity of flux through two half cells:
                // 1/Gface = 1/(2*Gl) + 1/(2*Gr). Stay scaled throughout.
                const auto conductance = gl.shared(gr, sum(gl, gr)).multiplied(2);
                integrated = conductance.multiplied(.5 * dt);
                speed = conductance.multiplied(g.spacing_m).shared(MacEnergy::from_double(1), capacity);
            } else integrated = speed.multiplied(.5 * dt).shared(1, g.spacing_m).shared(capacity, MacEnergy::from_double(1));
            links.push_back({axis, f, l, r, w, integrated, speed});
        }
    }
    MacEnergySum transferred;
    for (std::size_t sweep = 0; sweep < 2; ++sweep) for (std::size_t n = 0; n < links.size(); ++n) {
        const auto& link = links[sweep == 0 ? n : links.size() - 1 - n];
        auto& left = result.state.cells[link.left].mixture; auto& right = result.state.cells[link.right].mixture;
        auto pair = relax_pair(model,left,right,mac_material_volume(source,link.left),mac_material_volume(source,link.right),link.integrated,
            current[current_index[link.left]],current[current_index[link.right]],result.state.cells[link.left].energy_remainder_j,result.state.cells[link.right].energy_remainder_j);
        result.report.reused_pair_eos_readings+=2;
        left.energy = pair.after_left_j; right.energy = pair.after_right_j;
        result.state.cells[link.left].energy_remainder_j=pair.after_left_remainder_j;result.state.cells[link.right].energy_remainder_j=pair.after_right_remainder_j;
        result.report.primary_energy.merge(pair.primary_energy);
        transferred.add(pair.transfer.magnitude()); result.report.unresolved_updates += pair.unresolved_updates;
        result.report.events.push_back({link.axis, link.face, link.left, link.right, sweep, link.closing, std::move(pair), link.diffusion});
    }
    MacEnergyAccount balance; std::vector<EntropyReading> final; std::vector<EntropyCell> entropy_cells;
    for (std::size_t i = 0; i < source.cells.size(); ++i) if (!g.walls[i] && source.cells[i].mixture.mass() > 0) {
        account_mac_cell_internal_energy(balance,result.state.cells[i]); account_mac_cell_internal_energy(balance,source.cells[i],-1);
        final.emplace_back(current[current_index[i]]);++result.report.reused_endpoint_eos_readings;
        entropy_cells.push_back({&source.cells[i].mixture, &result.state.cells[i].mixture, mac_material_volume(source, i),source.cells[i].energy_remainder_j,result.state.cells[i].energy_remainder_j});
    }
    if (!balance.within_bound()) precision("Shock-heat global actual U account exceeds its operand bound");
    result.report.absolute_transfer = transferred.value(); result.report.energy_residual = balance.residual(); result.report.energy_bound = balance.bound();
    entropy_check(model, entropy_cells, initial, final, result.report.entropy_change_j_per_k, result.report.entropy_roundoff_j_per_k);
    return result;
}
}
