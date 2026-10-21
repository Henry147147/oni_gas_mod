#include "mixed_atmospheres/mac_phase_change.hpp"
#include "mac_kernel_detail.hpp"
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
    const MacTransportState& source, std::span<const MacSaturationPair> pairs, bool skip_sharp_interfaces, double fraction) {
    require_mac_energy_environment(); model.validate_registry(registry);
    require(std::isfinite(fraction) && fraction > 0 && fraction <= 1, "Invalid MAC phase relaxation fraction");
    MacPhaseChangeAdvance result{source, {}}; auto& total_report = result.report;
    const auto& g = source.grid; MacEnergySum total_entropy, total_allowance;
    // A filled gas-only cell's temperature and moles in closed form (ideal
    // gases), computed once: a cell holding only a pair's vapour can only
    // condense, which it cannot do above the critical temperature or below
    // saturation, so such cells skip the complete EOS (a 1% margin keeps every
    // borderline cell on the exact path).
    const auto& materials = model.reference().materials();
    std::vector<std::size_t> profiles(registry.size());
    for (std::size_t k = 0; k < registry.size(); ++k) profiles[k] = profile_of(model, registry, k);
    std::vector<double> gas_temperature(source.cells.size(), -1), gas_moles(source.cells.size(), 0);
    std::vector<std::uint8_t> changed(source.cells.size(), 0); // by an earlier pair this call
    // A filled gas-free liquid cell: an upper bound on its temperature (no
    // elastic energy) and a lower bound on its pressure (the softest liquid's
    // compression of the whole zero-pressure volume). It boils only beyond the
    // nucleation superheat at its own pressure, so a cell whose pressure bound
    // clearly exceeds saturation there skips the complete EOS.
    const auto liquid_bounds = [&](std::size_t i, double volume, double& t, double& p) {
        const auto& m = source.cells[i].mixture; double offset = 0, capacity = 0, zero_volume = 0, bulk = std::numeric_limits<double>::infinity();
        for (std::size_t k = 0; k < registry.size(); ++k) {
            const double mass = m.masses[k]; if (mass == 0) continue;
            if (registry[k].phase != Phase::liquid) return false;
            const auto& material = materials[profiles[k]];
            offset += mass * material.energy_offset_j_per_kg; capacity += mass * material.cv_j_per_kg_k;
            zero_volume += mass / material.zero_pressure_density_kg_per_m3; bulk = std::min(bulk, material.reference_bulk_modulus_pa);
        }
        t = capacity > 0 ? (m.energy - offset) / capacity : 0;
        p = bulk * (zero_volume / volume - 1);
        return std::isfinite(t) && t > 0 && std::isfinite(p);
    };
    const auto gas_only = [&](std::size_t i) {
        if (gas_temperature[i] != -1) return gas_temperature[i] > 0;
        const auto& m = source.cells[i].mixture; double offset = 0, capacity = 0, moles = 0;
        for (std::size_t k = 0; k < registry.size(); ++k) {
            const double mass = m.masses[k]; if (mass == 0) continue;
            if (registry[k].phase != Phase::gas) { gas_temperature[i] = 0; return false; }
            const auto& material = materials[profiles[k]];
            offset += mass * material.energy_offset_j_per_kg; capacity += mass * material.cv_j_per_kg_k; moles += mass / material.molar_mass_kg_per_mol;
        }
        const double t = capacity > 0 ? (m.energy - offset) / capacity : 0;
        gas_temperature[i] = std::isfinite(t) && t > 0 ? t : 0; gas_moles[i] = moles;
        return gas_temperature[i] > 0;
    };
    struct PreparedPair { MacSaturationPair pair; std::size_t liquid, vapour; };
    std::vector<PreparedPair> prepared; prepared.reserve(pairs.size());
    for (const auto& pair : pairs) {
        require(pair.liquid < registry.size() && pair.vapour < registry.size() && registry[pair.liquid].phase == Phase::liquid &&
            registry[pair.vapour].phase == Phase::gas, "Invalid MAC saturation pair");
        prepared.push_back({pair, profile_of(model, registry, pair.liquid), profile_of(model, registry, pair.vapour)});
    }
    struct Partial { MacPhaseChangeReport report; MacEnergySum entropy, allowance; };
    constexpr std::size_t block_size = 1024;
    const auto blocks = (source.cells.size() + block_size - 1) / block_size;
    std::vector<Partial> partials(blocks);
    const auto advance_cell = [&](std::size_t i, Partial& part) {
        if (g.walls[i]) return;
        auto& cell = result.state.cells[i].mixture;
        auto& report = part.report; auto& entropy = part.entropy; auto& allowance = part.allowance;
        for (const auto& entry : prepared) {
            const auto& pair = entry.pair;
            const auto liquid_profile = entry.liquid, vapour_profile = entry.vapour;
            const double ml = cell.masses[pair.liquid], mv = cell.masses[pair.vapour];
            if (ml == 0 && mv == 0) continue;
            // Void phase (MAC_VACUUM.md, "Phase change"): a partly filled cell
            // holding the pair's liquid relaxes at its grid volume, its gas first
            // expanding into its own void, so vapour fills the void at saturation;
            // the cell is then full. Entropy is measured from its occupied volume.
            const double held = mac_material_volume(result.state, i), grid_volume = g.volume_m3();
            const bool fill = held < grid_volume && ml > 0;
            const double volume = fill ? grid_volume : held;
            if (ml > 0 && mv == 0 && !changed[i] && !(held < grid_volume)) {
                double t = 0, p = 0;
                if (liquid_bounds(i, volume, t, p) && t < pair.critical_temperature_k * (1 - 1e-6) && p > 0) {
                    double saturation = std::numeric_limits<double>::infinity();
                    try { saturation = mac_saturation_pressure(model, liquid_profile, vapour_profile, t + 5, p); } catch (const std::exception&) {}
                    if (p > 1.01 * saturation) continue;
                }
            }
            if (ml == 0 && !changed[i] && gas_only(i)) {
                const double t = gas_temperature[i];
                if (t >= pair.critical_temperature_k * (1 + 1e-6)) continue;
                const double vapour = mv / materials[vapour_profile].molar_mass_kg_per_mol * gas_constant * t / volume;
                const double pressure = gas_moles[i] * gas_constant * t / volume;
                double saturation = 0;
                try { saturation = mac_saturation_pressure(model, liquid_profile, vapour_profile, t, pressure); } catch (const std::exception&) { saturation = 0; }
                if (vapour < .99 * saturation) continue;
            }
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
            // At or above the critical temperature the pair has no liquid:
            // none condenses and liquid evaporates.
            const bool supercritical = before.temperature_k >= pair.critical_temperature_k;
            if (supercritical && ml == 0) continue;
            bool evaporate = true; double near = 0;
            if (supercritical) near = ml;
            else {
                const auto start = departure(0); if (!start || *start == 0) continue;
                // A partial step (an owner's relaxation toward saturation) leaves
                // a cell within 0.1% of saturation as it is: it is there for any
                // purpose, and the root search would only move traces.
                if (fraction < 1) {
                    double saturation = 0;
                    try { saturation = mac_saturation_pressure(model, liquid_profile, vapour_profile, before.temperature_k, before.mechanical_pressure_pa); }
                    catch (const std::exception&) { saturation = 0; }
                    if (std::abs(*start) <= 1e-3 * saturation) continue;
                }
                // Evaporate toward a positive departure, condense toward a negative one.
                evaporate = *start < 0;
                if ((evaporate && ml == 0) || (!evaporate && mv == 0)) continue;
                if (evaporate && !interface && !(mac_saturation_pressure(model, liquid_profile, vapour_profile,
                        before.temperature_k - mac_nucleation_superheat_k, before.mechanical_pressure_pa) > before.mechanical_pressure_pa)) continue;
                double far = evaporate ? ml : -mv;
                // An unevaluable or overshooting end is beyond the root.
                const auto end = departure(far);
                if (end && (evaporate ? *end <= 0 : *end >= 0)) near = far;
                else for (int k = 0; k < 64; ++k) {
                    const double mid = .5 * (near + far); if (mid == near || mid == far) break;
                    // A partial step needs the root only to a thousandth.
                    if (fraction < 1 && std::abs(far - near) <= 1e-3 * std::max(std::abs(near), std::abs(far)) && near != 0) break;
                    const auto value = departure(mid);
                    if (value && (evaporate ? *value < 0 : *value > 0)) near = mid; else far = mid;
                }
            }
            if (fraction < 1) near *= fraction;
            if (near == 0) continue;
            cell.masses[pair.liquid] = ml - near; cell.masses[pair.vapour] = mv + near; changed[i] = 1;
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
    };
    // Pair order within a cell is unchanged. Fixed cell blocks own disjoint
    // primary writes; their report/entropy reductions have a fixed order too.
    const auto options = mac_kernel_default_options();
    const auto threads = static_cast<unsigned>(std::min<std::size_t>(options.threads,
        std::max<std::size_t>(1, blocks / options.minimum_blocks_per_thread)));
    mac_kernel::pool_for(threads).run(blocks, [&](std::size_t block) {
        for (std::size_t i = block * block_size; i < std::min(source.cells.size(), (block + 1) * block_size); ++i)
            advance_cell(i, partials[block]);
    });
    for (const auto& part : partials) {
        total_report.eos_evaluations += part.report.eos_evaluations; total_report.unevaluable_cells += part.report.unevaluable_cells;
        total_report.evaporating_cells += part.report.evaporating_cells; total_report.condensing_cells += part.report.condensing_cells;
        total_report.evaporated_kg += part.report.evaporated_kg; total_report.condensed_kg += part.report.condensed_kg;
        for (std::size_t k = 0; k < part.entropy.size(); ++k) total_entropy.add(part.entropy.component(k));
        for (std::size_t k = 0; k < part.allowance.size(); ++k) total_allowance.add(part.allowance.component(k));
    }
    // A void-phase state whose cells are all filled keeps no occupied volumes.
    auto& occupied = result.state.occupied_volumes_m3; bool partial = false;
    for (std::size_t i = 0; i < occupied.size(); ++i) partial = partial || (result.state.cells[i].mixture.mass() > 0 && occupied[i] < g.volume_m3());
    if (!partial) occupied.clear();
    MacEnergySum admitted; admitted.add(total_entropy.value()); admitted.add(total_allowance.value());
    if (admitted.sign() < 0) throw std::domain_error("MAC phase change decreased entropy beyond its roundoff allowance");
    total_report.entropy_change_j_per_k = total_entropy.value().project().value; total_report.entropy_allowance_j_per_k = total_allowance.value().project().value;
    return result;
}
}
