#include "liquid_reference.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using namespace ma::experimental;
namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void near(double actual, double expected, double relative = 2e-11, double absolute = 0) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > absolute + relative * std::max(std::abs(actual), std::abs(expected)))
        throw std::runtime_error("Mismatch: actual=" + std::to_string(actual) + " expected=" + std::to_string(expected));
}
template<class Action> void rejects(Action action, ReferenceErrorCode expected) {
    try { action(); } catch (const ReferenceError& error) { require(error.code() == expected, "Unexpected reference error category"); return; }
    throw std::runtime_error("Invalid reference state was accepted");
}
ReferenceMaterial water() { return {"water-surrogate", ReferencePhase::elastic_liquid, .018, 4184, 0, 200, 2000, 1000, 2.2e9, 1.6, "chosen test surrogate; not validated water data"}; }
ReferenceMaterial magma() { return {"magma-surrogate", ReferencePhase::elastic_liquid, .06, 1200, -2e5, 200, 2000, 2500, 8e9, 1.2, "chosen test surrogate; no universal magma EOS"}; }
ReferenceMaterial oxygen() { return {"oxygen", ReferencePhase::ideal_gas, .032, 660, 0, 200, 2000, 0, 0, 1, "constant-Cv ideal-gas test reference"}; }
ReferenceMaterial nitrogen() { return {"nitrogen", ReferencePhase::ideal_gas, .028, 740, 0, 200, 2000, 0, 0, 1, "constant-Cv ideal-gas test reference"}; }
LiquidThermodynamicReference liquids() { return {"test-surrogates-v1", {water(), magma()}}; }
LiquidThermodynamicReference mixed() { return {"test-surrogates-v1", {water(), magma(), oxygen(), nitrogen()}}; }
double liquid_volume(const ReferenceMaterial& material, double amount, double pressure) {
    return amount / material.zero_pressure_density_kg_per_m3 / (1 + pressure / material.reference_bulk_modulus_pa);
}
double volume_for(const std::vector<ReferenceMaterial>& materials, const std::vector<double>& masses, double pressure, double temperature) {
    double volume = 0;
    for (std::size_t i = 0; i < materials.size(); ++i) volume += materials[i].phase == ReferencePhase::elastic_liquid
        ? liquid_volume(materials[i], masses[i], pressure)
        : masses[i] / materials[i].molar_mass_kg_per_mol * reference_gas_constant * temperature / pressure;
    return volume;
}
double integrate_pressure_work(double v0, double v1, double bulk, double rho0) {
    // Independent numerical quadrature of -p(v)dv, rather than reusing the
    // implemented compression-energy formula as its own expected value.
    constexpr int count = 8192;
    const double step = (v1 - v0) / count;
    double sum = 0;
    for (int i = 0; i <= count; ++i) {
        const double v = v0 + step * i;
        const double p = bulk * (1 / (rho0 * v) - 1);
        sum += (i == 0 || i == count ? 1 : (i % 2 == 0 ? 2 : 4)) * p;
    }
    return -sum * step / 3;
}

void pure_compression() {
    const LiquidThermodynamicReference model("test-water-v1", {water()});
    const auto initial = model.initialize_at_temperature(std::vector<double>{1407}, 293.15, 1);
    near(initial.state.mechanical_pressure_pa, 895400000.0);
    require(!initial.state.has_gas && initial.state.gas_pressure_pa == 0 && initial.state.gas_volume_m3 == 0, "Pure liquid invented gas");
    near(initial.state.liquid_volume_m3, 1);
    near(initial.initialized_elastic_energy_j / 1407, 114822.03072535366);
    const auto recovered = model.evaluate(initial.cell);
    near(recovered.temperature_k, 293.15); near(recovered.mechanical_pressure_pa, initial.state.mechanical_pressure_pa);
    const double integral = integrate_pressure_work(.001, 1.0 / 1407, 2.2e9, 1000);
    near(initial.initialized_elastic_energy_j / 1407, integral, 3e-11);
}

void magma_compression() {
    const LiquidThermodynamicReference model("test-magma-v1", {magma()});
    const auto initial = model.initialize_at_temperature(std::vector<double>{2500 * 1.018}, 1500, 1);
    near(initial.state.mechanical_pressure_pa, .018 * 8e9);
    near(model.evaluate(initial.cell).temperature_k, 1500);
    require(initial.initialized_elastic_energy_j > 0, "Compressed magma surrogate stored no elastic energy");
}

void thermodynamic_derivatives() {
    const LiquidThermodynamicReference model("test-water-v1", {water()});
    const double mass = 1, volume = .001 / 1.2, temperature = 400, dv = volume * 2e-5, dt = .02;
    auto state = [&](double v, double t) { return model.initialize_at_temperature(std::vector<double>{mass}, t, v); };
    const auto centre = state(volume, temperature);
    const auto smaller = state(volume - dv, temperature), larger = state(volume + dv, temperature);
    near(-(larger.cell.internal_energy_j - smaller.cell.internal_energy_j) / (2 * dv), centre.state.mechanical_pressure_pa, 2e-8);
    near(-(larger.state.helmholtz_energy_j - smaller.state.helmholtz_energy_j) / (2 * dv), centre.state.mechanical_pressure_pa, 2e-8);
    const auto warmer = state(volume, temperature + dt), cooler = state(volume, temperature - dt);
    near((warmer.cell.internal_energy_j - cooler.cell.internal_energy_j) / (2 * dt), 4184, 2e-10);
    near((warmer.state.entropy_j_per_k - cooler.state.entropy_j_per_k) / (2 * dt), 4184 / temperature, 2e-9);
    near(-(warmer.state.helmholtz_energy_j - cooler.state.helmholtz_energy_j) / (2 * dt), centre.state.entropy_j_per_k, 3e-9);
    const auto response = model.liquid_response(0, centre.state.mechanical_pressure_pa);
    near(response.sound_speed_squared_m2_per_s2, 2.2e6);
    require(response.isothermal_bulk_modulus_pa > 0 && centre.state.equilibrium_heat_capacity_at_cell_volume_j_per_k > 0, "Unstable liquid derivative");
}

