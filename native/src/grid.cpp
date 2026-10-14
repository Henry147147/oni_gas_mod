#include "mixed_atmospheres/grid.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace ma {
namespace {
struct Primitive {
    double density = 0, p = 0, t = 0, ux = 0, uy = 0, sound = 0, total_energy_density = 0;
    double conductivity = 0;
};
Primitive sample(const SpeciesRegistry& r, const GasCell& cell, double volume) {
    Primitive p;
    const double mass = cell.mixture.mass();
    if (cell.wall || mass == 0) return p;
    p.density = mass / volume;
    p.t = cell.mixture.temperature(r);
    p.ux = cell.momentum_x / mass;
    p.uy = cell.momentum_y / mass;
    p.p = cell.mixture.gas_moles(r) * gas_constant * p.t / volume;
    const double cv = cell.mixture.heat_capacity(r) / mass;
    const double specific_r = p.p / (p.density * p.t);
    p.sound = std::sqrt((1 + specific_r / cv) * specific_r * p.t);
    p.total_energy_density = (cell.mixture.energy + cell.kinetic_energy()) / volume;
    for (std::size_t i = 0; i < r.size(); ++i) p.conductivity += cell.mixture.masses[i] / mass * r[i].conductivity;
    return p;
}
struct Flux {
    std::vector<double> mass;
    double qx = 0, qy = 0, energy = 0;
    explicit Flux(std::size_t count) : mass(count, 0) {}
    void reset() { std::fill(mass.begin(), mass.end(), 0); qx = qy = energy = 0; }
};
struct Increment : Flux { explicit Increment(std::size_t count) : Flux(count) {} };

// Contact-resolving gas flux. Species fractions share exactly the same mass
// flux and energy flux; static composition interfaces do not numerically sort.
void riemann(const GasCell& left, const GasCell& right, Primitive pl, Primitive pr,
             double left_scale, double right_scale, double volume, int axis, Flux& out) {
    out.reset();
    if (pl.density == 0 && pr.density == 0) return;
    const double ul = axis == 0 ? pl.ux : pl.uy;
    const double ur = axis == 0 ? pr.ux : pr.uy;
    pl.density *= left_scale; pl.p *= left_scale; pl.total_energy_density *= left_scale;
    pr.density *= right_scale; pr.p *= right_scale; pr.total_energy_density *= right_scale;
    const double sl = std::min(ul - pl.sound, ur - pr.sound);
    const double sr = std::max(ul + pl.sound, ur + pr.sound);
    const auto physical = [&](const GasCell& c, const Primitive& p, double scale, double u) {
        for (std::size_t i = 0; i < out.mass.size(); ++i) out.mass[i] = c.mixture.masses[i] / volume * scale * u;
        out.qx = p.density * p.ux * u + (axis == 0 ? p.p : 0);
        out.qy = p.density * p.uy * u + (axis == 1 ? p.p : 0);
        out.energy = (p.total_energy_density + p.p) * u;
    };
    if (sl >= 0) { physical(left, pl, left_scale, ul); return; }
    if (sr <= 0) { physical(right, pr, right_scale, ur); return; }
    const double denom = pl.density * (sl - ul) - pr.density * (sr - ur);
    const double sm = (pr.p - pl.p + pl.density * ul * (sl - ul) - pr.density * ur * (sr - ur)) / denom;
    if (!std::isfinite(sm) || sm <= sl || sm >= sr) {
        // HLL fallback retains conservation, including for degenerate vacuum.
        for (std::size_t i = 0; i < out.mass.size(); ++i) {
            const double ml = left.mixture.masses[i] / volume * left_scale;
            const double mr = right.mixture.masses[i] / volume * right_scale;
            out.mass[i] = (sr * ml * ul - sl * mr * ur + sl * sr * (mr - ml)) / (sr - sl);
        }
        out.qx = (sr * (pl.density * pl.ux * ul + (axis == 0 ? pl.p : 0)) -
                  sl * (pr.density * pr.ux * ur + (axis == 0 ? pr.p : 0)) + sl * sr * (pr.density * pr.ux - pl.density * pl.ux)) / (sr - sl);
        out.qy = (sr * (pl.density * pl.uy * ul + (axis == 1 ? pl.p : 0)) -
                  sl * (pr.density * pr.uy * ur + (axis == 1 ? pr.p : 0)) + sl * sr * (pr.density * pr.uy - pl.density * pl.uy)) / (sr - sl);
        out.energy = (sr * (pl.total_energy_density + pl.p) * ul - sl * (pr.total_energy_density + pr.p) * ur +
                      sl * sr * (pr.total_energy_density - pl.total_energy_density)) / (sr - sl);
        return;
    }
    const bool use_left = sm >= 0;
    const auto& c = use_left ? left : right;
    const auto& p = use_left ? pl : pr;
    const double scale = use_left ? left_scale : right_scale;
    const double u = use_left ? ul : ur;
    const double s = use_left ? sl : sr;
    const double factor = (s - u) / (s - sm);
    physical(c, p, scale, u);
    for (std::size_t i = 0; i < out.mass.size(); ++i)
        out.mass[i] += s * c.mixture.masses[i] / volume * scale * (factor - 1);
    out.qx += s * p.density * (factor * (axis == 0 ? sm : p.ux) - p.ux);
    out.qy += s * p.density * (factor * (axis == 1 ? sm : p.uy) - p.uy);
    const double star_e = factor * (p.total_energy_density + (sm - u) * (p.density * sm + p.p / (s - u)));
    out.energy += s * (star_e - p.total_energy_density);
}
}

