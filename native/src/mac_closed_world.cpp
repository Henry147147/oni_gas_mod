#include "mixed_atmospheres/mac_closed_world.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>

namespace ma::experimental {
namespace {
constexpr auto none = std::numeric_limits<std::size_t>::max();
constexpr double eps = std::numeric_limits<double>::epsilon();
void require(bool ok, const char* reason) { if (!ok) throw std::invalid_argument(reason); }
MacShockHeatLaw heat_law(MacShockHeatRegularization setting) {
    switch (setting) {
    case MacShockHeatRegularization::compression_activated: return MacShockHeatLaw::closing_velocity;
    case MacShockHeatRegularization::pressure_sensed_acoustic: return MacShockHeatLaw::pressure_sensed_viscosity;
    case MacShockHeatRegularization::pressure_sensed_series: return MacShockHeatLaw::pressure_sensed_series;
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
double product_ratio(double a, double b, double c) {
    if (a == 0 || b == 0) return 0;
    int ea = 0, eb = 0, ec = 0;
    const double ma = std::frexp(a, &ea), mb = std::frexp(b, &eb), mc = std::frexp(c, &ec);
    const double result = std::ldexp(ma * mb / mc, ea + eb - ec);
    if (!std::isfinite(result) || result == 0 || std::signbit(result) != std::signbit(a))
        throw FluidFatalError(FluidFatalCode::numerical_precision, "MAC closed mass flux cannot be represented");
    return result;
}
struct Pair { std::size_t left = none, right = none; };
struct Topology {
    std::vector<std::size_t> active, local;
    std::vector<Pair> x, y;
    explicit Topology(const MacGrid& g) : local(g.walls.size(), none) {
        for (std::size_t i = 0; i < g.walls.size(); ++i) if (!g.walls[i]) { local[i] = active.size(); active.push_back(i); }
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
        balance.add(next.cells[i].mixture.energy); balance.add(-old.cells[i].mixture.energy);
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
}
void MacClosedSettings::validate() const {
    require(std::isfinite(maximum_timestep_s) && maximum_timestep_s > 0 && maximum_timestep_s <= 1 &&
        std::isfinite(maximum_outgoing_volume_fraction) && maximum_outgoing_volume_fraction > 0 && maximum_outgoing_volume_fraction <= .45 &&
        (preconditioner == PressurePreconditioner::automatic || preconditioner == PressurePreconditioner::jacobi ||
         preconditioner == PressurePreconditioner::incomplete_cholesky) && std::isfinite(gravity_m_per_s2) && gravity_m_per_s2 >= 0 &&
        (reconstruction == MacMaterialReconstruction::donor_cell || reconstruction == MacMaterialReconstruction::limited_linear) &&
        (momentum_reconstruction == MacMomentumReconstruction::donor_cell || momentum_reconstruction == MacMomentumReconstruction::limited_linear) &&
        (compression_viscosity == MacCompressionViscosity::none || compression_viscosity == MacCompressionViscosity::quadratic ||
         compression_viscosity == MacCompressionViscosity::acoustic_quadratic) &&
        (shock_heat == MacShockHeatRegularization::none || shock_heat == MacShockHeatRegularization::compression_activated ||
         shock_heat == MacShockHeatRegularization::pressure_sensed_acoustic ||
         shock_heat == MacShockHeatRegularization::pressure_sensed_series), "Invalid MAC closed settings");
}
MacClosedSubstep advance_mac_closed_substep(const SpeciesRegistry& registry, const FluidThermodynamics& model,
    const MacTransportState& source, double dt, const MacClosedSettings& settings) {
    require_mac_energy_environment();
    settings.validate(); require(std::isfinite(dt) && dt > 0 && dt <= settings.maximum_timestep_s, "Invalid MAC closed substep time");
    const auto inertia = mac_face_inertias(registry, source, model); const auto& g = source.grid; const Topology topology(g);
    require(settings.gravity_m_per_s2 == 0 || !g.periodic_y, "Constant MAC gravity requires nonperiodic vertical boundaries");
    const auto jx0 = mac_field_values(source.momentum_x, source.full_momentum_x), jy0 = mac_field_values(source.momentum_y, source.full_momentum_y);
    validate_momentum(topology.x, inertia.x, jx0); validate_momentum(topology.y, inertia.y, jy0);
    if (settings.gravity_m_per_s2 != 0 && !source.full_momentum_y.empty())
        throw FluidFatalError(FluidFatalCode::unsupported_topology, "Range-extended vertical gravity work is not implemented");
    MacClosedSubstep result; result.pressure_state = source; result.active_cells = topology.active;
    result.material_volumes_m3.resize(source.cells.size()); result.geometric_residuals_m3.resize(source.cells.size());
    result.pressure_nodes_x.assign(topology.x.size(), none); result.pressure_nodes_y.assign(topology.y.size(), none);
    result.volume_fluxes_m3 = {std::vector<double>(topology.x.size()), std::vector<double>(topology.y.size())}; result.mass_fluxes_kg = result.volume_fluxes_m3;
    if (settings.gravity_m_per_s2 != 0) {
        result.gravity_body_work_y.resize(topology.y.size()); result.gravity_potential_flux_y.resize(topology.y.size()); result.gravity_internal_work_y.resize(topology.y.size());
        result.pre_gravity_internal_energy_j.resize(source.cells.size()); result.gravity_internal_changes_j.resize(source.cells.size());
    }
    std::vector<MaterialCell> materials; materials.reserve(topology.active.size());
    for (auto cell : topology.active) materials.push_back({source.cells[cell].mixture, g.volume_m3(), 0});
    const double area = g.spacing_m * g.depth_m; require(std::isfinite(area) && area > 0, "MAC closed area cannot be represented");
    std::vector<OccupiedMaterialNode> nodes;
    for (int axis = 0; axis < 2; ++axis) {
        const auto& pairs = axis == 0 ? topology.x : topology.y; const auto& m = axis == 0 ? inertia.x : inertia.y;
        const auto& j = axis == 0 ? jx0 : jy0; auto& map = axis == 0 ? result.pressure_nodes_x : result.pressure_nodes_y;
        for (std::size_t f = 0; f < pairs.size(); ++f) if (m[f] > 0) {
            map[f] = nodes.size(); const auto& pair = pairs[f];
            const auto velocity = j[f].shared(1, m[f]).project(); if (velocity.rounded) ++result.rounded_pressure_velocity_inputs;
            nodes.push_back({pair.left == none ? none : topology.local[pair.left], pair.right == none ? none : topology.local[pair.right],
                area, m[f], velocity.value, axis == 0 ? 0 : -settings.gravity_m_per_s2, pair.left == none || pair.right == none});
        }
    }
    ReferenceRootOptions work_root;
    // The ordinary 2e-13 inner pressure root can leave work-pressure noise
    // above this caller's 16-epsilon volume residual, particularly at rest.
    // Resolve the nested inversion to adjacent floating-point parameters;
    // retain the original outer geometry and energy acceptance bounds.
    work_root.relative_pressure_tolerance = eps / 4;
    if (!materials.empty()) result.pressure = advance_occupied_material_pressure(model, materials, nodes, dt, settings.preconditioner,
        LiquidFillRoundoffPolicy::strict_eos, 16 * eps, work_root);
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
        result.material_volumes_m3[cell] = result.pressure.cells[i].volume_m3;
    }
    std::vector<MacScaledSum> sweeps(source.cells.size()), outgoing(source.cells.size()); Account body_impulse; MacScaledAccount px, py;
    std::array<MacScaledSum, 2> wall_pressure, wall_body;
    std::vector<MacScaled> jx1 = jx0, jy1 = jy0, qx(topology.x.size()), qy(topology.y.size()), fx(topology.x.size()), fy(topology.y.size());
    for (int axis = 0; axis < 2; ++axis) {
        const auto& pairs = axis == 0 ? topology.x : topology.y; const auto& m = axis == 0 ? inertia.x : inertia.y;
        const auto& old_j = axis == 0 ? jx0 : jy0;
        auto& new_j = axis == 0 ? jx1 : jy1; auto& q = axis == 0 ? qx : qy; auto& flux = axis == 0 ? fx : fy;
        auto& momentum = axis == 0 ? px : py;
        for (std::size_t f = 0; f < pairs.size(); ++f) if (m[f] > 0) {
            const auto& pair = pairs[f];
            const double pl = pair.left == none ? 0 : result.pressure.work_pressures_pa[topology.local[pair.left]];
            const double pr = pair.right == none ? 0 : result.pressure.work_pressures_pa[topology.local[pair.right]];
            const double gravity_force = axis == 0 ? 0 : -m[f] * settings.gravity_m_per_s2;
            const double gravity_impulse = dt * gravity_force;
            const double impulse = settings.gravity_m_per_s2 == 0 ? dt * area * (pl - pr) : dt * (area * (pl - pr) + gravity_force);
            require_finite(impulse, "MAC pressure impulse");
            const bool fixed = pair.left == none || pair.right == none;
            MacScaledSum updated; updated.add(old_j[f]); updated.add(impulse); new_j[f] = fixed ? MacScaled{} : mac_scaled_primary(updated);
            if (fixed) {
                // Cancel boundary pressures before their common dt*area factor.
                // Scaling large opposing wall forces separately loses a small
                // net reaction even when the interior gradient is well resolved.
                wall_pressure[static_cast<std::size_t>(axis)].add(-pl);
                wall_pressure[static_cast<std::size_t>(axis)].add(pr);
                wall_body[static_cast<std::size_t>(axis)].add(-gravity_impulse);
                momentum.add(impulse);
            }
            else if (impulse != 0) {
                MacScaledSum difference; difference.add(new_j[f]); difference.add(old_j[f].negated()); difference.add(-impulse);
                MacScaledSum allowance; allowance.add(MacScaled::from_double(std::abs(impulse)).shifted(-47));
                for (std::size_t k = 0; k < difference.size(); ++k) allowance.add(difference.sign() < 0 ? difference.component(k) : difference.component(k).negated());
                if (allowance.sign() < 0) ++result.unresolved_momentum_updates;
            }
            momentum.add(new_j[f]); momentum.add(old_j[f].negated()); momentum.add(-gravity_impulse);
            if (axis == 1) body_impulse.add(gravity_impulse);
            if (axis == 1 && settings.gravity_m_per_s2 != 0) {
                result.gravity_body_work_y[f] = gravity_impulse * std::midpoint(old_j[f].shared(1, m[f]).require_representable(), new_j[f].shared(1, m[f]).require_representable());
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
    result.volume_fluxes_m3 = mac_fluxes(qx, qy); result.mass_fluxes_kg = mac_fluxes(fx, fy);
    if (settings.gravity_m_per_s2 != 0 && !result.volume_fluxes_m3.full_y.empty())
        throw FluidFatalError(FluidFatalCode::unsupported_topology, "Range-extended vertical gravity sweep is not implemented");
    const auto wall_reaction = [&](std::size_t axis) {
        MacScaledSum reaction; reaction.add(wall_pressure[axis].value().multiplied(dt).multiplied(area));
        reaction.add(wall_body[axis].value()); return mac_scaled_primary(reaction);
    };
    result.full_pressure_wall_impulse_x = wall_reaction(0); result.full_pressure_wall_impulse_y = wall_reaction(1);
    result.pressure_wall_impulse_x = result.full_pressure_wall_impulse_x.project().value;
    result.pressure_wall_impulse_y = result.full_pressure_wall_impulse_y.project().value;
    result.body_impulse_y = body_impulse.value();
    if (!px.within_bound() || !py.within_bound()) throw std::domain_error("MAC pressure full momentum plus wall reaction failed");
    for (auto cell : topology.active) {
        MacScaledSum limit; limit.add(MacScaled::from_double(result.material_volumes_m3[cell]).multiplied(settings.maximum_outgoing_volume_fraction));
        for (std::size_t k = 0; k < outgoing[cell].size(); ++k) limit.add(outgoing[cell].component(k).negated());
        if (limit.sign() < 0)
            throw std::domain_error("MAC pressure material-volume CFL exceeded");
        MacScaledAccount geometry; geometry.add(result.material_volumes_m3[cell]); geometry.add(-g.volume_m3());
        for (std::size_t k = 0; k < sweeps[cell].size(); ++k) geometry.add(sweeps[cell].component(k).negated());
        if (!geometry.within_bound()) throw std::domain_error("MAC pressure full material-volume closure exceeds its operand bound");
        result.geometric_residuals_m3[cell] = geometry.residual().project().value;
    }
    auto pressure_balance = energy_difference(source, result.pressure_state, inertia, inertia);
    if (settings.gravity_m_per_s2 != 0) for (const double work : result.gravity_body_work_y) pressure_balance.add(-work);
    if (!pressure_balance.within_bound()) throw std::domain_error("MAC pressure represented energy exceeds its operand bound");
    result.pressure_energy_residual_wide_j = pressure_balance.residual(); result.pressure_energy_bound_wide_j = pressure_balance.bound();
    result.pressure_energy_residual_j = projected_energy(result.pressure_energy_residual_wide_j, result.rounded_energy_projections);
    result.pressure_energy_bound_j = projected_energy(result.pressure_energy_bound_wide_j, result.rounded_energy_projections);
    MacTransportAdvance transported;
    if (settings.reconstruction == MacMaterialReconstruction::limited_linear) {
        auto plan = reconstruct_mac_parcels(registry, model, result.pressure_state, result.material_volumes_m3, result.volume_fluxes_m3, settings.maximum_outgoing_volume_fraction);
        transported = advance_mac_parcel_transport(registry, result.pressure_state, model, plan, settings.momentum_reconstruction);
        result.mass_fluxes_kg = std::move(transported.report.primal_mass_fluxes_kg); result.reconstruction = std::move(plan);
    } else transported = advance_mac_transport(registry, result.pressure_state, result.mass_fluxes_kg, model,
        result.material_volumes_m3, settings.momentum_reconstruction);
    result.transport = std::move(transported.report); result.state = std::move(transported.state);
    if (settings.gravity_m_per_s2 != 0) {
        if (!result.mass_fluxes_kg.full_y.empty()) throw FluidFatalError(FluidFatalCode::unsupported_topology, "Range-extended vertical gravity mass work is not implemented");
        Account potential, gravity_internal, change, entropy, entropy_roundoff;
        std::vector<Account> correction(source.cells.size());
        for (std::size_t f = 0; f < topology.y.size(); ++f) {
            const auto& pair = topology.y[f];
            result.gravity_potential_flux_y[f] = result.mass_fluxes_kg.y[f] * (settings.gravity_m_per_s2 * g.spacing_m);
            const double work = -(result.gravity_body_work_y[f] + result.gravity_potential_flux_y[f]);
            require_finite(work, "MAC signed gravity internal work"); result.gravity_internal_work_y[f] = work;
            potential.add(-result.gravity_potential_flux_y[f]);
            gravity_internal.add(result.gravity_body_work_y[f]); gravity_internal.add(result.gravity_potential_flux_y[f]);
            if (work == 0) continue;
            const double left = pair.left == none ? 0 : result.state.cells[pair.left].mixture.mass();
            const double right = pair.right == none ? 0 : result.state.cells[pair.right].mixture.mass();
            const double mass = left + right; require_finite(mass, "MAC gravity sharing mass");
            for (const auto cell : {pair.left, pair.right}) if (cell != none) correction[cell].add(product_ratio(work, result.state.cells[cell].mixture.mass(), mass));
        }
        for (std::size_t i = 0; i < source.cells.size(); ++i) {
            auto& cell = result.state.cells[i]; const double before = cell.mixture.energy, update = correction[i].value();
            result.pre_gravity_internal_energy_j[i] = before; Account local; local.add(before); local.add(update); cell.mixture.energy = local.value();
            result.gravity_internal_changes_j[i] = update; change.add(update); gravity_internal.add(cell.mixture.energy); gravity_internal.add(-before);
            if (update != 0 && std::abs((cell.mixture.energy - before) - update) > 32 * eps * std::abs(update)) ++result.unresolved_gravity_internal_updates;
            const double phi = settings.gravity_m_per_s2 * (static_cast<double>(i / g.width) + .5) * g.spacing_m;
            potential.add(cell.mixture.mass() * phi); potential.add(-source.cells[i].mixture.mass() * phi);
            if (!g.walls[i]) {
                const auto old_eos = model.evaluate(source.cells[i].mixture, g.volume_m3());
                const auto next_eos = model.evaluate(cell.mixture, g.volume_m3());
                entropy.add(next_eos.entropy_j_per_k); entropy.add(-old_eos.entropy_j_per_k);
                entropy_roundoff.add(128 * eps * (std::abs(source.cells[i].mixture.energy) + std::abs(before) + std::abs(cell.mixture.energy)) /
                    std::min(old_eos.temperature_k, next_eos.temperature_k));
            }
        }
        potential.check("MAC potential-energy mass transport exceeds its operand bound");
        gravity_internal.check("MAC represented gravity internal change exceeds its operand bound");
        result.potential_transport_residual_j = potential.value(); result.potential_transport_bound_j = potential.bound();
        result.gravity_internal_residual_j = gravity_internal.value(); result.gravity_internal_bound_j = gravity_internal.bound();
        result.gravity_internal_change_j = change.value(); result.entropy_change_j_per_k = entropy.value(); result.entropy_roundoff_j_per_k = entropy_roundoff.value();
    }
    const auto total = energy_difference(source, result.state, inertia, mac_face_inertias(registry, result.state, model), settings.gravity_m_per_s2);
    MacEnergySum budget; budget.add(result.pressure_energy_bound_wide_j); budget.add(result.transport.energy_bound_wide_j);
    budget.add(result.gravity_internal_bound_j); budget.add(result.potential_transport_bound_j);
    if (!within_energy_budget(total.terms(), budget)) throw std::domain_error("MAC composed energy exceeds the stage operand bounds");
    result.energy_residual_wide_j = total.residual(); result.energy_bound_wide_j = budget.value();
    result.rounded_energy_projections += result.transport.rounded_energy_projections;
    result.energy_residual_j = projected_energy(result.energy_residual_wide_j, result.rounded_energy_projections);
    result.energy_bound_j = projected_energy(result.energy_bound_wide_j, result.rounded_energy_projections);
    return result;
}
MacClosedWorld::MacClosedWorld(SpeciesRegistry registry, std::shared_ptr<const FluidThermodynamics> model,
    MacTransportState state, MacClosedSettings settings) : registry_(std::move(registry)),
    thermodynamics_(model ? std::make_shared<const FluidThermodynamics>(*model) : nullptr), state_(std::move(state)), settings_(settings) {
    require_mac_energy_environment();
    require(thermodynamics_ != nullptr, "MAC closed owner requires an immutable EOS"); settings_.validate();
    const auto inertia = mac_face_inertias(registry_, state_, *thermodynamics_); const Topology topology(state_.grid);
    require(settings_.gravity_m_per_s2 == 0 || !state_.grid.periodic_y, "Constant MAC gravity requires nonperiodic vertical boundaries");
    validate_momentum(topology.x, inertia.x, mac_field_values(state_.momentum_x, state_.full_momentum_x));
    validate_momentum(topology.y, inertia.y, mac_field_values(state_.momentum_y, state_.full_momentum_y));
}
MacClosedWorld MacClosedWorld::with_cell_heat(std::span<const MacCellHeat> edits) const {
    require_mac_energy_environment();
    require(!edits.empty() && edits.size() <= 4096, "MAC heat batch must contain 1..4096 edits");
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("MAC closed revision overflow");
    auto next=state_; MacEnergyAccount balance;
    for (const auto& edit : edits) {
        require(edit.cell_index < next.cells.size() && !next.grid.walls[edit.cell_index] &&
            std::isfinite(edit.joules) && std::isfinite(edit.maximum_temperature_k) && edit.maximum_temperature_k > 0,
            "Invalid MAC fluid heat exchange");
        auto& mixture=next.cells[edit.cell_index].mixture;
        const auto before=mixture.energy; const double after=before+edit.joules;
        require(std::isfinite(after), "MAC heat energy overflow");
        if (edit.joules != 0 && before == after)
            throw FluidFatalError(FluidFatalCode::numerical_precision,"MAC heat exchange cannot be represented in cell U");
        mixture.energy=after;
        const auto reading=thermodynamics_->evaluate(mixture,next.grid.volume_m3());
        require(edit.joules <= 0 || reading.temperature_k <= edit.maximum_temperature_k,
            "MAC heat exchange exceeds its reserved temperature limit");
        balance.add(before); balance.add(edit.joules); balance.add(-after);
    }
    require(balance.within_bound(), "MAC heat exchange energy account failed");
    MacClosedWorld candidate(registry_,thermodynamics_,std::move(next),settings_);
    candidate.time_s_=time_s_; candidate.revision_=revision_+1;
    return candidate;
}
MacClosedAdvance MacClosedWorld::advanced(double seconds, std::size_t budget) const {
    require_mac_energy_environment();
    require(std::isfinite(seconds) && seconds > 0 && seconds <= 10 && budget > 0 && budget <= 65536, "Invalid MAC interval or attempt budget");
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("MAC closed revision overflow");
    const double endpoint = time_s_ + seconds;
    if (!std::isfinite(endpoint) || endpoint <= time_s_) throw FluidFatalError(FluidFatalCode::numerical_precision, "MAC closed time advance cannot be represented");
    MacClosedAdvance result{*this, {}}; MacEnergySum bounds, mix, wall, compression_heat, conductive_redistribution, shock_heat_transfer;
    Account conductive_entropy;
    MacScaledSum wx, wy; Account gravity_change, body_impulse;
    const auto initial_inertia = mac_face_inertias(registry_, state_, *thermodynamics_);
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
                MacShockHeatReport before_shock_heat, after_shock_heat;
                const MacTransportState* pressure_source = &result.world.state_;
                if (settings_.thermal_conduction) {
                    auto conducted=advance_mac_conduction(registry_,*thermodynamics_,*pressure_source,.5*dt);
                    before_conduction=std::move(conducted.report); prepared=std::move(conducted.state); pressure_source=&prepared;
                }
                if (settings_.shock_heat != MacShockHeatRegularization::none) {
                    auto heated = advance_mac_shock_heat(registry_, *thermodynamics_, *pressure_source, .5 * dt,
                        heat_law(settings_.shock_heat));
                    before_shock_heat = std::move(heated.report); prepared = std::move(heated.state); pressure_source = &prepared;
                }
                if (settings_.compression_viscosity != MacCompressionViscosity::none) {
                    auto damped = advance_mac_compression(registry_, *thermodynamics_, *pressure_source, .5 * dt,
                        settings_.compression_viscosity == MacCompressionViscosity::quadratic ? MacCompressionLaw::quadratic : MacCompressionLaw::pressure_sensed_acoustic);
                    before_compression = std::move(damped.report); prepared = std::move(damped.state); pressure_source = &prepared;
                }
                auto candidate = advance_mac_closed_substep(registry_, *thermodynamics_, *pressure_source, dt, settings_);
                if (settings_.compression_viscosity != MacCompressionViscosity::none) {
                    auto damped = advance_mac_compression(registry_, *thermodynamics_, candidate.state, .5 * dt,
                        settings_.compression_viscosity == MacCompressionViscosity::quadratic ? MacCompressionLaw::quadratic : MacCompressionLaw::pressure_sensed_acoustic);
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
                bounds.add(candidate.energy_bound_wide_j); mix.add(candidate.transport.mixing_heat_wide_j); wall.add(candidate.transport.wall_heat_wide_j);
                result.report.rounded_energy_projections += candidate.rounded_energy_projections;
                wx.add(candidate.full_pressure_wall_impulse_x); wy.add(candidate.full_pressure_wall_impulse_y);
                for (const auto& reaction : candidate.transport.x.full_wall_impulse) wx.add(reaction);
                for (const auto& reaction : candidate.transport.y.full_wall_impulse) wy.add(reaction);
                for (const auto* compression : {&before_compression, &after_compression}) {
                    bounds.add(compression->energy_bound); compression_heat.add(compression->heat);
                    wx.add(compression->wall_impulse_x); wy.add(compression->wall_impulse_y);
                    result.report.compression_events += compression->events.size();
                    result.report.unresolved_compression_heat_updates += compression->unresolved_heat_updates;
                    result.report.unresolved_compression_momentum_updates += compression->unresolved_momentum_updates;
                }
                if (settings_.thermal_conduction) for (const auto* conduction : {&before_conduction,&after_conduction}) {
                    bounds.add(conduction->energy_bound); conductive_redistribution.add(conduction->redistributed_energy);
                    conductive_entropy.add(conduction->entropy_change_j_per_k);
                    ++result.report.conduction_stages; result.report.conduction_contacts+=conduction->contacts;
                    result.report.conduction_iterations+=conduction->iterations;
                    result.report.unresolved_conduction_updates+=conduction->unresolved_heat_updates;
                }
                if (settings_.shock_heat != MacShockHeatRegularization::none)
                    for (const auto* heat : {&before_shock_heat, &after_shock_heat}) {
                        bounds.add(heat->energy_bound); shock_heat_transfer.add(heat->absolute_transfer);
                        ++result.report.shock_heat_stages; result.report.shock_heat_events += heat->events.size();
                        result.report.unresolved_shock_heat_updates += heat->unresolved_updates;
                        for (const auto& event : heat->events) {
                            result.report.shock_heat_root_iterations += event.pair.equilibrium_iterations;
                            result.report.rejected_shock_heat_trials += event.pair.rejected_equilibrium_trials;
                        }
                    }
                result.report.pressure_iterations += candidate.pressure.pressure_iterations;
                result.report.nonlinear_iterations += candidate.pressure.nonlinear_iterations;
                result.report.unresolved_heat_updates += candidate.transport.unresolved_heat_updates;
                result.report.unresolved_momentum_updates += candidate.unresolved_momentum_updates;
                for (const auto* component : {&candidate.transport.x, &candidate.transport.y}) {
                    result.report.momentum_correction_events += component->corrections.size();
                    result.report.limited_momentum_corrections += component->limited_corrections;
                    result.report.unresolved_momentum_corrections += component->unresolved_momentum_corrections;
                    result.report.unresolved_remainder_corrections += component->unresolved_remainder_corrections;
                }
                result.report.unresolved_gravity_internal_updates += candidate.unresolved_gravity_internal_updates;
                gravity_change.add(candidate.gravity_internal_change_j); body_impulse.add(candidate.body_impulse_y);
                for (const auto residual : candidate.geometric_residuals_m3)
                    result.report.maximum_geometric_residual_m3 = std::max(result.report.maximum_geometric_residual_m3, std::abs(residual));
                result.world.state_ = std::move(candidate.state); result.world.time_s_ += dt;
                ++result.report.substeps; next_dt = std::min(settings_.maximum_timestep_s, 2 * dt); accepted = true;
            } catch (const std::domain_error&) { ++result.report.rejected_attempts; dt *= .5; }
        }
    }
    result.world.time_s_ = endpoint; result.world.revision_ = revision_ + 1;
    const auto total = energy_difference(state_, result.world.state_, initial_inertia, mac_face_inertias(registry_, result.world.state_, *thermodynamics_), settings_.gravity_m_per_s2);
    if (!within_energy_budget(total.terms(), bounds)) throw std::domain_error("MAC closed interval energy exceeds accumulated stage bounds");
    result.report.energy_residual_wide_j = total.residual(); result.report.energy_bound_wide_j = bounds.value();
    result.report.mixing_heat_wide_j = mix.value(); result.report.wall_heat_wide_j = wall.value();
    result.report.compression_heat_wide_j = compression_heat.value();
    result.report.conductive_redistribution_j=conductive_redistribution.value();
    result.report.conductive_entropy_j_per_k=conductive_entropy.value();
    result.report.absolute_shock_heat_transfer_wide_j = shock_heat_transfer.value();
    result.report.absolute_shock_heat_transfer_j = projected_energy(result.report.absolute_shock_heat_transfer_wide_j, result.report.rounded_energy_projections);
    result.report.compression_heat_j = projected_energy(result.report.compression_heat_wide_j, result.report.rounded_energy_projections);
    result.report.energy_residual_j = projected_energy(result.report.energy_residual_wide_j, result.report.rounded_energy_projections);
    result.report.energy_bound_j = projected_energy(result.report.energy_bound_wide_j, result.report.rounded_energy_projections);
    result.report.mixing_heat_j = projected_energy(result.report.mixing_heat_wide_j, result.report.rounded_energy_projections);
    result.report.wall_heat_j = projected_energy(result.report.wall_heat_wide_j, result.report.rounded_energy_projections);
    result.report.full_wall_impulse_x = wx.value(); result.report.full_wall_impulse_y = wy.value();
    result.report.wall_impulse_x = wx.value().project().value; result.report.wall_impulse_y = wy.value().project().value;
    result.report.gravity_internal_change_j = gravity_change.value(); result.report.body_impulse_y = body_impulse.value();
    // Report the requested interval's actual endpoints for every gravity mode.
    // Individual stage entropy diagnostics remain in their own reports.
    Account entropy, entropy_roundoff;
    for (std::size_t cell = 0; cell < state_.cells.size(); ++cell) if (!state_.grid.walls[cell]) {
        const auto& old = state_.cells[cell].mixture; const auto& next = result.world.state_.cells[cell].mixture;
        const auto a = thermodynamics_->evaluate(old, state_.grid.volume_m3());
        const auto b = thermodynamics_->evaluate(next, state_.grid.volume_m3());
        entropy.add(b.entropy_j_per_k); entropy.add(-a.entropy_j_per_k);
        entropy_roundoff.add(128 * eps * (std::abs(old.energy) / a.temperature_k + std::abs(next.energy) / b.temperature_k));
    }
    result.report.entropy_change_j_per_k = entropy.value(); result.report.entropy_roundoff_j_per_k = entropy_roundoff.value();
    return result;
}
}