void mixture_roundtrips() {
    const auto model = mixed();
    const std::vector<double> masses{800, 100, .3, .7};
    for (int i = 0; i < 60; ++i) {
        const double pressure = std::pow(10.0, -2 + i * (10.0 / 59));
        const double temperature = 300 + i * 8;
        const double volume = volume_for(model.materials(), masses, pressure, temperature);
        const auto initialized = model.initialize_at_temperature(masses, temperature, volume);
        const auto result = model.evaluate(initialized.cell);
        near(result.mechanical_pressure_pa, pressure, 2e-10);
        near(result.temperature_k, temperature, 2e-11);
        near(result.gas_pressure_pa, result.mechanical_pressure_pa);
        double partial_sum = 0;
        for (const double partial : result.partial_volumes_m3) { require(partial > 0, "Present species lost positive partial volume"); partial_sum += partial; }
        near(partial_sum, volume);
        double reconstructed_energy = 0;
        for (std::size_t species = 0; species < masses.size(); ++species)
            reconstructed_energy += masses[species] * result.specific_internal_energies_j_per_kg[species];
        near(reconstructed_energy, initialized.cell.internal_energy_j, 1e-13);
        require(result.equilibrium_heat_capacity_at_cell_volume_j_per_k >= result.intrinsic_heat_capacity_j_per_k, "Mixture equilibrium heat capacity became negative");
        require(initialized.cell.masses_kg == masses, "Equilibrium root altered conserved composition");
    }
}

void mixture_adiabat() {
    const auto model = mixed(); const std::vector<double> masses{800, 100, .3, .7};
    const double p0 = 1e6, p1 = 4e6, t0 = 400;
    const auto start = model.initialize_at_temperature(masses, t0, volume_for(model.materials(), masses, p0, t0));
    const double gas_moles = .3 / .032 + .7 / .028;
    const double k = gas_moles * reference_gas_constant / start.state.intrinsic_heat_capacity_j_per_k;
    const double t1 = t0 * std::pow(p1 / p0, k / (1 + k));
    const double vg1 = start.state.gas_volume_m3 * std::pow(p0 / p1, 1 / (1 + k));
    const double v1 = liquid_volume(water(), 800, p1) + liquid_volume(magma(), 100, p1) + vg1;
    const auto finish = model.initialize_at_temperature(masses, t1, v1);
    near(finish.state.entropy_j_per_k, start.state.entropy_j_per_k, 2e-11);
    require(finish.cell.internal_energy_j > start.cell.internal_energy_j, "Adiabatic compression failed to store work");
    near(model.evaluate(finish.cell).temperature_k, t1);
    const auto restored = model.initialize_at_temperature(masses, t0, start.cell.volume_m3);
    near(restored.cell.internal_energy_j, start.cell.internal_energy_j);
}

void equilibrium_heat_capacity() {
    const auto model = mixed(); const std::vector<double> masses{800, 100, .3, .7};
    const double temperature = 500, dt = .05, volume = volume_for(model.materials(), masses, 2e7, temperature);
    const auto centre = model.initialize_at_temperature(masses, temperature, volume);
    const auto warm = model.initialize_at_temperature(masses, temperature + dt, volume);
    const auto cool = model.initialize_at_temperature(masses, temperature - dt, volume);
    near((warm.cell.internal_energy_j - cool.cell.internal_energy_j) / (2 * dt), centre.state.equilibrium_heat_capacity_at_cell_volume_j_per_k, 3e-9);
}

void vacuum_and_reference_fill() {
    const LiquidThermodynamicReference model("test-water-v1", {water()});
    const auto partial = model.initialize_at_temperature(std::vector<double>{500}, 300, 1);
    near(partial.state.liquid_volume_m3, .5); near(partial.state.vacuum_volume_m3, .5);
    require(partial.state.mechanical_pressure_pa == 0 && !partial.state.has_gas && partial.state.has_vacuum, "Underfilled liquid closure invented gas or tension");
    near(model.evaluate(partial.cell).temperature_k, 300);
    const auto full = model.initialize_at_temperature(std::vector<double>{1000}, 300, 1);
    require(full.state.mechanical_pressure_pa == 0 && full.state.vacuum_volume_m3 == 0 && full.state.elastic_energy_j == 0, "Reference liquid fill changed");
    const auto empty = model.evaluate({{0}, 0, 1});
    require(empty.temperature_k == 0 && empty.vacuum_volume_m3 == 1 && empty.intrinsic_heat_capacity_j_per_k == 0, "Vacuum acquired a temperature or heat capacity");
}

void tiny_compression_and_trace() {
    const LiquidThermodynamicReference liquid("test-water-v1", {water()});
    const double q = 1e-12;
    const auto response = liquid.liquid_response(0, 2.2e9 * q);
    near(response.specific_elastic_energy_j_per_kg, 2.2e6 * q * q / 2, 3e-12);
    require(response.specific_elastic_energy_j_per_kg > 0, "Tiny compression energy was cancelled to zero");
    const LiquidThermodynamicReference model("trace-v1", {water(), oxygen()});
    const auto initial = model.initialize_at_temperature(std::vector<double>{1000, 1e-28}, 300, 1);
    const auto result = model.evaluate(initial.cell);
    require(result.has_gas && result.gas_pressure_pa > 0 && result.gas_volume_m3 > 0 && initial.cell.masses_kg[1] == 1e-28,
        "A real gas trace was removed or replaced by a floor");
    near(result.gas_pressure_pa * result.gas_volume_m3, (1e-28 / .032) * reference_gas_constant * result.temperature_k, 1e-12);
}

void gas_limit() {
    const LiquidThermodynamicReference model("gas-v1", {oxygen(), nitrogen()});
    const std::vector<double> masses{.23, .77};
    const auto initialized = model.initialize_at_temperature(masses, 300, 1);
    near(initialized.state.mechanical_pressure_pa, (.23 / .032 + .77 / .028) * reference_gas_constant * 300);
    near(model.evaluate(initialized.cell).temperature_k, 300);
    require(initialized.initialized_elastic_energy_j == 0, "Pure ideal gas acquired elastic liquid energy");
}

void initialization_accounting() {
    const LiquidThermodynamicReference model("test-water-v1", {water()});
    const auto initialized = model.initialize_at_temperature(std::vector<double>{1407}, 293.15, 1);
    near(initialized.cell.internal_energy_j, initialized.caloric_reference_and_thermal_energy_j + initialized.initialized_elastic_energy_j);
    auto old_caloric_energy = initialized.cell; old_caloric_energy.internal_energy_j = initialized.caloric_reference_and_thermal_energy_j;
    const double old_value = old_caloric_energy.internal_energy_j;
    const auto kept_energy = model.evaluate(old_caloric_energy);
    near(kept_energy.temperature_k, 293.15 - 27.443123978335006);
    require(old_caloric_energy.internal_energy_j == old_value, "Evaluation silently added initialization energy");
}

