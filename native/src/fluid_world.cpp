#include "mixed_atmospheres/fluid_world.hpp"
#include "mixed_atmospheres/thermal.hpp"
#include "mixed_atmospheres/transport.hpp"
#include "mixed_atmospheres/volume_work.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <map>

namespace ma {
namespace {
struct AccurateSum {
    double sum = 0, correction = 0;
    void add(double value) {
        const double next = sum + value;
        correction += std::abs(sum) >= std::abs(value) ? (sum - next) + value : (value - next) + sum;
        sum = next;
    }
    [[nodiscard]] double value() const { return sum + correction; }
};
std::map<std::uint64_t, double> passive_totals(const std::vector<FluidCell>& cells) {
    std::map<std::uint64_t, AccurateSum> sums;
    for (const auto& cell : cells) for (const auto& value : cell.passive_quantities) sums[value.identity].add(value.quantity);
    std::map<std::uint64_t, double> result;
    for (const auto& [identity, sum] : sums) {
        const double value = sum.value(); require_finite(value, "total passive quantity"); result.emplace(identity, value);
    }
    return result;
}
}
void FluidSolverSettings::validate() const {
    for (double value : {gravity, diffusivity_m2_per_s, viscosity_m2_per_s,
                         maximum_timestep_s, maximum_outgoing_fraction})
        require_finite(value, "fluid solver setting");
    if (gravity < 0 || diffusivity_m2_per_s < 0 || viscosity_m2_per_s < 0 ||
        maximum_timestep_s <= 0 || maximum_timestep_s > 10 ||
        maximum_outgoing_fraction <= 0 || maximum_outgoing_fraction > 0.5 ||
        (pressure_preconditioner != PressurePreconditioner::automatic &&
         pressure_preconditioner != PressurePreconditioner::jacobi &&
         pressure_preconditioner != PressurePreconditioner::incomplete_cholesky))
        throw std::invalid_argument("Invalid fluid solver settings");
}

FluidWorld::FluidWorld(std::size_t width, std::size_t height, double dx, double depth,
                      FluidSolverSettings settings, SpeciesRegistry registry)
    : registry_(std::move(registry)), width_(width), height_(height), dx_(dx), depth_(depth), settings_(settings) {
    settings_.validate();
    require_finite(dx, "cell size"); require_finite(depth, "depth");
    if (width == 0 || height == 0 || width > 1000000 || height > 1000000 || width > 1000000 / height ||
        dx <= 0 || depth <= 0 || !std::isfinite(volume()) || volume() <= 0 || registry_.size() == 0)
        throw std::invalid_argument("Invalid fluid world dimensions or registry");
    cells_.assign(width * height, FluidCell(registry_.size()));
}

FluidWorld::FluidWorld(std::size_t width, std::size_t height, double dx, double depth,
    FluidSolverSettings settings, SpeciesRegistry registry, FluidGeometry1D geometry)
    : FluidWorld(width, height, dx, depth, settings, std::move(registry)) {
    if (!geometry.thermodynamics ||
        (geometry.axis != FluidAxis::x && geometry.axis != FluidAxis::y) ||
        (geometry.axis == FluidAxis::x ? height != 1 : width != 1) ||
        (geometry.axis == FluidAxis::x && settings.gravity != 0) ||
        settings.conduction || settings.diffusivity_m2_per_s != 0 || settings.viscosity_m2_per_s != 0)
        throw std::invalid_argument("Occupied 1-D mode requires an EOS profile, one axis, and disabled diffusion/viscosity/conduction");
    if (geometry.liquid_fill_roundoff != LiquidFillRoundoffPolicy::strict_eos &&
        geometry.liquid_fill_roundoff != LiquidFillRoundoffPolicy::balanced_local_roundoff)
        throw std::invalid_argument("Invalid liquid fill representability policy");
    geometry.thermodynamics->validate_registry(registry_);
    geometry_ = std::move(geometry);
}

std::size_t FluidWorld::offset(std::size_t x, std::size_t y) const {
    if (x >= width_ || y >= height_) throw std::out_of_range("Fluid cell coordinates");
    return y * width_ + x;
}
const FluidCell& FluidWorld::cell(std::size_t x, std::size_t y) const { return cells_[offset(x, y)]; }
double FluidWorld::pressure(std::size_t x, std::size_t y) const {
    const auto& c = cell(x, y);
    if (c.wall || c.mixture.mass() == 0) return 0;
    if (geometry_) return thermodynamic_state(x, y).mechanical_pressure_pa;
    return c.mixture.gas_moles(registry_) == 0 ? c.liquid_pressure_pa : c.mixture.pressure(registry_, volume());
}
double FluidWorld::liquid_fraction(std::size_t x, std::size_t y) const {
    if (geometry_) return thermodynamic_state(x, y).liquid_volume_m3 / volume();
    return cell(x, y).mixture.condensed_volume(registry_) / volume();
}

double FluidWorld::occupied_volume(std::size_t x, std::size_t y) const {
    const auto& c = cell(x, y);
    if (c.wall || c.mixture.mass() == 0) return 0;
    return geometry_ ? (c.occupied_end_m - c.occupied_begin_m) * dx_ * depth_ : volume();
}

experimental::ReferenceEquilibriumState FluidWorld::thermodynamic_state(std::size_t x, std::size_t y) const {
    if (!geometry_) throw std::invalid_argument("Thermodynamic profile reading requires occupied geometry mode");
    const auto& c = cell(x, y);
    if (c.wall || c.mixture.mass() == 0) {
        experimental::ReferenceEquilibriumState empty;
        empty.vacuum_volume_m3 = volume(); empty.has_vacuum = true;
        return empty; // T=0 is an absent-temperature sentinel, never a heat body.
    }
    return geometry_->thermodynamics->evaluate(c.mixture, occupied_volume(x, y));
}

void FluidWorld::validate_cell(const FluidCell& c, bool require_filled) const {
    validate_passive_quantities(c.passive_quantities);
    if ((c.wall || c.mixture.mass() == 0 || geometry_) && !c.passive_quantities.empty())
        throw std::invalid_argument("Passive metadata requires occupied closed-mode fluid; solid/vacuum/1D geometry is unsupported");
    if (geometry_) {
        geometry_->thermodynamics->validate_inventory(c.mixture);
        require_finite(c.momentum_x, "occupied x momentum"); require_finite(c.momentum_y, "occupied y momentum");
        require_finite(c.occupied_begin_m, "occupied interval beginning");
        require_finite(c.occupied_end_m, "occupied interval end");
        if (c.wall || c.mixture.mass() == 0) {
            if (c.mixture.mass() != 0 || c.mixture.energy != 0 || c.momentum_x != 0 || c.momentum_y != 0 ||
                c.occupied_begin_m != 0 || c.occupied_end_m != 0)
                throw std::invalid_argument("Occupied-mode wall/vacuum must be exactly empty");
            return;
        }
        if (c.occupied_begin_m < 0 || c.occupied_end_m > dx_ || c.occupied_begin_m >= c.occupied_end_m ||
            (geometry_->axis == FluidAxis::x ? c.momentum_y != 0 : c.momentum_x != 0))
            throw std::invalid_argument("Invalid 1-D occupied interval or transverse momentum");
        require_finite(c.kinetic_energy(), "occupied kinetic energy");
        const double occupied = (c.occupied_end_m - c.occupied_begin_m) * dx_ * depth_;
        const auto state = geometry_->thermodynamics->evaluate(c.mixture, occupied);
        if (state.vacuum_volume_m3 > 32 * std::numeric_limits<double>::epsilon() * occupied)
            throw FluidFatalError(FluidFatalCode::unsupported_topology,
                "Internal liquid void requires cavitation/interface topology support; occupied volume excludes exterior vacuum");
        return;
    }
    c.mixture.validate(registry_);
    require_finite(c.momentum_x, "fluid x momentum"); require_finite(c.momentum_y, "fluid y momentum");
    require_finite(c.liquid_pressure_pa, "liquid pressure");
    if (c.liquid_pressure_pa < 0) throw std::invalid_argument("Liquid tension requires cavitation support");
    if (c.wall || c.mixture.mass() == 0) {
        if (c.mixture.mass() != 0 || c.momentum_x != 0 || c.momentum_y != 0)
            throw std::invalid_argument("Wall/vacuum contains material or momentum");
        if (require_filled && !c.wall)
            throw std::invalid_argument("Experimental fluid world requires filled non-wall cells; vacuum is unsupported");
        return;
    }
    require_finite(c.kinetic_energy(), "fluid kinetic energy");
    for (std::size_t s = 0; s < registry_.size(); ++s)
        if (c.mixture.masses[s] != 0 && registry_[s].phase == Phase::solid)
            throw std::invalid_argument("Fluid world cannot advect fixed solid species");
    const double condensed = c.mixture.condensed_volume(registry_);
    if (condensed > volume() * (1 + 1e-12))
        throw std::invalid_argument("Liquid exceeds cell volume; displacement is required");
    if (c.mixture.gas_moles(registry_) == 0) {
        if (std::abs(condensed - volume()) > volume() * 1e-12)
            throw std::invalid_argument("Partial pure liquid fill requires a free-surface treatment");
    } else if (condensed >= volume()) throw std::invalid_argument("Gas has no headspace");
}

void FluidWorld::set_cell(std::size_t x, std::size_t y, const MixtureInventory& mixture,
                          double ux, double uy, double liquid_pressure) {
    require_finite(ux, "fluid x velocity"); require_finite(uy, "fluid y velocity");
    set_conserved_cell(x, y, mixture, mixture.mass() * ux, mixture.mass() * uy, liquid_pressure);
}
void FluidWorld::set_conserved_cell(std::size_t x, std::size_t y, const MixtureInventory& mixture,
    double px, double py, double liquid_pressure, std::span<const PassiveQuantity> passives) {
    if (geometry_) throw std::invalid_argument("Use set_occupied_cell with explicit geometry and conserved elastic energy");
    const auto index = offset(x, y);
    if (cells_[index].wall) throw std::invalid_argument("Displace wall before placing fluid");
    FluidCell next(registry_.size());
    next.mixture = mixture; next.momentum_x = px; next.momentum_y = py;
    next.liquid_pressure_pa = liquid_pressure;
    next.passive_quantities.assign(passives.begin(), passives.end());
    validate_cell(next, false);
    cells_[index] = std::move(next);
}

void FluidWorld::set_occupied_cell(std::size_t x, std::size_t y, const MixtureInventory& mixture,
    double begin, double end, double velocity) {
    if (!geometry_) throw std::invalid_argument("Occupied setter requires explicit 1-D geometry mode");
    const auto index = offset(x, y);
    if (cells_[index].wall) throw std::invalid_argument("Displace wall before placing occupied fluid");
    require_finite(velocity, "occupied axial velocity");
    FluidCell next(registry_.size()); next.mixture = mixture;
    next.occupied_begin_m = begin; next.occupied_end_m = end;
    auto& momentum = geometry_->axis == FluidAxis::x ? next.momentum_x : next.momentum_y;
    momentum = mixture.mass() * velocity;
    if (velocity != 0 && mixture.mass() > 0 && momentum == 0)
        throw FluidFatalError(FluidFatalCode::numerical_precision, "Occupied momentum is unrepresentable");
    validate_cell(next, false);
    cells_[index] = std::move(next);
}
void FluidWorld::set_wall(std::size_t x, std::size_t y, bool wall) {
    auto& c = cells_[offset(x, y)];
    if (wall && c.mixture.mass() != 0) throw std::invalid_argument("Displace fluid before constructing a wall");
    c.wall = wall;
}

void FluidWorld::validate_occupied_geometry() const {
    std::optional<std::size_t> previous;
    for (std::size_t i = 0; i < cells_.size(); ++i) {
        validate_cell(cells_[i], false);
        if (cells_[i].mixture.mass() == 0) continue;
        if (previous && (i != *previous + 1 || cells_[*previous].occupied_end_m != dx_ || cells_[i].occupied_begin_m != 0))
            throw FluidFatalError(FluidFatalCode::unsupported_topology, "Occupied 1-D mode currently requires one connected material interval");
        previous = i;
    }
}

FluidWorld::Candidate FluidWorld::advance_occupied(double dt) const {
    const auto& model = *geometry_->thermodynamics;
    const double area = dx_ * depth_, acceleration = geometry_->axis == FluidAxis::y ? -settings_.gravity : 0;
    const double potential_gradient = geometry_->axis == FluidAxis::y ? settings_.gravity : 0;
    std::vector<std::size_t> active;
    std::vector<double> masses, velocities;
    std::vector<MaterialCell> material;
    for (std::size_t i = 0; i < cells_.size(); ++i) if (cells_[i].mixture.mass() > 0) {
        active.push_back(i);
        masses.push_back(cells_[i].mixture.mass());
        velocities.push_back((geometry_->axis == FluidAxis::x ? cells_[i].momentum_x : cells_[i].momentum_y) / masses.back());
        const double occupied = (cells_[i].occupied_end_m - cells_[i].occupied_begin_m) * dx_ * depth_;
        material.push_back({cells_[i].mixture, occupied, 0});
    }
    Candidate result;
    if (active.empty()) { result.cells = cells_; return result; }
    std::vector<OccupiedMaterialNode> nodes;
    std::vector<OccupiedMeshPosition> positions;
    nodes.reserve(active.size() + 1); positions.reserve(active.size() + 1);
    for (std::size_t j = 0; j <= active.size(); ++j) {
        const bool left = j > 0, right = j < active.size();
        const auto i = left ? active[j - 1] : active.front();
        const bool fixed = !left ? (cells_[i].occupied_begin_m == 0 && (i == 0 || cells_[i - 1].wall)) :
            !right && cells_[i].occupied_end_m == dx_ && (i + 1 == cells_.size() || cells_[i + 1].wall);
        const double ml = left ? masses[j - 1] : 0, mr = right ? masses[j] : 0;
        const double inertia = 0.5 * ml + 0.5 * mr;
        // Difference form preserves an exactly uniform velocity without a
        // multiply/add/divide round trip, including unequal cut-cell masses.
        const double velocity = fixed ? 0 : !left ? velocities[j] : !right ? velocities[j - 1] :
            velocities[j - 1] + (0.5 * mr / inertia) * (velocities[j] - velocities[j - 1]);
        nodes.push_back({left ? j - 1 : no_material_cell, right ? j : no_material_cell,
                         area, inertia, velocity, acceleration, fixed});
        const OccupiedMeshPosition position = !left ? OccupiedMeshPosition{i, cells_[i].occupied_begin_m} :
            right ? OccupiedMeshPosition{active[j], 0} : OccupiedMeshPosition{i, cells_[i].occupied_end_m};
        positions.push_back(position);
    }
    auto pressure = advance_occupied_material_pressure(model, material, nodes, dt, settings_.pressure_preconditioner,
        geometry_->liquid_fill_roundoff);
    result.pressure_iterations = pressure.pressure_iterations; result.nonlinear_iterations = pressure.nonlinear_iterations;
    result.maximum_linear_residual_norm = pressure.maximum_linear_residual_norm;
    result.maximum_linear_residual_ratio = pressure.maximum_linear_residual_ratio;
    result.maximum_volume_residual_m3 = pressure.maximum_volume_residual_m3;
    result.incomplete_cholesky_solves = pressure.incomplete_cholesky_solves;
    result.jacobi_solves = pressure.jacobi_solves; result.preconditioner_fallbacks = pressure.preconditioner_fallbacks;
    result.wall_reaction_impulse_kg_m_per_s += pressure.wall_reaction_impulse_kg_m_per_s;
    result.body_impulse_kg_m_per_s = pressure.body_impulse_kg_m_per_s;
    result.pressure_work_roundoff_j = pressure.pressure_work_roundoff_j;
    result.unresolved_energy_updates += pressure.unresolved_energy_updates;
    result.unresolved_volume_updates = pressure.unresolved_volume_updates;
    result.maximum_volume_roundoff_m3 = pressure.maximum_volume_roundoff_m3;
    result.maximum_volume_addition_roundoff_m3 = pressure.maximum_volume_addition_roundoff_m3;
    result.maximum_root_arithmetic_roundoff_m3 = pressure.maximum_root_arithmetic_roundoff_m3;
    result.maximum_endpoint_relative_volume_residual = pressure.maximum_endpoint_relative_volume_residual;
    result.zero_work_substitutions = pressure.zero_work_substitutions;
    result.maximum_omitted_pressure_pa = pressure.maximum_omitted_pressure_pa;
    result.omitted_elastic_work_bound_j = pressure.omitted_elastic_work_bound_j;
    auto next_positions = positions;
    bool moved = false;
    for (std::size_t j = 0; j < nodes.size(); ++j) {
        const double delta = dt * (0.5 * nodes[j].velocity_m_per_s + 0.5 * pressure.velocities_m_per_s[j]);
        if (std::abs(delta) > settings_.maximum_outgoing_fraction * dx_)
            throw std::domain_error("Occupied node displacement exceeded grid-spacing CFL; retry smaller timestep");
        next_positions[j].offset_m += delta;
        require_finite(next_positions[j].offset_m, "occupied node position");
        const double position_roundoff = std::abs((next_positions[j].offset_m - positions[j].offset_m) - delta);
        result.maximum_position_roundoff_m = std::max(result.maximum_position_roundoff_m, position_roundoff);
        if (position_roundoff > 32 * std::numeric_limits<double>::epsilon() * dx_)
            throw FluidFatalError(FluidFatalCode::numerical_precision, "Occupied node position rounding exceeds local grid resolution");
        if (position_roundoff > 32 * std::numeric_limits<double>::epsilon() * std::abs(delta)) ++result.unresolved_position_updates;
        moved = moved || next_positions[j].offset_m != positions[j].offset_m;
    }
    std::vector<OccupiedTransportCell> moving;
    moving.reserve(active.size());
    long double energy_scale = 0;
    for (std::size_t i = 0; i < active.size(); ++i) {
        // Transfer the pressure/body-force INCREMENT, retaining canonical
        // cell momentum. Replacing it by the average endpoint node velocity
        // would apply an O(1) PIC smoothing even as dt -> 0. Initial node
        // velocities are the half-mass adjoint of this increment transfer,
        // so their linear kinetic-work terms agree exactly in real arithmetic.
        const double dul = pressure.velocities_m_per_s[i] - nodes[i].velocity_m_per_s;
        const double dur = pressure.velocities_m_per_s[i + 1] - nodes[i + 1].velocity_m_per_s;
        const double difference = dur - dul;
        // Only the unresolved incremental kinetic variance becomes heat:
        // 1/2 sum I du^2 - 1/2 sum M dv^2 = sum M/8 (duR-duL)^2.
        // It is nonnegative and O(dt^2), unlike the old whole-velocity defect.
        const double heat = 0.125 * masses[i] * difference * difference;
        result.final_projection_heat_j += heat;
        auto& mixture = pressure.cells[i].mixture; const double previous_energy = mixture.energy; mixture.energy += heat;
        const double rounding = (mixture.energy - previous_energy) - heat;
        result.projection_heat_roundoff_j += rounding;
        if (std::abs(rounding) > 32 * std::numeric_limits<double>::epsilon() * heat) ++result.unresolved_energy_updates;
        require_finite(heat, "compatible incremental kinetic heat");
        const double old_momentum = geometry_->axis == FluidAxis::x ? cells_[active[i]].momentum_x : cells_[active[i]].momentum_y;
        const double momentum = old_momentum + masses[i] * (0.5 * dul + 0.5 * dur);
        if (occupied_interval_length(next_positions[i], next_positions[i + 1], dx_) <= 0)
            throw FluidFatalError(FluidFatalCode::numerical_precision, "Positive pressure endpoint has nonpositive represented occupied geometry");
        const double represented_volume = area * occupied_interval_length(next_positions[i], next_positions[i + 1], dx_);
        // Compare with the explicit requested change, not the independently
        // rounded V0+dV endpoint. A rapidly expanding tiny cut can have an
        // endpoint ULP larger than its initial-volume budget; subtracting that
        // rounded endpoint injects its addition error into the geometry test.
        AccurateSum geometry_residual;
        geometry_residual.add(represented_volume); geometry_residual.add(-material[i].volume_m3);
        geometry_residual.add(-pressure.volume_changes_m3[i]); geometry_residual.add(pressure.volume_residuals_m3[i]);
        const double geometry_roundoff = std::abs(geometry_residual.value());
        result.signed_geometry_volume_residual_m3 += geometry_residual.value();
        result.absolute_geometry_volume_residual_m3 += geometry_roundoff;
        result.maximum_volume_roundoff_m3 = std::max(result.maximum_volume_roundoff_m3, geometry_roundoff);
        if (geometry_roundoff > 32 * std::numeric_limits<double>::epsilon() * pressure.cells[i].volume_m3) {
            std::ostringstream message;
            message << std::setprecision(17) << "Occupied endpoint rounding materially changes its local material volume; cell=" << active[i] <<
                " initial=" << material[i].volume_m3 << " requested=" << pressure.cells[i].volume_m3 <<
                " requested_change=" << pressure.volume_changes_m3[i] <<
                " represented=" << represented_volume << " root_residual=" << pressure.volume_residuals_m3[i] <<
                " roundoff=" << geometry_roundoff;
            throw FluidFatalError(FluidFatalCode::numerical_precision, message.str());
        }
        if (pressure.zero_work_substituted[i]) {
            const auto anchor = model.reference().make_isentrope(model.to_reference(material[i].mixture, material[i].volume_m3));
            auto represented = anchor.reference_cell(); represented.volume_m3 = represented_volume;
            if (!model.liquid_fill_resolution(represented).within_roundoff)
                throw FluidFatalError(FluidFatalCode::numerical_precision, "Represented liquid endpoint left the explicit fill-roundoff policy domain");
            const auto represented_response = model.reference().isentropic_at_volume(anchor, represented_volume);
            const auto requested_response = model.reference().isentropic_response(anchor, pressure.volume_changes_m3[i]);
            const double bound = std::max(std::abs(represented_response.internal_energy_change_j),
                std::abs(requested_response.internal_energy_change_j));
            const double local_scale = anchor.reference_state().thermal_energy_j + anchor.reference_state().elastic_energy_j;
            if (bound > 64 * std::numeric_limits<double>::epsilon() * local_scale)
                throw FluidFatalError(FluidFatalCode::numerical_precision, "Represented fill-roundoff approximation has material local elastic work");
            // Pressure already included the requested endpoint's bound. Replace
            // it with the larger requested/represented bound, never subtract
            // away a positive uncertainty when geometry rounded differently.
            result.omitted_elastic_work_bound_j += bound - std::abs(requested_response.internal_energy_change_j);
            result.maximum_omitted_pressure_pa = std::max(result.maximum_omitted_pressure_pa,
                std::max(represented_response.work_pressure_pa, represented_response.state.mechanical_pressure_pa));
        }
        moving.push_back({std::move(mixture), next_positions[i], next_positions[i + 1], momentum});
        const auto before = model.evaluate(cells_[active[i]].mixture, material[i].volume_m3);
        energy_scale += before.thermal_energy_j + before.elastic_energy_j + cells_[active[i]].kinetic_energy() +
            std::abs(cells_[active[i]].mixture.mass() * potential_gradient *
                (static_cast<double>(positions[i].grid_line) * dx_ + positions[i].offset_m +
                    0.5 * occupied_interval_length(positions[i], positions[i + 1], dx_)));
    }
    // Avoid an unnecessary absolute/local coordinate round trip when a state
    // has exactly zero material displacement and no projection/work change.
    if (!moved && result.initial_projection_heat_j == 0 && result.final_projection_heat_j == 0) {
        bool unchanged = true;
        for (std::size_t i = 0; i < active.size(); ++i) unchanged = unchanged &&
            moving[i].mixture.energy == cells_[active[i]].mixture.energy &&
            moving[i].momentum_kg_m_per_s == (geometry_->axis == FluidAxis::x ? cells_[active[i]].momentum_x : cells_[active[i]].momentum_y);
        if (unchanged) { result.cells = cells_; return result; }
    }
    std::vector<std::uint8_t> walls;
    walls.reserve(cells_.size()); for (const auto& c : cells_) walls.push_back(c.wall ? 1 : 0);
    auto remapped = remap_occupied_mixtures(model, moving, walls, dx_, area, potential_gradient);
    result.cells.assign(cells_.size(), FluidCell(registry_.size()));
    AccurateSum energy_change, old_momentum, new_momentum;
    long double momentum_scale = std::abs(result.wall_reaction_impulse_kg_m_per_s) + std::abs(result.body_impulse_kg_m_per_s);
    for (std::size_t i = 0; i < cells_.size(); ++i) {
        const auto& old = cells_[i]; auto& next = result.cells[i];
        if (old.wall) { next = old; continue; }
        auto& source = remapped[i]; next.mixture = std::move(source.mixture);
        if (next.mixture.mass() > 0) {
            next.occupied_begin_m = source.begin.offset_m;
            next.occupied_end_m = source.end.offset_m;
            (geometry_->axis == FluidAxis::x ? next.momentum_x : next.momentum_y) = source.momentum_kg_m_per_s;
            const double occupied = area * (next.occupied_end_m - next.occupied_begin_m);
            next.liquid_pressure_pa = model.evaluate(next.mixture, occupied).mechanical_pressure_pa;
        }
        validate_cell(next, false);
        const double old_height = geometry_->axis == FluidAxis::y ? static_cast<double>(i) * dx_ +
            0.5 * (old.occupied_begin_m + old.occupied_end_m) : 0;
        const double new_height = geometry_->axis == FluidAxis::y ? static_cast<double>(i) * dx_ +
            0.5 * (next.occupied_begin_m + next.occupied_end_m) : 0;
        // Accumulate changes by cell, before summing large formation references.
        energy_change.add(next.mixture.energy - old.mixture.energy);
        energy_change.add(next.kinetic_energy() - old.kinetic_energy());
        energy_change.add(next.mixture.mass() * settings_.gravity * new_height - old.mixture.mass() * settings_.gravity * old_height);
        const double po = geometry_->axis == FluidAxis::x ? old.momentum_x : old.momentum_y;
        const double pn = geometry_->axis == FluidAxis::x ? next.momentum_x : next.momentum_y;
        old_momentum.add(po); new_momentum.add(pn); momentum_scale += std::abs(po) + std::abs(pn);
    }
    const double error = energy_change.value();
    result.maximum_conservation_error_j = std::abs(error);
    if (!std::isfinite(error) || std::abs(error) > 2e-10L * energy_scale)
        throw FluidFatalError(FluidFatalCode::numerical_precision, "Occupied world failed represented total-energy balance");
    const double momentum_error = new_momentum.value() - old_momentum.value() -
        result.wall_reaction_impulse_kg_m_per_s - result.body_impulse_kg_m_per_s;
    if (!std::isfinite(momentum_error) || std::abs(momentum_error) > 128 * std::numeric_limits<double>::epsilon() * momentum_scale)
        throw FluidFatalError(FluidFatalCode::numerical_precision, "Occupied world failed total-momentum balance");
    return result;
}

FluidWorld::Candidate FluidWorld::advance(double dt) const {
    if (geometry_) return advance_occupied(dt);
    const auto none = cells_.size();
    const double area = dx_ * depth_, vol = volume();
    std::vector<std::size_t> active, local(cells_.size(), none);
    std::vector<MaterialCell> material;
    std::vector<double> potentials, targets;
    active.reserve(cells_.size()); material.reserve(cells_.size());
    for (std::size_t i = 0; i < cells_.size(); ++i) if (!cells_[i].wall) {
        local[i] = active.size(); active.push_back(i);
        material.push_back({cells_[i].mixture, vol, cells_[i].liquid_pressure_pa});
        potentials.push_back(settings_.gravity * (static_cast<double>(i / width_) + 0.5) * dx_);
        targets.push_back(vol);
    }
    Candidate result;
    if (active.empty()) { result.cells = cells_; return result; }
    std::vector<Face> geometry;
    std::vector<AcousticFace> faces;
    geometry.reserve(2 * active.size()); faces.reserve(2 * active.size());
    const auto add_face = [&](std::size_t first, std::size_t second, int axis) {
        if (local[first] == none || local[second] == none) return;
        const auto& a = cells_[first]; const auto& b = cells_[second];
        const double ma = a.mixture.mass(), mb = b.mixture.mass();
        const double ua = (axis == 0 ? a.momentum_x : a.momentum_y) / ma;
        const double ub = (axis == 0 ? b.momentum_x : b.momentum_y) / mb;
        geometry.push_back({local[first], local[second], axis});
        faces.push_back({local[first], local[second], area, 0.5 * (ma + mb),
                         0.5 * (ua + ub), axis == 0 ? 0 : -settings_.gravity});
    };
    for (std::size_t y = 0; y < height_; ++y) for (std::size_t x = 0; x < width_; ++x) {
        const auto i = y * width_ + x;
        if (x + 1 < width_) add_face(i, i + 1, 0);
        if (y + 1 < height_) add_face(i, i + width_, 1);
    }
    auto pressure = advance_material_pressure(registry_, material, faces, dt, true, settings_.pressure_preconditioner);
    result.pressure_iterations = pressure.pressure_iterations;
    result.nonlinear_iterations = pressure.nonlinear_iterations;
    result.maximum_linear_residual_norm = pressure.maximum_linear_residual_norm;
    result.maximum_linear_residual_ratio = pressure.maximum_linear_residual_ratio;
    result.maximum_volume_residual_m3 = pressure.maximum_volume_residual_m3;
    result.incomplete_cholesky_solves = pressure.incomplete_cholesky_solves;
    result.jacobi_solves = pressure.jacobi_solves;
    result.preconditioner_fallbacks = pressure.preconditioner_fallbacks;
    std::vector<SweptFace> sweeps;
    std::vector<double> outgoing(active.size(), 0), mechanical(active.size(), 0);
    std::vector<TransportCell> moving;
    moving.reserve(active.size()); sweeps.reserve(faces.size());
    for (std::size_t i = 0; i < active.size(); ++i) {
        const auto& c = cells_[active[i]];
        // These material inventories are consumed only by remap from here on;
        // endpoint pressures/volumes remain separately available for publish.
        moving.push_back({std::move(pressure.cells[i].mixture), pressure.cells[i].volume_m3, c.momentum_x, c.momentum_y, c.passive_quantities});
        mechanical[i] = c.kinetic_energy();
    }
    for (std::size_t f = 0; f < faces.size(); ++f) {
        const auto& face = faces[f]; const double next = pressure.velocities_m_per_s[f];
        const double mean = 0.5 * (face.velocity_m_per_s + next);
        const double swept = dt * area * mean;
        sweeps.push_back({face.left, face.right, swept});
        outgoing[swept >= 0 ? face.left : face.right] += std::abs(swept);
        const double impulse = 0.5 * face.inertia_kg * (next - face.velocity_m_per_s);
        const double kinetic_change = 0.5 * face.inertia_kg * (next - face.velocity_m_per_s) * mean;
        // The pressure solve accounts staggered gravity work. Replace it with
        // actual transported potential energy during remap, avoiding a density-
        // dependent energy leak at gas/liquid interfaces.
        const double body_work = 0.5 * dt * face.inertia_kg * face.acceleration_m_per_s2 * mean;
        for (const auto i : {face.left, face.right}) {
            if (geometry[f].axis == 0) moving[i].momentum_x += impulse;
            else moving[i].momentum_y += impulse;
            mechanical[i] += kinetic_change - body_work;
        }
    }
    for (std::size_t i = 0; i < moving.size(); ++i) {
        if (outgoing[i] > settings_.maximum_outgoing_fraction * moving[i].volume_m3)
            throw std::domain_error("Fluid remap CFL exceeded; retry smaller timestep");
        moving[i].mixture.energy += mechanical[i] - moving[i].kinetic_energy();
        moving[i].mixture.validate(registry_);
    }
    auto remapped = remap_material_mixtures(registry_, moving, sweeps, targets, potentials);

    // Pair exchanges are symmetric and bounded. Each species retains its own
    // mass, including traces; diffusion does not invent an effective species.
    for (const auto& f : geometry) {
        auto& a = remapped[f.left]; auto& b = remapped[f.right];
        if (settings_.diffusivity_m2_per_s > 0) {
            double ga = 0, gb = 0;
            for (std::size_t s = 0; s < registry_.size(); ++s) if (registry_[s].phase == Phase::gas) {
                ga += a.mixture.masses[s]; gb += b.mixture.masses[s];
            }
            if (ga > 0 && gb > 0) {
                const double exchange = std::min(ga, gb) * std::min(0.1, settings_.diffusivity_m2_per_s * dt / (dx_ * dx_));
                const double fa = exchange / ga, fb = exchange / gb;
                // Core metadata follows the same mass-limited bulk parcels as
                // extraction. Gas exchange carries exchange/total_mass of each
                // well-mixed cell's passive inventory, not exchange/gas_mass.
                if (!a.passive_quantities.empty() || !b.passive_quantities.empty()) {
                    const double qa = exchange / a.mixture.mass(), qb = exchange / b.mixture.mass();
                    auto pa = scale_passive_quantities(a.passive_quantities, 1 - qa);
                    auto pb = scale_passive_quantities(b.passive_quantities, 1 - qb);
                    add_passive_quantities(pa, b.passive_quantities, qb);
                    add_passive_quantities(pb, a.passive_quantities, qa);
                    a.passive_quantities = std::move(pa); b.passive_quantities = std::move(pb);
                }
                const double ta = a.mixture.temperature(registry_), tb = b.mixture.temperature(registry_);
                double heat = 0;
                for (std::size_t s = 0; s < registry_.size(); ++s) if (registry_[s].phase == Phase::gas) {
                    const double ma = a.mixture.masses[s], mb = b.mixture.masses[s];
                    a.mixture.masses[s] = (1 - fa) * ma + fb * mb;
                    b.mixture.masses[s] = (1 - fb) * mb + fa * ma;
                    heat += fa * ma * registry_[s].internal_energy(ta) - fb * mb * registry_[s].internal_energy(tb);
                }
                a.mixture.energy -= heat; b.mixture.energy += heat;
            }
        }
        if (settings_.viscosity_m2_per_s > 0) {
            const double ma = a.mixture.mass(), mb = b.mixture.mass();
            const double coefficient = std::min(0.1, settings_.viscosity_m2_per_s * dt / (dx_ * dx_)) * ma * mb / (ma + mb);
            const double olda = a.kinetic_energy(), oldb = b.kinetic_energy();
            const double ix = coefficient * (b.momentum_x / mb - a.momentum_x / ma);
            const double iy = coefficient * (b.momentum_y / mb - a.momentum_y / ma);
            a.momentum_x += ix; a.momentum_y += iy; b.momentum_x -= ix; b.momentum_y -= iy;
            const double loss = olda + oldb - a.kinetic_energy() - b.kinetic_energy();
            a.mixture.energy += 0.5 * loss; b.mixture.energy += 0.5 * loss;
        }
    }
    if (settings_.conduction) {
        std::vector<ThermalBody> bodies; std::vector<ThermalContact> contacts;
        std::vector<double> conductivity;
        bodies.reserve(remapped.size()); conductivity.reserve(remapped.size()); contacts.reserve(faces.size());
        double minimum = std::numeric_limits<double>::max(), maximum = 0;
        for (const auto& c : remapped) {
            const double t = c.mixture.temperature(registry_);
            bodies.push_back({c.mixture.heat_capacity(registry_), t});
            minimum = std::min(minimum, t); maximum = std::max(maximum, t);
            double k = 0;
            for (std::size_t s = 0; s < registry_.size(); ++s) k += c.mixture.masses[s] * registry_[s].conductivity;
            conductivity.push_back(k / c.mixture.mass());
        }
        if (maximum - minimum > 1e-12 * maximum) {
            for (const auto& f : geometry) contacts.push_back({f.left, f.right,
                thermal_conductance(conductivity[f.left], conductivity[f.right], 0.5 * dx_, 0.5 * dx_, area)});
            const auto heat = conduct_heat(bodies, contacts, dt);
            result.thermal_iterations = heat.iterations;
            for (std::size_t i = 0; i < remapped.size(); ++i) remapped[i].mixture.energy += heat.energy_changes_j[i];
        }
    }
    // Every active inventory is replaced below. Only walls need an old-state
    // copy; preserve their metadata and correctly sized empty species vector.
    result.cells.resize(cells_.size());
    for (std::size_t i = 0; i < cells_.size(); ++i) if (cells_[i].wall) result.cells[i] = cells_[i];
    for (std::size_t i = 0; i < active.size(); ++i) {
        auto& c = result.cells[active[i]];
        c.mixture = std::move(remapped[i].mixture);
        c.passive_quantities = std::move(remapped[i].passive_quantities);
        c.momentum_x = remapped[i].momentum_x; c.momentum_y = remapped[i].momentum_y;
        c.liquid_pressure_pa = pressure.endpoint_pressures_pa[i];
        try { validate_cell(c, true); }
        catch (const std::invalid_argument& e) { throw std::domain_error(e.what()); }
    }
    return result;
}

FluidStepResult FluidWorld::step(double seconds, std::uint64_t budget) {
    require_finite(seconds, "fluid step duration");
    if (seconds < 0 || seconds > 10 || budget == 0) throw std::invalid_argument("Invalid fluid step duration or budget");
    if (seconds == 0) {
        FluidStepResult empty;
        empty.cumulative_omitted_elastic_work_bound_j = cumulative_omitted_elastic_work_bound_j_;
        return empty;
    }
    if (geometry_) validate_occupied_geometry();
    else for (const auto& c : cells_) validate_cell(c, true);
    const auto passive_before = passive_totals(cells_);
    // advance() does not mutate this world. Move the original allocation aside
    // at the first validated commit, retaining it for whole-call rollback.
    // Failure before that commit already leaves the original cells untouched.
    std::vector<FluidCell> backup;
    static_assert(noexcept(cells_ = std::move(backup)), "Whole-call rollback requires noexcept cell ownership transfer");
    bool published = false;
    FluidStepResult result;
    std::uint64_t attempts = 0;
    double dt = std::min(seconds, settings_.maximum_timestep_s);
    try {
        while (result.seconds < seconds) {
            dt = std::min(dt, seconds - result.seconds);
            if (dt <= 0 || result.seconds + dt == result.seconds) throw std::runtime_error("Fluid timestep underflow");
            if (++attempts > budget) throw std::runtime_error("Fluid attempt budget exceeded; whole step rolled back");
            try {
                auto candidate = advance(dt);
                if (!published) { backup = std::move(cells_); published = true; }
                cells_ = std::move(candidate.cells);
                result.pressure_iterations += candidate.pressure_iterations;
                result.nonlinear_iterations += candidate.nonlinear_iterations;
                result.thermal_iterations += candidate.thermal_iterations;
                result.maximum_linear_residual_norm = std::max(result.maximum_linear_residual_norm, candidate.maximum_linear_residual_norm);
                result.maximum_linear_residual_ratio = std::max(result.maximum_linear_residual_ratio, candidate.maximum_linear_residual_ratio);
                result.maximum_volume_residual_m3 = std::max(result.maximum_volume_residual_m3, candidate.maximum_volume_residual_m3);
                result.incomplete_cholesky_solves += candidate.incomplete_cholesky_solves;
                result.jacobi_solves += candidate.jacobi_solves;
                result.preconditioner_fallbacks += candidate.preconditioner_fallbacks;
                result.initial_projection_heat_j += candidate.initial_projection_heat_j;
                result.final_projection_heat_j += candidate.final_projection_heat_j;
                result.projection_heat_roundoff_j += candidate.projection_heat_roundoff_j;
                result.wall_reaction_impulse_kg_m_per_s += candidate.wall_reaction_impulse_kg_m_per_s;
                result.body_impulse_kg_m_per_s += candidate.body_impulse_kg_m_per_s;
                result.maximum_conservation_error_j = std::max(result.maximum_conservation_error_j, candidate.maximum_conservation_error_j);
                result.pressure_work_roundoff_j += candidate.pressure_work_roundoff_j;
                result.unresolved_energy_updates += candidate.unresolved_energy_updates;
                result.unresolved_volume_updates += candidate.unresolved_volume_updates;
                result.unresolved_position_updates += candidate.unresolved_position_updates;
                result.maximum_volume_roundoff_m3 = std::max(result.maximum_volume_roundoff_m3, candidate.maximum_volume_roundoff_m3);
                result.maximum_position_roundoff_m = std::max(result.maximum_position_roundoff_m, candidate.maximum_position_roundoff_m);
                result.maximum_volume_addition_roundoff_m3 = std::max(result.maximum_volume_addition_roundoff_m3,
                    candidate.maximum_volume_addition_roundoff_m3);
                result.maximum_root_arithmetic_roundoff_m3 = std::max(result.maximum_root_arithmetic_roundoff_m3,
                    candidate.maximum_root_arithmetic_roundoff_m3);
                result.maximum_endpoint_relative_volume_residual = std::max(result.maximum_endpoint_relative_volume_residual,
                    candidate.maximum_endpoint_relative_volume_residual);
                result.signed_geometry_volume_residual_m3 += candidate.signed_geometry_volume_residual_m3;
                result.absolute_geometry_volume_residual_m3 += candidate.absolute_geometry_volume_residual_m3;
                result.zero_work_substitutions += candidate.zero_work_substitutions;
                result.maximum_omitted_pressure_pa = std::max(result.maximum_omitted_pressure_pa, candidate.maximum_omitted_pressure_pa);
                result.omitted_elastic_work_bound_j += candidate.omitted_elastic_work_bound_j;
            } catch (const FluidFatalError&) {
                throw; // Unsupported topology/precision is not repaired by endless dt retries.
            } catch (const std::domain_error&) {
                ++result.rejected_substeps; dt *= 0.5; continue;
            } catch (const std::runtime_error&) {
                ++result.rejected_substeps; dt *= 0.5; continue;
            }
            result.seconds += dt; ++result.substeps;
            dt = std::min(settings_.maximum_timestep_s, 1.5 * dt);
        }
        if (!passive_before.empty()) {
            const auto after = passive_totals(cells_);
            if (after.size() != passive_before.size()) throw std::domain_error("Fluid passive identity set changed during closed transport");
            for (const auto& [identity, quantity] : passive_before) {
                const auto found = after.find(identity);
                if (found == after.end() || found->second <= 0 || std::abs(found->second - quantity) >
                    256 * std::numeric_limits<double>::epsilon() * std::max(quantity, found->second))
                    throw std::domain_error("Fluid passive transport failed its relative conservation bound");
            }
        }
        require_finite(time_ + seconds, "fluid simulation time");
        require_finite(cumulative_omitted_elastic_work_bound_j_ + result.omitted_elastic_work_bound_j, "cumulative omitted elastic work bound");
        time_ += seconds;
        cumulative_omitted_elastic_work_bound_j_ += result.omitted_elastic_work_bound_j;
        result.cumulative_omitted_elastic_work_bound_j = cumulative_omitted_elastic_work_bound_j_;
    } catch (...) {
        if (published) cells_ = std::move(backup);
        throw;
    }
    return result;
}

ConservationTotals FluidWorld::totals() const {
    ConservationTotals result{std::vector<double>(registry_.size(), 0), 0, 0, 0};
    std::vector<AccurateSum> masses(registry_.size());
    AccurateSum internal, kinetic, potential;
    for (std::size_t i = 0; i < cells_.size(); ++i) {
        const auto& c = cells_[i];
        for (std::size_t s = 0; s < registry_.size(); ++s) masses[s].add(c.mixture.masses[s]);
        internal.add(c.mixture.energy); kinetic.add(c.kinetic_energy());
        const double height = geometry_ ? (geometry_->axis == FluidAxis::y ?
            static_cast<double>(i) * dx_ + 0.5 * (c.occupied_begin_m + c.occupied_end_m) : 0) :
            (static_cast<double>(i / width_) + 0.5) * dx_;
        potential.add(c.mixture.mass() * settings_.gravity * height);
    }
    for (std::size_t s = 0; s < registry_.size(); ++s) result.masses[s] = masses[s].value();
    result.internal_energy = internal.value(); result.kinetic_energy = kinetic.value();
    result.potential_energy = potential.value();
    return result;
}
}
