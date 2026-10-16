#include "mixed_atmospheres/mac_closed_world.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace ma::experimental {
namespace {
constexpr auto none = std::numeric_limits<std::size_t>::max();
constexpr double eps = std::numeric_limits<double>::epsilon();
void require(bool ok, const char* reason) { if (!ok) throw std::invalid_argument(reason); }
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
double kinetic(double mass, double momentum) {
    if (momentum == 0) return 0;
    const double result = .5 * momentum * (momentum / mass);
    if (!(result > 0) || !std::isfinite(result))
        throw FluidFatalError(FluidFatalCode::numerical_precision, "MAC closed kinetic energy cannot be represented");
    return result;
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
void validate_momentum(const std::vector<Pair>& pairs, const std::vector<double>& inertia, const std::vector<double>& j) {
    require(j.size() == pairs.size(), "MAC closed momentum shape differs");
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        require_finite(j[i], "MAC closed canonical momentum");
        if (pairs[i].left == none || pairs[i].right == none) require(j[i] == 0, "MAC closed fixed normal face has momentum");
        static_cast<void>(kinetic(inertia[i], j[i]));
    }
}
Account energy_difference(const MacTransportState& old, const MacTransportState& next,
    const MacFaceInertias& i0, const MacFaceInertias& i1, double gravity = 0) {
    Account balance;
    for (std::size_t i = 0; i < old.cells.size(); ++i) {
        balance.add(next.cells[i].mixture.energy); balance.add(-old.cells[i].mixture.energy);
        if (gravity != 0) {
            const double potential = gravity * (static_cast<double>(i / old.grid.width) + .5) * old.grid.spacing_m;
            balance.add(next.cells[i].mixture.mass() * potential); balance.add(-old.cells[i].mixture.mass() * potential);
        }
    }
    for (int axis = 0; axis < 2; ++axis) {
        const auto& a = axis == 0 ? old.momentum_x : old.momentum_y; const auto& b = axis == 0 ? next.momentum_x : next.momentum_y;
        const auto& mass0 = axis == 0 ? i0.x : i0.y; const auto& mass1 = axis == 0 ? i1.x : i1.y;
        for (std::size_t f = 0; f < a.size(); ++f) { balance.add(kinetic(mass1[f], b[f])); balance.add(-kinetic(mass0[f], a[f])); }
    }
    return balance;
}
}
void MacClosedSettings::validate() const {
    require(std::isfinite(maximum_timestep_s) && maximum_timestep_s > 0 && maximum_timestep_s <= 1 &&
        std::isfinite(maximum_outgoing_volume_fraction) && maximum_outgoing_volume_fraction > 0 && maximum_outgoing_volume_fraction <= .45 &&
        (preconditioner == PressurePreconditioner::automatic || preconditioner == PressurePreconditioner::jacobi ||
         preconditioner == PressurePreconditioner::incomplete_cholesky) && std::isfinite(gravity_m_per_s2) && gravity_m_per_s2 >= 0 &&
        (reconstruction == MacMaterialReconstruction::donor_cell || reconstruction == MacMaterialReconstruction::limited_linear), "Invalid MAC closed settings");
}
MacClosedSubstep advance_mac_closed_substep(const SpeciesRegistry& registry, const FluidThermodynamics& model,
    const MacTransportState& source, double dt, const MacClosedSettings& settings) {
    settings.validate(); require(std::isfinite(dt) && dt > 0 && dt <= settings.maximum_timestep_s, "Invalid MAC closed substep time");
    const auto inertia = mac_face_inertias(registry, source, model); const auto& g = source.grid; const Topology topology(g);
    require(settings.gravity_m_per_s2 == 0 || !g.periodic_y, "Constant MAC gravity requires nonperiodic vertical boundaries");
    validate_momentum(topology.x, inertia.x, source.momentum_x); validate_momentum(topology.y, inertia.y, source.momentum_y);
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
        const auto& j = axis == 0 ? source.momentum_x : source.momentum_y; auto& map = axis == 0 ? result.pressure_nodes_x : result.pressure_nodes_y;
        for (std::size_t f = 0; f < pairs.size(); ++f) if (m[f] > 0) {
            map[f] = nodes.size(); const auto& pair = pairs[f];
            nodes.push_back({pair.left == none ? none : topology.local[pair.left], pair.right == none ? none : topology.local[pair.right],
                area, m[f], j[f] / m[f], axis == 0 ? 0 : -settings.gravity_m_per_s2, pair.left == none || pair.right == none});
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
    for (std::size_t i = 0; i < topology.active.size(); ++i) {
        const auto cell = topology.active[i]; result.pressure_state.cells[cell].mixture = result.pressure.cells[i].mixture;
        result.material_volumes_m3[cell] = result.pressure.cells[i].volume_m3;
    }
    std::vector<Account> sweeps(source.cells.size()), outgoing(source.cells.size()); Account wx, wy, px, py, body_impulse;
    for (int axis = 0; axis < 2; ++axis) {
        const auto& pairs = axis == 0 ? topology.x : topology.y; const auto& m = axis == 0 ? inertia.x : inertia.y;
        const auto& old_j = axis == 0 ? source.momentum_x : source.momentum_y;
        auto& new_j = axis == 0 ? result.pressure_state.momentum_x : result.pressure_state.momentum_y;
        auto& q = axis == 0 ? result.volume_fluxes_m3.x : result.volume_fluxes_m3.y;
        auto& flux = axis == 0 ? result.mass_fluxes_kg.x : result.mass_fluxes_kg.y;
        auto& reaction = axis == 0 ? wx : wy; auto& momentum = axis == 0 ? px : py;
        for (std::size_t f = 0; f < pairs.size(); ++f) if (m[f] > 0) {
            const auto& pair = pairs[f];
            const double pl = pair.left == none ? 0 : result.pressure.work_pressures_pa[topology.local[pair.left]];
            const double pr = pair.right == none ? 0 : result.pressure.work_pressures_pa[topology.local[pair.right]];
            const double gravity_force = axis == 0 ? 0 : -m[f] * settings.gravity_m_per_s2;
            const double gravity_impulse = dt * gravity_force;
            const double impulse = settings.gravity_m_per_s2 == 0 ? dt * area * (pl - pr) : dt * (area * (pl - pr) + gravity_force);
            require_finite(impulse, "MAC pressure impulse");
            const bool fixed = pair.left == none || pair.right == none;
            new_j[f] = fixed ? 0 : old_j[f] + impulse; require_finite(new_j[f], "MAC pressure canonical momentum");
            if (fixed) { reaction.add(-impulse); momentum.add(impulse); }
            else if (impulse != 0 && std::abs((new_j[f] - old_j[f]) - impulse) > 32 * eps * std::abs(impulse)) ++result.unresolved_momentum_updates;
            momentum.add(new_j[f]); momentum.add(-old_j[f]); momentum.add(-gravity_impulse);
            if (axis == 1) body_impulse.add(gravity_impulse);
            if (axis == 1 && settings.gravity_m_per_s2 != 0) {
                result.gravity_body_work_y[f] = gravity_impulse * std::midpoint(old_j[f] / m[f], new_j[f] / m[f]);
            }
            if (fixed) continue;
            q[f] = dt * area * std::midpoint(old_j[f] / m[f], new_j[f] / m[f]); require_finite(q[f], "MAC pressure volume sweep");
            sweeps[pair.left].add(q[f]); sweeps[pair.right].add(-q[f]);
            if (q[f] != 0) {
                const auto donor = q[f] > 0 ? pair.left : pair.right;
                outgoing[donor].add(std::abs(q[f]));
                if (settings.reconstruction == MacMaterialReconstruction::donor_cell)
                    flux[f] = product_ratio(q[f], source.cells[donor].mixture.mass(), result.material_volumes_m3[donor]);
            }
        }
    }
    result.pressure_wall_impulse_x = wx.value(); result.pressure_wall_impulse_y = wy.value();
    result.body_impulse_y = body_impulse.value();
    px.check("MAC pressure X momentum plus wall reaction failed"); py.check("MAC pressure Y momentum plus wall reaction failed");
    for (auto cell : topology.active) {
        if (outgoing[cell].value() > settings.maximum_outgoing_volume_fraction * result.material_volumes_m3[cell])
            throw std::domain_error("MAC pressure material-volume CFL exceeded");
        Account geometry; geometry.add(result.material_volumes_m3[cell]); geometry.add(-g.volume_m3()); geometry.add(-sweeps[cell].value());
        geometry.check("MAC pressure material-volume closure exceeds its operand bound"); result.geometric_residuals_m3[cell] = geometry.value();
    }
    auto pressure_balance = energy_difference(source, result.pressure_state, inertia, inertia);
    if (settings.gravity_m_per_s2 != 0) for (const double work : result.gravity_body_work_y) pressure_balance.add(-work);
    pressure_balance.check("MAC pressure represented energy exceeds its operand bound");
    result.pressure_energy_residual_j = pressure_balance.value(); result.pressure_energy_bound_j = pressure_balance.bound();
    MacTransportAdvance transported;
    if (settings.reconstruction == MacMaterialReconstruction::limited_linear) {
        auto plan = reconstruct_mac_parcels(registry, model, result.pressure_state, result.material_volumes_m3, result.volume_fluxes_m3, settings.maximum_outgoing_volume_fraction);
        transported = advance_mac_parcel_transport(registry, result.pressure_state, model, plan);
        result.mass_fluxes_kg = std::move(transported.report.primal_mass_fluxes_kg); result.reconstruction = std::move(plan);
    } else transported = advance_mac_transport(registry, result.pressure_state, result.mass_fluxes_kg, model, result.material_volumes_m3);
    result.transport = std::move(transported.report); result.state = std::move(transported.state);
    if (settings.gravity_m_per_s2 != 0) {
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
    result.energy_residual_j = total.value(); result.energy_bound_j = result.pressure_energy_bound_j + result.transport.energy_bound_j +
        result.gravity_internal_bound_j + result.potential_transport_bound_j;
    if (std::abs(result.energy_residual_j) > result.energy_bound_j) throw std::domain_error("MAC composed energy exceeds the two stage operand bounds");
    return result;
}
MacClosedWorld::MacClosedWorld(SpeciesRegistry registry, std::shared_ptr<const FluidThermodynamics> model,
    MacTransportState state, MacClosedSettings settings) : registry_(std::move(registry)),
    thermodynamics_(model ? std::make_shared<const FluidThermodynamics>(*model) : nullptr), state_(std::move(state)), settings_(settings) {
    require(thermodynamics_ != nullptr, "MAC closed owner requires an immutable EOS"); settings_.validate();
    const auto inertia = mac_face_inertias(registry_, state_, *thermodynamics_); const Topology topology(state_.grid);
    require(settings_.gravity_m_per_s2 == 0 || !state_.grid.periodic_y, "Constant MAC gravity requires nonperiodic vertical boundaries");
    validate_momentum(topology.x, inertia.x, state_.momentum_x); validate_momentum(topology.y, inertia.y, state_.momentum_y);
}
MacClosedAdvance MacClosedWorld::advanced(double seconds, std::size_t budget) const {
    require(std::isfinite(seconds) && seconds > 0 && seconds <= 10 && budget > 0 && budget <= 65536, "Invalid MAC interval or attempt budget");
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("MAC closed revision overflow");
    const double endpoint = time_s_ + seconds;
    if (!std::isfinite(endpoint) || endpoint <= time_s_) throw FluidFatalError(FluidFatalCode::numerical_precision, "MAC closed time advance cannot be represented");
    MacClosedAdvance result{*this, {}}; Account bounds, mix, wall, wx, wy, gravity_change, body_impulse, entropy, entropy_roundoff;
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
                auto candidate = advance_mac_closed_substep(registry_, *thermodynamics_, result.world.state_, dt, settings_);
                bounds.add(candidate.energy_bound_j); mix.add(candidate.transport.mixing_heat_j); wall.add(candidate.transport.wall_heat_j);
                wx.add(candidate.pressure_wall_impulse_x); wy.add(candidate.pressure_wall_impulse_y);
                for (const double reaction : candidate.transport.x.wall_impulse) wx.add(reaction);
                for (const double reaction : candidate.transport.y.wall_impulse) wy.add(reaction);
                result.report.pressure_iterations += candidate.pressure.pressure_iterations;
                result.report.nonlinear_iterations += candidate.pressure.nonlinear_iterations;
                result.report.unresolved_heat_updates += candidate.transport.unresolved_heat_updates;
                result.report.unresolved_momentum_updates += candidate.unresolved_momentum_updates;
                result.report.unresolved_gravity_internal_updates += candidate.unresolved_gravity_internal_updates;
                gravity_change.add(candidate.gravity_internal_change_j); body_impulse.add(candidate.body_impulse_y);
                entropy.add(candidate.entropy_change_j_per_k); entropy_roundoff.add(candidate.entropy_roundoff_j_per_k);
                for (const auto residual : candidate.geometric_residuals_m3)
                    result.report.maximum_geometric_residual_m3 = std::max(result.report.maximum_geometric_residual_m3, std::abs(residual));
                result.world.state_ = std::move(candidate.state); result.world.time_s_ += dt;
                ++result.report.substeps; next_dt = std::min(settings_.maximum_timestep_s, 2 * dt); accepted = true;
            } catch (const std::domain_error&) { ++result.report.rejected_attempts; dt *= .5; }
        }
    }
    result.world.time_s_ = endpoint; result.world.revision_ = revision_ + 1;
    const auto total = energy_difference(state_, result.world.state_, initial_inertia, mac_face_inertias(registry_, result.world.state_, *thermodynamics_), settings_.gravity_m_per_s2);
    result.report.energy_residual_j = total.value(); result.report.energy_bound_j = bounds.value();
    if (std::abs(total.value()) > bounds.value()) throw std::domain_error("MAC closed interval energy exceeds accumulated stage bounds");
    result.report.mixing_heat_j = mix.value(); result.report.wall_heat_j = wall.value(); result.report.wall_impulse_x = wx.value(); result.report.wall_impulse_y = wy.value();
    result.report.gravity_internal_change_j = gravity_change.value(); result.report.body_impulse_y = body_impulse.value();
    result.report.entropy_change_j_per_k = entropy.value(); result.report.entropy_roundoff_j_per_k = entropy_roundoff.value();
    return result;
}
}