void boundary_and_reference_energy() {
    auto material = water();
    const LiquidThermodynamicReference liquid("water-boundaries-v1", {material});
    const double maximum_pressure = material.reference_bulk_modulus_pa * (material.maximum_density_ratio - 1);
    const auto boundary = liquid.liquid_response(0, maximum_pressure);
    require(boundary.specific_elastic_energy_j_per_kg > 0, "Declared compression boundary was rejected");
    const LiquidThermodynamicReference trace("trace-boundary-v1", {material, oxygen()});
    const auto at_minimum = trace.initialize_at_temperature(std::vector<double>{1000, 1e-28}, material.minimum_temperature_k, 1);
    const auto restored = trace.evaluate(at_minimum.cell);
    near(restored.temperature_k, material.minimum_temperature_k);
    require(restored.has_gas && restored.gas_volume_m3 > 0, "Roundoff at minimum temperature deleted trace gas");
    material.energy_offset_j_per_kg = -15860000;
    const LiquidThermodynamicReference shifted("formation-reference-v1", {material});
    const auto a = liquid.initialize_at_temperature(std::vector<double>{1407}, 293.15, 1);
    const auto b = shifted.initialize_at_temperature(std::vector<double>{1407}, 293.15, 1);
    require(b.cell.internal_energy_j < 0, "Negative reference internal energy was not retained");
    near(b.cell.internal_energy_j - a.cell.internal_energy_j, 1407 * material.energy_offset_j_per_kg);
    near(shifted.evaluate(b.cell).temperature_k, a.state.temperature_k);
    near(b.state.mechanical_pressure_pa, a.state.mechanical_pressure_pa);
}

void tiny_temperature_domain() {
    auto tiny_water = water(); auto tiny_oxygen = oxygen();
    for (auto* material : {&tiny_water, &tiny_oxygen}) {
        material->minimum_temperature_k = 1e-20; material->maximum_temperature_k = 2e-20;
    }
    const LiquidThermodynamicReference liquid("tiny-temperature-liquid-v1", {tiny_water});
    const LiquidThermodynamicReference gas("tiny-temperature-gas-v1", {tiny_oxygen});
    const double minimum = tiny_water.minimum_temperature_k, maximum = tiny_water.maximum_temperature_k;
    for (const double temperature : {minimum, maximum, std::nextafter(minimum, 0.0),
        std::nextafter(maximum, std::numeric_limits<double>::infinity())}) {
        const auto filled = liquid.initialize_at_temperature(std::vector<double>{500}, temperature, 1);
        near(filled.state.temperature_k, temperature);
        near(liquid.evaluate(filled.cell).temperature_k, temperature);
        const auto gas_cell = gas.initialize_at_temperature(std::vector<double>{1}, temperature, 1);
        near(gas.evaluate(gas_cell.cell).temperature_k, temperature);
        require(filled.state.temperature_k == temperature, "Boundary tolerance clamped the caller's temperature");
    }
    for (const double temperature : {.99 * minimum, .5 * minimum, 1.01 * maximum, 2 * maximum}) {
        rejects([&] { (void)liquid.initialize_at_temperature(std::vector<double>{500}, temperature, 1); },
            ReferenceErrorCode::temperature_outside_model);
        rejects([&] { (void)gas.evaluate({{1}, tiny_oxygen.cv_j_per_kg_k * temperature, 1}); },
            ReferenceErrorCode::temperature_outside_model);
    }
    const LiquidThermodynamicReference trace("tiny-temperature-trace-v1", {tiny_water, tiny_oxygen});
    const auto initialized = trace.initialize_at_temperature(std::vector<double>{1000, 1e-28}, minimum, 1);
    const auto recovered = trace.evaluate(initialized.cell);
    near(recovered.temperature_k, minimum);
    require(recovered.has_gas && recovered.gas_pressure_pa > 0 && recovered.gas_volume_m3 > 0,
        "Declared tiny positive Tmin lost the real trace in the energy bracket");
}

void minimum_compression_domain() {
    auto material = water(); material.maximum_density_ratio = std::nextafter(1.0, std::numeric_limits<double>::infinity());
    const LiquidThermodynamicReference model("minimum-compression-domain-v1", {material});
    const double maximum_pressure = material.reference_bulk_modulus_pa * (material.maximum_density_ratio - 1);
    require(model.liquid_response(0, maximum_pressure).specific_elastic_energy_j_per_kg > 0,
        "Minimum representable declared compression lost its elastic energy");
    double roundoff_pressure = maximum_pressure;
    for (int n = 0; n < 8; ++n) roundoff_pressure = std::nextafter(roundoff_pressure, std::numeric_limits<double>::infinity());
    require(model.liquid_response(0, roundoff_pressure).pressure_pa == roundoff_pressure,
        "Bound-relative compression roundoff was clamped or rejected");
    for (const double pressure : {1.01 * maximum_pressure, 2 * maximum_pressure})
        rejects([&] { (void)model.liquid_response(0, pressure); }, ReferenceErrorCode::compression_outside_model);

    // q^2 itself is below double range here, but the scaled elastic energy
    // is still representable (about 22 double subnormal increments).
    const auto tiny = model.liquid_response(0, 2.2e9 * 1e-164);
    require(tiny.specific_elastic_energy_j_per_kg > 0, "Representable subnormal compression energy was erased by q*q");
    near(tiny.specific_elastic_energy_j_per_kg, 1.1e-322, 0, std::numeric_limits<double>::denorm_min());
    rejects([&] { (void)model.liquid_response(0, std::numeric_limits<double>::min()); }, ReferenceErrorCode::numerical_range);
}

