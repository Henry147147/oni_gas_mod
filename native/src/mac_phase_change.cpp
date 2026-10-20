#include "mixed_atmospheres/mac_phase_change.hpp"
#include "mixed_atmospheres/mac_energy.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>

namespace ma::experimental {
namespace {
void require(bool ok, const char* why) { if (!ok) throw std::invalid_argument(why); }
std::size_t profile_of(const FluidThermodynamics& model, const SpeciesRegistry& registry, std::size_t species) {
    const auto& materials = model.reference().materials();
    for (std::size_t k = 0; k < materials.size(); ++k) if (materials[k].id == registry[species].id) return k;
    throw std::invalid_argument("Saturation pair constituent has no EOS material");
}
// Specific Gibbs energy of the liquid at (T, P), entropy offset included.
double liquid_gibbs(const ReferenceMaterial& m, double t, double specific_volume, double elastic, double p) {
    return m.energy_offset_j_per_kg + m.cv_j_per_kg_k * t + elastic + p * specific_volume -
        t * (m.cv_j_per_kg_k * std::log(t / entropy_reference_temperature_k) + m.entropy_offset_j_per_kg_k);
}
// Vapour Gibbs energy at T without its partial-pressure term (R/M) T ln(R T / p).
double vapour_gibbs_base(const ReferenceMaterial& m, double t) {
    const double r = reference_gas_constant / m.molar_mass_kg_per_mol;
    return m.energy_offset_j_per_kg + m.cv_j_per_kg_k * t + r * t -
        t * (m.cv_j_per_kg_k * std::log(t / entropy_reference_temperature_k) + m.entropy_offset_j_per_kg_k);
}
}

double mac_saturation_entropy_offset(const ReferenceMaterial& liquid, const ReferenceMaterial& vapour, double t, double p) {
    require(liquid.phase == ReferencePhase::elastic_liquid && vapour.phase == ReferencePhase::ideal_gas &&
        liquid.molar_mass_kg_per_mol == vapour.molar_mass_kg_per_mol && std::isfinite(t) && t > 0 && std::isfinite(p) && p > 0,
        "Invalid saturation calibration pair or point");
    const double q = p / liquid.reference_bulk_modulus_pa, scale = liquid.reference_bulk_modulus_pa / liquid.zero_pressure_density_kg_per_m3;
    const double volume = 1 / liquid.zero_pressure_density_kg_per_m3 / (1 + q), elastic = scale * (std::log1p(q) - q / (1 + q));
    auto uncalibrated = vapour; uncalibrated.entropy_offset_j_per_kg_k = 0;
    const double r = reference_gas_constant / vapour.molar_mass_kg_per_mol;
    // g_v(T, p) - T s0 = g_l(T, p)
    const double gv = vapour_gibbs_base(uncalibrated, t) - r * t * std::log(reference_gas_constant * t / p);
    return (gv - liquid_gibbs(liquid, t, volume, elastic, p)) / t;
}
double mac_saturation_pressure(const FluidThermodynamics& model, std::size_t liquid, std::size_t vapour, double t, double liquid_pressure) {
    const auto& materials = model.reference().materials();
    require(liquid < materials.size() && vapour < materials.size() && materials[liquid].phase == ReferencePhase::elastic_liquid &&
        materials[vapour].phase == ReferencePhase::ideal_gas && std::isfinite(t) && t > 0 && std::isfinite(liquid_pressure) && liquid_pressure >= 0,
        "Invalid saturation pressure query");
    const auto response = model.reference().liquid_response(liquid, liquid_pressure);
    const double gl = liquid_gibbs(materials[liquid], t, response.specific_volume_m3_per_kg, response.specific_elastic_energy_j_per_kg, liquid_pressure);
    const double r = reference_gas_constant / materials[vapour].molar_mass_kg_per_mol;
    // (R/M) T ln(R T / p) = g_v,base - g_l
    return reference_gas_constant * t * std::exp(-(vapour_gibbs_base(materials[vapour], t) - gl) / (r * t));
}
double mac_latent_heat(const FluidThermodynamics& model, std::size_t liquid, std::size_t vapour, double t, double liquid_pressure) {
    const auto& materials = model.reference().materials();
    require(liquid < materials.size() && vapour < materials.size() && materials[liquid].phase == ReferencePhase::elastic_liquid &&
        materials[vapour].phase == ReferencePhase::ideal_gas && std::isfinite(t) && t > 0 && std::isfinite(liquid_pressure) && liquid_pressure >= 0,
        "Invalid latent heat query");
    const auto response = model.reference().liquid_response(liquid, liquid_pressure);
    const auto& l = materials[liquid]; const auto& v = materials[vapour];
    const double hv = v.energy_offset_j_per_kg + v.cv_j_per_kg_k * t + reference_gas_constant / v.molar_mass_kg_per_mol * t;
    const double hl = l.energy_offset_j_per_kg + l.cv_j_per_kg_k * t + response.specific_elastic_energy_j_per_kg +
        liquid_pressure * response.specific_volume_m3_per_kg;
    return hv - hl;
}
double mac_saturation_temperature(const FluidThermodynamics& model, std::size_t liquid, std::size_t vapour,
    double vapour_pressure, double liquid_pressure, double guess) {
    require(std::isfinite(vapour_pressure) && vapour_pressure > 0 && std::isfinite(guess) && guess > 0, "Invalid saturation temperature query");
    const double r = reference_gas_constant / model.reference().materials()[vapour].molar_mass_kg_per_mol;
    // d ln p_sat / dT = (h_v - h_l) / (r T^2) (Clausius-Clapeyron; the ln(R T)
    // term cancels the vapour base's r T enthalpy). The
    // Gibbs difference behind p_sat cancels to about 1e-11 of ln p_sat, so the
    // root resolves T to 1e-10 of itself, not to binary64.
    double t = guess;
    for (int it = 0; it < 64; ++it) {
        const double f = std::log(mac_saturation_pressure(model, liquid, vapour, t, liquid_pressure) / vapour_pressure);
        const double slope = mac_latent_heat(model, liquid, vapour, t, liquid_pressure) / (r * t * t);
        if (!std::isfinite(f) || !std::isfinite(slope) || !(slope > 0)) break;
        const double next = std::clamp(t - f / slope, .5 * t, 2 * t);
        if (std::abs(next - t) <= 1e-10 * t) return next;
        t = next;
    }
    throw std::domain_error("Saturation temperature did not converge");
}
std::vector<MacSaturationPair> mac_saturation_pairs(const SpeciesRegistry& registry, const FluidThermodynamics& model) {
    model.validate_registry(registry); std::vector<MacSaturationPair> pairs;
    const auto& materials = model.reference().materials();
    for (std::size_t l = 0; l < materials.size(); ++l) if (materials[l].phase == ReferencePhase::elastic_liquid)
        for (std::size_t v = 0; v < materials.size(); ++v) if (materials[v].phase == ReferencePhase::ideal_gas && materials[v].entropy_offset_j_per_kg_k != 0) {
            const auto& a = registry[registry.index(materials[l].id)]; const auto& b = registry[registry.index(materials[v].id)];
            if (a.atoms == b.atoms && a.molar_mass == b.molar_mass) pairs.push_back({registry.index(a.id), registry.index(b.id)});
        }
    return pairs;
}

MacPhaseChangeAdvance advance_mac_phase_change(const SpeciesRegistry& registry, const FluidThermodynamics& model,
    const MacTransportState& source, std::span<const MacSaturationPair> pairs, bool skip_sharp_interfaces) {
    require_mac_energy_environment(); model.validate_registry(registry);
    MacPhaseChangeAdvance result{source, {}}; auto& report = result.report;
    const auto& g = source.grid; MacEnergySum entropy, allowance;
    for (const auto& pair : pairs) {
        require(pair.liquid < registry.size() && pair.vapour < registry.size() && registry[pair.liquid].phase == Phase::liquid &&
            registry[pair.vapour].phase == Phase::gas, "Invalid MAC saturation pair");
        const auto liquid_profile = profile_of(model, registry, pair.liquid), vapour_profile = profile_of(model, registry, pair.vapour);
        for (std::size_t i = 0; i < source.cells.size(); ++i) {
            if (g.walls[i]) continue;
            auto& cell = result.state.cells[i].mixture;
            const double ml = cell.masses[pair.liquid], mv = cell.masses[pair.vapour];
            if (ml == 0 && mv == 0) continue;
            // Void phase (MAC_VACUUM.md, "Phase change"): a partly filled cell
            // holding the pair's liquid relaxes at its grid volume, its gas first
            // expanding into its own void, so vapour fills the void at saturation;
            // the cell is then full. Entropy is measured from its occupied volume.
            const double held = mac_material_volume(result.state, i), grid_volume = g.volume_m3();
            const bool fill = held < grid_volume && ml > 0;
            const double volume = fill ? grid_volume : held;
            // A cell the strict EOS cannot read (traces below binary64 range,
            // which the production kernel admits) has no interface to relax.
            ReferenceEquilibriumState before, source_state;
            try { before = model.evaluate(cell, volume); source_state = fill ? model.evaluate(cell, held) : before; }
            catch (const ReferenceError&) { ++report.unevaluable_cells; continue; }
            ++report.eos_evaluations;
            if (skip_sharp_interfaces && ml > 0 && before.has_gas && before.gas_volume_m3 >= mac_interface_phase_fraction * volume &&
                before.liquid_volume_m3 >= mac_interface_phase_fraction * volume &&
                before.partial_volumes_m3[vapour_profile] >= mac_interface_vapour_fraction * before.gas_volume_m3) {
                bool only = true;
                for (std::size_t k = 0; k < registry.size(); ++k)
                    if (k != pair.liquid && registry[k].phase == Phase::liquid && cell.masses[k] != 0) only = false;
                if (only) continue;
            }
            // Departure from equilibrium after moving `moved` kg from liquid to
            // vapour: vapour partial pressure minus saturation pressure. A cell
            // without gas forms vapour at its own pressure.
            const auto departure = [&](double moved) -> std::optional<double> {
                auto trial = cell; trial.masses[pair.liquid] = ml - moved; trial.masses[pair.vapour] = mv + moved;
                if (trial.masses[pair.liquid] < 0 || trial.masses[pair.vapour] < 0) return std::nullopt;
                try {
                    const auto e = moved == 0 ? before : model.evaluate(trial, volume); ++report.eos_evaluations;
                    // Dalton partial pressure; vapour formed in a gas-free cell starts at the cell pressure.
                    const double vapour_pressure = e.has_gas && e.gas_volume_m3 > 0 ?
                        e.gas_pressure_pa * e.partial_volumes_m3[vapour_profile] / e.gas_volume_m3 : e.mechanical_pressure_pa;
                    return vapour_pressure - mac_saturation_pressure(model, liquid_profile, vapour_profile, e.temperature_k, e.mechanical_pressure_pa);
                } catch (const ReferenceError&) { return std::nullopt; }
            };
            // Liquid evaporates at an interface (a gas phase of at least a
            // millionth of the cell; less is a trace), or beyond the nucleation
            // superheat at its own pressure, so pressure ripples at saturation
            // cannot flash micro-bubbles. Vapour condenses wherever it is.
            const bool interface = before.has_gas && before.gas_volume_m3 > 1e-6 * volume;
            const auto start = departure(0); if (!start || *start == 0) continue;
            // Evaporate toward a positive departure, condense toward a negative one.
            const bool evaporate = *start < 0;
            if ((evaporate && ml == 0) || (!evaporate && mv == 0)) continue;
            if (evaporate && !interface && !(mac_saturation_pressure(model, liquid_profile, vapour_profile,
                    before.temperature_k - mac_nucleation_superheat_k, before.mechanical_pressure_pa) > before.mechanical_pressure_pa)) continue;
            double near = 0, far = evaporate ? ml : -mv;
            // An unevaluable or overshooting end is beyond the root.
            const auto end = departure(far);
            if (end && (evaporate ? *end <= 0 : *end >= 0)) near = far;
            else for (int k = 0; k < 64; ++k) {
                const double mid = .5 * (near + far); if (mid == near || mid == far) break;
                const auto value = departure(mid);
                if (value && (evaporate ? *value < 0 : *value > 0)) near = mid; else far = mid;
            }
            if (near == 0) continue;
            cell.masses[pair.liquid] = ml - near; cell.masses[pair.vapour] = mv + near;
            if (near == ml) cell.masses[pair.liquid] = 0;
            if (near == -mv) cell.masses[pair.vapour] = 0;
            const auto after = model.evaluate(cell, volume); ++report.eos_evaluations;
            if (fill) result.state.occupied_volumes_m3[i] = grid_volume;
            entropy.add(MacEnergy::from_double(after.entropy_j_per_k)); entropy.add(MacEnergy::from_double(-source_state.entropy_j_per_k));
            account_mac_cell_entropy_remainder(entropy, result.state.cells[i], after.temperature_k);
            account_mac_cell_entropy_remainder(entropy, source.cells[i], source_state.temperature_k, -1);
            allowance.add(MacEnergy::from_double(128 * std::numeric_limits<double>::epsilon() * std::abs(cell.energy) / std::min(source_state.temperature_k, after.temperature_k)));
            (evaporate ? report.evaporating_cells : report.condensing_cells) += 1;
            (evaporate ? report.evaporated_kg : report.condensed_kg) += std::abs(near);
        }
    }
    // A void-phase state whose cells are all filled keeps no occupied volumes.
    auto& occupied = result.state.occupied_volumes_m3; bool partial = false;
    for (std::size_t i = 0; i < occupied.size(); ++i) partial = partial || (result.state.cells[i].mixture.mass() > 0 && occupied[i] < g.volume_m3());
    if (!partial) occupied.clear();
    MacEnergySum admitted; admitted.add(entropy.value()); admitted.add(allowance.value());
    if (admitted.sign() < 0) throw std::domain_error("MAC phase change decreased entropy beyond its roundoff allowance");
    report.entropy_change_j_per_k = entropy.value().project().value; report.entropy_allowance_j_per_k = allowance.value().project().value;
    return result;
}
}
