#include "mixed_atmospheres/fluid_world.hpp"
#include "mixed_atmospheres/thermal.hpp"
#include "mixed_atmospheres/transport.hpp"
#include "mixed_atmospheres/volume_work.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

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
}
void FluidSolverSettings::validate() const {
    for (double value : {gravity, diffusivity_m2_per_s, viscosity_m2_per_s,
                         maximum_timestep_s, maximum_outgoing_fraction})
        require_finite(value, "fluid solver setting");
    if (gravity < 0 || diffusivity_m2_per_s < 0 || viscosity_m2_per_s < 0 ||
        maximum_timestep_s <= 0 || maximum_timestep_s > 10 ||
        maximum_outgoing_fraction <= 0 || maximum_outgoing_fraction > 0.5)
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

std::size_t FluidWorld::offset(std::size_t x, std::size_t y) const {
    if (x >= width_ || y >= height_) throw std::out_of_range("Fluid cell coordinates");
    return y * width_ + x;
}
const FluidCell& FluidWorld::cell(std::size_t x, std::size_t y) const { return cells_[offset(x, y)]; }
double FluidWorld::pressure(std::size_t x, std::size_t y) const {
    const auto& c = cell(x, y);
    if (c.wall || c.mixture.mass() == 0) return 0;
    return c.mixture.gas_moles(registry_) == 0 ? c.liquid_pressure_pa : c.mixture.pressure(registry_, volume());
}
double FluidWorld::liquid_fraction(std::size_t x, std::size_t y) const {
    return cell(x, y).mixture.condensed_volume(registry_) / volume();
}

void FluidWorld::validate_cell(const FluidCell& c, bool require_filled) const {
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
    const auto index = offset(x, y);
    if (cells_[index].wall) throw std::invalid_argument("Displace wall before placing fluid");
    require_finite(ux, "fluid x velocity"); require_finite(uy, "fluid y velocity");
    FluidCell next(registry_.size());
    next.mixture = mixture; next.momentum_x = mixture.mass() * ux; next.momentum_y = mixture.mass() * uy;
    next.liquid_pressure_pa = liquid_pressure;
    validate_cell(next, false);
    cells_[index] = std::move(next);
}
void FluidWorld::set_wall(std::size_t x, std::size_t y, bool wall) {
    auto& c = cells_[offset(x, y)];
    if (wall && c.mixture.mass() != 0) throw std::invalid_argument("Displace fluid before constructing a wall");
    c.wall = wall;
}

FluidWorld::Candidate FluidWorld::advance(double dt) const {
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
    const auto pressure = advance_material_pressure(registry_, material, faces, dt, true);
    result.pressure_iterations = pressure.pressure_iterations;
    result.nonlinear_iterations = pressure.nonlinear_iterations;
    std::vector<SweptFace> sweeps;
    std::vector<double> outgoing(active.size(), 0), mechanical(active.size(), 0);
    std::vector<TransportCell> moving;
    moving.reserve(active.size()); sweeps.reserve(faces.size());
    for (std::size_t i = 0; i < active.size(); ++i) {
        const auto& c = cells_[active[i]];
        moving.push_back({pressure.cells[i].mixture, pressure.cells[i].volume_m3, c.momentum_x, c.momentum_y});
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
    result.cells = cells_;
    for (std::size_t i = 0; i < active.size(); ++i) {
        auto& c = result.cells[active[i]];
        c.mixture = std::move(remapped[i].mixture);
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
    if (seconds == 0) return {};
    for (const auto& c : cells_) validate_cell(c, true);
    auto backup = cells_;
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
                cells_ = std::move(candidate.cells);
                result.pressure_iterations += candidate.pressure_iterations;
                result.nonlinear_iterations += candidate.nonlinear_iterations;
                result.thermal_iterations += candidate.thermal_iterations;
            } catch (const std::domain_error&) {
                ++result.rejected_substeps; dt *= 0.5; continue;
            } catch (const std::runtime_error&) {
                ++result.rejected_substeps; dt *= 0.5; continue;
            }
            result.seconds += dt; ++result.substeps;
            dt = std::min(settings_.maximum_timestep_s, 1.5 * dt);
        }
        require_finite(time_ + seconds, "fluid simulation time");
        time_ += seconds;
    } catch (...) {
        cells_ = std::move(backup);
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
        potential.add(c.mixture.mass() * settings_.gravity * (static_cast<double>(i / width_) + 0.5) * dx_);
    }
    for (std::size_t s = 0; s < registry_.size(); ++s) result.masses[s] = masses[s].value();
    result.internal_energy = internal.value(); result.kinetic_energy = kinetic.value();
    result.potential_energy = potential.value();
    return result;
}
}