double GasCell::kinetic_energy() const {
    const double mass = mixture.mass();
    return mass == 0 ? 0 : (momentum_x * momentum_x + momentum_y * momentum_y) / (2 * mass);
}

void SolverSettings::validate() const {
    require_finite(gravity, "gravity"); require_finite(diffusivity, "diffusivity");
    if (gravity < 0 || diffusivity < 0) throw std::invalid_argument("Invalid solver settings");
}

GasWorld::GasWorld(std::size_t width, std::size_t height, double dx, double depth,
                   SolverSettings settings, SpeciesRegistry registry)
    : registry_(std::move(registry)), width_(width), height_(height), dx_(dx), depth_(depth), settings_(settings), escaped_masses_(registry_.size(), 0) {
    settings_.validate();
    require_finite(dx_, "cell size"); require_finite(depth_, "depth");
    if (width == 0 || height == 0 || width > 1000000 || height > 1000000 || width > 1000000 / height ||
        dx <= 0 || depth <= 0 || !std::isfinite(volume()) || volume() <= 0)
        throw std::invalid_argument("Invalid world dimensions (maximum 1,000,000 cells)");
    if (registry_.size() == 0) throw std::invalid_argument("Empty species registry");
    cells_.assign(width * height, GasCell(registry_.size()));
}

std::size_t GasWorld::offset(std::size_t x, std::size_t y) const {
    if (x >= width_ || y >= height_) throw std::out_of_range("Cell coordinates");
    return y * width_ + x;
}
const GasCell& GasWorld::cell(std::size_t x, std::size_t y) const { return cells_[offset(x, y)]; }

void GasWorld::set_cell(std::size_t x, std::size_t y, const MixtureInventory& mixture, double ux, double uy) {
    mixture.validate(registry_);
    require_finite(ux, "x velocity"); require_finite(uy, "y velocity");
    auto& c = cells_[offset(x, y)];
    if (c.wall) throw std::domain_error("Cannot insert fluid into a wall");
    for (std::size_t i = 0; i < registry_.size(); ++i)
        if (mixture.masses[i] != 0 && registry_[i].phase != Phase::gas)
            throw std::domain_error("Gas reference solver cannot advect condensed phases");
    GasCell next(registry_.size());
    next.mixture = mixture;
    next.momentum_x = mixture.mass() * ux;
    next.momentum_y = mixture.mass() * uy;
    require_finite(next.kinetic_energy(), "kinetic energy");
    c = std::move(next);
}

void GasWorld::set_wall(std::size_t x, std::size_t y, bool wall) {
    auto& c = cells_[offset(x, y)];
    if (wall && c.mixture.mass() != 0) throw std::domain_error("Displace fluid before constructing a wall");
    c.wall = wall;
}

