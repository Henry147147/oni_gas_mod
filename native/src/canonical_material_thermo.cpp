#include "canonical_material_internal.hpp"

namespace ma::experimental::canonical_detail {
namespace {
double log_ratio(Kernel& k, double a, double b) {
    k.positive(a, Field::operand_left); k.positive(b, Field::operand_right);
    const double delta = difference(k, a, b);
    if (std::abs(delta) < k.mul(.5, b)) return k.finite(std::log1p(k.div(delta, b)));
    return difference(k, k.finite(std::log(a)), k.finite(std::log(b)));
}
double exp_value(Kernel& k, double value) {
    k.finite(value, Field::operand_left); const double result = k.finite(std::exp(value));
    k.require(result > 0, Code::numerical_precision, "Independent exponential lost its positive value"); return result;
}
double expm1_value(Kernel& k, double value) {
    k.finite(value, Field::operand_left); const double result = k.finite(std::expm1(value));
    k.require(value == 0 || (result != 0 && std::signbit(value) == std::signbit(result)),
        Code::numerical_precision, "Independent shifted exponential lost its increment"); return result;
}
struct Caloric { double capacity = 0, gas_coefficient = 0, offset = 0; };
Caloric caloric(Kernel& k, const Data& d, std::size_t owner) {
    Sum capacity(k, 2), gas(k, 3), offset(k, 4);
    const auto& profiles = d.thermodynamics->reference().materials();
    for (std::size_t s = 0; s < profiles.size(); ++s) {
        const double mass = d.cells[owner].masses_kg[s]; const auto& profile = profiles[s];
        capacity.add(k.mul(mass, profile.cv_j_per_kg_k)); offset.add(k.mul(mass, profile.energy_offset_j_per_kg));
        if (profile.phase == ReferencePhase::ideal_gas) gas.add(k.mul(k.div(mass, profile.molar_mass_kg_per_mol), reference_gas_constant));
    }
    return {capacity.get(), gas.get(), offset.get()};
}
double cold_curve_factor(Kernel& k, double q) {
    // log(1+q)-q/(1+q). The fixed Horner polynomial avoids subtracting nearly
    // equal leading q terms; its omitted terms are below 1e-38 for q<1e-3.
    if (q < 1e-3) {
        double polynomial = k.div(11, 12);
        for (int n = 11; n >= 2; --n) {
            const double coefficient = k.div(static_cast<double>(n - 1), static_cast<double>(n));
            polynomial = sum(k, {n % 2 == 0 ? coefficient : -coefficient, k.mul(q, polynomial)});
        }
        return k.mul(k.mul(q, q), polynomial);
    }
    return difference(k, k.finite(std::log1p(q)), k.div(q, sum(k, {1, q})));
}
void cache_frame(Kernel& k, const ReferenceEquilibriumState& state) {
    const std::array<double, 18> values{state.temperature_k, state.mechanical_pressure_pa, state.gas_pressure_pa,
        state.gas_volume_m3, state.liquid_volume_m3, state.vacuum_volume_m3, state.reference_energy_j,
        state.thermal_energy_j, state.elastic_energy_j, state.entropy_j_per_k, state.helmholtz_energy_j,
        state.intrinsic_heat_capacity_j_per_k, state.equilibrium_heat_capacity_at_cell_volume_j_per_k,
        state.volume_residual_m3, state.energy_residual_j, static_cast<double>(state.pressure_iterations),
        state.has_gas ? 1.0 : 0.0, state.has_vacuum ? 1.0 : 0.0};
    k.row(Field::thermodynamic_reading, values);
    k.row(Field::partial_volume, state.partial_volumes_m3);
    k.row(Field::stored_internal_energy, state.specific_internal_energies_j_per_kg);
    for (double value : values) k.finite(value);
    for (double value : state.partial_volumes_m3) k.nonnegative(value, Field::partial_volume);
    for (double value : state.specific_internal_energies_j_per_kg) k.finite(value, Field::stored_internal_energy);
}
double independent_pressure(Kernel& k, const Data& d, std::size_t owner, double change) {
    const auto& old = d.thermodynamics_readings[owner]; const auto& profiles = d.thermodynamics->reference().materials();
    if (change == 0) return old.mechanical_pressure_pa;
    const auto c = caloric(k, d, owner);
    if (!old.has_gas) {
        std::size_t selected = profiles.size();
        for (std::size_t s = 0; s < profiles.size(); ++s) if (d.cells[owner].masses_kg[s] > 0) {
            k.require(selected == profiles.size(), Code::unsupported_configuration, "Independent gasless oracle requires one active liquid"); selected = s;
        }
        k.require(selected < profiles.size(), Code::unsupported_topology, "Independent liquid oracle has no material");
        const auto& material = profiles[selected]; const double volume = sum(k, {d.cells[owner].volume_m3, change});
        k.require(volume > 0, Code::numerical_precision, "Independent liquid oracle volume is nonpositive");
        const double nominal = k.div(d.cells[owner].masses_kg[selected], material.zero_pressure_density_kg_per_m3);
        return k.mul(material.reference_bulk_modulus_pa, k.div(difference(k, nominal, volume), volume));
    }
    const double capacity = sum(k, {c.capacity, c.gas_coefficient});
    const double alpha = k.div(c.gas_coefficient, capacity), beta = k.div(c.capacity, capacity), p0 = old.mechanical_pressure_pa;
    double low = -1, high = 1;
    for (std::size_t s = 0; s < profiles.size(); ++s) if (d.cells[owner].masses_kg[s] > 0) {
        const auto& material = profiles[s];
        low = std::max(low, k.div(log_ratio(k, material.minimum_temperature_k, old.temperature_k), alpha));
        high = std::min(high, k.div(log_ratio(k, material.maximum_temperature_k, old.temperature_k), alpha));
        if (material.phase == ReferencePhase::elastic_liquid) {
            const double maximum_pressure = k.mul(material.reference_bulk_modulus_pa, difference(k, material.maximum_density_ratio, 1));
            high = std::min(high, log_ratio(k, maximum_pressure, p0));
        }
    }
    const auto residual = [&](double ell) {
        k.oracle_tick();
        const double pressure = k.mul(p0, exp_value(k, ell)), delta_pressure = k.mul(p0, expm1_value(k, ell));
        Sum volume(k, 5); volume.add(k.mul(old.gas_volume_m3, expm1_value(k, -k.mul(beta, ell))));
        for (std::size_t s = 0; s < profiles.size(); ++s)
            if (profiles[s].phase == ReferencePhase::elastic_liquid && d.cells[owner].masses_kg[s] > 0) {
                const auto& material = profiles[s];
                const double nominal = k.div(d.cells[owner].masses_kg[s], material.zero_pressure_density_kg_per_m3);
                const double initial = k.div(nominal, sum(k, {1, k.div(p0, material.reference_bulk_modulus_pa)}));
                volume.add(-k.div(k.mul(initial, delta_pressure), sum(k, {material.reference_bulk_modulus_pa, pressure})));
            }
        volume.add(-change); return volume.get();
    };
    k.require(low <= 0 && high >= 0, Code::numerical_precision, "Independent pressure bracket excludes its anchor");
    const double left = residual(low), right = residual(high);
    k.require(left >= 0 && right <= 0, Code::numerical_precision, "Independent fixed pressure bracket cannot contain the volume change");
    for (int iteration = 0; iteration < 96; ++iteration) {
        const double middle = sum(k, {low, k.mul(.5, difference(k, high, low))});
        if (middle == low || middle == high) break;
        const double value = residual(middle);
        if (value == 0) { low = middle; high = middle; break; }
        if (value > 0) low = middle; else high = middle;
    }
    return k.mul(p0, exp_value(k, sum(k, {low, k.mul(.5, difference(k, high, low))})));
}
}

Code reference_failure_code(ReferenceErrorCode code) noexcept {
    return code == ReferenceErrorCode::numerical_range || code == ReferenceErrorCode::root_not_converged ||
        code == ReferenceErrorCode::unsupported_derivative ? Code::numerical_precision : Code::unsupported_configuration;
}
void supported(Kernel& k, const ReferenceConservedCell& cell, const ReferenceEquilibriumState& state) {
    k.row(Field::species_mass, cell.masses_kg); k.scalar(Field::stored_internal_energy, cell.internal_energy_j);
    k.scalar(Field::occupied_volume, cell.volume_m3); cache_frame(k, state);
    Sum mass(k, 1); for (double value : cell.masses_kg) mass.add(value);
    k.require(mass.get() > 0, Code::unsupported_topology, "Canonical vacuum owner has no supported pressure degree of freedom");
    k.require(!state.has_vacuum && state.vacuum_volume_m3 == 0, Code::unsupported_topology, "Canonical internal liquid void is unsupported");
    k.require(state.temperature_k > 0 && state.mechanical_pressure_pa > 0, Code::unsupported_configuration,
        "Canonical pressure step does not support the gasless fill kink or nonpositive temperature/pressure");
    k.require(state.gas_volume_m3 >= 0 && state.liquid_volume_m3 >= 0, Code::numerical_precision, "EOS phase volumes are negative");
    k.require(!state.has_gas || (state.gas_volume_m3 > 0 && state.gas_pressure_pa == state.mechanical_pressure_pa),
        Code::numerical_precision, "Actual gas lost headspace or common pressure");
    k.require(state.has_gas || (state.gas_volume_m3 == 0 && state.gas_pressure_pa == 0), Code::numerical_precision,
        "Gasless reading assigns gas pressure or headspace");
}
ReferenceEquilibriumState evaluate_owner(Kernel& k, const Data& d, const ReferenceConservedCell& cell, std::size_t owner) {
    k.scope(Stage::thermodynamics, canonical_absent_index, static_cast<std::uint32_t>(owner));
    k.row(Field::species_mass, cell.masses_kg); k.scalar(Field::stored_internal_energy, cell.internal_energy_j);
    k.scalar(Field::retained_internal_energy_low, d.primary.internal_energy_j[owner].low());
    k.require(std::bit_cast<std::uint64_t>(cell.internal_energy_j) == std::bit_cast<std::uint64_t>(d.primary.internal_energy_j[owner].high()) &&
        std::bit_cast<std::uint64_t>(cell.volume_m3) == std::bit_cast<std::uint64_t>(d.owner_geometry[owner].projected_volume_m3),
        Code::numerical_precision, "EOS input does not match the declared retained-state projection");
    k.scalar(Field::occupied_volume, cell.volume_m3); k.charge(6);
    ReferenceEquilibriumState state;
    try { const auto inventory = d.thermodynamics->from_reference(cell); state = d.thermodynamics->evaluate(inventory, cell.volume_m3); }
    catch (const ReferenceError& error) {
        k.scalar(Field::constitutive_error_code, static_cast<double>(error.code()));
        k.fail(reference_failure_code(error.code()), "Actual EOS rejected stored canonical owner m/U/V");
    }
    catch (const std::invalid_argument&) { k.fail(Code::unsupported_configuration, "Actual EOS bridge rejected canonical inventory"); }
    const auto& profiles = d.thermodynamics->reference().materials();
    k.require(state.partial_volumes_m3.size() == profiles.size() && state.specific_internal_energies_j_per_kg.size() == profiles.size(),
        Code::numerical_precision, "Actual EOS constituent reading shape differs");
    supported(k, cell, state);
    bool gas_present = false; std::size_t liquids = 0;
    for (std::size_t s = 0; s < profiles.size(); ++s) {
        k.scalar(Field::species_mass, cell.masses_kg[s], static_cast<std::uint32_t>(s));
        k.scalar(Field::partial_volume, state.partial_volumes_m3[s], static_cast<std::uint32_t>(s));
        if (cell.masses_kg[s] == 0) k.require(state.partial_volumes_m3[s] == 0, Code::numerical_precision, "Absent material has a partial volume");
        else {
            k.require(state.partial_volumes_m3[s] > 0, Code::numerical_precision, "Positive constituent lost its EOS partial volume");
            if (profiles[s].phase == ReferencePhase::ideal_gas) gas_present = true; else ++liquids;
        }
    }
    k.require(gas_present == state.has_gas && (gas_present || liquids == 1), Code::unsupported_configuration,
        "Actual phase flags differ or gasless owner has multiple active liquids");
    return state;
}
double independent_work_pressure(Kernel& k, const Data& d, std::size_t owner, double change) {
    if (change == 0) return d.thermodynamics_readings[owner].mechanical_pressure_pa;
    Sum weighted(k, 6);
    for (int panel = 0; panel <= 64; ++panel) {
        const double point = k.mul(change, k.div(static_cast<double>(panel), 64));
        const double weight = panel == 0 || panel == 64 ? 1 : panel % 2 == 0 ? 2 : 4;
        weighted.add(k.mul(weight, independent_pressure(k, d, owner, point)));
    }
    return k.div(weighted.get(), 192);
}
void check_thermodynamics(Kernel& k, const Data& old, const Data& next, std::size_t owner, CanonicalStepReport& report) {
    k.scope(Stage::thermodynamics, canonical_absent_index, static_cast<std::uint32_t>(owner));
    const auto& state = next.thermodynamics_readings[owner]; const auto& before = old.thermodynamics_readings[owner];
    const auto& cell = next.cells[owner]; const auto& profiles = next.thermodynamics->reference().materials();
    supported(k, cell, state); const auto c = caloric(k, next, owner); Sum volume(k, 5), cold(k, 6);
    if (state.has_gas) volume.add(k.div(k.mul(c.gas_coefficient, state.temperature_k), state.mechanical_pressure_pa));
    for (std::size_t s = 0; s < profiles.size(); ++s)
        if (profiles[s].phase == ReferencePhase::elastic_liquid && cell.masses_kg[s] > 0) {
            const auto& material = profiles[s]; const double q = k.div(state.mechanical_pressure_pa, material.reference_bulk_modulus_pa);
            volume.add(k.div(k.div(cell.masses_kg[s], material.zero_pressure_density_kg_per_m3), sum(k, {1, q})));
            const double factor = k.div(k.mul(cell.masses_kg[s], material.reference_bulk_modulus_pa), material.zero_pressure_density_kg_per_m3);
            cold.add(k.mul(factor, cold_curve_factor(k, q)));
        }
    const auto& stored_energy = next.primary.internal_energy_j[owner];
    const double temperature = k.div(sum(k, {stored_energy.high(), stored_energy.low(), -c.offset, -cold.get()}), c.capacity);
    const double ti = k.ratio(difference(k, temperature, state.temperature_k), std::max(std::abs(temperature), std::abs(state.temperature_k)));
    const double total_volume = volume.get();
    Sum volume_difference(k, 7); volume_difference.add(total_volume);
    const auto& geometry = next.owner_geometry[owner];
    for (std::size_t i = 0; i < geometry.volume_term_count; ++i) volume_difference.add(-geometry.volume_terms_m3[i]);
    const double vi = k.ratio(volume_difference.get(), std::max(std::abs(total_volume), std::abs(cell.volume_m3)));
    k.scalar(Field::temperature, temperature); k.scalar(Field::occupied_volume, total_volume);
    k.require(ti <= 2e-10 && vi <= 2e-10, Code::numerical_precision, "Independent stored-owner temperature/volume closure failed");
    report.maximum_thermodynamic_ratio = std::max({report.maximum_thermodynamic_ratio, ti, vi});
    if (state.has_gas) {
        const double independent = k.div(k.mul(state.mechanical_pressure_pa, state.gas_volume_m3), c.gas_coefficient);
        const double ratio = k.ratio(difference(k, independent, state.temperature_k), std::max(std::abs(independent), std::abs(state.temperature_k)));
        k.require(ratio <= 2e-10, Code::numerical_precision, "Independent common gas pressure failed");
        report.maximum_thermodynamic_ratio = std::max(report.maximum_thermodynamic_ratio, ratio);
    }
    const double temperature_part = k.mul(c.capacity, log_ratio(k, state.temperature_k, before.temperature_k));
    const double gas_part = state.has_gas ? k.mul(c.gas_coefficient, log_ratio(k, state.gas_volume_m3, before.gas_volume_m3)) : 0;
    const double change = sum(k, {temperature_part, gas_part});
    const double capacity = sum(k, {c.capacity, c.gas_coefficient});
    report.entropy_change.push_back(change); k.scalar(Field::entropy_change, change);
    k.within(change, capacity, 2e-11, "Actual stored homogeneous entropy drift exceeded its physical bound");
    const double cache_change = difference(k, state.entropy_j_per_k, before.entropy_j_per_k);
    const double cache_difference = difference(k, cache_change, change);
    report.entropy_cache_difference.push_back(cache_difference); k.scalar(Field::entropy_cache_difference, cache_difference);
    k.within(cache_difference, capacity, 64 * epsilon, "Actual entropy cache does not describe stored m/U/V");
}
}
