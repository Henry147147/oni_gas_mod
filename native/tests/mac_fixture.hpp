#pragma once
#include "mixed_atmospheres/mac_parcel.hpp"
#include "mixed_atmospheres/mac_closed_world.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
namespace ma::testing {
using namespace ma;
using namespace ma::experimental;
inline constexpr auto none = std::numeric_limits<std::size_t>::max();
struct Setup {
    SpeciesRegistry registry = SpeciesRegistry::standard();
    std::shared_ptr<const FluidThermodynamics> eos;
    Setup() {
        std::vector<ReferenceMaterial> definitions;
        for (const std::string id : {"nitrogen", "oxygen", "helium", "water"}) {
            const auto& s = registry[registry.index(id)]; ReferenceMaterial m;
            m.id = id; m.phase = s.phase == Phase::gas ? ReferencePhase::ideal_gas : ReferencePhase::elastic_liquid;
            m.molar_mass_kg_per_mol = s.molar_mass; m.cv_j_per_kg_k = s.cv(); m.energy_offset_j_per_kg = s.internal_energy(0);
            m.minimum_temperature_k = 1; m.maximum_temperature_k = 1e6;
            m.zero_pressure_density_kg_per_m3 = s.phase == Phase::gas ? 0 : 1000;
            m.reference_bulk_modulus_pa = s.phase == Phase::gas ? 0 : 2.2e9;
            m.maximum_density_ratio = s.phase == Phase::gas ? 1 : 2;
            m.provenance = "Explicit constant-capacity, zero-expansion test surrogate"; definitions.push_back(std::move(m));
        }
        eos = std::make_shared<const FluidThermodynamics>(registry, "mac-closed-controls-v1", std::move(definitions));
    }
    MixtureInventory mixture(double volume, double p, double t, double water = 0, double oxygen = .22) const {
        std::vector<double> masses(registry.size());
        if (water < 1) {
            const double coefficient = gas_constant * ((1 - oxygen) / registry[registry.index("nitrogen")].molar_mass +
                oxygen / registry[registry.index("oxygen")].molar_mass + 1e-28 / registry[registry.index("helium")].molar_mass);
            const double gas = (1 - water) * volume * p / (coefficient * t);
            masses[registry.index("nitrogen")] = (1 - oxygen) * gas; masses[registry.index("oxygen")] = oxygen * gas;
            masses[registry.index("helium")] = gas * 1e-28;
        }
        masses[registry.index("water")] = water * volume * 1000 * (1 + p / 2.2e9);
        return eos->initialize_at_temperature(masses, t, volume).mixture;
    }
    MacTransportState state(std::size_t w, std::size_t h, bool periodic, bool wet = false, bool corner = false) const {
        MacTransportState s; s.grid = {w, h, 1, 1, periodic, periodic && h > 1, std::vector<std::uint8_t>(w * h)};
        if (corner) s.grid.walls[1 + w] = 1;
        for (std::size_t i = 0; i < w * h; ++i) {
            MacMaterialCell c{MixtureInventory(registry.size()), {}};
            if (!s.grid.walls[i]) {
                c.mixture = mixture(1, 100000, 300, wet ? .2 + .05 * std::sin(static_cast<double>(i)) : 0);
                c.passive_quantities = {{17, c.mixture.mass() * (1 + .125 * static_cast<double>(i))}, {UINT64_C(0x80000000), c.mixture.mass() * 1e-30}};
            }
            s.cells.push_back(std::move(c));
        }
        const auto inertia = mac_face_inertias(registry, s, *eos); s.momentum_x.resize(inertia.x.size()); s.momentum_y.resize(inertia.y.size()); return s;
    }
};
inline const Setup& setup() { static const Setup value; return value; }
inline std::string fingerprint(const MacTransportState& s) {
    std::ostringstream t; t << std::hexfloat << s.grid.width << ' ' << s.grid.height << ' ' << s.grid.spacing_m << ' ' << s.grid.depth_m <<
        ' ' << s.grid.periodic_x << ' ' << s.grid.periodic_y;
    for (auto value : s.grid.walls) t << ' ' << static_cast<int>(value);
    for (const auto& c : s.cells) {
        t << ' ' << c.mixture.energy; for (auto value : c.mixture.masses) t << ' ' << value;
        t << ' ' << c.passive_quantities.size(); for (const auto& p : c.passive_quantities) t << ' ' << p.identity << ' ' << p.quantity;
    }
    for (auto value : s.momentum_x) t << ' ' << value;
    for (auto value : s.momentum_y) t << ' ' << value;
    for (const auto* values : {&s.full_momentum_x, &s.full_momentum_y}) {
        t << " full " << values->size(); for (auto value : *values) t << ' ' << value.mantissa() << ' ' << value.exponent();
    }
    return t.str();
}
inline void export_extended_face(int id, int axis, std::size_t face, MacScaled old_j, MacScaled new_j, MacScaled impulse, MacScaled flux, MacScaled residual) {
    const std::array<MacScaled, 5> fields{old_j, new_j, impulse, flux, residual}; unsigned mask = 0;
    for (std::size_t k = 0; k < fields.size(); ++k) if (fields[k].project().rounded) mask |= 1U << k;
    if (mask == 0) return;
    std::cout << "XFACE " << id << ' ' << axis << ' ' << face << ' ' << mask;
    for (auto field : fields) std::cout << ' ' << field.mantissa() << ' ' << field.exponent();
    std::cout << '\n';
}
inline void export_dual(int id, int axis, const MacDualFlux& flux) {
    std::cout << "DUAL " << id << ' ' << axis << ' ' << flux.left << ' ' << flux.right << ' ' << flux.mass_kg << '\n';
    const auto value = mac_dual_mass(flux);
    if (value.project().rounded) std::cout << "XDUAL " << id << ' ' << axis << ' ' << flux.left << ' ' << flux.right << ' ' << value.mantissa() << ' ' << value.exponent() << '\n';
}
inline void export_face_energy(int id, int axis, std::size_t face, const MacFaceEnergy& values) {
    if (values.rounded_projection_mask == 0) return;
    std::cout << "EFACE " << id << ' ' << axis << ' ' << face << ' ' << values.rounded_projection_mask;
    for (const auto& energy : {values.old_kinetic, values.new_kinetic, values.transported_kinetic, values.mixing_heat, values.wall_heat})
        std::cout << ' ' << energy.mantissa() << ' ' << energy.exponent();
    std::cout << '\n';
}
inline void export_energy_report(int id, const MacTransportReport& report) {
    if (report.rounded_energy_projections == 0) return;
    std::cout << "ECASE " << id << ' ' << report.rounded_energy_projections << ' ' << report.unresolved_heat_updates << ' ' <<
        report.mixing_heat_j << ' ' << report.wall_heat_j;
    for (const auto& energy : {report.energy_residual_wide_j, report.energy_bound_wide_j, report.mixing_heat_wide_j, report.wall_heat_wide_j})
        std::cout << ' ' << energy.mantissa() << ' ' << energy.exponent();
    std::cout << '\n';
}
inline void export_reconstruction(int id, double nominal_volume, const MacParcelPlan& plan) {
    std::cout << "RPLAN " << id << ' ' << nominal_volume << ' ' << plan.eos_backtracks << ' ' << plan.unresolved_stencil_cells << '\n';
    for (std::size_t i = 0; i < plan.retained.size(); ++i) {
        const auto& c = plan.retained[i];
        std::cout << "RCELL " << id << ' ' << i << ' ' << plan.source_volumes_m3[i] << ' ' << plan.retained_volumes_m3[i] << ' ' << c.mixture.energy << ' ' << plan.limiter[i] << '\n';
        for (std::size_t s = 0; s < c.mixture.masses.size(); ++s) if (c.mixture.masses[s] != 0) std::cout << "RMASS " << id << ' ' << i << ' ' << s << ' ' << c.mixture.masses[s] << '\n';
        for (const auto& p : c.passive_quantities) std::cout << "RPASSIVE " << id << ' ' << i << ' ' << p.identity << ' ' << p.quantity << '\n';
    }
    for (const auto& p : plan.parcels) {
        if (!p.full_volume_m3.is_zero()) std::cout << "XVOLUME " << id << ' ' << p.axis << ' ' << p.face << ' ' << p.full_volume_m3.mantissa() << ' ' << p.full_volume_m3.exponent() << '\n';
        if (p.reference_volume_m3 != 0) {
            const auto energy = mac_parcel_quantity(p, p.contents.mixture.energy);
            std::cout << "SPARCEL " << id << ' ' << p.axis << ' ' << p.face << ' ' << p.donor << ' ' << p.target << ' ' << p.volume_m3 << ' ' <<
                p.reference_volume_m3 << ' ' << p.contents.mixture.energy << ' ' << energy.mantissa() << ' ' << energy.exponent() << '\n';
            const auto field = [&](const char* kind, std::uint64_t identity, double value) {
                const auto actual = mac_parcel_quantity(p, value);
                std::cout << kind << ' ' << id << ' ' << p.axis << ' ' << p.face << ' ' << identity << ' ' << value << ' ' << actual.mantissa() << ' ' << actual.exponent() << '\n';
            };
            for (std::size_t s = 0; s < p.contents.mixture.masses.size(); ++s) if (p.contents.mixture.masses[s] != 0) field("SPMASS", s, p.contents.mixture.masses[s]);
            for (const auto& v : p.contents.passive_quantities) field("SPPASSIVE", v.identity, v.quantity);
            continue;
        }
        std::cout << "PARCEL " << id << ' ' << p.axis << ' ' << p.face << ' ' << p.donor << ' ' << p.target << ' ' << p.volume_m3 << ' ' << p.contents.mixture.energy << '\n';
        for (std::size_t s = 0; s < p.contents.mixture.masses.size(); ++s) if (p.contents.mixture.masses[s] != 0)
            std::cout << "PMASS " << id << ' ' << p.axis << ' ' << p.face << ' ' << s << ' ' << p.contents.mixture.masses[s] << '\n';
        for (const auto& v : p.contents.passive_quantities) std::cout << "PPASSIVE " << id << ' ' << p.axis << ' ' << p.face << ' ' << v.identity << ' ' << v.quantity << '\n';
    }
}
inline void export_momentum_reconstruction(int id, const MacTransportReport& r) {
    if (r.x.reconstruction != r.y.reconstruction) throw std::runtime_error("Transport components disagree on momentum mode");
    const bool corrected = r.x.reconstruction == MacMomentumReconstruction::limited_linear;
    std::cout << "MCONFIG " << id << ' ' << corrected << '\n';
    if (!corrected) return;
    const auto scaled = [](MacScaled value) { std::cout << ' ' << value.mantissa() << ' ' << value.exponent(); };
    for (std::size_t i = 0; i < r.advected_internal_energy_j.size(); ++i)
        std::cout << "MRCELL " << id << ' ' << i << ' ' << r.advected_internal_energy_j[i] << '\n';
    for (int axis = 0; axis < 2; ++axis) {
        const auto& f = axis == 0 ? r.x : r.y;
        std::cout << "MRECON " << id << ' ' << axis << ' ' << f.corrections.size() << ' ' << f.limited_corrections << ' '
            << f.unresolved_momentum_corrections << ' ' << f.unresolved_remainder_corrections << '\n';
        for (std::size_t i = 0; i < f.energies.size(); ++i) {
            std::cout << "MRFACE " << id << ' ' << axis << ' ' << i;
            for (const auto& value : {f.upwind_momentum[i], f.velocity_slope_x[i], f.velocity_slope_y[i],
                f.centered_courant_x[i], f.centered_courant_y[i], f.upwind_mixing_heat[i], f.upwind_transported_kinetic[i],
                f.energies[i].composed_energy_bound}) scaled(value);
            std::cout << '\n';
        }
        for (std::size_t i = 0; i < f.fluxes.size(); ++i) {
            const auto& edge = f.fluxes[i];
            std::cout << "MRDUAL " << id << ' ' << axis << ' ' << i << ' ' << edge.left << ' ' << edge.right;
            scaled(mac_dual_mass(edge)); std::cout << '\n';
        }
        for (std::size_t i = 0; i < f.corrections.size(); ++i) {
            const auto& event = f.corrections[i]; const auto& p = event.pair;
            std::cout << "MREVENT " << id << ' ' << axis << ' ' << i << ' ' << event.dual_edge << ' ' << event.transport_axis << ' '
                << p.limiter_roundoff_backoffs << ' ' << p.unresolved_momentum_updates << ' ' << p.unresolved_heat_updates;
            for (const auto& value : {event.predictor_delta_velocity, event.bounded_delta_velocity, p.before_left, p.before_right,
                p.before_heat_left, p.before_heat_right, p.requested_impulse, p.limiter, p.impulse, p.after_left, p.after_right,
                p.after_heat_left, p.after_heat_right, p.energy_flux, p.heat_change_left, p.heat_change_right}) scaled(value);
            std::cout << '\n';
        }
    }
}
inline void export_transport(int id, const MacTransportState& before, const MacTransportAdvance& after, const MacParcelPlan& plan) {
    const auto& g = before.grid; const auto& r = after.report;
    std::cout << "CASE " << id << ' ' << g.width << ' ' << g.height << ' ' << g.periodic_x << ' ' << g.periodic_y << ' ' << r.energy_residual_j << ' ' << r.energy_bound_j << '\n';
    for (std::size_t i = 0; i < g.walls.size(); ++i) std::cout << "WALL " << id << ' ' << i << ' ' << static_cast<int>(g.walls[i]) << '\n';
    for (int endpoint = 0; endpoint < 2; ++endpoint) {
        const auto& s = endpoint == 0 ? before : after.state;
        for (std::size_t i = 0; i < s.cells.size(); ++i) {
            std::cout << "CELL " << id << ' ' << endpoint << ' ' << i << ' ' << s.cells[i].mixture.energy << '\n';
            for (std::size_t k = 0; k < s.cells[i].mixture.masses.size(); ++k) if (s.cells[i].mixture.masses[k] != 0)
                std::cout << "MASS " << id << ' ' << endpoint << ' ' << i << ' ' << k << ' ' << s.cells[i].mixture.masses[k] << '\n';
            for (const auto& p : s.cells[i].passive_quantities) std::cout << "PASSIVE " << id << ' ' << endpoint << ' ' << i << ' ' << p.identity << ' ' << p.quantity << '\n';
        }
    }
    for (int axis = 0; axis < 2; ++axis) {
        const auto& f = axis == 0 ? r.x : r.y;
        const auto& j0 = axis == 0 ? before.momentum_x : before.momentum_y; const auto& j1 = axis == 0 ? after.state.momentum_x : after.state.momentum_y;
        const auto& flux = axis == 0 ? r.primal_mass_fluxes_kg.x : r.primal_mass_fluxes_kg.y;
        const auto full_j0 = axis == 0 ? mac_field_values(before.momentum_x, before.full_momentum_x) : mac_field_values(before.momentum_y, before.full_momentum_y);
        const auto full_j1 = axis == 0 ? mac_field_values(after.state.momentum_x, after.state.full_momentum_x) : mac_field_values(after.state.momentum_y, after.state.full_momentum_y);
        const auto full_flux = axis == 0 ? mac_field_values(r.primal_mass_fluxes_kg.x, r.primal_mass_fluxes_kg.full_x) : mac_field_values(r.primal_mass_fluxes_kg.y, r.primal_mass_fluxes_kg.full_y);
        for (std::size_t i = 0; i < j0.size(); ++i) {
            std::cout << "FACE " << id << ' ' << axis << ' ' << i << ' ' << flux[i] << ' ' << j0[i] << ' ' << j1[i];
            for (const auto value : {f.old_inertia[i], f.new_inertia[i], f.old_kinetic[i], f.new_kinetic[i], f.transported_kinetic[i], f.mixing_heat[i], f.wall_heat[i], f.wall_impulse[i], f.dual_mass_residual[i]}) std::cout << ' ' << value;
            std::cout << '\n';
            export_face_energy(id, axis, i, f.energies[i]);
            export_extended_face(id, axis, i, full_j0[i], full_j1[i], f.full_wall_impulse[i], full_flux[i], f.full_dual_mass_residual[i]);
        }
        for (const auto& v : f.fluxes) export_dual(id, axis, v);
    }
    export_reconstruction(id, g.volume_m3(), plan);
    export_momentum_reconstruction(id, r); export_energy_report(id, r);
}
inline void export_case(int id, const MacTransportState& before, const MacClosedSubstep& result, double dt) {
    const auto& set = setup(); const auto& g = before.grid; const auto& r = result.transport;
    std::cout << "CASE " << id << ' ' << g.width << ' ' << g.height << ' ' << g.periodic_x << ' ' << g.periodic_y << ' ' << r.energy_residual_j << ' ' << r.energy_bound_j << '\n';
    std::cout << "PCASE " << id << ' ' << dt << ' ' << g.spacing_m << ' ' << g.depth_m << ' ' << result.pressure_energy_residual_j << ' ' <<
        result.pressure_energy_bound_j << ' ' << result.energy_residual_j << ' ' << result.energy_bound_j << ' ' <<
        result.pressure_wall_impulse_x << ' ' << result.pressure_wall_impulse_y << '\n';
    for (std::size_t i = 0; i < g.walls.size(); ++i) {
        std::cout << "WALL " << id << ' ' << i << ' ' << static_cast<int>(g.walls[i]) << '\n';
        if (!g.walls[i]) {
            const auto at = static_cast<std::size_t>(std::find(result.active_cells.begin(), result.active_cells.end(), i) - result.active_cells.begin());
            const auto initial = set.eos->evaluate(before.cells[i].mixture, g.volume_m3());
            const auto final = set.eos->evaluate(result.state.cells[i].mixture, g.volume_m3());
            std::cout << "PCELL " << id << ' ' << i << ' ' << before.cells[i].mixture.energy << ' ' << result.material_volumes_m3[i] << ' ' << result.pressure.volume_changes_m3[at] << ' ' <<
                result.pressure.work_pressures_pa[at] << ' ' << result.pressure.endpoint_pressures_pa[at] << ' ' << final.mechanical_pressure_pa << ' ' <<
                final.temperature_k << ' ' << initial.entropy_j_per_k << ' ' << final.entropy_j_per_k << ' ' << result.geometric_residuals_m3[i] << '\n';
        }
    }
    for (int endpoint = 0; endpoint < 2; ++endpoint) {
        const auto& s = endpoint == 0 ? result.pressure_state : result.state;
        for (std::size_t i = 0; i < s.cells.size(); ++i) {
            std::cout << "CELL " << id << ' ' << endpoint << ' ' << i << ' ' << (endpoint == 1 && !result.pre_gravity_internal_energy_j.empty() ? result.pre_gravity_internal_energy_j[i] : s.cells[i].mixture.energy) << '\n';
            for (std::size_t k = 0; k < set.registry.size(); ++k) if (s.cells[i].mixture.masses[k] != 0)
                std::cout << "MASS " << id << ' ' << endpoint << ' ' << i << ' ' << k << ' ' << s.cells[i].mixture.masses[k] << '\n';
            for (const auto& p : s.cells[i].passive_quantities) std::cout << "PASSIVE " << id << ' ' << endpoint << ' ' << i << ' ' << p.identity << ' ' << p.quantity << '\n';
        }
    }
    for (int axis = 0; axis < 2; ++axis) {
        const auto& report = axis == 0 ? r.x : r.y;
        const auto& j_initial = axis == 0 ? before.momentum_x : before.momentum_y;
        const auto& j0 = axis == 0 ? result.pressure_state.momentum_x : result.pressure_state.momentum_y;
        const auto& j1 = axis == 0 ? result.state.momentum_x : result.state.momentum_y;
        const auto& flux = axis == 0 ? result.mass_fluxes_kg.x : result.mass_fluxes_kg.y;
        const auto& sweep = axis == 0 ? result.volume_fluxes_m3.x : result.volume_fluxes_m3.y;
        const auto& map = axis == 0 ? result.pressure_nodes_x : result.pressure_nodes_y;
        const auto initial = axis == 0 ? mac_field_values(before.momentum_x, before.full_momentum_x) : mac_field_values(before.momentum_y, before.full_momentum_y);
        const auto pressure_j = axis == 0 ? mac_field_values(result.pressure_state.momentum_x, result.pressure_state.full_momentum_x) : mac_field_values(result.pressure_state.momentum_y, result.pressure_state.full_momentum_y);
        const auto final_j = axis == 0 ? mac_field_values(result.state.momentum_x, result.state.full_momentum_x) : mac_field_values(result.state.momentum_y, result.state.full_momentum_y);
        const auto full_flux = axis == 0 ? mac_field_values(result.mass_fluxes_kg.x, result.mass_fluxes_kg.full_x) : mac_field_values(result.mass_fluxes_kg.y, result.mass_fluxes_kg.full_y);
        const auto full_sweep = axis == 0 ? mac_field_values(result.volume_fluxes_m3.x, result.volume_fluxes_m3.full_x) : mac_field_values(result.volume_fluxes_m3.y, result.volume_fluxes_m3.full_y);
        for (std::size_t f = 0; f < j0.size(); ++f) {
            std::cout << "PFACE " << id << ' ' << axis << ' ' << f << ' ' << j_initial[f] << ' ' << sweep[f] << ' ' <<
                (map[f] == none ? 0 : result.pressure.velocities_m_per_s[map[f]]) << '\n';
            std::cout << "FACE " << id << ' ' << axis << ' ' << f << ' ' << flux[f] << ' ' << j0[f] << ' ' << j1[f];
            for (const double value : {report.old_inertia[f], report.new_inertia[f], report.old_kinetic[f], report.new_kinetic[f], report.transported_kinetic[f],
                report.mixing_heat[f], report.wall_heat[f], report.wall_impulse[f], report.dual_mass_residual[f]}) std::cout << ' ' << value;
            std::cout << '\n';
            export_face_energy(id, axis, f, report.energies[f]);
            export_extended_face(id, axis, f, pressure_j[f], final_j[f], report.full_wall_impulse[f], full_flux[f], report.full_dual_mass_residual[f]);
            const auto helper = map[f] == none ? MacScaled{} : result.full_pressure_velocities[map[f]];
            const std::array<MacScaled, 3> values{initial[f], full_sweep[f], helper}; unsigned mask = 0;
            for (std::size_t k = 0; k < values.size(); ++k) if (values[k].project().rounded) mask |= 1U << k;
            if (mask != 0) {
                std::cout << "XPFACE " << id << ' ' << axis << ' ' << f << ' ' << mask;
                for (auto value : values) std::cout << ' ' << value.mantissa() << ' ' << value.exponent();
                std::cout << ' ' << (map[f] == none ? 0 : result.root_input_velocities[map[f]]) << ' ' << (map[f] == none ? 0 : result.root_output_velocities[map[f]]) << '\n';
            }
        }
        for (const auto& f : report.fluxes) export_dual(id, axis, f);
    }
    if (result.reconstruction) export_reconstruction(id, g.volume_m3(), *result.reconstruction);
    export_momentum_reconstruction(id, r); export_energy_report(id, r);
    if (result.rounded_energy_projections != 0) {
        std::cout << "EOWNER " << id << ' ' << result.rounded_energy_projections;
        for (const auto& energy : {result.pressure_energy_residual_wide_j, result.pressure_energy_bound_wide_j, result.energy_residual_wide_j, result.energy_bound_wide_j})
            std::cout << ' ' << energy.mantissa() << ' ' << energy.exponent();
        std::cout << '\n';
    }
}
}