void unrepresentable_reference_energy() {
    auto plus = oxygen(); auto minus = nitrogen();
    plus.cv_j_per_kg_k = minus.cv_j_per_kg_k = 1;
    plus.energy_offset_j_per_kg = 1e300; minus.energy_offset_j_per_kg = -1e300;
    const LiquidThermodynamicReference cancelling("finite-cancelling-references-v1", {plus, minus});
    const ReferenceConservedCell input{{1, 1}, 600, 1};
    rejects([&] { (void)cancelling.evaluate(input); }, ReferenceErrorCode::numerical_range);
    rejects([&] { (void)cancelling.initialize_at_temperature(input.masses_kg, 300, 1); }, ReferenceErrorCode::numerical_range);
    const auto gas = LiquidThermodynamicReference("valid-output-v1", {oxygen(), nitrogen()});
    auto output = gas.initialize_at_temperature(input.masses_kg, 300, 1).state;
    const auto original = output;
    rejects([&] { cancelling.evaluate_into(input, output); }, ReferenceErrorCode::numerical_range);
    require(output.temperature_k == original.temperature_k && output.helmholtz_energy_j == original.helmholtz_energy_j &&
        output.specific_internal_energies_j_per_kg == original.specific_internal_energies_j_per_kg && input.internal_energy_j == 600,
        "Unrepresentable cancelling references changed conserved input or prior output");
    // A single extreme offset also cannot silently absorb initialized heat.
    const LiquidThermodynamicReference large_offset("finite-large-reference-v1", {plus});
    rejects([&] { (void)large_offset.initialize_at_temperature(std::vector<double>{1}, 300, 1); }, ReferenceErrorCode::numerical_range);
    auto underflow = oxygen(); underflow.cv_j_per_kg_k = 1e-200;
    underflow.minimum_temperature_k = 5e-201; underflow.maximum_temperature_k = 2e-200;
    const LiquidThermodynamicReference tiny_heat("finite-underflowing-heat-v1", {underflow});
    rejects([&] { (void)tiny_heat.initialize_at_temperature(std::vector<double>{1}, 1e-200, 1); }, ReferenceErrorCode::numerical_range);
}

void metadata_and_invalid_model() {
    const auto baseline = liquids(); const auto repeat = liquids();
    require(baseline.metadata() == repeat.metadata() && baseline.metadata().find(liquid_reference_eos_version) != std::string::npos &&
        baseline.metadata().find("zero_thermal_expansion") != std::string::npos && baseline.metadata().find("not validated water data") != std::string::npos,
        "Explicit stable EOS metadata or surrogate provenance is absent");
    auto changed = water(); changed.reference_bulk_modulus_pa *= 2;
    require(baseline.metadata() != LiquidThermodynamicReference("test-surrogates-v1", {changed, magma()}).metadata(), "Stiffness change was omitted from metadata");
    for (int parameter = 0; parameter < 5; ++parameter) {
        auto bad = water();
        if (parameter == 0) bad.cv_j_per_kg_k = 0;
        if (parameter == 1) bad.reference_bulk_modulus_pa = -1;
        if (parameter == 2) bad.maximum_density_ratio = 1;
        if (parameter == 3) bad.maximum_temperature_k = std::numeric_limits<double>::infinity();
        if (parameter == 4) bad.provenance.clear();
        rejects([&] { LiquidThermodynamicReference rejected("bad-v1", {bad}); }, ReferenceErrorCode::invalid_model);
    }
    rejects([&] { LiquidThermodynamicReference rejected("", {water()}); }, ReferenceErrorCode::invalid_model);
    rejects([&] { LiquidThermodynamicReference rejected("duplicate-v1", {water(), water()}); }, ReferenceErrorCode::invalid_model);
}

void invalid_states_and_rollback() {
    const auto model = mixed(); const std::vector<double> masses{800, 100, .3, .7};
    const auto valid = model.initialize_at_temperature(masses, 400, volume_for(model.materials(), masses, 1e6, 400));
    auto output = valid.state; auto invalid = valid.cell; invalid.masses_kg[0] = -1;
    rejects([&] { model.evaluate_into(invalid, output); }, ReferenceErrorCode::invalid_inventory);
    require(output.temperature_k == valid.state.temperature_k && output.partial_volumes_m3 == valid.state.partial_volumes_m3 && invalid.masses_kg[0] == -1,
        "Invalid solve changed input or prior output");
    auto options = ReferenceRootOptions{}; options.maximum_iterations = 1;
    rejects([&] { model.evaluate_into(valid.cell, output, options); }, ReferenceErrorCode::root_not_converged);
    require(output.mechanical_pressure_pa == valid.state.mechanical_pressure_pa && valid.cell.masses_kg == masses,
        "Failed pressure root changed conserved input or prior output");
    const LiquidThermodynamicReference water_model("water-v1", {water()});
    rejects([&] { (void)water_model.initialize_at_temperature(std::vector<double>{1700}, 300, 1); }, ReferenceErrorCode::compression_outside_model);
    rejects([&] { (void)water_model.initialize_at_temperature(std::vector<double>{1000}, 100, 1); }, ReferenceErrorCode::temperature_outside_model);
    rejects([&] { (void)water_model.evaluate({{0}, 1, 1}); }, ReferenceErrorCode::invalid_inventory);
    rejects([&] { (void)water_model.evaluate({{1}, 0, 1}); }, ReferenceErrorCode::insufficient_thermal_energy);
    rejects([&] { (void)water_model.evaluate({{1}, 300 * 4184, 0}); }, ReferenceErrorCode::invalid_inventory);
    rejects([&] { (void)water_model.evaluate({{1}, std::numeric_limits<double>::quiet_NaN(), 1}); }, ReferenceErrorCode::invalid_inventory);
    const auto cold_compressed = water_model.initialize_at_temperature(std::vector<double>{1407}, 293.15, 1);
    auto depleted = cold_compressed.cell; depleted.internal_energy_j = 1407 * 200 * 4184;
    rejects([&] { (void)water_model.evaluate(depleted); }, ReferenceErrorCode::temperature_outside_model);
    const LiquidThermodynamicReference trapped_gas("energy-limited-v1", {water(), oxygen()});
    const ReferenceConservedCell insufficient{{1407, .01}, (1407 * 4184 + .01 * 660) * 220, 1};
    rejects([&] { (void)trapped_gas.evaluate(insufficient); }, ReferenceErrorCode::compression_outside_model);
}

template<class Function> double quadrature(Function function, double first, double last, int count = 1024) {
    const double step = (last - first) / count;
    double sum = 0;
    for (int n = 0; n <= count; ++n)
        sum += (n == 0 || n == count ? 1 : n % 2 == 0 ? 2 : 4) * function(first + step * n);
    return sum * step / 3;
}

