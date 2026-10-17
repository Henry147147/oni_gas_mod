#pragma once
#include "mac_compression_fixture.hpp"
#include "mixed_atmospheres/mac_shock_heat.hpp"

namespace ma::testing {
inline void export_shock_heat_scaled(MacEnergy value) { ma::testing::export_compression_scaled(value); }
inline void export_shock_heat_pair(std::size_t id, const MixtureInventory& l, const MixtureInventory& r, double vl, double vr, const MacShockHeatPair& p) {
    std::cout << "HPAIR " << id << ' ' << vl << ' ' << vr << ' ' << p.equilibrium_iterations << ' ' << p.unresolved_updates;
    for (auto v : {p.before_left_j, p.before_right_j, p.after_left_j, p.after_right_j, p.temperature_left_k, p.temperature_right_k,
        p.after_temperature_left_k, p.after_temperature_right_k, p.capacity_left_j_per_k, p.capacity_right_j_per_k}) std::cout << ' ' << v;
    for (auto v : {p.integrated_conductance, p.relaxation_number, p.fraction, p.equilibrium_transfer, p.transfer, p.energy_residual, p.energy_bound}) export_shock_heat_scaled(v);
    std::cout << ' ' << p.entropy_change_j_per_k << ' ' << p.entropy_roundoff_j_per_k << '\n';
    std::cout << "HEQCERT " << id << ' ' << p.rejected_equilibrium_trials << ' ' << p.equilibrium_temperature_left_k << ' ' << p.equilibrium_temperature_right_k;
    export_shock_heat_scaled(p.equilibrium_temperature_residual); export_shock_heat_scaled(p.equilibrium_temperature_bound); std::cout << '\n';
    for (std::size_t side = 0; side < 2; ++side) {
        const auto& masses = side == 0 ? l.masses : r.masses;
        for (std::size_t i = 0; i < masses.size(); ++i) if (masses[i] != 0) std::cout << "HPMASS " << id << ' ' << side << ' ' << i << ' ' << masses[i] << '\n';
    }
}
inline void export_shock_heat(std::size_t id, const MacTransportState& source, const MacShockHeatAdvance& result, double dt) {
    const auto& set = ma::testing::setup(); const auto& g = source.grid; const auto& report = result.report;
    std::cout << "HGRID " << id << ' ' << g.width << ' ' << g.height << ' ' << g.spacing_m << ' ' << g.depth_m << ' ' << g.periodic_x << ' ' << g.periodic_y << ' ' << dt << ' ' << report.events.size() << ' ' << report.unresolved_updates;
    for (auto v : {report.absolute_transfer, report.energy_residual, report.energy_bound}) export_shock_heat_scaled(v);
    std::cout << ' ' << report.entropy_change_j_per_k << ' ' << report.entropy_roundoff_j_per_k << '\n';
    if (report.law == MacShockHeatLaw::pressure_sensed_viscosity) {
        std::cout << "HMLAW " << id << " 1\n";
        const auto& coefficients = report.compression_coefficients;
        for (std::size_t i = 0; i < source.cells.size(); ++i) {
            std::cout << "HMCP " << id << ' ' << i; export_shock_heat_scaled(report.frozen_conductance_capacity_j_per_k[i]); std::cout << '\n';
            std::cout << "HMSPEED " << id << ' ' << i; export_shock_heat_scaled(coefficients.source_linear_speeds[i]); std::cout << '\n';
            std::cout << "HMPRESSURE " << id << ' ' << i << ' ' << coefficients.source_pressures_pa[i] << '\n';
            for (std::size_t axis = 0; axis < 2; ++axis) {
                std::cout << "HMSENSOR " << id << ' ' << axis << ' ' << i;
                export_shock_heat_scaled(coefficients.source_pressure_sensors[axis][i]); std::cout << '\n';
            }
        }
        for (std::size_t i = 0; i < report.events.size(); ++i) {
            std::cout << "HMDIFFUSION " << id << ' ' << i; export_shock_heat_scaled(report.events[i].diffusion_speed); std::cout << '\n';
        }
    }
    for (std::size_t i = 0; i < report.events.size(); ++i) {
        const auto& e = report.events[i]; const auto key = 1000 + id * 1000 + i;
        auto l = source.cells[e.left].mixture, r = source.cells[e.right].mixture; l.energy = e.pair.before_left_j; r.energy = e.pair.before_right_j;
        export_shock_heat_pair(key, l, r, g.volume_m3(), g.volume_m3(), e.pair);
        std::cout << "HEVENT " << id << ' ' << i << ' ' << e.axis << ' ' << e.face << ' ' << e.left << ' ' << e.right << ' ' << e.sweep << ' ' << key; export_shock_heat_scaled(e.closing_velocity); std::cout << '\n';
    }
    const auto inertia = mac_face_inertias(set.registry, source, *set.eos);
    for (std::size_t i = 0; i < source.cells.size(); ++i) {
        std::cout << "SOLID " << id << ' ' << i << ' ' << static_cast<int>(g.walls[i]) << '\n';
        std::cout << "HCAP " << id << ' ' << i << ' ' << report.frozen_capacity_j_per_k[i] << '\n';
    }
    for (int endpoint = 0; endpoint < 2; ++endpoint) {
        const auto& state = endpoint == 0 ? source : result.state;
        for (std::size_t i = 0; i < state.cells.size(); ++i) if (!g.walls[i]) {
            const auto& c = state.cells[i]; const auto eos = set.eos->evaluate(c.mixture, g.volume_m3());
            std::cout << "SCELL " << id << ' ' << endpoint << ' ' << i << ' ' << c.mixture.energy << ' ' << eos.temperature_k << ' ' << eos.mechanical_pressure_pa << ' ' << eos.entropy_j_per_k << '\n';
            for (std::size_t k = 0; k < c.mixture.masses.size(); ++k) if (c.mixture.masses[k] != 0)
                std::cout << "SMASS " << id << ' ' << endpoint << ' ' << i << ' ' << k << ' ' << c.mixture.masses[k] << '\n';
            for (const auto& p : c.passive_quantities) std::cout << "SPASSIVE " << id << ' ' << endpoint << ' ' << i << ' ' << p.identity << ' ' << p.quantity << '\n';
        }
        for (int axis = 0; axis < 2; ++axis) {
            const auto j = axis == 0 ? mac_field_values(state.momentum_x, state.full_momentum_x) : mac_field_values(state.momentum_y, state.full_momentum_y);
            const auto& m = axis == 0 ? inertia.x : inertia.y;
            for (std::size_t f = 0; f < j.size(); ++f) {
                std::cout << "SFACE " << id << ' ' << axis << ' ' << endpoint << ' ' << f << ' ' << m[f] << ' ' << ma::testing::compression_fixed_face(g, axis, f); export_shock_heat_scaled(j[f]); std::cout << '\n';
            }
        }
    }
}
}