double GasWorld::stable_timestep() const {
    double rate = 0;
    for (const auto& c : cells_) {
        const auto p = sample(registry_, c, volume());
        if (p.density == 0) continue;
        rate = std::max(rate, (std::abs(p.ux) + std::abs(p.uy) + 2 * p.sound) / dx_);
        const double thermal_diffusivity = settings_.conduction ? p.conductivity * volume() / c.mixture.heat_capacity(registry_) : 0;
        rate = std::max(rate, 4 * std::max(settings_.diffusivity, thermal_diffusivity) / (dx_ * dx_));
    }
    if (!std::isfinite(rate)) throw std::domain_error("Nonfinite timestep rate");
    return rate == 0 ? 1.0 : 0.35 / rate;
}

void GasWorld::advance(double dt) {
    const double vol = volume(), area = dx_ * depth_;
    std::vector<Primitive> primitives;
    primitives.reserve(cells_.size());
    std::vector<Increment> changes;
    changes.reserve(cells_.size());
    for (const auto& c : cells_) {
        primitives.push_back(sample(registry_, c, vol));
        changes.emplace_back(registry_.size());
    }
    GasCell vacuum(registry_.size());
    Flux flux(registry_.size());
    std::vector<double> diffusion(registry_.size());
    const auto face_scale = [&](const Primitive& p, double displacement) {
        if (p.density == 0 || displacement == 0 || settings_.gravity == 0) return 1.0;
        return std::exp(-settings_.gravity * displacement * p.density / p.p);
    };
    const auto face = [&](std::size_t left_index, std::size_t right_index, int axis, double height) {
        const auto none = cells_.size();
        const bool left_out = left_index == none, right_out = right_index == none;
        const bool left_wall = !left_out && cells_[left_index].wall;
        const bool right_wall = !right_out && cells_[right_index].wall;
        if ((left_out || left_wall) && (right_out || right_wall)) return;
        const bool vacuum_boundary = axis == 1 && right_out && settings_.open_top;
        const bool wall_boundary = left_wall || right_wall || left_out || (right_out && !vacuum_boundary);
        const auto& left = left_out ? vacuum : cells_[left_index];
        const auto& right = right_out ? vacuum : cells_[right_index];
        const auto pl = left_out ? Primitive{} : primitives[left_index];
        const auto pr = right_out ? Primitive{} : primitives[right_index];
        const double ls = face_scale(pl, axis == 1 ? dx_ / 2 : 0);
        const double rs = face_scale(pr, axis == 1 ? -dx_ / 2 : 0);
        flux.reset();
        if (wall_boundary) {
            const bool fluid_is_left = !left_out && !left_wall;
            const auto& p = fluid_is_left ? pl : pr;
            const double u = axis == 0 ? p.ux : p.uy;
            const double wall_p = p.p * (fluid_is_left ? ls : rs);
            // Reflective Riemann pressure supplies a normal wall force. For a
            // static cell this is exactly the hydrostatic reconstructed pressure.
            const double pressure = std::max(0.0, wall_p + p.density * p.sound * u * (fluid_is_left ? 1 : -1));
            if (axis == 0) flux.qx = pressure; else flux.qy = pressure;
        } else {
            riemann(left, right, pl, pr, ls, rs, vol, axis, flux);
            if (!left_out && !right_out && pl.density > 0 && pr.density > 0) {
                const double rho = 2 * pl.density * pr.density / (pl.density + pr.density);
                double limiter = 1;
                for (std::size_t i = 0; i < registry_.size(); ++i) {
                    const double yl = left.mixture.masses[i] / (pl.density * vol);
                    const double yr = right.mixture.masses[i] / (pr.density * vol);
                    diffusion[i] = settings_.diffusivity * rho * (yl - yr) / dx_;
                    const double available = diffusion[i] >= 0 ? left.mixture.masses[i] : right.mixture.masses[i];
                    if (diffusion[i] != 0) limiter = std::min(limiter, 0.1 * available / (std::abs(diffusion[i]) * dt * area));
                }
                for (std::size_t i = 0; i < registry_.size(); ++i) {
                    const double j = diffusion[i] * limiter;
                    flux.mass[i] += j;
                    flux.energy += j * registry_[i].enthalpy(j >= 0 ? pl.t : pr.t);
                }
                if (settings_.conduction) {
                    const double k = pl.conductivity + pr.conductivity == 0 ? 0 :
                        2 * pl.conductivity * pr.conductivity / (pl.conductivity + pr.conductivity);
                    flux.energy += k * (pl.t - pr.t) / dx_;
                }
            }
        }
        double mass_flux = 0;
        for (double value : flux.mass) mass_flux += value;
        const auto apply = [&](std::size_t index, double sign) {
            if (index == none || cells_[index].wall) return;
            auto& change = changes[index];
            const double factor = sign * dt * area;
            for (std::size_t i = 0; i < registry_.size(); ++i) change.mass[i] += factor * flux.mass[i];
            change.qx += factor * flux.qx;
            change.qy += factor * flux.qy;
            const double center_height = (static_cast<double>(index / width_) + 0.5) * dx_;
            // Conservative transport of E + rho*g*y. Momentum gravity changes
            // kinetic/internal partition; total (including potential) stays closed.
            change.energy += factor * (flux.energy + mass_flux * settings_.gravity * (height - center_height));
        };
        apply(left_index, -1);
        apply(right_index, 1);
        if (vacuum_boundary) {
            for (std::size_t i = 0; i < registry_.size(); ++i) escaped_masses_[i] += dt * area * flux.mass[i];
            escaped_energy_ += dt * area * (flux.energy + mass_flux * settings_.gravity * height);
        }
    };
    const auto none = cells_.size();
    for (std::size_t y = 0; y < height_; ++y) {
        for (std::size_t x = 0; x <= width_; ++x)
            face(x == 0 ? none : y * width_ + x - 1, x == width_ ? none : y * width_ + x, 0, (static_cast<double>(y) + 0.5) * dx_);
    }
    for (std::size_t y = 0; y <= height_; ++y) {
        for (std::size_t x = 0; x < width_; ++x)
            face(y == 0 ? none : (y - 1) * width_ + x, y == height_ ? none : y * width_ + x, 1, static_cast<double>(y) * dx_);
    }
    for (std::size_t index = 0; index < cells_.size(); ++index) {
        auto& c = cells_[index];
        if (c.wall) continue;
        const auto& p = primitives[index];
        auto& delta = changes[index];
        if (p.density > 0) {
            const double bottom = p.p * face_scale(p, -dx_ / 2);
            const double top = p.p * face_scale(p, dx_ / 2);
            delta.qy -= dt * area * (bottom - top);
        }
        const double total_energy = c.mixture.energy + c.kinetic_energy() + delta.energy;
        for (std::size_t i = 0; i < registry_.size(); ++i) c.mixture.masses[i] += delta.mass[i];
        c.momentum_x += delta.qx;
        c.momentum_y += delta.qy;
        if (c.mixture.mass() == 0) {
            if (c.momentum_x != 0 || c.momentum_y != 0 || total_energy != 0)
                throw std::domain_error("Vacuum acquired unaccounted momentum or energy");
        }
        c.mixture.energy = total_energy - c.kinetic_energy();
        c.mixture.validate(registry_);
        require_finite(c.momentum_x, "momentum"); require_finite(c.momentum_y, "momentum");
    }
}