void isentropic_gas_analytic() {
    const LiquidThermodynamicReference model("isentropic-ideal-gas-v1", {oxygen()});
    const auto initialized = model.initialize_at_temperature(std::vector<double>{1}, 300, 1);
    const auto anchor = model.make_isentrope(initialized.cell);
    const double gamma = 1 + reference_gas_constant / (.032 * 660);
    const double pressure = anchor.reference_state().mechanical_pressure_pa;
    for (const double volume : {.6, .9, 1.0, 1.2, 1.7}) {
        const auto result = model.isentropic_at_volume(anchor, volume);
        near(result.state.mechanical_pressure_pa, pressure * std::pow(volume, -gamma), 2e-12);
        near(result.state.temperature_k, 300 * std::pow(volume, 1 - gamma), 2e-12);
        near(result.state.entropy_j_per_k, anchor.reference_state().entropy_j_per_k, 0, 2e-9);
        const double integral = quadrature([&](double v) { return pressure * std::pow(v, -gamma); }, 1, volume);
        near(result.internal_energy_change_j, -integral, 2e-11, 1e-10);
        if (volume != 1) near(result.work_pressure_pa, integral / (volume - 1), 2e-11);
        else require(result.internal_energy_change_j == 0 && result.work_pressure_pa == pressure,
            "Zero gas displacement did not use the continuous work-pressure limit");
        require(result.endpoint.masses_kg == initialized.cell.masses_kg && result.branch == ReferenceIsentropicBranch::gas,
            "Isentropic gas query changed composition or branch");
    }
    require(anchor.reference_cell().internal_energy_j == initialized.cell.internal_energy_j,
        "Isentropic query mutated its conserved reference");
}

void isentropic_liquid_release() {
    const LiquidThermodynamicReference model("isentropic-liquid-v1", {water()});
    const auto initialized = model.initialize_at_temperature(std::vector<double>{1407}, 300, 1);
    const auto anchor = model.make_isentrope(initialized.cell);
    const double nominal = 1.407;
    for (const double volume : {.95, 1.1, 1.3, 1.407, 1.6, 2.0}) {
        const auto result = model.isentropic_at_volume(anchor, volume);
        const double endpoint_pressure = volume < nominal ? 2.2e9 * (nominal / volume - 1) : 0;
        near(result.state.mechanical_pressure_pa, endpoint_pressure, 3e-12, 1e-6);
        near(result.state.temperature_k, 300);
        const double last_wet = std::min(volume, nominal);
        const double integral = quadrature([&](double v) { return 2.2e9 * (nominal / v - 1); }, 1, last_wet, 4096);
        near(result.internal_energy_change_j, -integral, 3e-11);
        near(result.work_pressure_pa * result.volume_change_m3, integral, 3e-11);
        if (volume >= nominal) {
            near(result.internal_energy_change_j, -initialized.initialized_elastic_energy_j, 3e-12);
            require(result.state.elastic_energy_j == 0 && result.state.gas_volume_m3 == 0 && !result.state.has_gas,
                "Releasing compressed liquid clamped away work or invented gas");
        }
        if (volume < nominal) {
            const auto derivative = model.isentropic_derivatives(anchor, volume - 1);
            near(derivative.pressure_volume_derivative_pa_per_m3, -2.2e9 * nominal / (volume * volume), 3e-12);
            near(derivative.energy_volume_derivative_pa, -endpoint_pressure, 3e-12);
            require(derivative.temperature_volume_derivative_k_per_m3 == 0, "Zero-expansion liquid acquired adiabatic heating");
        }
    }
    const auto underfilled = model.make_isentrope(model.initialize_at_temperature(std::vector<double>{500}, 300, 1).cell);
    const auto compressed = model.isentropic_at_volume(underfilled, .4);
    const double integral = quadrature([&](double v) { return 2.2e9 * (.5 / v - 1); }, .5, .4, 4096);
    near(compressed.internal_energy_change_j, -integral, 3e-11);
    near(compressed.work_pressure_pa, integral / -.6, 3e-11);
    const double h = 1e-7;
    near((model.isentropic_response(underfilled, -.6 + h).work_pressure_pa - model.isentropic_response(underfilled, -.6 - h).work_pressure_pa) / (2 * h),
        model.isentropic_work_pressure_derivative(underfilled, -.6), 3e-7);
    const auto free = model.isentropic_at_volume(underfilled, 2);
    require(free.work_pressure_pa == 0 && free.internal_energy_change_j == 0 && free.state.vacuum_volume_m3 == 1.5,
        "Underfilled liquid/vacuum branch performed pressure work");
}

void isentropic_mixture_known_roots() {
    const auto model = mixed(); const std::vector<double> masses{800, 100, .3, .7};
    const double p0 = 1e6, t0 = 400;
    const auto initialized = model.initialize_at_temperature(masses, t0, volume_for(model.materials(), masses, p0, t0));
    const auto anchor = model.make_isentrope(initialized.cell);
    const double nr = (.3 / .032 + .7 / .028) * reference_gas_constant;
    const double c = 800 * 4184 + 100 * 1200 + .3 * 660 + .7 * 740;
    for (const double ratio : {.25, .5, 1.0, 2.0, 20.0, 200.0}) {
        const double pressure = p0 * ratio, temperature = t0 * std::pow(ratio, nr / (c + nr));
        const double volume = volume_for(model.materials(), masses, pressure, temperature);
        const auto result = model.isentropic_at_volume(anchor, volume);
        const auto independent = model.initialize_at_temperature(masses, temperature, volume);
        near(result.state.mechanical_pressure_pa, pressure, 3e-10);
        near(result.state.temperature_k, temperature, 3e-12);
        near(result.endpoint.internal_energy_j, independent.cell.internal_energy_j, 3e-12);
        near(result.state.entropy_j_per_k, anchor.reference_state().entropy_j_per_k, 0, 1e-6);
        require(result.endpoint.masses_kg == masses, "Common-pressure isentrope changed a species mass");
    }
}

