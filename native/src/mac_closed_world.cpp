#include "mac_game_bridge.hpp"
#include "mixed_atmospheres/mac_closed_world.hpp"
#include "mixed_atmospheres/mac_phase_change.hpp"
#include "mixed_atmospheres/mac_kernel.hpp"
#include "mixed_atmospheres/mac_parcel.hpp"
#include "mac_interval_fallback.hpp"
#include "mac_kernel_detail.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <limits>
#include <map>
#include <numeric>

namespace ma::experimental {
namespace {
constexpr auto none = std::numeric_limits<std::size_t>::max();
constexpr double eps = std::numeric_limits<double>::epsilon();
void require(bool ok, const char* reason) { if (!ok) throw std::invalid_argument(reason); }
void require_diffusion_scope(const SpeciesRegistry& registry,const MacTransportState& state,MacDiffusionSettings settings) {
    if(settings.mode==MacDiffusionMode::disabled)return;
    for(std::size_t i=0;i<state.cells.size();++i)if(!state.grid.walls[i])
        for(std::size_t s=0;s<registry.size();++s)
            require(state.cells[i].mixture.masses[s]==0 || registry[s].phase==Phase::gas,"Gas diffusion requires gas-only occupied cells");
}
MacCompressionLaw split_compression_law(MacCompressionViscosity setting) {
    return setting == MacCompressionViscosity::quadratic ? MacCompressionLaw::quadratic :
        setting == MacCompressionViscosity::monotone_acoustic_quadratic ? MacCompressionLaw::monotone_pressure_sensed_acoustic :
        MacCompressionLaw::pressure_sensed_acoustic;
}
MacShockHeatLaw heat_law(MacShockHeatRegularization setting) {
    switch (setting) {
    case MacShockHeatRegularization::compression_activated: return MacShockHeatLaw::closing_velocity;
    case MacShockHeatRegularization::pressure_sensed_acoustic: return MacShockHeatLaw::pressure_sensed_viscosity;
    case MacShockHeatRegularization::pressure_sensed_series: return MacShockHeatLaw::pressure_sensed_series;
    case MacShockHeatRegularization::monotone_pressure_sensed: return MacShockHeatLaw::monotone_pressure_sensed;
    case MacShockHeatRegularization::none: break;
    }
    throw std::invalid_argument("Disabled or invalid MAC shock-heat law selection");
}
struct Account {
    double hi = 0, lo = 0, scale = 0;
    void add(double value) {
        require_finite(value, "MAC closed account operand");
        const double s = hi + value, z = s - hi, e = (hi - (s - z)) + (value - z);
        const double t = lo + e, h = s + t, q = h - s;
        lo = (s - (h - q)) + (t - q); hi = h; scale += std::abs(value);
        require_finite(hi, "MAC closed account sum"); require_finite(lo, "MAC closed account tail"); require_finite(scale, "MAC closed account scale");
    }
    [[nodiscard]] double value() const { return hi + lo; }
    [[nodiscard]] double bound() const { return 128 * eps * scale; }
    void check(const char* reason) const { if (std::abs(value()) > bound()) throw std::domain_error(reason); }
};
double projected_energy(MacEnergy value, std::size_t& rounded) {
    const auto projection = value.project(); if (projection.rounded) ++rounded;
    return projection.value;
}
bool within_energy_budget(const MacEnergySum& residual, const MacEnergySum& budget) {
    MacEnergySum difference = budget; const int polarity = residual.sign();
    for (std::size_t i = 0; i < residual.size(); ++i)
        difference.add(polarity < 0 ? residual.component(i) : residual.component(i).negated());
    return difference.sign() >= 0;
}
MacScaled product(MacScaled a, MacScaled b) {
    return (b.mantissa() < 0 ? a.negated() : a).shared(b.magnitude(), MacScaled::from_double(1));
}
// Keep the ordinary force arithmetic (and exact represented hydrostatic
// cancellation), extending only products that would underflow. Subnormal
// products are evaluated before projection so their missing bits survive.
MacScaled force_product(double a, double b) {
    const double value = a * b;
    require_finite(value, "MAC closed force product");
    if (a == 0 || b == 0 || std::isnormal(value)) return MacScaled::from_double(value);
    return MacScaled::from_double(a).multiplied(b);
}
bool resolved_update(MacEnergy before, MacEnergy after, MacEnergy update) {
    MacEnergySum residual; residual.add(after); residual.add(before.negated()); residual.add(update.negated());
    MacEnergySum budget; budget.add(update.magnitude().shifted(-47));
    return within_energy_budget(residual, budget);
}
struct Pair { std::size_t left = none, right = none; };
struct Topology {
    std::vector<std::size_t> active, local;
    std::vector<Pair> x, y;
    explicit Topology(const MacGrid& g, const MacTransportState* state = nullptr) : local(g.walls.size(), none) {
        for (std::size_t i = 0; i < g.walls.size(); ++i)
            if (!g.walls[i] && (!state || state->cells[i].mixture.mass() > 0)) { local[i] = active.size(); active.push_back(i); }
        const auto fluid = [&](std::size_t cell) { return g.walls[cell] ? none : cell; };
        x.resize(g.x_width() * g.height); y.resize(g.width * g.y_height());
        for (std::size_t row = 0; row < g.height; ++row) for (std::size_t col = 0; col < g.x_width(); ++col) {
            auto& pair = x[g.x_face(col, row)];
            if (col > 0 || g.periodic_x) pair.left = fluid((col + g.width - 1) % g.width + row * g.width);
            if (col < g.width) pair.right = fluid(col + row * g.width);
        }
        for (std::size_t row = 0; row < g.y_height(); ++row) for (std::size_t col = 0; col < g.width; ++col) {
            auto& pair = y[g.y_face(col, row)];
            if (row > 0 || g.periodic_y) pair.left = fluid(col + ((row + g.height - 1) % g.height) * g.width);
            if (row < g.height) pair.right = fluid(col + row * g.width);
        }
    }
};
void validate_momentum(const std::vector<Pair>& pairs, const std::vector<double>& inertia, const std::vector<MacScaled>& j) {
    require(j.size() == pairs.size(), "MAC closed momentum shape differs");
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        if (pairs[i].left == none || pairs[i].right == none) require(j[i].is_zero(), "MAC closed fixed normal face has momentum");
        static_cast<void>(MacEnergy::kinetic(inertia[i], j[i]).project());
    }
}
MacEnergyAccount energy_difference(const MacTransportState& old, const MacTransportState& next,
    const MacFaceInertias& i0, const MacFaceInertias& i1, double gravity = 0) {
    MacEnergyAccount balance;
    for (std::size_t i = 0; i < old.cells.size(); ++i) {
        account_mac_cell_internal_energy(balance, next.cells[i]); account_mac_cell_internal_energy(balance, old.cells[i], -1);
        if (gravity != 0) {
            const auto potential = MacEnergy::from_double(gravity).multiplied(static_cast<double>(i / old.grid.width) + .5).multiplied(old.grid.spacing_m);
            balance.add(potential.multiplied(next.cells[i].mixture.mass())); balance.add(potential.multiplied(old.cells[i].mixture.mass()).negated());
        }
    }
    for (int axis = 0; axis < 2; ++axis) {
        const auto a = axis == 0 ? mac_field_values(old.momentum_x, old.full_momentum_x) : mac_field_values(old.momentum_y, old.full_momentum_y);
        const auto b = axis == 0 ? mac_field_values(next.momentum_x, next.full_momentum_x) : mac_field_values(next.momentum_y, next.full_momentum_y);
        const auto& mass0 = axis == 0 ? i0.x : i0.y; const auto& mass1 = axis == 0 ? i1.x : i1.y;
        for (std::size_t f = 0; f < a.size(); ++f) { balance.add(MacEnergy::kinetic(mass1[f], b[f])); balance.add(MacEnergy::kinetic(mass0[f], a[f]).negated()); }
    }
    return balance;
}
struct StaticEquilibrium { MacScaled body_force_y; };
std::optional<StaticEquilibrium> exact_static_equilibrium(const SpeciesRegistry& registry, const MacTransportState& state,
    const MacClosedSettings& settings, const FluidThermodynamics& model) {
    for (const auto* values : {&state.momentum_x, &state.momentum_y})
        for (const double value : *values) if (value != 0) return {};
    // A projected zero may retain a nonzero canonical momentum below binary64.
    for (const auto* values : {&state.full_momentum_x, &state.full_momentum_y})
        for (const auto& value : *values) if (!value.is_zero()) return {};
    const Topology topology(state.grid);
    if (topology.active.empty() || !state.occupied_volumes_m3.empty()) return {};
    const auto identical = [&](const Pair& pair) {
        if (pair.left == none || pair.right == none) return true;
        const auto& a=state.cells[pair.left].mixture;const auto& b=state.cells[pair.right].mixture;
        return same_mac_cell_internal_energy(state.cells[pair.left],state.cells[pair.right]) && a.masses==b.masses;
    };
    if (settings.gravity_m_per_s2 == 0) {
        // Compare only physically connected neighbours. Separate sealed
        // regions may have different inventories, pressures and temperatures.
        for (const auto* pairs : {&topology.x,&topology.y})
            for (const auto& pair : *pairs) if (!identical(pair)) return {};
        return StaticEquilibrium{};
    }
    for(const auto& cell:state.cells)if(!cell.energy_remainder_j.is_zero())return {};
    // A hydrostatic certificate requires the actual discrete forces to cancel
    // exactly. No tolerance may silently suppress a small evolving mode.
    std::vector<double> pressure(state.cells.size()),temperature(state.cells.size()),mass(state.cells.size());
    for (const auto i : topology.active) {
        const auto reading=model.evaluate(state.cells[i].mixture,state.grid.volume_m3());
        pressure[i]=reading.mechanical_pressure_pa;temperature[i]=reading.temperature_k;mass[i]=state.cells[i].mixture.mass();
    }
    const double area=state.grid.spacing_m*state.grid.depth_m;
    // Sharp interfaces weigh y faces by their PLIC half-cell masses.
    const auto weights=settings.reconstruction==MacMaterialReconstruction::interface_plic ?
        mac_interface_gravity_weights(registry,model,state) : std::vector<double>{};
    MacScaledSum body;
    for (int axis=0;axis<2;++axis) for (std::size_t f=0;f<(axis==0 ? topology.x : topology.y).size();++f) {
        const auto& pair=(axis==0 ? topology.x : topology.y)[f];
        const double weight=axis==0 ? 0 : !weights.empty() ? weights[f] :
            (pair.left==none ? 0 : .5*mass[pair.left])+(pair.right==none ? 0 : .5*mass[pair.right]);
        if (axis==1) {
            if (!std::isfinite(weight*settings.gravity_m_per_s2)) return {};
            body.add(force_product(-weight,settings.gravity_m_per_s2));
        }
        if (pair.left==none || pair.right==none) continue;
        if (settings.thermal_conduction && temperature[pair.left]!=temperature[pair.right]) return {};
        if (axis==0) { if (pressure[pair.left]!=pressure[pair.right]) return {}; }
        else {
            const double inertia=weight;
            MacScaledSum force;
            force.add(force_product(area,pressure[pair.left]-pressure[pair.right]));
            force.add(force_product(-inertia,settings.gravity_m_per_s2));
            if (force.sign()!=0) return {};
        }
    }
    // Passive amounts may differ: no passive diffusion is implemented. Any
    // future evolving process must be included in this certificate's checks.
    return StaticEquilibrium{body.value()};
}
}
void MacClosedSettings::validate() const {
    if (numerical_kernel==MacNumericalKernel::game) {
        game.validate();
        const bool original = game_profile==MacGameProfile::game_kernel &&
            game.liquid_law==MacGameLiquidLaw::linear &&
            game.pressure_maximum_iterations==200 && game.heat_maximum_iterations==200;
        const bool positive = (game_profile==MacGameProfile::game_kernel_positive_liquid ||
            game_profile==MacGameProfile::game_kernel_free_surface ||
            game_profile==MacGameProfile::game_kernel_trace_heat) &&
            game.liquid_law==MacGameLiquidLaw::positive_segments;
        require((original || positive) && vacuum==MacVacuumRepresentation::void_phase &&
            step_control==MacStepControl::doubling && subcell_void==MacSubcellVoidPolicy::exact_geometry &&
            !compatible_void_pressure_work && !recover_mixed_eos_endpoint && !trace_sink_per_substep && !fast_phase_trial_eos &&
            diffusion.mode==MacDiffusionMode::disabled && phase_change==MacPhaseChange::none &&
            pressure_integration==MacPressureIntegration::discrete_gradient_midpoint &&
            std::isfinite(gravity_m_per_s2) && gravity_m_per_s2>=0 &&
            std::isfinite(maximum_timestep_s) && maximum_timestep_s>0 && maximum_timestep_s<=1 &&
            std::isfinite(trace_sink_threshold_kg) && trace_sink_threshold_kg>=0 && trace_sink_threshold_kg<=1e-6 &&
            preconditioner==PressurePreconditioner::automatic && maximum_outgoing_volume_fraction==.4 &&
            reconstruction==MacMaterialReconstruction::limited_linear && momentum_reconstruction==MacMomentumReconstruction::limited_linear &&
            compression_viscosity==MacCompressionViscosity::acoustic_quadratic && shock_heat==MacShockHeatRegularization::pressure_sensed_acoustic &&
            gas_expansion_volume_fraction==16*eps && viscosity.shear_pa_s==0 && viscosity.bulk_pa_s==0,
            "Invalid diagnostic game-kernel settings");
        return;
    }
    require(game_profile!=MacGameProfile::game_kernel &&
        game_profile!=MacGameProfile::game_kernel_positive_liquid &&
        game_profile!=MacGameProfile::game_kernel_free_surface &&
        game_profile!=MacGameProfile::game_kernel_trace_heat,"Game profile requires numerical kernel3");
    viscosity.validate();
    diffusion.validate();
    require(game_profile == MacGameProfile::base_game_floor ||
        (!compatible_void_pressure_work && !recover_mixed_eos_endpoint && !fast_phase_trial_eos),
        "MAC game numerical policies require a saved game profile");
    require(!trace_sink_per_substep || (game_profile == MacGameProfile::base_game_floor && trace_sink_threshold_kg > 0),
        "Substep trace removal requires an enabled saved game trace sink");
    require(std::isfinite(trace_sink_threshold_kg) && trace_sink_threshold_kg >= 0 && trace_sink_threshold_kg <= 1e-6 &&
        ((game_profile == MacGameProfile::legacy && trace_sink_threshold_kg == 0) ||
         (game_profile == MacGameProfile::base_game_floor && numerical_kernel == MacNumericalKernel::production &&
          step_control == MacStepControl::regional_predictive && vacuum == MacVacuumRepresentation::void_phase &&
          phase_change == MacPhaseChange::none && diffusion.mode == MacDiffusionMode::disabled)),
        "Invalid MAC game profile or trace-sink threshold");
    require(std::isfinite(gas_expansion_volume_fraction) &&
        gas_expansion_volume_fraction >= 16 * eps && gas_expansion_volume_fraction <= 1 &&
        (gas_expansion_volume_fraction == 16 * eps || subcell_void == MacSubcellVoidPolicy::expand_unresolved_gas),
        "Invalid MAC gas-expansion volume fraction");
    require(subcell_void == MacSubcellVoidPolicy::exact_geometry ||
        (subcell_void == MacSubcellVoidPolicy::expand_unresolved_gas &&
            vacuum == MacVacuumRepresentation::void_phase && numerical_kernel == MacNumericalKernel::production && mac_kernel_supports(*this)),
        "Invalid MAC subcell-void policy (unresolved expansion requires supported production void-phase selectors)");
    require(step_control == MacStepControl::doubling || ((step_control == MacStepControl::predictive || step_control == MacStepControl::regional_predictive) &&
        numerical_kernel == MacNumericalKernel::production && mac_kernel_supports(*this)),
        "Invalid MAC step control (predictive control requires supported production selectors)");
    require(phase_change == MacPhaseChange::none || (phase_change == MacPhaseChange::saturation &&
        pressure_integration == MacPressureIntegration::discrete_gradient_midpoint),
        "Invalid MAC phase change (saturation phase change needs midpoint pressure)");
    // Conduction and viscosity through partly filled cells exist in the
    // production kernel only (MAC_VACUUM.md, "Conduction and viscosity").
    const bool physical = numerical_kernel == MacNumericalKernel::production ||
        (!thermal_conduction && viscosity.shear_pa_s == 0 && viscosity.bulk_pa_s == 0);
    require(vacuum == MacVacuumRepresentation::empty_cells || (vacuum == MacVacuumRepresentation::void_phase &&
        reconstruction != MacMaterialReconstruction::donor_cell && pressure_integration == MacPressureIntegration::discrete_gradient_midpoint &&
        physical && diffusion.mode == MacDiffusionMode::disabled),
        "Invalid MAC vacuum representation (the void phase needs reconstructed parcels, midpoint pressure and no diffusion; "
        "conduction and viscosity need production kernel 2)");
    require(pressure_integration == MacPressureIntegration::discrete_gradient_midpoint ||
        ((pressure_integration == MacPressureIntegration::two_moment || pressure_integration == MacPressureIntegration::two_moment_coupled) &&
            numerical_kernel == MacNumericalKernel::production),
        "Invalid MAC pressure integration (two moments require production kernel 2)");
    require(numerical_kernel != MacNumericalKernel::retired_interval_audited, "MAC interval kernel 1 is retired; its checkpoints cannot continue under a different kernel");
    require((numerical_kernel == MacNumericalKernel::reference ||
        numerical_kernel == MacNumericalKernel::production) &&
        std::isfinite(maximum_timestep_s) && maximum_timestep_s > 0 && maximum_timestep_s <= 1 &&
        std::isfinite(maximum_outgoing_volume_fraction) && maximum_outgoing_volume_fraction > 0 && maximum_outgoing_volume_fraction <= .45 &&
        (preconditioner == PressurePreconditioner::automatic || preconditioner == PressurePreconditioner::jacobi ||
         preconditioner == PressurePreconditioner::incomplete_cholesky) && std::isfinite(gravity_m_per_s2) && gravity_m_per_s2 >= 0 &&
        (reconstruction == MacMaterialReconstruction::donor_cell || reconstruction == MacMaterialReconstruction::limited_linear ||
            reconstruction == MacMaterialReconstruction::swept_first_moment || reconstruction == MacMaterialReconstruction::interface_plic) &&
        (momentum_reconstruction == MacMomentumReconstruction::donor_cell || momentum_reconstruction == MacMomentumReconstruction::limited_linear ||
            momentum_reconstruction == MacMomentumReconstruction::quadratic_limited_linear ||
            momentum_reconstruction == MacMomentumReconstruction::swept_physical ||
            momentum_reconstruction == MacMomentumReconstruction::swept_energy_flux ||
            momentum_reconstruction == MacMomentumReconstruction::swept_nodal_1d) &&
        (compression_viscosity == MacCompressionViscosity::none || compression_viscosity == MacCompressionViscosity::quadratic ||
         compression_viscosity == MacCompressionViscosity::acoustic_quadratic ||
         compression_viscosity == MacCompressionViscosity::coupled_pressure ||
         compression_viscosity == MacCompressionViscosity::monotone_acoustic_quadratic) &&
        (shock_heat == MacShockHeatRegularization::none || shock_heat == MacShockHeatRegularization::compression_activated ||
         shock_heat == MacShockHeatRegularization::pressure_sensed_acoustic ||
         shock_heat == MacShockHeatRegularization::pressure_sensed_series ||
         shock_heat == MacShockHeatRegularization::monotone_pressure_sensed), "Invalid MAC closed settings");
}
MacClosedSubstep advance_mac_closed_substep(const SpeciesRegistry& registry, const FluidThermodynamics& model,
    const MacTransportState& source, double dt, const MacClosedSettings& settings) {
    require_mac_energy_environment();
    settings.validate(); require(std::isfinite(dt) && dt > 0 && dt <= settings.maximum_timestep_s, "Invalid MAC closed substep time");
    require(settings.subcell_void == MacSubcellVoidPolicy::exact_geometry,
        "Unresolved gas expansion is available only through the production kernel");
    require(settings.pressure_integration == MacPressureIntegration::discrete_gradient_midpoint,
        "The reference substep cannot execute two-moment pressure integration");
    const auto inertia = mac_face_inertias(registry, source, model); const auto& g = source.grid; const Topology topology(g, &source);
    require(settings.gravity_m_per_s2 == 0 || !g.periodic_y, "Constant MAC gravity requires nonperiodic vertical boundaries");
    const auto jx0 = mac_field_values(source.momentum_x, source.full_momentum_x), jy0 = mac_field_values(source.momentum_y, source.full_momentum_y);
    validate_momentum(topology.x, inertia.x, jx0); validate_momentum(topology.y, inertia.y, jy0);
    MacClosedSubstep result; result.pressure_state = source; result.active_cells = topology.active;
    result.material_volumes_m3.resize(source.cells.size()); result.geometric_residuals_m3.resize(source.cells.size());
    result.pressure_nodes_x.assign(topology.x.size(), none); result.pressure_nodes_y.assign(topology.y.size(), none);
    result.volume_fluxes_m3 = {std::vector<double>(topology.x.size()), std::vector<double>(topology.y.size())}; result.mass_fluxes_kg = result.volume_fluxes_m3;
    if (settings.gravity_m_per_s2 != 0) {
        result.gravity_body_work_y.resize(topology.y.size()); result.gravity_potential_flux_y.resize(topology.y.size()); result.gravity_internal_work_y.resize(topology.y.size());
        result.full_gravity_body_work_y.resize(topology.y.size()); result.full_gravity_potential_flux_y.resize(topology.y.size()); result.full_gravity_internal_work_y.resize(topology.y.size());
        result.pre_gravity_internal_energy_j.resize(source.cells.size()); result.gravity_internal_changes_j.resize(source.cells.size());
        result.pre_gravity_energy_remainders_j.resize(source.cells.size());
        result.full_gravity_internal_changes_j.resize(source.cells.size());
    }
    std::vector<MaterialCell> materials; materials.reserve(topology.active.size());
    const bool voids = settings.vacuum == MacVacuumRepresentation::void_phase;
    require(voids || source.occupied_volumes_m3.empty(), "MAC occupied volumes require the void phase");
    require(!voids || (!settings.thermal_conduction && settings.viscosity.shear_pa_s == 0 && settings.viscosity.bulk_pa_s == 0),
        "The reference void phase runs no conduction or viscosity (production kernel only)");
    for (auto cell : topology.active) materials.push_back({source.cells[cell].mixture, mac_material_volume(source, cell), 0, source.cells[cell].energy_remainder_j});
    // Sharp interfaces weigh each y face by the mass of its two half cells
    // (PLIC split) at the substep start; a kick after transport time-centres
    // that weight (MAC_PRESSURE.md, "Gravity with sharp interfaces").
    const bool sharp_gravity = settings.gravity_m_per_s2 != 0 && settings.reconstruction == MacMaterialReconstruction::interface_plic;
    const auto weight_start = sharp_gravity ? mac_interface_gravity_weights(registry, model, source) : std::vector<double>{};
    const double area = g.spacing_m * g.depth_m; require(std::isfinite(area) && area > 0, "MAC closed area cannot be represented");
    std::vector<OccupiedMaterialNode> nodes;
    for (int axis = 0; axis < 2; ++axis) {
        const auto& pairs = axis == 0 ? topology.x : topology.y; const auto& m = axis == 0 ? inertia.x : inertia.y;
        const auto& j = axis == 0 ? jx0 : jy0; auto& map = axis == 0 ? result.pressure_nodes_x : result.pressure_nodes_y;
        for (std::size_t f = 0; f < pairs.size(); ++f) if (m[f] > 0) {
            map[f] = nodes.size(); const auto& pair = pairs[f];
            const auto velocity = j[f].shared(1, m[f]).project(); if (velocity.rounded) ++result.rounded_pressure_velocity_inputs;
            const double weight_ratio = axis == 1 && sharp_gravity ? weight_start[f] / m[f] : 1;
            nodes.push_back({pair.left == none ? none : topology.local[pair.left], pair.right == none ? none : topology.local[pair.right],
                area, m[f], velocity.value, axis == 0 ? 0 : -settings.gravity_m_per_s2 * weight_ratio, pair.left == none || pair.right == none});
        }
    }
    ReferenceRootOptions work_root;
    // The ordinary 2e-13 inner pressure root can leave work-pressure noise
    // above this caller's 16-epsilon volume residual, particularly at rest.
    // Resolve the nested inversion to adjacent floating-point parameters;
    // retain the original outer geometry and energy acceptance bounds.
    work_root.relative_pressure_tolerance = eps / 4;
    if (!materials.empty()) {
        if (settings.compression_viscosity == MacCompressionViscosity::coupled_pressure) {
            result.compressive_coefficients = freeze_mac_compression_coefficients(registry, model, source,
                MacCompressionLaw::pressure_sensed_acoustic);
            for (const auto cell : topology.active) {
                // An isotropic bulk stress uses the larger of the unchanged
                // directional incoming pressure sensors, not their sum.
                const auto x=result.compressive_coefficients->linear_speed(0,cell);
                const auto y=result.compressive_coefficients->linear_speed(1,cell);
                result.compressive_linear_speeds.push_back(mac_scaled_difference(x,y).mantissa()>=0 ? x : y);
            }
            auto coupled=advance_mac_compressive_pressure(model,materials,nodes,dt,g.spacing_m,
                result.compressive_linear_speeds,settings.preconditioner,16*eps,work_root);
            result.pressure=std::move(coupled.pressure);result.compressive_responses=std::move(coupled.responses);
        } else result.pressure = advance_occupied_material_pressure(model, materials, nodes, dt, settings.preconditioner,
            LiquidFillRoundoffPolicy::strict_eos, 16 * eps, work_root,true);
    }
    result.root_output_velocities = result.pressure.velocities_m_per_s;
    result.root_input_velocities.reserve(nodes.size()); result.full_pressure_velocities.resize(nodes.size());
    for (const auto& node : nodes) result.root_input_velocities.push_back(node.velocity_m_per_s);
    for (int axis = 0; axis < 2; ++axis) {
        const auto& j = axis == 0 ? jx0 : jy0; const auto& m = axis == 0 ? inertia.x : inertia.y;
        const auto& map = axis == 0 ? result.pressure_nodes_x : result.pressure_nodes_y;
        for (std::size_t f = 0; f < map.size(); ++f) if (map[f] != none) {
            const auto at = map[f]; MacScaledSum corrected;
            corrected.add(result.root_output_velocities[at]); corrected.add(j[f].shared(1, m[f])); corrected.add(-result.root_input_velocities[at]);
            result.full_pressure_velocities[at] = mac_scaled_primary(corrected);
            result.pressure.velocities_m_per_s[at] = result.full_pressure_velocities[at].project().value;
        }
    }
    for (std::size_t i = 0; i < topology.active.size(); ++i) {
        const auto cell = topology.active[i]; result.pressure_state.cells[cell].mixture = result.pressure.cells[i].mixture;
        result.pressure_state.cells[cell].energy_remainder_j = result.pressure.cells[i].energy_remainder_j;
        result.material_volumes_m3[cell] = result.pressure.cells[i].volume_m3;
    }
    std::vector<MacScaledSum> sweeps(source.cells.size()), outgoing(source.cells.size()); MacScaledSum body_impulse; MacScaledAccount px, py;
    std::array<MacScaledSum, 2> wall_pressure, wall_body;
    std::vector<MacScaled> jx1 = jx0, jy1 = jy0, qx(topology.x.size()), qy(topology.y.size()), fx(topology.x.size()), fy(topology.y.size());
    for (int axis = 0; axis < 2; ++axis) {
        const auto& pairs = axis == 0 ? topology.x : topology.y; const auto& m = axis == 0 ? inertia.x : inertia.y;
        const auto& old_j = axis == 0 ? jx0 : jy0;
        auto& new_j = axis == 0 ? jx1 : jy1; auto& q = axis == 0 ? qx : qy; auto& flux = axis == 0 ? fx : fy;
        auto& momentum = axis == 0 ? px : py;
        for (std::size_t f = 0; f < pairs.size(); ++f) if (m[f] > 0) {
            const auto& pair = pairs[f];
            const double pl = pair.left == none || topology.local[pair.left] == none ? 0 : result.pressure.work_pressures_pa[topology.local[pair.left]];
            const double pr = pair.right == none || topology.local[pair.right] == none ? 0 : result.pressure.work_pressures_pa[topology.local[pair.right]];
            const double pressure_difference = result.pressure.node_pressure_differences_pa.empty() ? pl - pr
                : result.pressure.node_pressure_differences_pa[(axis == 0 ? result.pressure_nodes_x : result.pressure_nodes_y)[f]];
            const auto gravity_force = axis == 0 ? MacScaled{} : force_product(-(sharp_gravity ? weight_start[f] : m[f]), settings.gravity_m_per_s2);
            const auto gravity_impulse = gravity_force.multiplied(dt);
            MacScaled impulse;
            if (settings.gravity_m_per_s2 == 0) {
                const double value = dt * area * pressure_difference; require_finite(value, "MAC pressure impulse");
                impulse = MacScaled::from_double(value);
            } else {
                const auto pressure_force = force_product(area, pressure_difference);
                const auto body = gravity_force.project(), pressure = pressure_force.project();
                if (!body.rounded && !pressure.rounded) {
                    // Cancellation at a represented hydrostatic balance must
                    // precede multiplication by dt, as in the ordinary solver.
                    const double force = pressure.value + body.value;
                    require_finite(force, "MAC pressure/body force"); impulse = force_product(dt, force);
                } else {
                    MacScaledSum force; force.add(pressure_force); force.add(gravity_force);
                    impulse = mac_scaled_primary(force).multiplied(dt);
                }
            }
            const bool fixed = pair.left == none || pair.right == none;
            MacScaledSum updated; updated.add(old_j[f]); updated.add(impulse); new_j[f] = fixed ? MacScaled{} : mac_scaled_primary(updated);
            if (fixed) {
                // Cancel boundary pressures before their common dt*area factor.
                // Scaling large opposing wall forces separately loses a small
                // net reaction even when the interior gradient is well resolved.
                if (result.pressure.reference_pressures_pa.empty()) {
                    wall_pressure[static_cast<std::size_t>(axis)].add(-pl);
                    wall_pressure[static_cast<std::size_t>(axis)].add(pr);
                } else {
                    for (const auto& field : {&result.pressure.reference_pressures_pa, &result.pressure.work_pressure_changes_pa}) {
                        if (pair.left != none && topology.local[pair.left] != none)
                            wall_pressure[static_cast<std::size_t>(axis)].add(-(*field)[topology.local[pair.left]]);
                        if (pair.right != none && topology.local[pair.right] != none)
                            wall_pressure[static_cast<std::size_t>(axis)].add((*field)[topology.local[pair.right]]);
                    }
                }
                wall_body[static_cast<std::size_t>(axis)].add(gravity_impulse.negated());
                momentum.add(impulse);
            }
            else if (!impulse.is_zero() && !resolved_update(old_j[f], new_j[f], impulse)) ++result.unresolved_momentum_updates;
            momentum.add(new_j[f]); momentum.add(old_j[f].negated()); momentum.add(gravity_impulse.negated());
            if (axis == 1) body_impulse.add(gravity_impulse);
            if (axis == 1 && settings.gravity_m_per_s2 != 0) {
                MacScaledSum midpoint; midpoint.add(old_j[f]); midpoint.add(new_j[f]);
                result.full_gravity_body_work_y[f] = product(gravity_impulse, midpoint.value().shifted(-1).shared(1, m[f]));
                result.gravity_body_work_y[f] = projected_energy(result.full_gravity_body_work_y[f], result.rounded_gravity_projections);
            }
            if (fixed) continue;
            MacScaledSum midpoint; midpoint.add(old_j[f]); midpoint.add(new_j[f]);
            q[f] = midpoint.value().shifted(-1).shared(1, m[f]).multiplied(dt).multiplied(area);
            sweeps[pair.left].add(q[f]); sweeps[pair.right].add(q[f].negated());
            if (!q[f].is_zero()) {
                const auto donor = q[f].mantissa() > 0 ? pair.left : pair.right;
                outgoing[donor].add(q[f].magnitude());
                if (settings.reconstruction == MacMaterialReconstruction::donor_cell)
                    flux[f] = q[f].shared(source.cells[donor].mixture.mass(), result.material_volumes_m3[donor]);
            }
        }
    }
    store_mac_field(result.pressure_state.momentum_x, result.pressure_state.full_momentum_x, jx1);
    store_mac_field(result.pressure_state.momentum_y, result.pressure_state.full_momentum_y, jy1);
    // Empty cells contribute geometry, never a pressure unknown or invented
    // inertia. Their swept empty volume closes the same fixed-grid rezone.
    for (std::size_t cell = 0; cell < source.cells.size(); ++cell)
        if (!g.walls[cell] && topology.local[cell] == none) {
            auto volume = sweeps[cell]; volume.add(g.volume_m3());
            result.material_volumes_m3[cell] = store_mac_primary(volume, true);
            if (!(result.material_volumes_m3[cell] > 0)) throw std::domain_error("MAC swept vacuum volume exhausted");
        }
    result.volume_fluxes_m3 = mac_fluxes(qx, qy); result.mass_fluxes_kg = mac_fluxes(fx, fy);
    const auto wall_reaction = [&](std::size_t axis) {
        MacScaledSum reaction; reaction.add(wall_pressure[axis].value().multiplied(dt).multiplied(area));
        reaction.add(wall_body[axis].value()); return mac_scaled_primary(reaction);
    };
    result.full_pressure_wall_impulse_x = wall_reaction(0); result.full_pressure_wall_impulse_y = wall_reaction(1);
    result.pressure_wall_impulse_x = result.full_pressure_wall_impulse_x.project().value;
    result.pressure_wall_impulse_y = result.full_pressure_wall_impulse_y.project().value;
    result.full_body_impulse_y = body_impulse.value();
    result.body_impulse_y = projected_energy(result.full_body_impulse_y, result.rounded_gravity_projections);
    if (!px.within_bound() || !py.within_bound()) throw std::domain_error("MAC pressure full momentum plus wall reaction failed");
    MacEnergyAccount compressive_work;
    if(settings.compression_viscosity==MacCompressionViscosity::coupled_pressure) {
        result.full_compressive_volume_changes_m3.resize(source.cells.size());
        result.full_compressive_internal_work_j.resize(source.cells.size());
    }
    // Void phase: a partly filled cell's swept volume holds its material
    // volume and its unchanged void (the face sweeps move the material).
    std::vector<double> held_volumes;
    if (voids) {
        result.swept_volumes_m3 = result.material_volumes_m3; held_volumes = result.material_volumes_m3;
        for (std::size_t cell = 0; cell < source.cells.size(); ++cell) if (topology.local[cell] == none) held_volumes[cell] = 0;
        for (std::size_t cell = 0; cell < source.cells.size(); ++cell)
            if (!g.walls[cell] && topology.local[cell] != none && mac_material_volume(source, cell) < g.volume_m3()) {
                auto volume = sweeps[cell]; volume.add(g.volume_m3());
                result.swept_volumes_m3[cell] = std::max(store_mac_primary(volume, true), result.material_volumes_m3[cell]);
            }
    }
    for (std::size_t cell = 0; cell < source.cells.size(); ++cell) if (!g.walls[cell]) {
        MacScaledSum limit; limit.add(MacScaled::from_double((voids ? result.swept_volumes_m3 : result.material_volumes_m3)[cell])
            .multiplied(settings.maximum_outgoing_volume_fraction));
        for (std::size_t k = 0; k < outgoing[cell].size(); ++k) limit.add(outgoing[cell].component(k).negated());
        if (limit.sign() < 0)
            throw std::domain_error("MAC pressure material-volume CFL exceeded");
        MacScaledAccount geometry; geometry.add(result.material_volumes_m3[cell]);
        geometry.add(-(topology.local[cell] == none ? g.volume_m3() : mac_material_volume(source, cell)));
        for (std::size_t k = 0; k < sweeps[cell].size(); ++k) geometry.add(sweeps[cell].component(k).negated());
        if (!geometry.within_bound()) throw std::domain_error("MAC pressure full material-volume closure exceeds its operand bound");
        result.geometric_residuals_m3[cell] = geometry.residual().project().value;
        if(settings.compression_viscosity==MacCompressionViscosity::coupled_pressure && topology.local[cell] != none) {
            const auto at=topology.local[cell];MacEnergySum requested;
            const double work_pressure=result.pressure.work_pressures_pa[at];
            for(std::size_t k=0;k<sweeps[cell].size();++k)
                requested.add(sweeps[cell].component(k).multiplied(-work_pressure));
            const auto work=mac_scaled_primary(requested);
            result.full_compressive_volume_changes_m3[cell]=mac_scaled_primary(sweeps[cell]);
            result.full_compressive_internal_work_j[cell]=work;
            MacEnergyAccount local;
            account_mac_cell_internal_energy(local,result.pressure_state.cells[cell]);account_mac_cell_internal_energy(local,source.cells[cell],-1);
            for(std::size_t k=0;k<requested.size();++k)local.add(requested.component(k).negated());
            if(!local.within_bound())throw std::domain_error("Coupled pressure actual full cell work exceeds its operand bound");
            account_mac_cell_internal_energy(compressive_work,result.pressure_state.cells[cell]);account_mac_cell_internal_energy(compressive_work,source.cells[cell],-1);
            for(std::size_t k=0;k<requested.size();++k)compressive_work.add(requested.component(k).negated());
            if(!work.is_zero() && !mac_cell_internal_energy_resolved(source.cells[cell],result.pressure_state.cells[cell],work))
                ++result.unresolved_compressive_work_updates;
        }
    }
    result.full_compressive_work_residual_j=compressive_work.residual();
    result.full_compressive_work_bound_j=compressive_work.bound();
    auto pressure_balance = energy_difference(source, result.pressure_state, inertia, inertia);
    if (settings.gravity_m_per_s2 != 0) for (const auto& work : result.full_gravity_body_work_y) pressure_balance.add(work.negated());
    if (!pressure_balance.within_bound()) throw std::domain_error("MAC pressure represented energy exceeds its operand bound");
    result.pressure_energy_residual_wide_j = pressure_balance.residual(); result.pressure_energy_bound_wide_j = pressure_balance.bound();
    result.pressure_energy_residual_j = projected_energy(result.pressure_energy_residual_wide_j, result.rounded_energy_projections);
    result.pressure_energy_bound_j = projected_energy(result.pressure_energy_bound_wide_j, result.rounded_energy_projections);
    MacTransportAdvance transported;
    if (settings.reconstruction != MacMaterialReconstruction::donor_cell) {
        const auto reconstruct = settings.reconstruction == MacMaterialReconstruction::swept_first_moment ? reconstruct_mac_swept_parcels :
            settings.reconstruction == MacMaterialReconstruction::interface_plic ? reconstruct_mac_interface_parcels : reconstruct_mac_parcels;
        auto plan = voids ? reconstruct(registry, model, result.pressure_state, result.swept_volumes_m3, result.volume_fluxes_m3,
                settings.maximum_outgoing_volume_fraction, held_volumes) :
            reconstruct(registry, model, result.pressure_state, result.material_volumes_m3, result.volume_fluxes_m3, settings.maximum_outgoing_volume_fraction, {});
        transported = advance_mac_parcel_transport(registry, result.pressure_state, model, plan, settings.momentum_reconstruction);
        result.mass_fluxes_kg = std::move(transported.report.primal_mass_fluxes_kg); result.reconstruction = std::move(plan);
    } else transported = advance_mac_transport(registry, result.pressure_state, result.mass_fluxes_kg, model,
        result.material_volumes_m3, settings.momentum_reconstruction);
    result.transport = std::move(transported.report); result.state = std::move(transported.state);
    // Time-centred sharp-interface weight: the pressure stage applied
    // -g W_start dt; each y face now receives -g (W_end - W_start) dt / 2.
    // It vanishes at rest, so hydrostatic balance is unchanged. Its body work
    // joins the gravity reconciliation; boundary faces pass it to the wall.
    MacEnergy kick_bound;
    if (sharp_gravity) {
        const auto weight_end = mac_interface_gravity_weights(registry, model, result.state);
        const auto end_inertia = mac_face_inertias(registry, result.state, model);
        auto jy = mac_field_values(result.state.momentum_y, result.state.full_momentum_y);
        const auto before = result.state; std::vector<MacEnergy> works;
        MacScaledSum kicks, reactions;
        for (std::size_t f = 0; f < topology.y.size(); ++f) {
            const double difference = weight_end[f] - weight_start[f]; require_finite(difference, "MAC sharp gravity weight change");
            if (difference == 0) continue;
            const auto& pair = topology.y[f];
            // Void phase: a weight change beyond the face's remaining inertia means
            // its material has left, so the correction is skipped (production rule).
            if (voids && pair.left != none && pair.right != none && end_inertia.y[f] > 0 && std::abs(difference) > end_inertia.y[f]) continue;
            const auto kick = force_product(-difference, settings.gravity_m_per_s2).multiplied(.5 * dt);
            kicks.add(kick);
            if (pair.left == none || pair.right == none || !(end_inertia.y[f] > 0)) { reactions.add(kick.negated()); continue; }
            MacScaledSum updated; updated.add(jy[f]); updated.add(kick); const auto next = mac_scaled_primary(updated);
            if (!resolved_update(jy[f], next, kick)) ++result.unresolved_momentum_updates;
            MacScaledSum midpoint; midpoint.add(jy[f]); midpoint.add(next);
            const auto work = product(kick, midpoint.value().shifted(-1).shared(1, end_inertia.y[f]));
            works.push_back(work);
            MacEnergySum total; total.add(result.full_gravity_body_work_y[f]); total.add(work);
            result.full_gravity_body_work_y[f] = mac_scaled_primary(total);
            result.gravity_body_work_y[f] = projected_energy(result.full_gravity_body_work_y[f], result.rounded_gravity_projections);
            jy[f] = next;
        }
        store_mac_field(result.state.momentum_y, result.state.full_momentum_y, jy);
        MacScaledSum body; body.add(result.full_body_impulse_y); body.add(mac_scaled_primary(kicks));
        result.full_body_impulse_y = mac_scaled_primary(body);
        result.body_impulse_y = projected_energy(result.full_body_impulse_y, result.rounded_gravity_projections);
        MacScaledSum wall; wall.add(result.full_pressure_wall_impulse_y); wall.add(mac_scaled_primary(reactions));
        result.full_pressure_wall_impulse_y = mac_scaled_primary(wall);
        result.pressure_wall_impulse_y = result.full_pressure_wall_impulse_y.project().value;
        auto balance = energy_difference(before, result.state, end_inertia, end_inertia);
        for (const auto& work : works) balance.add(work.negated());
        if (!balance.within_bound()) throw std::domain_error("MAC sharp-interface gravity kick energy exceeds its operand bound");
        kick_bound = balance.bound();
    }
    if (settings.gravity_m_per_s2 != 0) {
        MacEnergyAccount potential, gravity_internal;
        MacEnergySum change;
        Account entropy, entropy_roundoff, gravity_entropy;
        std::vector<MacEnergySum> correction(source.cells.size());
        const auto flux = mac_field_values(result.mass_fluxes_kg.y, result.mass_fluxes_kg.full_y);
        for (std::size_t f = 0; f < topology.y.size(); ++f) {
            const auto& pair = topology.y[f];
            const auto potential_flux = flux[f].multiplied(settings.gravity_m_per_s2).multiplied(g.spacing_m);
            result.full_gravity_potential_flux_y[f] = potential_flux;
            result.gravity_potential_flux_y[f] = projected_energy(potential_flux, result.rounded_gravity_projections);
            MacEnergySum work_terms;
            work_terms.add(result.full_gravity_body_work_y[f].negated()); work_terms.add(potential_flux.negated());
            const auto work = mac_scaled_primary(work_terms);
            result.full_gravity_internal_work_y[f] = work;
            result.gravity_internal_work_y[f] = projected_energy(work, result.rounded_gravity_projections);
            potential.add(potential_flux.negated());
            gravity_internal.add(result.full_gravity_body_work_y[f]); gravity_internal.add(potential_flux);
            if (work.is_zero()) continue;
            const double left = pair.left == none ? 0 : result.state.cells[pair.left].mixture.mass();
            const double right = pair.right == none ? 0 : result.state.cells[pair.right].mixture.mass();
            const double mass = left + right; require_finite(mass, "MAC gravity sharing mass");
            for (const auto cell : {pair.left, pair.right}) if (cell != none)
                correction[cell].add(work.shared(result.state.cells[cell].mixture.mass(), mass));
        }
        for (std::size_t i = 0; i < source.cells.size(); ++i) {
            auto& cell = result.state.cells[i]; const auto previous = cell; const double before = cell.mixture.energy;
            const auto update = mac_scaled_primary(correction[i]);
            result.pre_gravity_internal_energy_j[i] = before;
            result.pre_gravity_energy_remainders_j[i] = previous.energy_remainder_j;
            auto local = correction[i]; account_mac_cell_internal_energy(local, cell);
            const auto write = store_mac_cell_internal_energy(cell, local,&result.primary_energy);
            result.rounded_gravity_projections += static_cast<std::size_t>(write.high_rounded);
            result.full_gravity_internal_changes_j[i] = update;
            result.gravity_internal_changes_j[i] = projected_energy(update, result.rounded_gravity_projections);
            change.add(update); account_mac_cell_internal_energy(gravity_internal,cell); account_mac_cell_internal_energy(gravity_internal,previous,-1);
            if (!update.is_zero() && !mac_cell_internal_energy_resolved(previous,cell,update))
                ++result.unresolved_gravity_internal_updates;
            const auto phi = MacEnergy::from_double(settings.gravity_m_per_s2).multiplied(static_cast<double>(i / g.width) + .5).multiplied(g.spacing_m);
            potential.add(phi.multiplied(cell.mixture.mass())); potential.add(phi.multiplied(source.cells[i].mixture.mass()).negated());
            if (!g.walls[i]) {
                const auto old_eos = model.evaluate(source.cells[i].mixture,
                    source.cells[i].mixture.mass() > 0 ? mac_material_volume(source, i) : g.volume_m3());
                const auto next_eos = model.evaluate(cell.mixture, cell.mixture.mass() > 0 ? mac_material_volume(result.state, i) : g.volume_m3());
                entropy.add(next_eos.entropy_j_per_k); entropy.add(-old_eos.entropy_j_per_k);
                if(cell.mixture.mass()>0)
                    entropy.add(mac_stored_remainder_value(cell.energy_remainder_j).shared(1,next_eos.temperature_k).project().value);
                if(source.cells[i].mixture.mass()>0)
                    entropy.add(mac_stored_remainder_value(source.cells[i].energy_remainder_j).shared(1,old_eos.temperature_k).negated().project().value);
                if (!same_mac_cell_internal_energy(previous,cell) && cell.mixture.mass() > 0) {
                    // Entropy is concave in U: S(U) - S(before) <= (U - before) / T(before).
                    auto pivot = cell.mixture; pivot.energy = before;
                    const double t = model.evaluate(pivot, mac_material_volume(result.state, i)).temperature_k;
                    gravity_entropy.add(mac_cell_internal_energy_change(previous,cell).value().shared(1,t).project().value);
                }
                if (source.cells[i].mixture.mass() > 0 && cell.mixture.mass() > 0)
                    entropy_roundoff.add(128 * eps * (std::abs(source.cells[i].mixture.energy) + std::abs(before) + std::abs(cell.mixture.energy)) /
                        std::min(old_eos.temperature_k, next_eos.temperature_k));
                else {
                    if (source.cells[i].mixture.mass() > 0)
                        entropy_roundoff.add(128 * eps * std::abs(source.cells[i].mixture.energy) / old_eos.temperature_k);
                    if (cell.mixture.mass() > 0)
                        entropy_roundoff.add(128 * eps * (std::abs(before) + std::abs(cell.mixture.energy)) / next_eos.temperature_k);
                }
            }
        }
        if (!potential.within_bound()) throw std::domain_error("MAC potential-energy mass transport exceeds its operand bound");
        if (!gravity_internal.within_bound()) throw std::domain_error("MAC represented gravity internal change exceeds its operand bound");
        result.potential_transport_residual_wide_j = potential.residual(); result.potential_transport_bound_wide_j = potential.bound();
        result.gravity_internal_residual_wide_j = gravity_internal.residual(); result.gravity_internal_bound_wide_j = gravity_internal.bound();
        result.potential_transport_residual_j = projected_energy(potential.residual(), result.rounded_gravity_projections);
        result.potential_transport_bound_j = projected_energy(potential.bound(), result.rounded_gravity_projections);
        result.gravity_internal_residual_j = projected_energy(gravity_internal.residual(), result.rounded_gravity_projections);
        result.gravity_internal_bound_j = projected_energy(gravity_internal.bound(), result.rounded_gravity_projections);
        result.gravity_internal_change_wide_j = change.value();
        result.gravity_internal_change_j = projected_energy(change.value(), result.rounded_gravity_projections);
        result.entropy_change_j_per_k = entropy.value(); result.entropy_roundoff_j_per_k = entropy_roundoff.value();
        result.gravity_entropy_bound_j_per_k = gravity_entropy.value(); result.gravity_entropy_operands_j_per_k = gravity_entropy.scale;
    }
    const auto total = energy_difference(source, result.state, inertia, mac_face_inertias(registry, result.state, model), settings.gravity_m_per_s2);
    MacEnergySum budget; budget.add(result.pressure_energy_bound_wide_j); budget.add(result.transport.energy_bound_wide_j);
    budget.add(result.gravity_internal_bound_wide_j); budget.add(result.potential_transport_bound_wide_j); budget.add(kick_bound);
    if (!within_energy_budget(total.terms(), budget)) throw std::domain_error("MAC composed energy exceeds the stage operand bounds");
    result.energy_residual_wide_j = total.residual(); result.energy_bound_wide_j = budget.value();
    result.rounded_energy_projections += result.transport.rounded_energy_projections;
    result.energy_residual_j = projected_energy(result.energy_residual_wide_j, result.rounded_energy_projections);
    result.energy_bound_j = projected_energy(result.energy_bound_wide_j, result.rounded_energy_projections);
    return result;
}
MacClosedWorld::MacClosedWorld(SpeciesRegistry registry, std::shared_ptr<const FluidThermodynamics> model,
    MacTransportState state, MacClosedSettings settings,std::shared_ptr<const GasTransportProfile> transport) : registry_(std::move(registry)),
    thermodynamics_(model ? std::make_shared<const FluidThermodynamics>(*model) : nullptr), settings_(settings),transport_(std::move(transport)) {
    require_mac_energy_environment();
    require(thermodynamics_ != nullptr && transport_!=nullptr, "MAC closed owner requires immutable EOS and transport profiles"); settings_.validate();
    require(settings_.vacuum == MacVacuumRepresentation::void_phase || state.occupied_volumes_m3.empty(), "MAC occupied volumes require the void phase");
    require(settings_.phase_change == MacPhaseChange::none || !mac_saturation_pairs(registry_, *thermodynamics_).empty(),
        "MAC saturation phase change requires a calibrated liquid/vapour pair");
    const auto inertia = settings_.numerical_kernel==MacNumericalKernel::game ?
        MacGameKernelAccess::inertias(registry_,state) : mac_face_inertias(registry_, state, *thermodynamics_); const Topology topology(state.grid);
    if(settings_.numerical_kernel==MacNumericalKernel::game)MacGameKernelAccess::validate(*this,state);
    // A void-phase state whose occupied cells are all filled has no occupied volumes.
    bool partial = false;
    for (std::size_t i = 0; i < state.occupied_volumes_m3.size(); ++i)
        partial = partial || (state.cells[i].mixture.mass() > 0 && state.occupied_volumes_m3[i] < state.grid.volume_m3());
    if (!partial) state.occupied_volumes_m3.clear();
    require_diffusion_scope(registry_,state,settings_.diffusion);
    require(settings_.gravity_m_per_s2 == 0 || !state.grid.periodic_y, "Constant MAC gravity requires nonperiodic vertical boundaries");
    validate_momentum(topology.x, inertia.x, mac_field_values(state.momentum_x, state.full_momentum_x));
    validate_momentum(topology.y, inertia.y, mac_field_values(state.momentum_y, state.full_momentum_y));
    replace_state(std::move(state));
}
MacClosedWorld::MacClosedWorld(Derived, const MacClosedWorld& from, MacTransportState state) : registry_(from.registry_),
    thermodynamics_(from.thermodynamics_), settings_(from.settings_), transport_(from.transport_), time_s_(from.time_s_),
    revision_(from.revision_ + 1) {
    bool partial = false;
    for (std::size_t i = 0; i < state.occupied_volumes_m3.size() && !partial; ++i)
        partial = state.cells[i].mixture.mass() > 0 && state.occupied_volumes_m3[i] < state.grid.volume_m3();
    if (!partial) state.occupied_volumes_m3.clear();
    replace_state(std::move(state));
}
MacClosedWorld::MacClosedWorld(Derived, const MacClosedWorld& from, std::shared_ptr<const MacTransportState> state) :
    registry_(from.registry_), thermodynamics_(from.thermodynamics_), state_snapshot_(std::move(state)), settings_(from.settings_),
    transport_(from.transport_), time_s_(from.time_s_), revision_(from.revision_ + 1) {
    if (from.revision_ == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("MAC closed revision overflow");
}
void MacClosedWorld::replace_state(MacTransportState next) {
    // A mutable object: an expiring owner that alone holds it may edit it in
    // place (MacGameKernelAccess).
    state_snapshot_ = std::make_shared<MacTransportState>(std::move(next));
}
MacClosedWorld MacClosedWorld::derived(MacTransportState next) const {
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("MAC closed revision overflow");
    return MacClosedWorld(Derived{}, *this, std::move(next));
}
MacTraceSink MacClosedWorld::with_trace_sink() const {
    if(settings_.numerical_kernel==MacNumericalKernel::game)return MacGameKernelAccess::trace(*this);
    require_mac_energy_environment();
    const double threshold=settings_.trace_sink_threshold_kg;
    if (settings_.game_profile==MacGameProfile::legacy || threshold==0) return {*this,{}};
    // Avoid a full state copy on an unchanged inventory.
    bool any=false;
    for (std::size_t i=0;i<state().cells.size() && !any;++i) if (!state().grid.walls[i])
        for (const double mass : state().cells[i].mixture.masses) if (mass>0 && mass<threshold) { any=true; break; }
    if (!any) return {*this,{}};
    auto next=state(); const auto& grid=next.grid;
    MacTraceSinkReport report;
    std::vector<MacScaledSum> removed_mass(registry_.size());
    MacEnergySum removed_internal,removed_potential,removed_kinetic;
    MacScaledSum removed_px,removed_py;
    std::map<std::uint64_t,MacScaledSum> removed_passives;
    std::map<std::uint64_t,MacScaledAccount> passive_accounts;
    MacEnergyAccount energy_account; MacScaledAccount px_account,py_account;
    std::vector<double> old_mass(next.cells.size()),new_mass(next.cells.size());
    std::vector<std::uint8_t> changed(next.cells.size());
    const auto& materials=thermodynamics_->reference().materials();
    std::vector<double> density(registry_.size()),capacity(registry_.size()),offset(registry_.size());
    for (std::size_t k=0;k<registry_.size();++k)
        for (const auto& material : materials) if (material.id==registry_[k].id) {
            density[k]=material.zero_pressure_density_kg_per_m3;
            capacity[k]=material.cv_j_per_kg_k; offset[k]=material.energy_offset_j_per_kg; break;
        }
    for (std::size_t i=0;i<next.cells.size();++i) {
        auto& cell=next.cells[i]; old_mass[i]=new_mass[i]=cell.mixture.mass();
        if (grid.walls[i] || old_mass[i]==0) continue;
        for (std::size_t k=0;k<registry_.size();++k) {
            const double mass=cell.mixture.masses[k];
            if (mass>0 && mass<threshold) {
                removed_mass[k].add(mass); cell.mixture.masses[k]=0;
                changed[i]=1; ++report.constituents;
            }
        }
        if (!changed[i]) continue;
        ++report.cells;
        new_mass[i]=cell.mixture.mass();
        const auto& before=state().cells[i];
        MacEnergySum old_u; account_mac_cell_internal_energy(old_u,before);
        if (new_mass[i]==0) { cell.mixture.energy=0; cell.energy_remainder_j={}; }
        else {
            // Keep chemical offsets with their constituents. Sharing the
            // remaining energy by heat capacity preserves ideal-mixture T;
            // elastic energy is shared by that same numerical trace rule.
            MacScaledSum old_cv,new_cv; MacEnergySum sensible=old_u,retained;
            for (std::size_t k=0;k<registry_.size();++k) {
                if (before.mixture.masses[k]==0) continue;
                const auto old_m=MacScaled::from_double(before.mixture.masses[k]);
                const auto new_m=MacScaled::from_double(cell.mixture.masses[k]);
                old_cv.add(old_m.multiplied(capacity[k])); new_cv.add(new_m.multiplied(capacity[k]));
                sensible.add(old_m.multiplied(-offset[k])); retained.add(new_m.multiplied(offset[k]));
            }
            require(old_cv.sign()>0 && new_cv.sign()>0,"Trace sink retained material has no heat capacity");
            const auto retained_capacity=new_cv.value(),total_capacity=old_cv.value();
            for (std::size_t k=0;k<sensible.size();++k)
                retained.add(sensible.component(k).shared(retained_capacity,total_capacity));
            (void)store_mac_cell_internal_energy(cell,retained,&report.primary_energy);
        }
        MacEnergySum taken_u; account_mac_cell_internal_energy(taken_u,before);
        account_mac_cell_internal_energy(taken_u,cell,-1);
        for (std::size_t k=0;k<taken_u.size();++k) removed_internal.add(taken_u.component(k));
        account_mac_cell_internal_energy(energy_account,before);
        account_mac_cell_internal_energy(energy_account,cell,-1);
        // Book the actual stored passive difference, including all of an empty
        // cell's inventory; a sub-ulp fraction leaves that passive unchanged.
        cell.passive_quantities.clear();
        for (const auto& passive : before.passive_quantities) {
            const double kept=new_mass[i]==0 ? 0 : MacScaled::from_double(passive.quantity).shared(new_mass[i],old_mass[i]).project().value;
            if (kept>0) cell.passive_quantities.push_back({passive.identity,kept});
            removed_passives[passive.identity].add(passive.quantity);
            removed_passives[passive.identity].add(-kept);
            passive_accounts[passive.identity].add(passive.quantity);
            passive_accounts[passive.identity].add(-kept);
        }
        const double height=(static_cast<double>(i/grid.width)+.5)*grid.spacing_m;
        const auto potential=[&](double mass) { return MacEnergy::from_double(mass).multiplied(settings_.gravity_m_per_s2).multiplied(height); };
        removed_potential.add(potential(old_mass[i])); removed_potential.add(potential(new_mass[i]).negated());
        energy_account.add(potential(old_mass[i])); energy_account.add(potential(new_mass[i]).negated());
        if (next.occupied_volumes_m3.empty()) {
            next.occupied_volumes_m3.resize(next.cells.size());
            for (std::size_t j=0;j<next.cells.size();++j)
                if (!grid.walls[j] && next.cells[j].mixture.mass()>0) next.occupied_volumes_m3[j]=grid.volume_m3();
        }
        if (new_mass[i]==0) next.occupied_volumes_m3[i]=0;
        else {
            bool gas=false; double liquid_volume=0;
            for (std::size_t k=0;k<registry_.size();++k) if (cell.mixture.masses[k]>0) {
                if (registry_[k].phase==Phase::gas) gas=true;
                else {
                    require(density[k]>0,"Trace sink liquid has no density profile");
                    liquid_volume+=cell.mixture.masses[k]/density[k];
                }
            }
            if (!gas) next.occupied_volumes_m3[i]=std::min(grid.volume_m3(),liquid_volume);
        }
    }
    // Scale each canonical face once using simultaneous endpoint masses. The
    // sink carries actual J and K differences, preserving the retained wind.
    for (std::size_t axis=0;axis<2;++axis) {
        const auto original=axis==0 ? mac_field_values(state().momentum_x,state().full_momentum_x) : mac_field_values(state().momentum_y,state().full_momentum_y);
        auto momenta=original;
        for (std::size_t f=0;f<momenta.size();++f) {
            const auto x=axis==0 ? f%grid.x_width() : f%grid.width;
            const auto y=axis==0 ? f/grid.x_width() : f/grid.width;
            auto first=std::numeric_limits<std::size_t>::max(),second=first;
            if (axis==0) {
                if (x || grid.periodic_x) first=(x+grid.width-1)%grid.width+y*grid.width;
                if (x<grid.width) second=x+y*grid.width;
            } else {
                if (y || grid.periodic_y) first=x+((y+grid.height-1)%grid.height)*grid.width;
                if (y<grid.height) second=x+y*grid.width;
            }
            const auto absent=std::numeric_limits<std::size_t>::max();
            if (first!=absent && grid.walls[first]) first=absent;
            if (second!=absent && grid.walls[second]) second=absent;
            if (!(first!=absent && changed[first]) && !(second!=absent && changed[second])) continue;
            double old_i=0,new_i=0;
            for (const auto i : {first,second}) if (i!=absent) { old_i+=.5*old_mass[i]; new_i+=.5*new_mass[i]; }
            if (old_i==new_i || original[f].is_zero()) continue;
            require(old_i>0 && new_i>=0,"Trace sink face has invalid inertia");
            momenta[f]=new_i==0 ? MacScaled{} : original[f].shared(new_i,old_i);
            auto& carried=axis==0 ? removed_px : removed_py;
            auto& account=axis==0 ? px_account : py_account;
            carried.add(original[f]); carried.add(momenta[f].negated());
            account.add(original[f]); account.add(momenta[f].negated());
            const auto old_k=MacEnergy::kinetic(old_i,original[f]),new_k=MacEnergy::kinetic(new_i,momenta[f]);
            removed_kinetic.add(old_k); removed_kinetic.add(new_k.negated());
            energy_account.add(old_k); energy_account.add(new_k.negated());
        }
        if (axis==0) store_mac_field(next.momentum_x,next.full_momentum_x,momenta);
        else store_mac_field(next.momentum_y,next.full_momentum_y,momenta);
    }
    report.mass_kg.reserve(registry_.size());
    for (const auto& mass : removed_mass) report.mass_kg.push_back(mass.value());
    report.internal_energy_j=removed_internal.value(); report.kinetic_energy_j=removed_kinetic.value();
    report.potential_energy_j=removed_potential.value();
    report.momentum_x=removed_px.value(); report.momentum_y=removed_py.value();
    for (const auto& [identity,amount] : removed_passives) if (amount.sign()>0)
        report.passives.push_back({identity,amount.value().require_representable()});
    energy_account.add(report.internal_energy_j.negated()); energy_account.add(report.kinetic_energy_j.negated());
    energy_account.add(report.potential_energy_j.negated());
    px_account.add(report.momentum_x.negated()); py_account.add(report.momentum_y.negated());
    require(energy_account.within_bound() && px_account.within_bound() && py_account.within_bound(),
        "Trace sink U+K+G/momentum account failed");
    for (std::size_t k=0;k<registry_.size();++k) {
        MacScaledAccount mass_account;
        for (std::size_t i=0;i<next.cells.size();++i) if (changed[i] && state().cells[i].mixture.masses[k]!=next.cells[i].mixture.masses[k]) {
            mass_account.add(state().cells[i].mixture.masses[k]); mass_account.add(-next.cells[i].mixture.masses[k]);
        }
        mass_account.add(report.mass_kg[k].negated());
        require(mass_account.within_bound(),"Trace sink constituent account failed");
    }
    for (const auto& passive : report.passives) passive_accounts[passive.identity].add(-passive.quantity);
    for (const auto& [identity,account] : passive_accounts) {
        (void)identity; require(account.within_bound(),"Trace sink passive account failed");
    }
    // EOS validation is performed by the next ordinary stage. This operation
    // deliberately does not evaluate a trace that is about to leave the world.
    return {derived(std::move(next)),std::move(report)};
}

MacPhaseRelaxation MacClosedWorld::with_phase_relaxation(std::span<const MacSaturationPair> pairs, double fraction) const {
    require_mac_energy_environment();
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("MAC closed revision overflow");
    auto advanced = advance_mac_phase_change(registry_, *thermodynamics_, state(), pairs, false, fraction, settings_.fast_phase_trial_eos);
    auto candidate=derived(std::move(advanced.state));
    return {std::move(candidate), advanced.report};
}
MacPhaseRelaxation MacClosedWorld::with_phase_relaxation_in_cells(std::span<const MacSaturationPair> pairs,
    std::span<const std::size_t> cells, double fraction) const {
    require_mac_energy_environment();
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("MAC closed revision overflow");
    auto advanced = advance_mac_phase_change_in_cells(registry_, *thermodynamics_, state(), pairs, cells, fraction, settings_.fast_phase_trial_eos);
    return {derived(std::move(advanced.state)), advanced.report};
}
MacClosedWorld MacClosedWorld::with_cell_heat(std::span<const MacCellHeat> edits,MacPrimaryRoundingCounts* accepted_rounding) && {
    if(settings_.numerical_kernel!=MacNumericalKernel::game)return std::as_const(*this).with_cell_heat(edits,accepted_rounding);
    auto next=MacGameKernelAccess::heat(std::move(*this),edits);if(accepted_rounding)*accepted_rounding={};return next;
}
MacClosedWorld MacClosedWorld::with_cell_heat(std::span<const MacCellHeat> edits,MacPrimaryRoundingCounts* accepted_rounding) const& {
    if(settings_.numerical_kernel==MacNumericalKernel::game){auto next=MacGameKernelAccess::heat(*this,edits);if(accepted_rounding)*accepted_rounding={};return next;}
    require_mac_energy_environment();
    require(!edits.empty() && edits.size() <= 4096, "MAC heat batch must contain 1..4096 edits");
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("MAC closed revision overflow");
    auto next=state(); MacEnergyAccount balance; MacPrimaryRoundingCounts rounding;
    struct Receipt {
        MacMaterialCell before, after; // only U and its remainder are needed
        MacPrimaryRoundingCounts rounding;
        std::exception_ptr error;
    };
    std::vector<Receipt> receipts(edits.size());
    std::vector<std::size_t> order(edits.size()); std::iota(order.begin(),order.end(),0);
    std::stable_sort(order.begin(),order.end(),[&](std::size_t a,std::size_t b) {
        return edits[a].cell_index < edits[b].cell_index;
    });
    std::vector<std::size_t> groups;
    for (std::size_t n=0;n<order.size();++n)
        if (n==0 || edits[order[n]].cell_index!=edits[order[n-1]].cell_index) groups.push_back(n);
    const auto options=mac_kernel_default_options();
    const auto threads=static_cast<unsigned>(std::min<std::size_t>(options.threads,groups.size()));
    // Distinct cells are independent; repeated edits of one cell retain their
    // original sequence, including every intermediate EOS/temperature check.
    mac_kernel::pool_for(threads).run(groups.size(),[&](std::size_t group) {
        const auto stop=group+1<groups.size() ? groups[group+1] : order.size();
        for (auto n=groups[group];n<stop;++n) {
            const auto index=order[n]; const auto& edit=edits[index]; auto& receipt=receipts[index];
            try {
                require(edit.cell_index < next.cells.size() && !next.grid.walls[edit.cell_index] &&
                    std::isfinite(edit.joules) && std::isfinite(edit.maximum_temperature_k) && edit.maximum_temperature_k > 0,
                    "Invalid MAC fluid heat exchange");
                auto& cell=next.cells[edit.cell_index];
                receipt.before.mixture.energy=cell.mixture.energy; receipt.before.energy_remainder_j=cell.energy_remainder_j;
                (void)add_mac_cell_internal_energy(cell,MacEnergy::from_double(edit.joules),&receipt.rounding);
                if (edit.joules != 0 && same_mac_cell_internal_energy(receipt.before,cell))
                    throw FluidFatalError(FluidFatalCode::numerical_precision,"MAC heat exchange cannot be represented in cell U and remainder");
                const auto reading=thermodynamics_->evaluate(cell.mixture,cell.mixture.mass() > 0 ? mac_material_volume(next,edit.cell_index) : next.grid.volume_m3());
                require(edit.joules <= 0 || reading.temperature_k <= edit.maximum_temperature_k,
                    "MAC heat exchange exceeds its reserved temperature limit");
                receipt.after.mixture.energy=cell.mixture.energy; receipt.after.energy_remainder_j=cell.energy_remainder_j;
            } catch (...) { receipt.error=std::current_exception(); break; }
        }
    });
    // Keep the audit operand order and report the first invalid input edit,
    // regardless of which worker finished first. Failed candidates stay local.
    for (std::size_t n=0;n<edits.size();++n) {
        const auto& receipt=receipts[n]; if (receipt.error) std::rethrow_exception(receipt.error);
        account_mac_cell_internal_energy(balance,receipt.before); balance.add(edits[n].joules);
        account_mac_cell_internal_energy(balance,receipt.after,-1);
        rounding.merge(receipt.rounding);
    }
    require(balance.within_bound(), "MAC heat exchange energy account failed");
    auto candidate=derived(std::move(next));
    if(accepted_rounding)*accepted_rounding=rounding;
    return candidate;
}
MacClosedWorld MacClosedWorld::with_diffusion(MacDiffusionSettings setting) const {
    setting.validate();
    require_diffusion_scope(registry_,state(),setting);
    if(revision_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("MAC revision overflow");
    auto candidate=*this;candidate.settings_.diffusion=setting;++candidate.revision_;return candidate;
}
namespace {
void combine(double& a,double b) { a+=b; }
void combine(std::size_t& a,std::size_t b) { a+=b; }
void combine(MacEnergy& a,MacEnergy b) { MacEnergySum sum;sum.add(a);sum.add(b);a=sum.value(); }
void combine_report(MacClosedAdvanceReport& a,const MacClosedAdvanceReport& b) {
    a.trace_sink.merge(b.trace_sink);
    a.primary_energy.merge(b.primary_energy);
    // Every MacClosedAdvanceReport field is accumulated here; a field added to
    // the report must be added to this list (main's pressure-root restarts were
    // initially missed when the diffusion composition was merged).
#define MA_ADD(field) combine(a.field,b.field)
    MA_ADD(substeps);MA_ADD(attempts);MA_ADD(rejected_attempts);MA_ADD(exact_equilibrium_intervals);
    MA_ADD(interval_kernel_intervals);MA_ADD(interval_reference_fallbacks);MA_ADD(interval_audit_exact_fallbacks);
    MA_ADD(interval_audit_retries);MA_ADD(interval_rounded_updates);MA_ADD(interval_unresolved_updates);
    MA_ADD(interval_multigrid_solves);MA_ADD(interval_multigrid_cycles);MA_ADD(interval_multigrid_fallbacks);MA_ADD(interval_topology_builds);
    MA_ADD(pressure_iterations);MA_ADD(nonlinear_iterations);MA_ADD(pressure_increment_restarts);MA_ADD(unresolved_heat_updates);MA_ADD(unresolved_momentum_updates);
    MA_ADD(unresolved_gravity_internal_updates);MA_ADD(compression_events);MA_ADD(unresolved_compression_heat_updates);MA_ADD(unresolved_compression_momentum_updates);
    MA_ADD(unresolved_compressive_work_updates);
    MA_ADD(momentum_correction_events);MA_ADD(limited_momentum_corrections);MA_ADD(unresolved_momentum_corrections);MA_ADD(unresolved_remainder_corrections);
    MA_ADD(conduction_stages);MA_ADD(conduction_contacts);MA_ADD(conduction_iterations);MA_ADD(unresolved_conduction_updates);
    MA_ADD(conductive_redistribution_j);MA_ADD(conductive_entropy_j_per_k);MA_ADD(viscosity_stages);MA_ADD(viscosity_modes);
    MA_ADD(unresolved_viscous_heat_updates);MA_ADD(unresolved_viscous_momentum_updates);MA_ADD(viscous_heat);MA_ADD(viscous_stored_heat);
    MA_ADD(viscous_wall_impulse_x);MA_ADD(viscous_wall_impulse_y);MA_ADD(viscous_entropy_lower_bound_j_per_k);
    MA_ADD(energy_residual_j);MA_ADD(energy_bound_j);MA_ADD(mixing_heat_j);MA_ADD(wall_heat_j);MA_ADD(wall_impulse_x);MA_ADD(wall_impulse_y);
    MA_ADD(full_wall_impulse_x);MA_ADD(full_wall_impulse_y);MA_ADD(gravity_internal_change_j);MA_ADD(body_impulse_y);
    MA_ADD(gravity_internal_change_wide_j);MA_ADD(full_body_impulse_y);MA_ADD(rounded_gravity_projections);MA_ADD(entropy_change_j_per_k);MA_ADD(entropy_roundoff_j_per_k);
    MA_ADD(gravity_entropy_bound_j_per_k);
    MA_ADD(energy_residual_wide_j);MA_ADD(energy_bound_wide_j);MA_ADD(mixing_heat_wide_j);MA_ADD(wall_heat_wide_j);MA_ADD(remap_work_wide_j);
    MA_ADD(physical_admission_backtracks);MA_ADD(compression_heat_wide_j);MA_ADD(compression_heat_j);MA_ADD(rounded_energy_projections);
    MA_ADD(shock_heat_stages);MA_ADD(shock_heat_events);MA_ADD(shock_heat_root_iterations);MA_ADD(rejected_shock_heat_trials);MA_ADD(unresolved_shock_heat_updates);
    MA_ADD(phase_change_stages);MA_ADD(evaporating_cells);MA_ADD(condensing_cells);MA_ADD(evaporated_kg);MA_ADD(condensed_kg);
    MA_ADD(phase_change_entropy_j_per_k);
    MA_ADD(subcell_void_events);MA_ADD(subcell_void_entropy_j_per_k);
    MA_ADD(absolute_shock_heat_transfer_wide_j);MA_ADD(absolute_shock_heat_transfer_j);
#undef MA_ADD
    a.maximum_geometric_residual_m3=std::max(a.maximum_geometric_residual_m3,b.maximum_geometric_residual_m3);
    a.interval_multigrid_levels=std::max(a.interval_multigrid_levels,b.interval_multigrid_levels);
}
void combine_diffusion(MacDiffusionReport& a,const MacDiffusionReport& b) {
    a.primary_energy.merge(b.primary_energy);
    a.substeps+=b.substeps;a.rejected_attempts+=b.rejected_attempts;a.faces+=b.faces;a.unresolved_updates+=b.unresolved_updates;
    a.exchanged_mass_kg+=b.exchanged_mass_kg;a.elapsed_ms+=b.elapsed_ms;combine(a.energy_residual,b.energy_residual);combine(a.energy_bound,b.energy_bound);
    a.rounded_mass_transfers+=b.rounded_mass_transfers;a.rounded_energy_transfers+=b.rounded_energy_transfers;
    combine(a.energy_transfer_roundoff,b.energy_transfer_roundoff);
    a.species_rounded_transfers.resize(std::max(a.species_rounded_transfers.size(),b.species_rounded_transfers.size()));
    a.species_transfer_roundoff.resize(std::max(a.species_transfer_roundoff.size(),b.species_transfer_roundoff.size()));
    for(std::size_t k=0;k<b.species_rounded_transfers.size();++k) { a.species_rounded_transfers[k]+=b.species_rounded_transfers[k];combine(a.species_transfer_roundoff[k],b.species_transfer_roundoff[k]); }
    a.merge_out_of_range(b);
    a.entropy_change_j_per_k+=b.entropy_change_j_per_k;a.entropy_roundoff_j_per_k+=b.entropy_roundoff_j_per_k;
    a.species_mass_residual.resize(b.species_mass_residual.size());a.species_mass_bound.resize(b.species_mass_bound.size());
    for(std::size_t i=0;i<b.species_mass_residual.size();++i) { a.species_mass_residual[i]+=b.species_mass_residual[i];a.species_mass_bound[i]+=b.species_mass_bound[i]; }
}
}
void MacTraceSinkReport::merge(const MacTraceSinkReport& other) {
    constituents+=other.constituents; cells+=other.cells;
    primary_energy.merge(other.primary_energy);
    if (!other.mass_kg.empty()) {
        if (mass_kg.empty()) mass_kg.resize(other.mass_kg.size());
        require(mass_kg.size()==other.mass_kg.size(),"Trace sink registry coverage differs");
        for (std::size_t k=0;k<mass_kg.size();++k) combine(mass_kg[k],other.mass_kg[k]);
    }
    combine(internal_energy_j,other.internal_energy_j); combine(kinetic_energy_j,other.kinetic_energy_j);
    combine(potential_energy_j,other.potential_energy_j); combine(momentum_x,other.momentum_x); combine(momentum_y,other.momentum_y);
    add_passive_quantities(passives,other.passives,1);
}
MacClosedAdvance MacClosedWorld::advanced(double seconds,std::size_t budget,MacExecutionPolicy policy) const {
    if(settings_.numerical_kernel==MacNumericalKernel::game)return MacGameKernelAccess::advance(*this,seconds,budget);
    if (settings_.game_profile==MacGameProfile::legacy || settings_.trace_sink_threshold_kg==0)
        return advanced_without_trace_sink(seconds,budget,policy);
    require(std::isfinite(seconds) && seconds>0 && seconds<=10 && budget>0 && budget<=65536,"Invalid MAC trace-sink interval/budget");
    auto before=with_trace_sink();
    auto result=before.world.advanced_without_trace_sink(seconds,budget,policy);
    auto after=result.world.with_trace_sink();
    result.world=std::move(after.world);
    result.report.trace_sink.merge(before.report); result.report.trace_sink.merge(after.report);
    result.report.primary_energy.merge(before.report.primary_energy); result.report.primary_energy.merge(after.report.primary_energy);
    return result;
}
MacClosedAdvance MacClosedWorld::advanced_without_trace_sink(double seconds,std::size_t budget,MacExecutionPolicy policy) const {
    // Persist an explicit production request before any diffusion/certificate
    // path. Restoring then advances the same kernel without a caller policy.
    if (policy == MacExecutionPolicy::production && settings_.numerical_kernel != MacNumericalKernel::production) {
        auto selected = *this; selected.settings_.numerical_kernel = MacNumericalKernel::production;
        return selected.advanced(seconds, budget, policy);
    }
    if(settings_.diffusion.mode==MacDiffusionMode::disabled)return advanced_flow(seconds,budget,policy);
    require_mac_energy_environment();settings_.validate();
    require(std::isfinite(seconds) && seconds>0 && seconds<=10 && budget>0 && budget<=65536,"Invalid MAC diffusion interval/budget");
    require(policy==MacExecutionPolicy::reference || policy==MacExecutionPolicy::exact_equilibrium ||
        policy==MacExecutionPolicy::production,"Invalid MAC execution policy");
    require(revision_!=std::numeric_limits<std::uint64_t>::max(),"MAC revision overflow");
    MacClosedAdvance result{*this,{}};const double endpoint=time_s_+seconds;
    require(std::isfinite(endpoint) && endpoint>time_s_,"MAC clock cannot advance");
    std::size_t used=0;
    while(result.world.time_s_<endpoint) {
        const double dt=std::min(.1,endpoint-result.world.time_s_);
        const auto diffuse=[&] {
            if(mac_diffusion_stationary(registry_,result.world.state()))return;
            if(used>=budget)throw std::runtime_error("MAC diffusion interval exhausted its shared attempt budget");
            auto next=advance_mac_diffusion(registry_,*thermodynamics_,result.world.state(),.5*dt,settings_.diffusion,*transport_,budget-used,
                settings_.numerical_kernel==MacNumericalKernel::production ? MacDiffusionRounding::counted:MacDiffusionRounding::reference);
            used+=next.report.substeps+next.report.rejected_attempts;
            result.report.interval_rounded_updates+=next.report.rounded_mass_transfers+next.report.rounded_energy_transfers;
            result.world.replace_state(std::move(next.state));combine_diffusion(result.report.diffusion,next.report);
        };
        diffuse();
        if(used>=budget)throw std::runtime_error("MAC diffusion interval exhausted its shared attempt budget");
        auto flow=result.world.advanced_flow(dt,budget-used,policy);used+=flow.report.attempts;
        result.world=std::move(flow.world);combine_report(result.report,flow.report);diffuse();
    }
    result.world.revision_=revision_+1;
    combine(result.report.energy_residual_wide_j,result.report.diffusion.energy_residual);
    combine(result.report.energy_bound_wide_j,result.report.diffusion.energy_bound);
    result.report.energy_residual_j=result.report.energy_residual_wide_j.project().value;
    result.report.energy_bound_j=result.report.energy_bound_wide_j.project().value;
    result.report.entropy_change_j_per_k+=result.report.diffusion.entropy_change_j_per_k;
    result.report.entropy_roundoff_j_per_k+=result.report.diffusion.entropy_roundoff_j_per_k;
    const auto before_inertia=mac_face_inertias(registry_,state(),*thermodynamics_);
    const auto after_inertia=mac_face_inertias(registry_,result.world.state(),*thermodynamics_);
    const auto balance=energy_difference(state(),result.world.state(),before_inertia,after_inertia,settings_.gravity_m_per_s2);
    MacEnergySum allowance;allowance.add(result.report.energy_bound_wide_j);
    if(!within_energy_budget(balance.terms(),allowance))throw std::runtime_error("Diffusion/flow composed energy account failed");
    return result;
}
MacClosedAdvance MacClosedWorld::advanced_flow(double seconds, std::size_t budget, MacExecutionPolicy policy, bool validation_reference) const {
    require_mac_energy_environment();
    require(std::isfinite(seconds) && seconds > 0 && seconds <= 10 && budget > 0 && budget <= 65536, "Invalid MAC interval or attempt budget");
    require(policy == MacExecutionPolicy::reference || policy == MacExecutionPolicy::exact_equilibrium ||
        policy == MacExecutionPolicy::production, "Invalid MAC execution policy");
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("MAC closed revision overflow");
    const double endpoint = time_s_ + seconds;
    if (!std::isfinite(endpoint) || endpoint <= time_s_) throw FluidFatalError(FluidFatalCode::numerical_precision, "MAC closed time advance cannot be represented");
    auto equilibrium=!validation_reference &&
        (policy!=MacExecutionPolicy::reference || settings_.numerical_kernel==MacNumericalKernel::production) &&
        (settings_.diffusion.mode==MacDiffusionMode::disabled || mac_diffusion_stationary(registry_,state())) ?
        exact_static_equilibrium(registry_,state(),settings_,*thermodynamics_) : std::nullopt;
    MacScaled equilibrium_body;
    if (equilibrium) equilibrium_body=equilibrium->body_force_y.multiplied(seconds);
    if (equilibrium) {
        MacClosedAdvance result{*this,{}};
        result.world.time_s_=endpoint; result.world.revision_=revision_+1;
        // One certified interval, with no pressure/transport stages executed.
        result.report.substeps=1; result.report.attempts=1; result.report.exact_equilibrium_intervals=1;
        result.report.full_body_impulse_y=equilibrium_body;
        result.report.body_impulse_y=projected_energy(equilibrium_body,result.report.rounded_gravity_projections);
        result.report.full_wall_impulse_y=equilibrium_body.negated();result.report.wall_impulse_y=-result.report.body_impulse_y;
        return result;
    }
    // An explicit production execution request takes the production kernel;
    // otherwise the persisted numerical-kernel setting applies.
    if (!validation_reference && (policy == MacExecutionPolicy::production || settings_.numerical_kernel == MacNumericalKernel::production) &&
        mac_kernel_supports(settings_)) {
        auto produced = advance_mac_production_flow(*this, seconds, budget, mac_kernel_default_options());
        // The production kernel converts sharp boiling interfaces each substep
        // (with conduction on); its interval is followed by one equilibrium
        // relaxation of every other pair cell (operator splitting per interval).
        if (settings_.phase_change == MacPhaseChange::saturation) {
            auto changed = advance_mac_phase_change(registry_, *thermodynamics_, produced.world.state(), mac_saturation_pairs(registry_, *thermodynamics_),
                settings_.thermal_conduction);
            produced.world.replace_state(std::move(changed.state)); ++produced.report.phase_change_stages;
            produced.report.evaporating_cells += changed.report.evaporating_cells; produced.report.condensing_cells += changed.report.condensing_cells;
            produced.report.evaporated_kg += changed.report.evaporated_kg; produced.report.condensed_kg += changed.report.condensed_kg;
            produced.report.phase_change_entropy_j_per_k += changed.report.entropy_change_j_per_k;
            produced.report.entropy_change_j_per_k += changed.report.entropy_change_j_per_k;
            produced.report.entropy_roundoff_j_per_k += changed.report.entropy_allowance_j_per_k;
        }
        return produced;
    }
    require(settings_.pressure_integration == MacPressureIntegration::discrete_gradient_midpoint,
        "Two-moment pressure integration cannot continue through the legacy reference path");
    require(state().grid.closed_x.empty() && state().grid.closed_y.empty(), "Closed faces need the production kernel");
    MacClosedAdvance result{*this, {}}; MacEnergySum bounds, mix, wall, compression_heat, conductive_redistribution, shock_heat_transfer, remap_work;
    if (settings_.numerical_kernel != MacNumericalKernel::reference) result.report.interval_reference_fallbacks = 1;
    Account conductive_entropy,viscous_entropy;
    MacEnergySum viscous_heat,viscous_stored_heat;
    MacScaledSum wx, wy,viscous_wx,viscous_wy,body_impulse; MacEnergySum gravity_change; Account gravity_entropy, gravity_entropy_operands;
    const auto initial_inertia = mac_face_inertias(registry_, state(), *thermodynamics_);
    const auto saturation_pairs = settings_.phase_change == MacPhaseChange::saturation ?
        mac_saturation_pairs(registry_, *thermodynamics_) : std::vector<MacSaturationPair>{};
    double next_dt = settings_.maximum_timestep_s;
    while (result.world.time_s_ < endpoint) {
        double dt = std::min(next_dt, endpoint - result.world.time_s_);
        bool accepted = false;
        while (!accepted) {
            if (result.report.attempts == budget) throw std::runtime_error("MAC closed interval exhausted its shared attempt budget");
            ++result.report.attempts;
            if (!(dt > 0) || result.world.time_s_ + dt <= result.world.time_s_)
                throw FluidFatalError(FluidFatalCode::numerical_precision, "MAC closed retry time cannot be represented");
            try {
                MacCompressionReport before_compression, after_compression; MacTransportState prepared;
                MacThermalReport before_conduction, after_conduction;
                MacViscousReport before_viscosity, after_viscosity;
                MacShockHeatReport before_shock_heat, after_shock_heat;
                const MacTransportState* pressure_source = &result.world.state();
                if (settings_.viscosity.shear_pa_s>0 || settings_.viscosity.bulk_pa_s>0) {
                    auto damped=advance_mac_viscosity(registry_,*thermodynamics_,*pressure_source,.5*dt,settings_.viscosity);
                    before_viscosity=std::move(damped.report);prepared=std::move(damped.state);pressure_source=&prepared;
                }
                if (settings_.thermal_conduction) {
                    auto conducted=advance_mac_conduction(registry_,*thermodynamics_,*pressure_source,.5*dt);
                    before_conduction=std::move(conducted.report); prepared=std::move(conducted.state); pressure_source=&prepared;
                }
                if (settings_.shock_heat != MacShockHeatRegularization::none) {
                    auto heated = advance_mac_shock_heat(registry_, *thermodynamics_, *pressure_source, .5 * dt,
                        heat_law(settings_.shock_heat));
                    before_shock_heat = std::move(heated.report); prepared = std::move(heated.state); pressure_source = &prepared;
                }
                if (settings_.compression_viscosity != MacCompressionViscosity::none &&
                    settings_.compression_viscosity != MacCompressionViscosity::coupled_pressure) {
                    auto damped = advance_mac_compression(registry_, *thermodynamics_, *pressure_source, .5 * dt,
                        split_compression_law(settings_.compression_viscosity));
                    before_compression = std::move(damped.report); prepared = std::move(damped.state); pressure_source = &prepared;
                }
                auto candidate = advance_mac_closed_substep(registry_, *thermodynamics_, *pressure_source, dt, settings_);
                if (settings_.compression_viscosity != MacCompressionViscosity::none &&
                    settings_.compression_viscosity != MacCompressionViscosity::coupled_pressure) {
                    auto damped = advance_mac_compression(registry_, *thermodynamics_, candidate.state, .5 * dt,
                        split_compression_law(settings_.compression_viscosity));
                    after_compression = std::move(damped.report); candidate.state = std::move(damped.state);
                }
                if (settings_.shock_heat != MacShockHeatRegularization::none) {
                    auto heated = advance_mac_shock_heat(registry_, *thermodynamics_, candidate.state, .5 * dt,
                        heat_law(settings_.shock_heat));
                    after_shock_heat = std::move(heated.report); candidate.state = std::move(heated.state);
                }
                if (settings_.thermal_conduction) {
                    auto conducted=advance_mac_conduction(registry_,*thermodynamics_,candidate.state,.5*dt);
                    after_conduction=std::move(conducted.report); candidate.state=std::move(conducted.state);
                }
                if (settings_.viscosity.shear_pa_s>0 || settings_.viscosity.bulk_pa_s>0) {
                    auto damped=advance_mac_viscosity(registry_,*thermodynamics_,candidate.state,.5*dt,settings_.viscosity);
                    after_viscosity=std::move(damped.report);candidate.state=std::move(damped.state);
                }
                // Phase equilibrium at the end of the substep. Only the pair's
                // masses change: U, momentum and every energy account hold.
                MacPhaseChangeReport phase;
                if (settings_.phase_change == MacPhaseChange::saturation) {
                    auto changed = advance_mac_phase_change(registry_, *thermodynamics_, candidate.state, saturation_pairs);
                    candidate.state = std::move(changed.state); phase = changed.report;
                }
                bounds.add(candidate.energy_bound_wide_j); mix.add(candidate.transport.mixing_heat_wide_j); wall.add(candidate.transport.wall_heat_wide_j);
                remap_work.add(candidate.transport.remap_work_wide_j);
                result.report.primary_energy.merge(candidate.primary_energy);
                result.report.primary_energy.merge(candidate.pressure.primary_energy);
                result.report.primary_energy.merge(candidate.transport.primary_energy);
                if(candidate.reconstruction)result.report.primary_energy.merge(candidate.reconstruction->primary_energy);
                result.report.physical_admission_backtracks += candidate.transport.physical_admission_backtracks;
                result.report.rounded_energy_projections += candidate.rounded_energy_projections;
                wx.add(candidate.full_pressure_wall_impulse_x); wy.add(candidate.full_pressure_wall_impulse_y);
                for (const auto& reaction : candidate.transport.x.full_wall_impulse) wx.add(reaction);
                for (const auto& reaction : candidate.transport.y.full_wall_impulse) wy.add(reaction);
                for (const auto* compression : {&before_compression, &after_compression}) {
                    result.report.primary_energy.merge(compression->primary_energy);
                    bounds.add(compression->energy_bound); compression_heat.add(compression->heat);
                    wx.add(compression->wall_impulse_x); wy.add(compression->wall_impulse_y);
                    result.report.compression_events += compression->events.size();
                    result.report.unresolved_compression_heat_updates += compression->unresolved_heat_updates;
                    result.report.unresolved_compression_momentum_updates += compression->unresolved_momentum_updates;
                }
                // This heat is already in the pressure candidate's actual U
                // and energy account. Report it once without adding energy or
                // wall work a second time.
                for (const auto& response : candidate.compressive_responses) {
                    compression_heat.add(response.dissipated_heat_j);
                    if (!response.dissipated_heat_j.is_zero()) ++result.report.compression_events;
                    if (response.unresolved_energy_update) ++result.report.unresolved_compression_heat_updates;
                }
                result.report.unresolved_compressive_work_updates+=candidate.unresolved_compressive_work_updates;
                if (settings_.thermal_conduction) for (const auto* conduction : {&before_conduction,&after_conduction}) {
                    result.report.primary_energy.merge(conduction->primary_energy);
                    bounds.add(conduction->energy_bound); conductive_redistribution.add(conduction->redistributed_energy);
                    conductive_entropy.add(conduction->entropy_change_j_per_k);
                    ++result.report.conduction_stages; result.report.conduction_contacts+=conduction->contacts;
                    result.report.conduction_iterations+=conduction->iterations;
                    result.report.unresolved_conduction_updates+=conduction->unresolved_heat_updates;
                }
                if (settings_.viscosity.shear_pa_s>0 || settings_.viscosity.bulk_pa_s>0)
                    for (const auto* viscosity : {&before_viscosity,&after_viscosity}) {
                        result.report.primary_energy.merge(viscosity->primary_energy);
                        bounds.add(viscosity->energy_bound);viscous_heat.add(viscosity->heat);
                        viscous_stored_heat.add(viscosity->stored_heat);
                        viscous_entropy.add(viscosity->entropy_lower_bound_j_per_k);
                        wx.add(viscosity->wall_impulse_x);wy.add(viscosity->wall_impulse_y);
                        viscous_wx.add(viscosity->wall_impulse_x);viscous_wy.add(viscosity->wall_impulse_y);
                        ++result.report.viscosity_stages;result.report.viscosity_modes+=viscosity->modes;
                        result.report.unresolved_viscous_heat_updates+=viscosity->unresolved_heat_updates;
                        result.report.unresolved_viscous_momentum_updates+=viscosity->unresolved_momentum_updates;
                    }
                if (settings_.shock_heat != MacShockHeatRegularization::none)
                    for (const auto* heat : {&before_shock_heat, &after_shock_heat}) {
                        result.report.primary_energy.merge(heat->primary_energy);
                        bounds.add(heat->energy_bound); shock_heat_transfer.add(heat->absolute_transfer);
                        ++result.report.shock_heat_stages; result.report.shock_heat_events += heat->events.size();
                        result.report.unresolved_shock_heat_updates += heat->unresolved_updates;
                        for (const auto& event : heat->events) {
                            result.report.shock_heat_root_iterations += event.pair.equilibrium_iterations;
                            result.report.rejected_shock_heat_trials += event.pair.rejected_equilibrium_trials;
                        }
                    }
                result.report.pressure_iterations += candidate.pressure.pressure_iterations;
                result.report.pressure_increment_restarts += candidate.pressure.pressure_increment_restarts;
                result.report.nonlinear_iterations += candidate.pressure.nonlinear_iterations;
                result.report.unresolved_heat_updates += candidate.transport.unresolved_heat_updates;
                result.report.unresolved_momentum_updates += candidate.unresolved_momentum_updates;
                for (const auto* component : {&candidate.transport.x, &candidate.transport.y}) {
                    result.report.momentum_correction_events += component->corrections.size() + component->remap_corrections.size() + component->nodal_conversions.size();
                    result.report.limited_momentum_corrections += component->limited_corrections;
                    result.report.unresolved_momentum_corrections += component->unresolved_momentum_corrections;
                    result.report.unresolved_remainder_corrections += component->unresolved_remainder_corrections;
                }
                result.report.unresolved_gravity_internal_updates += candidate.unresolved_gravity_internal_updates;
                gravity_change.add(candidate.gravity_internal_change_wide_j); body_impulse.add(candidate.full_body_impulse_y);
                gravity_entropy.add(candidate.gravity_entropy_bound_j_per_k); gravity_entropy_operands.add(candidate.gravity_entropy_operands_j_per_k);
                result.report.rounded_gravity_projections += candidate.rounded_gravity_projections;
                for (const auto residual : candidate.geometric_residuals_m3)
                    result.report.maximum_geometric_residual_m3 = std::max(result.report.maximum_geometric_residual_m3, std::abs(residual));
                if (settings_.phase_change == MacPhaseChange::saturation) {
                    ++result.report.phase_change_stages;
                    result.report.evaporating_cells += phase.evaporating_cells; result.report.condensing_cells += phase.condensing_cells;
                    result.report.evaporated_kg += phase.evaporated_kg; result.report.condensed_kg += phase.condensed_kg;
                    result.report.phase_change_entropy_j_per_k += phase.entropy_change_j_per_k;
                }
                result.world.replace_state(std::move(candidate.state)); result.world.time_s_ += dt;
                ++result.report.substeps; next_dt = std::min(settings_.maximum_timestep_s, 2 * dt); accepted = true;
            } catch (const std::domain_error&) { ++result.report.rejected_attempts; dt *= .5; }
        }
    }
    result.world.time_s_ = endpoint; result.world.revision_ = revision_ + 1;
    const auto total = energy_difference(state(), result.world.state(), initial_inertia, mac_face_inertias(registry_, result.world.state(), *thermodynamics_), settings_.gravity_m_per_s2);
    if (!within_energy_budget(total.terms(), bounds)) throw std::domain_error("MAC closed interval energy exceeds accumulated stage bounds");
    result.report.energy_residual_wide_j = total.residual(); result.report.energy_bound_wide_j = bounds.value();
    result.report.mixing_heat_wide_j = mix.value(); result.report.wall_heat_wide_j = wall.value();
    result.report.remap_work_wide_j = remap_work.value();
    result.report.compression_heat_wide_j = compression_heat.value();
    result.report.conductive_redistribution_j=conductive_redistribution.value();
    result.report.conductive_entropy_j_per_k=conductive_entropy.value();
    result.report.viscous_heat=viscous_heat.value();
    result.report.viscous_stored_heat=viscous_stored_heat.value();
    result.report.viscous_wall_impulse_x=viscous_wx.value();result.report.viscous_wall_impulse_y=viscous_wy.value();
    result.report.viscous_entropy_lower_bound_j_per_k=viscous_entropy.value();
    result.report.absolute_shock_heat_transfer_wide_j = shock_heat_transfer.value();
    result.report.absolute_shock_heat_transfer_j = projected_energy(result.report.absolute_shock_heat_transfer_wide_j, result.report.rounded_energy_projections);
    result.report.compression_heat_j = projected_energy(result.report.compression_heat_wide_j, result.report.rounded_energy_projections);
    result.report.energy_residual_j = projected_energy(result.report.energy_residual_wide_j, result.report.rounded_energy_projections);
    result.report.energy_bound_j = projected_energy(result.report.energy_bound_wide_j, result.report.rounded_energy_projections);
    result.report.mixing_heat_j = projected_energy(result.report.mixing_heat_wide_j, result.report.rounded_energy_projections);
    result.report.wall_heat_j = projected_energy(result.report.wall_heat_wide_j, result.report.rounded_energy_projections);
    result.report.full_wall_impulse_x = wx.value(); result.report.full_wall_impulse_y = wy.value();
    result.report.wall_impulse_x = wx.value().project().value; result.report.wall_impulse_y = wy.value().project().value;
    result.report.gravity_internal_change_wide_j = gravity_change.value(); result.report.full_body_impulse_y = body_impulse.value();
    result.report.gravity_internal_change_j = projected_energy(gravity_change.value(), result.report.rounded_gravity_projections);
    result.report.body_impulse_y = projected_energy(body_impulse.value(), result.report.rounded_gravity_projections);
    // Report the requested interval's actual endpoints for every gravity mode.
    // Individual stage entropy diagnostics remain in their own reports.
    Account entropy, entropy_roundoff;
    for (std::size_t cell = 0; cell < state().cells.size(); ++cell) if (!state().grid.walls[cell]) {
        const auto& old = state().cells[cell].mixture; const auto& next = result.world.state().cells[cell].mixture;
        // An empty cell's reading is the EOS sentinel at its grid volume.
        const auto a = thermodynamics_->evaluate(old, old.mass() > 0 ? mac_material_volume(state(), cell) : state().grid.volume_m3());
        const auto b = thermodynamics_->evaluate(next, next.mass() > 0 ? mac_material_volume(result.world.state(), cell) : state().grid.volume_m3());
        entropy.add(b.entropy_j_per_k); entropy.add(-a.entropy_j_per_k);
        if(old.mass()>0)entropy.add(mac_stored_remainder_value(state().cells[cell].energy_remainder_j).shared(1,a.temperature_k).negated().project().value);
        if(next.mass()>0)entropy.add(mac_stored_remainder_value(result.world.state().cells[cell].energy_remainder_j).shared(1,b.temperature_k).project().value);
        if (old.mass() > 0 && next.mass() > 0)
            entropy_roundoff.add(128 * eps * (std::abs(old.energy) / a.temperature_k + std::abs(next.energy) / b.temperature_k));
        else {
            if (old.mass() > 0) entropy_roundoff.add(128 * eps * std::abs(old.energy) / a.temperature_k);
            if (next.mass() > 0) entropy_roundoff.add(128 * eps * std::abs(next.energy) / b.temperature_k);
        }
    }
    result.report.entropy_change_j_per_k = entropy.value();
    result.report.entropy_roundoff_j_per_k = entropy_roundoff.value() + 128 * eps * gravity_entropy_operands.value();
    result.report.gravity_entropy_bound_j_per_k = gravity_entropy.value();
    if (settings_.numerical_kernel != MacNumericalKernel::reference) {
        if (!total.within_bound()) throw std::domain_error("MAC reference fallback failed its interval energy audit");
        result.report.energy_bound_wide_j = total.bound();
        result.report.energy_bound_j = projected_energy(total.bound(), result.report.rounded_energy_projections);
        validate_mac_interval_fallback(state(), result.world.state(), result.report);
    }
    return result;
}
}