StepResult GasWorld::step(double seconds, std::uint64_t budget) {
    require_finite(seconds, "step duration");
    if (seconds < 0 || seconds > 10 || budget == 0) throw std::invalid_argument("Invalid step duration or substep budget");
    if (seconds == 0) return {};
    auto backup = *this;
    StepResult result;
    try {
        while (result.seconds < seconds) {
            if (result.substeps == budget) throw std::runtime_error("Acoustic substep budget exceeded; step rolled back");
            const double dt = std::min(seconds - result.seconds, stable_timestep());
            if (dt <= 0 || result.seconds + dt == result.seconds) throw std::runtime_error("Timestep underflow");
            advance(dt);
            result.seconds += dt;
            ++result.substeps;
        }
        time_ += seconds;
        require_finite(time_, "simulation time");
    } catch (...) {
        *this = std::move(backup);
        throw;
    }
    return result;
}

ConservationTotals GasWorld::totals() const {
    ConservationTotals result{std::vector<double>(registry_.size(), 0), 0, 0, 0};
    for (std::size_t index = 0; index < cells_.size(); ++index) {
        const auto& c = cells_[index];
        for (std::size_t i = 0; i < registry_.size(); ++i) result.masses[i] += c.mixture.masses[i];
        result.internal_energy += c.mixture.energy;
        result.kinetic_energy += c.kinetic_energy();
        result.potential_energy += c.mixture.mass() * settings_.gravity * (static_cast<double>(index / width_) + 0.5) * dx_;
    }
    return result;
}
}