void isentropic_derivatives_and_work() {
    const auto model = mixed(); const std::vector<double> masses{800, 100, .3, .7};
    const auto initialized = model.initialize_at_temperature(masses, 500, volume_for(model.materials(), masses, 2e7, 500));
    const auto anchor = model.make_isentrope(initialized.cell);
    const double volume = initialized.cell.volume_m3, h = volume * 1e-6, delta = volume * .001;
    const auto derivatives = model.isentropic_derivatives(anchor, 0);
    const auto minus = model.isentropic_response(anchor, -h), plus = model.isentropic_response(anchor, h);
    near((plus.endpoint.internal_energy_j - minus.endpoint.internal_energy_j) / (2 * h), derivatives.energy_volume_derivative_pa, 3e-7);
    near((plus.state.mechanical_pressure_pa - minus.state.mechanical_pressure_pa) / (2 * h), derivatives.pressure_volume_derivative_pa_per_m3, 3e-7);
    near((plus.state.temperature_k - minus.state.temperature_k) / (2 * h), derivatives.temperature_volume_derivative_k_per_m3, 2e-5);
    near(derivatives.work_pressure_volume_derivative_pa_per_m3, .5 * derivatives.pressure_volume_derivative_pa_per_m3, 2e-12);
    const auto work_plus = model.isentropic_response(anchor, delta + h), work_minus = model.isentropic_response(anchor, delta - h);
    near((work_plus.work_pressure_pa - work_minus.work_pressure_pa) / (2 * h),
        model.isentropic_work_pressure_derivative(anchor, delta), 2e-6);
    const auto result = model.isentropic_response(anchor, delta);
    const double integral = quadrature([&](double v) { return model.isentropic_at_volume(anchor, v).state.mechanical_pressure_pa; }, volume, volume + delta, 256);
    near(-result.internal_energy_change_j, integral, 3e-9);
    require(derivatives.pressure_volume_derivative_pa_per_m3 < 0 && derivatives.work_pressure_volume_derivative_pa_per_m3 < 0,
        "Isentropic response lost positive mechanical stiffness");
}

void isentropic_reversible_cycles() {
    const auto model = mixed(); const std::vector<double> masses{800, 100, .3, .7};
    const auto initial = model.initialize_at_temperature(masses, 400, volume_for(model.materials(), masses, 1e6, 400));
    auto cell = initial.cell; double work_energy = 0;
    for (const double ratio : {.92, 1.1, .96, 1.0}) {
        const auto anchor = model.make_isentrope(cell);
        const auto result = model.isentropic_at_volume(anchor, initial.cell.volume_m3 * ratio);
        work_energy += result.internal_energy_change_j;
        near(result.state.entropy_j_per_k, initial.state.entropy_j_per_k, 0, 2e-6);
        cell = result.endpoint;
    }
    near(cell.internal_energy_j, initial.cell.internal_energy_j, 2e-13);
    near(work_energy, 0, 0, 1e-4);
    near(model.evaluate(cell).temperature_k, 400, 2e-12);
    require(cell.masses_kg == initial.cell.masses_kg, "Reversible cycle changed conserved composition");
}

void isentropic_tiny_increments() {
    const LiquidThermodynamicReference gas("tiny-gas-displacement-v1", {oxygen()});
    const auto gas_anchor = gas.make_isentrope(gas.initialize_at_temperature(std::vector<double>{1}, 300, 1).cell);
    const auto derivative = gas.isentropic_derivatives(gas_anchor, 0);
    for (const double delta : {-1e-300, 1e-300, -1e-200, 1e-200, -1e-20, 1e-20, -1e-12, 1e-12}) {
        const auto result = gas.isentropic_response(gas_anchor, delta);
        near(result.work_pressure_pa, gas_anchor.reference_state().mechanical_pressure_pa, 3e-12);
        near(result.internal_energy_change_j, -gas_anchor.reference_state().mechanical_pressure_pa * delta, 3e-12);
        near(gas.isentropic_work_pressure_derivative(gas_anchor, delta), derivative.work_pressure_volume_derivative_pa_per_m3, 3e-12);
        if (std::abs(delta) < 1e-16) require(!result.endpoint_volume_resolves_change && !result.endpoint_energy_resolves_change &&
            result.represented_volume_change_m3 == 0 && result.represented_internal_energy_change_j == 0 && result.internal_energy_change_j != 0,
            "Sub-ULP work was erased or falsely reported as an exact represented endpoint");
    }
    const LiquidThermodynamicReference liquid("tiny-liquid-displacement-v1", {water()});
    const auto compressed = liquid.make_isentrope(liquid.initialize_at_temperature(std::vector<double>{1407}, 300, 1).cell);
    const auto change = liquid.isentropic_response(compressed, 1e-20);
    near(change.work_pressure_pa, compressed.reference_state().mechanical_pressure_pa, 3e-12);
    require(!change.endpoint_volume_resolves_change && !change.endpoint_energy_resolves_change && change.internal_energy_change_j < 0,
        "Compressed-liquid sub-ULP work precision was not reported");
    const auto mixture = mixed(); const std::vector<double> mixture_masses{800, 100, .3, .7};
    const auto mixture_anchor = mixture.make_isentrope(mixture.initialize_at_temperature(mixture_masses, 400,
        volume_for(mixture.materials(), mixture_masses, 1e6, 400)).cell);
    for (const auto& test : std::vector<std::pair<const LiquidThermodynamicReference*, const ReferenceIsentrope*>>{
        {&gas, &gas_anchor}, {&liquid, &compressed}, {&mixture, &mixture_anchor}}) {
        const double reference_volume = test.second->reference_cell().volume_m3;
        for (const double endpoint : {std::nextafter(reference_volume, 0.0),
            std::nextafter(reference_volume, std::numeric_limits<double>::infinity())}) {
            const auto adjacent = test.first->isentropic_at_volume(*test.second, endpoint);
            near(adjacent.work_pressure_pa, test.second->reference_state().mechanical_pressure_pa, 3e-12);
            require(adjacent.endpoint_volume_resolves_change && adjacent.volume_change_m3 == endpoint - reference_volume &&
                adjacent.internal_energy_change_j != 0, "Adjacent absolute-volume query lost or failed to resolve its real increment");
        }
    }
    const auto filled = liquid.make_isentrope(liquid.initialize_at_temperature(std::vector<double>{1000}, 300, 1).cell);
    const auto inward = liquid.isentropic_response(filled, -1e-20), outward = liquid.isentropic_response(filled, 1e-20);
    near(inward.work_pressure_pa, 1.1e-11, 3e-12);
    near(liquid.isentropic_work_pressure_derivative(filled, -1e-20), -1.1e9, 3e-12);
    require(inward.branch == ReferenceIsentropicBranch::compressed_liquid && outward.branch == ReferenceIsentropicBranch::liquid_with_void &&
        !inward.endpoint_volume_resolves_change && !outward.endpoint_volume_resolves_change && outward.work_pressure_pa == 0,
        "Sub-ULP displacements incorrectly merged the liquid compression and void branches");
    const LiquidThermodynamicReference trace("trace-isentrope-v1", {water(), oxygen()});
    const auto trace_anchor = trace.make_isentrope(trace.initialize_at_temperature(std::vector<double>{1000, 1e-28}, 300, 1).cell);
    const auto trace_step = trace.isentropic_response(trace_anchor, 1e-30);
    require(trace_step.state.has_gas && trace_step.state.gas_volume_m3 > 0 && trace_step.endpoint.masses_kg[1] == 1e-28 && trace_step.work_pressure_pa > 0,
        "Tiny isentropic displacement removed a genuine gas trace");
    auto cold_gas = oxygen(); cold_gas.minimum_temperature_k = 1e-20; cold_gas.maximum_temperature_k = 2e-20;
    const LiquidThermodynamicReference tiny_temperature("tiny-positive-isentrope-v1", {cold_gas});
    const auto cold_anchor = tiny_temperature.make_isentrope(tiny_temperature.initialize_at_temperature(std::vector<double>{1}, 1.5e-20, 1).cell);
    const auto cold_step = tiny_temperature.isentropic_response(cold_anchor, 1e-20);
    require(cold_step.work_pressure_pa > 0 && cold_step.internal_energy_change_j < 0 && cold_step.state.temperature_k > 0,
        "Tiny positive-temperature isentrope erased pressure work");
    near(cold_step.work_pressure_pa, cold_anchor.reference_state().mechanical_pressure_pa, 3e-12);
}

