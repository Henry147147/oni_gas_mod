#include "mixed_atmospheres/fluid_world.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void near(double actual, double expected, double relative, double absolute = 0) {
    require(std::isfinite(actual) && std::isfinite(expected) &&
        std::abs(actual - expected) <= relative * std::abs(expected) + absolute, "Analytic comparison failed");
}
struct Integrals { double mass = 0, momentum = 0, pressure = 0, kinetic = 0; };
struct Problem {
    double length = 8, interface_x = 2, rho = 1, pressure = 100000, velocity = 0;
    double gamma = 0, sound_speed = 0, temperature = 0;
    [[nodiscard]] double time() const { return .5 / sound_speed; }
    [[nodiscard]] double front_speed() const { return velocity + 2 * sound_speed / (gamma - 1); }
    [[nodiscard]] double head(double t) const { return interface_x + (velocity - sound_speed) * t; }
    [[nodiscard]] double front(double t) const { return interface_x + front_speed() * t; }
    [[nodiscard]] Integrals integrate(double left, double right, double t) const {
        Integrals result;
        const double uniform_end = std::min(right, head(t));
        if (uniform_end > left) {
            const double width = uniform_end - left;
            result.mass = rho * width; result.momentum = rho * velocity * width;
            result.pressure = pressure * width; result.kinetic = .5 * rho * velocity * velocity * width;
        }
        const double a = std::max(left, head(t)), b = std::min(right, front(t));
        if (b <= a) return result;
        const double exponent = 2 / (gamma - 1), slope = (gamma - 1) / (gamma + 1);
        const auto lambda = [&](double x) {
            // The exact fan is bounded by c/c0=1 and 0. No numerical fluid
            // state or density is clipped by this analytic integration.
            if (x == head(t)) return 1.0;
            if (x == front(t)) return 0.0;
            return (2 * sound_speed + (gamma - 1) * (velocity - (x - interface_x) / t)) /
                ((gamma + 1) * sound_speed);
        };
        const auto integral_power = [&](double power) {
            return sound_speed * t / (slope * (power + 1)) *
                (std::pow(lambda(a), power + 1) - std::pow(lambda(b), power + 1));
        };
        const double i0 = integral_power(exponent), i1 = integral_power(exponent + 1), i2 = integral_power(exponent + 2);
        const double terminal = front_speed(), coefficient = 2 * sound_speed / (gamma - 1);
        result.mass += rho * i0;
        result.momentum += rho * (terminal * i0 - coefficient * i1);
        result.pressure += pressure * integral_power(exponent * gamma);
        result.kinetic += .5 * rho * (terminal * terminal * i0 - 2 * terminal * coefficient * i1 + coefficient * coefficient * i2);
        return result;
    }
};
struct Setup {
    ma::SpeciesRegistry registry = ma::SpeciesRegistry::standard();
    std::vector<double> fractions;
    std::shared_ptr<const ma::FluidThermodynamics> eos;
    Problem problem;
    Setup() : fractions(registry.size()) {
        fractions[registry.index("oxygen")] = .22; fractions[registry.index("nitrogen")] = .78;
        fractions[registry.index("helium")] = 1e-28;
        std::vector<ma::experimental::ReferenceMaterial> materials;
        double gas_coefficient = 0, capacity = 0;
        for (const auto* id : {"oxygen", "nitrogen", "helium"}) {
            const auto index = registry.index(id); const auto& species = registry[index];
            ma::experimental::ReferenceMaterial m;
            m.id = id; m.phase = ma::experimental::ReferencePhase::ideal_gas;
            m.molar_mass_kg_per_mol = species.molar_mass; m.cv_j_per_kg_k = species.cv();
            m.energy_offset_j_per_kg = species.internal_energy(0);
            m.minimum_temperature_k = 1e-20; m.maximum_temperature_k = 1e9;
            m.provenance = "Explicit constant-property ideal-gas rarefaction validation profile";
            materials.push_back(std::move(m));
            gas_coefficient += fractions[index] * ma::gas_constant / species.molar_mass;
            capacity += fractions[index] * species.cv();
        }
        problem.gamma = 1 + gas_coefficient / capacity;
        problem.sound_speed = std::sqrt(problem.gamma * problem.pressure / problem.rho);
        problem.temperature = problem.pressure / (problem.rho * gas_coefficient);
        eos = std::make_shared<const ma::FluidThermodynamics>(registry, "ideal-gas-vacuum-validation-v1", std::move(materials));
    }
};
struct Sample { double density = 0, velocity = 0, pressure = 0, occupied_fraction = 0; };
struct Evidence {
    std::size_t count = 0;
    double courant = 0, dx = 0, front = 0, recent_speed = 0, tip_velocity = 0, tip_density = 0, tip_pressure = 0;
    double density_l1 = 0, velocity_l1 = 0, pressure_l1 = 0;
    double mass_relative = 0, composition_relative = 0, energy_relative = 0, momentum_balance = 0, analytic_momentum_relative = 0;
    double entropy_change = 0, signed_geometry_residual = 0, absolute_geometry_residual = 0;
    double maximum_addition_roundoff = 0, maximum_root_arithmetic_roundoff = 0, maximum_relative_root_residual = 0;
    std::uint64_t substeps = 0, rejected = 0;
    std::size_t unresolved_energy = 0, unresolved_volume = 0, unresolved_position = 0;
    std::vector<Sample> samples;
};
double position(const ma::FluidWorld& world, double dx) {
    double result = 0;
    for (std::size_t i = 0; i < world.width(); ++i) if (world.cell(i, 0).mixture.mass() > 0)
        result = static_cast<double>(i) * dx + world.cell(i, 0).occupied_end_m;
    return result;
}
Evidence run(const Setup& setup, std::size_t count, double courant, const std::string& csv = {}) {
    require(count >= 16 && count % 4 == 0 && std::isfinite(courant) && courant > 0, "Invalid rarefaction case");
    const auto& problem = setup.problem;
    Evidence result; result.count = count; result.courant = courant; result.dx = problem.length / static_cast<double>(count);
    ma::FluidSolverSettings settings; settings.gravity = 0; settings.diffusivity_m2_per_s = 0;
    settings.viscosity_m2_per_s = 0; settings.conduction = false;
    settings.maximum_timestep_s = courant * result.dx / problem.sound_speed;
    ma::FluidWorld world(count, 1, result.dx, 1 / result.dx, settings, setup.registry,
        {ma::FluidAxis::x, setup.eos}); // Strict EOS: no liquid approximation or gas floor.
    auto masses = setup.fractions; for (auto& mass : masses) mass *= problem.rho * result.dx;
    const auto initial = setup.eos->initialize_at_temperature(masses, problem.temperature, result.dx);
    for (std::size_t i = 0; i < count / 4; ++i) world.set_occupied_cell(i, 0, initial.mixture, 0, result.dx, problem.velocity);
    const auto before = world.totals();
    const double initial_entropy = initial.state.entropy_j_per_k * static_cast<double>(count / 4);
    const double entropy_capacity = initial.state.intrinsic_heat_capacity_j_per_k * static_cast<double>(count / 4);
    const auto first = world.step(.8 * problem.time());
    const double first_front = position(world, result.dx);
    const auto second = world.step(.2 * problem.time());
    const auto after = world.totals(); result.front = position(world, result.dx);
    result.recent_speed = (result.front - first_front) / (.2 * problem.time());
    result.substeps = first.substeps + second.substeps; result.rejected = first.rejected_substeps + second.rejected_substeps;
    result.signed_geometry_residual = first.signed_geometry_volume_residual_m3 + second.signed_geometry_volume_residual_m3;
    result.absolute_geometry_residual = first.absolute_geometry_volume_residual_m3 + second.absolute_geometry_volume_residual_m3;
    result.maximum_addition_roundoff = std::max(first.maximum_volume_addition_roundoff_m3, second.maximum_volume_addition_roundoff_m3);
    result.maximum_root_arithmetic_roundoff = std::max(first.maximum_root_arithmetic_roundoff_m3, second.maximum_root_arithmetic_roundoff_m3);
    result.maximum_relative_root_residual = std::max(first.maximum_endpoint_relative_volume_residual, second.maximum_endpoint_relative_volume_residual);
    result.unresolved_energy = first.unresolved_energy_updates + second.unresolved_energy_updates;
    result.unresolved_volume = first.unresolved_volume_updates + second.unresolved_volume_updates;
    result.unresolved_position = first.unresolved_position_updates + second.unresolved_position_updates;
    const double thermal_scale = initial.state.thermal_energy_j * static_cast<double>(count / 4);
    result.energy_relative = (after.energy() - before.energy()) / thermal_scale;
    for (std::size_t s = 0; s < before.masses.size(); ++s) {
        if (before.masses[s] > 0) result.mass_relative = std::max(result.mass_relative, std::abs(after.masses[s] / before.masses[s] - 1));
        else require(after.masses[s] == 0, "Rarefaction invented an absent constituent");
    }
    const double fan_width = problem.front(problem.time()) - problem.head(problem.time());
    double momentum = 0, entropy = 0, velocity_weight = 0;
    std::ofstream output;
    if (!csv.empty()) {
        output.open(csv); require(output.good(), "Cannot open rarefaction profile output");
        output << std::setprecision(17) << "x,rho,rho_exact,u,u_exact,p,p_exact,occupied_fraction\n";
    }
    for (std::size_t i = 0; i < count; ++i) {
        const double left = static_cast<double>(i) * result.dx, right = left + result.dx;
        const auto exact = problem.integrate(left, right, problem.time());
        const auto& cell = world.cell(i, 0); const double mass = cell.mixture.mass(), volume = world.occupied_volume(i, 0);
        const auto state = world.thermodynamic_state(i, 0);
        Sample sample{mass / result.dx, mass > 0 ? cell.momentum_x / mass : 0, state.mechanical_pressure_pa * volume / result.dx, volume / result.dx};
        result.samples.push_back(sample); momentum += cell.momentum_x; entropy += state.entropy_j_per_k;
        if (mass > 0) {
            require(state.mechanical_pressure_pa > 0 && state.temperature_k > 0 && !state.has_vacuum,
                "Positive gas acquired a pressure/density floor or an internal vacuum state");
            result.tip_velocity = sample.velocity; result.tip_density = mass / volume; result.tip_pressure = state.mechanical_pressure_pa;
            for (std::size_t s = 0; s < setup.fractions.size(); ++s) {
                if (setup.fractions[s] > 0) result.composition_relative = std::max(result.composition_relative,
                    std::abs(cell.mixture.masses[s] / mass / setup.fractions[s] - 1));
                else require(cell.mixture.masses[s] == 0, "Remap invented a constituent");
            }
        } else require(cell.mixture.energy == 0 && cell.momentum_x == 0 && cell.occupied_begin_m == 0 && cell.occupied_end_m == 0,
            "Exact vacuum carried material, heat, momentum or occupied geometry");
        result.density_l1 += std::abs(mass - exact.mass) / (problem.rho * fan_width);
        result.pressure_l1 += std::abs(sample.pressure * result.dx - exact.pressure) / (problem.pressure * fan_width);
        const double exact_velocity = exact.mass > 0 ? exact.momentum / exact.mass : 0;
        const double centre = .5 * (left + right);
        if (exact.mass > 0 && left >= problem.head(problem.time()) && right <= problem.front(problem.time()) - .5 * problem.sound_speed * problem.time()) {
            result.velocity_l1 += exact.mass * std::abs(sample.velocity - exact_velocity); velocity_weight += exact.mass;
        }
        if (output.is_open()) output << centre << ',' << sample.density << ',' << exact.mass / result.dx << ',' << sample.velocity << ',' << exact_velocity <<
            ',' << sample.pressure << ',' << exact.pressure / result.dx << ',' << sample.occupied_fraction << '\n';
    }
    require(velocity_weight > 0, "Grid has no resolved interior fan velocity sample");
    result.velocity_l1 /= velocity_weight * problem.sound_speed;
    require(world.cell(count - 1, 0).mixture.mass() == 0, "Far exterior vacuum was seeded");
    result.entropy_change = entropy - initial_entropy;
    result.momentum_balance = momentum - first.wall_reaction_impulse_kg_m_per_s - second.wall_reaction_impulse_kg_m_per_s;
    result.analytic_momentum_relative = momentum / (problem.pressure * problem.time()) - 1;
    require(result.mass_relative < 2e-11 && result.composition_relative < 2e-10 && std::abs(result.energy_relative) < 2e-10,
        "Rarefaction failed constituent/trace/total-energy conservation");
    require(std::abs(result.momentum_balance) < 2e-10 * problem.pressure * problem.time(), "Wall reaction does not balance gas momentum");
    require(result.entropy_change >= -512 * std::numeric_limits<double>::epsilon() * entropy_capacity,
        "Gas pressure/remap decreased total thermodynamic entropy");
    return result;
}
void print(const Setup& setup, const Evidence& result) {
    const auto& p = setup.problem;
    std::cout << std::setprecision(17) << "N=" << result.count << " C=" << result.courant << " substeps=" << result.substeps <<
        " rejected=" << result.rejected << " front=" << result.front << " front_exact=" << p.front(p.time()) <<
        " front_error=" << std::abs(result.front - p.front(p.time())) << " recent_speed/c=" << result.recent_speed / p.sound_speed <<
        " exact_speed/c=" << p.front_speed() / p.sound_speed << " tip_u/c=" << result.tip_velocity / p.sound_speed <<
        " tip_rho=" << result.tip_density << " tip_p=" << result.tip_pressure << " rho_L1=" << result.density_l1 <<
        " u_L1=" << result.velocity_l1 << " p_L1=" << result.pressure_l1 << " mass_rel=" << result.mass_relative <<
        " composition_rel=" << result.composition_relative << " energy_rel=" << result.energy_relative <<
        " wall_momentum_residual=" << result.momentum_balance << " analytic_momentum_rel=" << result.analytic_momentum_relative <<
        " entropy_change=" << result.entropy_change << " signed_geometry_residual=" << result.signed_geometry_residual <<
        " absolute_geometry_residual=" << result.absolute_geometry_residual << " maximum_addition_roundoff=" << result.maximum_addition_roundoff <<
        " maximum_root_arithmetic_roundoff=" << result.maximum_root_arithmetic_roundoff <<
        " maximum_endpoint_relative_root_residual=" << result.maximum_relative_root_residual <<
        " unresolved_energy=" << result.unresolved_energy << " unresolved_volume=" << result.unresolved_volume <<
        " unresolved_position=" << result.unresolved_position << '\n';
}
double distance(const Setup& setup, const Evidence& a, const Evidence& b) {
    require(a.count == b.count, "Timestep comparison must keep the spatial grid fixed");
    double result = 0;
    for (std::size_t i = 0; i < a.samples.size(); ++i) {
        const auto& x = a.samples[i]; const auto& y = b.samples[i];
        result += a.dx * (std::abs(x.density - y.density) / setup.problem.rho +
            std::abs(x.pressure - y.pressure) / setup.problem.pressure +
            std::abs(x.density * x.velocity - y.density * y.velocity) / (setup.problem.rho * setup.problem.sound_speed));
    }
    return result / (setup.problem.front(setup.problem.time()) - setup.problem.head(setup.problem.time()));
}
void oracle(const Setup& setup) {
    const auto& p = setup.problem; const auto total = p.integrate(0, p.length, p.time());
    near(total.mass, p.rho * p.interface_x, 2e-14);
    near(total.momentum, p.pressure * p.time(), 2e-14);
    near(total.pressure / (p.gamma - 1) + total.kinetic, p.pressure * p.interface_x / (p.gamma - 1), 3e-14);
    near(p.front_speed(), p.velocity + 2 * std::sqrt(p.gamma * p.pressure / p.rho) / (p.gamma - 1), 1e-15);
}
}
int main(int argc, char** argv) {
    try {
        const Setup setup; oracle(setup);
        if (argc > 1 && std::string(argv[1]) == "--case") {
            require(argc >= 4, "Use --case N Courant [profile.csv]");
            const auto result = run(setup, static_cast<std::size_t>(std::stoul(argv[2])), std::stod(argv[3]), argc > 4 ? argv[4] : "");
            print(setup, result); return 0;
        }
        if (argc > 1 && std::string(argv[1]) == "--refine") {
            const auto maximum = argc > 2 ? static_cast<std::size_t>(std::stoul(argv[2])) : 1024;
            std::vector<Evidence> cases;
            for (std::size_t n = 32; n <= maximum; n *= 2) { cases.push_back(run(setup, n, .025)); print(setup, cases.back()); }
            require(cases.size() >= 3, "Front refinement needs at least three grids");
            const auto& a = cases[cases.size() - 3]; const auto& b = cases[cases.size() - 2]; const auto& c = cases.back();
            const double speed = setup.problem.front_speed();
            const bool front_trend = std::abs(b.front - setup.problem.front(setup.problem.time())) < std::abs(a.front - setup.problem.front(setup.problem.time())) &&
                std::abs(c.front - setup.problem.front(setup.problem.time())) < std::abs(b.front - setup.problem.front(setup.problem.time())) &&
                std::abs(b.recent_speed - speed) < std::abs(a.recent_speed - speed) && std::abs(c.recent_speed - speed) < std::abs(b.recent_speed - speed);
            if (!front_trend) { std::cerr << "FAILED front-position AND front-speed refinement gate; interior/conservation checks do not establish vacuum-front convergence\n"; return 2; }
            std::cout << "Observed front position/speed refinement on these grids; asymptotic/general convergence remains unproved\n";
            return 0;
        }
        const auto coarse = run(setup, 32, .025), middle = run(setup, 64, .025), fine = run(setup, 128, .025);
        for (const auto* result : {&coarse, &middle, &fine}) print(setup, *result);
        require(middle.density_l1 < coarse.density_l1 && fine.density_l1 < middle.density_l1 &&
            middle.pressure_l1 < coarse.pressure_l1 && fine.pressure_l1 < middle.pressure_l1 &&
            middle.velocity_l1 < coarse.velocity_l1 && fine.velocity_l1 < middle.velocity_l1,
            "Bounded interior gas profiles did not refine");
        const auto large_dt = run(setup, 64, .05), small_dt = run(setup, 64, .0125);
        print(setup, large_dt); print(setup, small_dt);
        const double large_change = distance(setup, large_dt, middle), small_change = distance(setup, middle, small_dt);
        std::cout << "Fixed-grid timestep state changes=" << large_change << ',' << small_change << '\n';
        require(small_change < .75 * large_change && std::abs(middle.front - small_dt.front) < .8 * std::abs(large_dt.front - middle.front),
            "Gas profile/front timestep refinement did not approach a fixed-grid limit");
        std::cout << "PASS analytic oracle, bounded interior profiles, fixed-grid timestep consistency, trace/energy/entropy/wall accounting\n";
        std::cout << "Full vacuum-front convergence is a separate --refine gate; this consistency test does not establish it\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