void isentropic_kinks_empty_and_profiles() {
    const LiquidThermodynamicReference model("kink-isentrope-v1", {water()});
    const auto filled = model.make_isentrope(model.initialize_at_temperature(std::vector<double>{1000}, 300, 1).cell);
    require(model.isentropic_response(filled, 0).branch == ReferenceIsentropicBranch::zero_pressure_fill,
        "Zero-pressure fill kink was not exposed");
    rejects([&] { (void)model.isentropic_derivatives(filled, 0); }, ReferenceErrorCode::unsupported_derivative);
    rejects([&] { (void)model.isentropic_work_pressure_derivative(filled, 0); }, ReferenceErrorCode::unsupported_derivative);
    const auto compressed = model.make_isentrope(model.initialize_at_temperature(std::vector<double>{1000}, 300, .75).cell);
    const auto endpoint_kink = model.isentropic_response(compressed, .25);
    require(endpoint_kink.branch == ReferenceIsentropicBranch::zero_pressure_fill && endpoint_kink.work_pressure_pa > 0,
        "A release ending at the fill kink lost its elastic work");
    rejects([&] { (void)model.isentropic_derivatives(compressed, .25); }, ReferenceErrorCode::unsupported_derivative);
    near(model.isentropic_work_pressure_derivative(compressed, .25), -endpoint_kink.work_pressure_pa / .25, 3e-12);
    const auto empty = model.make_isentrope({{0}, 0, 1});
    const auto vacuum = model.isentropic_at_volume(empty, 2);
    require(vacuum.state.temperature_k == 0 && vacuum.state.intrinsic_heat_capacity_j_per_k == 0 && vacuum.endpoint.internal_energy_j == 0 &&
        vacuum.state.vacuum_volume_m3 == 2 && vacuum.work_pressure_pa == 0 && vacuum.branch == ReferenceIsentropicBranch::vacuum,
        "Empty entropy reference invented temperature, heat capacity, energy or matter");
    rejects([&] { (void)model.isentropic_derivatives(empty, 0); }, ReferenceErrorCode::unsupported_derivative);
    require(model.isentropic_work_pressure_derivative(empty, .1) == 0, "Vacuum pressure work acquired a derivative");
    const LiquidThermodynamicReference same("kink-isentrope-v1", {water()});
    require(same.isentropic_response(filled, .1).state.mechanical_pressure_pa == 0, "Identical immutable model metadata did not validate the anchor");
    const LiquidThermodynamicReference changed("changed-profile-v1", {water()});
    rejects([&] { (void)changed.isentropic_response(filled, .1); }, ReferenceErrorCode::incompatible_entropy_reference);
    auto changed_water = water(); changed_water.reference_bulk_modulus_pa *= 2;
    const LiquidThermodynamicReference changed_eos("kink-isentrope-v1", {changed_water});
    rejects([&] { (void)changed_eos.isentropic_response(filled, .1); }, ReferenceErrorCode::incompatible_entropy_reference);
}

void isentropic_vacuum_limits() {
    auto material = oxygen(); material.minimum_temperature_k = 1e-100;
    const LiquidThermodynamicReference model("gas-vacuum-limit-v1", {material});
    const auto anchor = model.make_isentrope(model.initialize_at_temperature(std::vector<double>{1}, 300, 1).cell);
    const double gamma = 1 + reference_gas_constant / (.032 * 660);
    const auto expanded = model.isentropic_at_volume(anchor, 1e60);
    near(expanded.state.temperature_k, 300 * std::pow(1e60, 1 - gamma), 3e-11);
    require(expanded.state.has_gas && expanded.state.gas_pressure_pa > 0 && !expanded.state.has_vacuum && expanded.endpoint.masses_kg[0] == 1 &&
        expanded.endpoint.internal_energy_j > 0 && expanded.state.temperature_k > 0,
        "Finite-volume expansion toward vacuum deleted gas or erased its remaining heat");
    const auto gasless = model.make_isentrope({{0}, 0, 1});
    require(model.isentropic_at_volume(gasless, 1e60).state.gas_pressure_pa == 0, "True vacuum acquired seed gas");
    const LiquidThermodynamicReference limited("finite-temperature-vacuum-limit-v1", {oxygen()});
    const auto warm = limited.make_isentrope(limited.initialize_at_temperature(std::vector<double>{1}, 300, 1).cell);
    rejects([&] { (void)limited.isentropic_at_volume(warm, 1e60); }, ReferenceErrorCode::temperature_outside_model);
}

void isentropic_domains_and_rollback() {
    const LiquidThermodynamicReference model("rollback-isentrope-v1", {water(), oxygen()});
    const auto initialized = model.initialize_at_temperature(std::vector<double>{800, .1}, 400, 1);
    const auto anchor = model.make_isentrope(initialized.cell);
    auto output = model.isentropic_response(anchor, .01); const auto original = output;
    for (const double invalid : {-1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        rejects([&] { model.isentropic_response_into(anchor, invalid, output); }, ReferenceErrorCode::invalid_inventory);
        require(output.endpoint.internal_energy_j == original.endpoint.internal_energy_j && output.endpoint.masses_kg == original.endpoint.masses_kg &&
            output.internal_energy_change_j == original.internal_energy_change_j && output.work_pressure_pa == original.work_pressure_pa,
            "Invalid isentropic query changed the prior output");
    }
    auto options = ReferenceRootOptions{}; options.maximum_iterations = 1;
    rejects([&] { model.isentropic_response_into(anchor, .01, output, options); }, ReferenceErrorCode::root_not_converged);
    require(output.work_pressure_pa == original.work_pressure_pa && anchor.reference_cell().masses_kg == initialized.cell.masses_kg &&
        anchor.reference_cell().internal_energy_j == initialized.cell.internal_energy_j, "Failed isentropic root changed its reference or prior output");
    rejects([&] { (void)model.make_isentrope({{0, 0}, 1, 1}); }, ReferenceErrorCode::invalid_inventory);
    rejects([&] { (void)model.make_isentrope({{-1, 0}, 0, 1}); }, ReferenceErrorCode::invalid_inventory);
    rejects([&] { (void)model.isentropic_at_volume(anchor, .1); }, ReferenceErrorCode::compression_outside_model);
}

void ideal_gas_entropy_extreme_ratios_and_denormal_trace() {
    auto gas = oxygen(); gas.minimum_temperature_k = 1e-315;
    const LiquidThermodynamicReference pure("stable-gas-entropy-v1", {gas});
    for (const auto& [mass, temperature, volume] : std::vector<std::array<double, 3>>{
        {1e-10, 300, 1e300}, {1e24, 1e-310, 1e-300}, {gas.molar_mass_kg_per_mol, entropy_reference_temperature_k, 1}}) {
        const double moles = mass / gas.molar_mass_kg_per_mol;
        const auto state = pure.initialize_at_temperature(std::vector<double>{mass}, temperature, volume);
        const double expected = mass * gas.cv_j_per_kg_k * (std::log(temperature) - std::log(entropy_reference_temperature_k)) +
            moles * reference_gas_constant * (std::log(volume) - std::log(moles));
        near(state.state.entropy_j_per_k, expected, 5e-14);
        require(std::isfinite(state.state.helmholtz_energy_j) && state.cell.masses_kg[0] == mass,
            "Extreme finite gas entropy changed mass or overflowed Helmholtz energy");
        const auto expanded = pure.initialize_at_temperature(std::vector<double>{mass}, temperature, volume * 1.01);
        near(expanded.state.entropy_j_per_k - state.state.entropy_j_per_k,
            moles * reference_gas_constant * std::log1p(.01), 5e-10);
    }
    auto trace = nitrogen(); trace.id = "denormal-helium-test"; trace.molar_mass_kg_per_mol = .004; trace.cv_j_per_kg_k = 3120;
    const LiquidThermodynamicReference mixture("stable-trace-entropy-v1", {oxygen(), trace});
    const double tiny = std::numeric_limits<double>::denorm_min();
    const auto initial = mixture.initialize_at_temperature(std::vector<double>{1, tiny}, 300, .5);
    require(initial.cell.masses_kg[1] == tiny && initial.state.partial_volumes_m3[1] > 0 &&
        std::isfinite(initial.state.entropy_j_per_k) && initial.state.has_gas,
        "Positive denormal trace disappeared or made finite entropy overflow");
    const auto anchor = mixture.make_isentrope(initial.cell);
    const auto response = mixture.isentropic_response(anchor, .01);
    require(response.endpoint.masses_kg[1] == tiny && response.state.partial_volumes_m3[1] > 0,
        "Stable entropy query changed the denormal trace");
    near(response.state.entropy_j_per_k, initial.state.entropy_j_per_k, 1e-12);
}
}

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests{
        {"analytic pure-liquid compression and independent pressure-work integral", pure_compression},
        {"observed magma density ratio without gas", magma_compression},
        {"Helmholtz/caloric/entropy derivatives and positive stiffness", thermodynamic_derivatives},
        {"common-pressure gas/two-liquid conserved-state roots", mixture_roundtrips},
        {"gas/liquid reversible entropy and energy cycle", mixture_adiabat},
        {"positive equilibrium heat capacity matches fixed-volume derivative", equilibrium_heat_capacity},
        {"underfilled liquid/vacuum and filled reference state", vacuum_and_reference_fill},
        {"tiny compression energy and genuine gas trace", tiny_compression_and_trace},
        {"pure ideal-gas limit", gas_limit},
        {"explicit initialization energy versus unchanged conserved energy", initialization_accounting},
        {"domain boundaries and negative formation-energy reference", boundary_and_reference_energy},
        {"tiny positive temperature bounds and minimum-temperature gas/liquid bracket", tiny_temperature_domain},
        {"minimum representable compression bound and elastic-energy numerical range", minimum_compression_domain},
        {"finite cancelling energy references fail without changing conserved state", unrepresentable_reference_energy},
        {"versioned metadata/provenance and invalid model rejection", metadata_and_invalid_model},
        {"domain/root failures preserve input and output", invalid_states_and_rollback},
        {"isentropic ideal-gas analytic state and pressure-work quadrature", isentropic_gas_analytic},
        {"gasless liquid release through zero pressure recovers all elastic work", isentropic_liquid_release},
        {"common-pressure entropy-reference mixture volume queries", isentropic_mixture_known_roots},
        {"isentropic endpoint/work derivatives and independent quadrature", isentropic_derivatives_and_work},
        {"reversible isentropic volume cycle preserves composition and work", isentropic_reversible_cycles},
        {"sub-ULP volume/work changes and explicit endpoint precision reports", isentropic_tiny_increments},
        {"typed liquid-fill kinks, empty reference and immutable model identity", isentropic_kinks_empty_and_profiles},
        {"finite-volume gas expansion toward vacuum without deleting matter", isentropic_vacuum_limits},
        {"isentropic domain/root failures preserve reference and prior output", isentropic_domains_and_rollback},
        {"ideal-gas entropy scale extremes and positive denormal trace", ideal_gas_entropy_extreme_ratios_and_denormal_trace}
    };
    int failed = 0;
    for (const auto& [name, test] : tests) {
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
    }
    return failed == 0 ? 0 : 1;
}
