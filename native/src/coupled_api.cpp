#include "mixed_atmospheres/coupled_api.h"
#include "mixed_atmospheres/fluid_world.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

static_assert(sizeof(ma_coupled_config) == 96);
static_assert(sizeof(ma_coupled_species) == 40);
static_assert(sizeof(ma_coupled_cell_input) == 40);
static_assert(sizeof(ma_coupled_cell) == 64);
static_assert(sizeof(ma_coupled_snapshot) == 80);
static_assert(sizeof(ma_coupled_step_result) == 128);

struct ma_coupled_world {
    ma_coupled_config config;
    ma::FluidWorld value;
    ma_coupled_snapshot snapshot{};
    std::vector<ma_coupled_cell> cells;
    std::vector<double> masses;
    ma_coupled_world(const ma_coupled_config& c, ma::FluidSolverSettings settings, const ma::SpeciesRegistry& registry)
        : config(c), value(c.width, c.height, c.cell_size, c.depth, settings, registry) {}
};
namespace {
thread_local char error_text[2048]{};
void error(const char* text) noexcept { std::snprintf(error_text, sizeof(error_text), "%s", text); }
struct BufferError : std::runtime_error { using runtime_error::runtime_error; };
template<class Function> ma_status guard(Function&& function) noexcept {
    try { function(); error_text[0] = '\0'; return MA_OK; }
    catch (const BufferError& e) { error(e.what()); return MA_BUFFER_TOO_SMALL; }
    catch (const std::invalid_argument& e) { error(e.what()); return MA_INVALID_ARGUMENT; }
    catch (const std::out_of_range& e) { error(e.what()); return MA_INVALID_ARGUMENT; }
    catch (const std::domain_error& e) { error(e.what()); return MA_NUMERICAL_ERROR; }
    catch (const std::runtime_error& e) { error(e.what()); return MA_NUMERICAL_ERROR; }
    catch (const std::exception& e) { error(e.what()); return MA_INTERNAL_ERROR; }
    catch (...) { error("Unknown coupled native failure"); return MA_INTERNAL_ERROR; }
}
void require(bool condition, const char* reason) { if (!condition) throw std::invalid_argument(reason); }
const ma::SpeciesRegistry& registry() { static const auto result = ma::SpeciesRegistry::standard(); return result; }
ma::FluidSolverSettings settings(const ma_coupled_config& c) {
    require(c.struct_size == sizeof(c) && c.abi_version == MA_COUPLED_ABI_VERSION &&
            c.model == MA_COUPLED_CLOSED_REFERENCE_MODEL, "Unsupported coupled ABI, structure size or physical model");
    require(c.conduction <= 1 && c.preconditioner <= 2 && c.reserved == 0 && c.reserved2 == 0, "Invalid coupled flags");
    ma::FluidSolverSettings result;
    result.gravity = c.gravity; result.diffusivity_m2_per_s = c.diffusivity; result.viscosity_m2_per_s = c.viscosity;
    result.maximum_timestep_s = c.maximum_timestep; result.maximum_outgoing_fraction = c.maximum_outgoing_fraction;
    result.conduction = c.conduction != 0;
    result.pressure_preconditioner = static_cast<ma::PressurePreconditioner>(c.preconditioner);
    result.validate(); return result;
}
void capture(ma_coupled_world& w, uint64_t revision) {
    const auto& r = w.value.registry(); const auto count = r.size();
    w.cells.clear(); w.masses.clear();
    w.cells.reserve(static_cast<std::size_t>(w.config.width) * w.config.height);
    w.masses.reserve(w.cells.capacity() * count);
    for (uint32_t y = 0; y < w.config.height; ++y) for (uint32_t x = 0; x < w.config.width; ++x) {
        const auto& c = w.value.cell(x, y);
        const auto m = c.mixture.mass();
        w.cells.push_back({c.mixture.energy, c.momentum_x, c.momentum_y, w.value.pressure(x, y),
            c.wall || m == 0 ? 0 : c.mixture.temperature(r), c.wall ? 0 : w.value.liquid_fraction(x, y), m, c.wall ? 1U : 0U, 0});
        w.masses.insert(w.masses.end(), c.mixture.masses.begin(), c.mixture.masses.end());
    }
    const auto totals = w.value.totals();
    w.snapshot = {revision, w.value.time(), static_cast<uint64_t>(w.cells.size()), static_cast<uint64_t>(w.masses.size()),
        static_cast<uint32_t>(count), 0, totals.internal_energy, totals.kinetic_energy, totals.potential_energy, {0, 0}};
}
}
extern "C" {
uint32_t MA_CALL ma_coupled_abi_version(void) { return MA_COUPLED_ABI_VERSION; }
const char* MA_CALL ma_coupled_last_error(void) { return error_text; }
ma_status MA_CALL ma_coupled_species_count(uint32_t* output) {
    return guard([&] { require(output, "Null coupled species count"); *output = static_cast<uint32_t>(registry().size()); });
}
ma_status MA_CALL ma_coupled_species_definition(uint32_t index, ma_coupled_species* output) {
    return guard([&] {
        require(output, "Null coupled species definition"); const auto& s = registry()[index];
        *output = {s.molar_mass, s.cv(), s.internal_energy(0), s.density, static_cast<uint32_t>(s.phase), 0};
    });
}
ma_status MA_CALL ma_coupled_species_id(uint32_t index, char* buffer, uint64_t capacity, uint64_t* required) {
    return guard([&] {
        require(required, "Null coupled species length"); const auto& id = registry()[index].id;
        *required = id.size() + 1;
        if (!buffer && capacity == 0) return;
        require(buffer, "Null coupled species buffer");
        if (capacity < *required) throw BufferError("Coupled species buffer is too small");
        std::memcpy(buffer, id.c_str(), id.size() + 1);
    });
}
ma_status MA_CALL ma_coupled_create(const ma_coupled_config* c, const ma_coupled_cell_input* cells,
    uint64_t cell_count, const double* masses, uint64_t mass_count, ma_coupled_world** output) {
    if (output) *output = nullptr;
    return guard([&] {
        require(c && cells && masses && output, "Null coupled initialization argument");
        const auto solver = settings(*c); const auto& r = registry();
        require(c->width > 0 && c->height > 0 && cell_count == static_cast<uint64_t>(c->width) * c->height &&
                cell_count <= 1000000 && mass_count == cell_count * r.size(), "Coupled initialization shape differs");
        auto candidate = std::make_unique<ma_coupled_world>(*c, solver, r);
        for (uint64_t i = 0; i < cell_count; ++i) {
            const auto& input = cells[i]; require(input.wall <= 1 && input.reserved == 0, "Invalid coupled cell flags");
            ma::MixtureInventory mixture(r.size());
            std::copy_n(masses + i * r.size(), r.size(), mixture.masses.begin()); mixture.energy = input.internal_energy;
            const auto x = static_cast<uint32_t>(i % c->width), y = static_cast<uint32_t>(i / c->width);
            if (input.wall) {
                mixture.validate(r);
                require(mixture.mass() == 0 && mixture.energy == 0 && input.momentum_x == 0 &&
                    input.momentum_y == 0 && input.liquid_pressure == 0, "Coupled wall is not empty");
                candidate->value.set_wall(x, y, true);
            } else {
                require(mixture.mass() > 0, "Closed coupled reference does not support vacuum cells");
                candidate->value.set_conserved_cell(x, y, mixture, input.momentum_x, input.momentum_y, input.liquid_pressure);
            }
        }
        capture(*candidate, 0); *output = candidate.release();
    });
}
void MA_CALL ma_coupled_destroy(ma_coupled_world* world) { delete world; }
ma_status MA_CALL ma_coupled_get_config(const ma_coupled_world* world, ma_coupled_config* output) {
    return guard([&] { require(world && output, "Null coupled configuration argument"); *output = world->config; });
}
ma_status MA_CALL ma_coupled_advance(const ma_coupled_world* source, double seconds, uint64_t budget,
    ma_coupled_world** output, ma_coupled_step_result* result) {
    if (output) *output = nullptr;
    return guard([&] {
        require(source && output && result && seconds > 0 && seconds <= 10 && budget != 0, "Invalid coupled advance argument");
        require(source->snapshot.revision != std::numeric_limits<uint64_t>::max(), "Coupled revision exhausted");
        auto candidate = std::make_unique<ma_coupled_world>(*source);
        const auto r = candidate->value.step(seconds, budget);
        capture(*candidate, source->snapshot.revision + 1);
        const ma_coupled_step_result report{r.seconds, candidate->value.time(), candidate->snapshot.revision,
            r.substeps, r.rejected_substeps, static_cast<uint64_t>(r.pressure_iterations), static_cast<uint64_t>(r.nonlinear_iterations),
            static_cast<uint64_t>(r.thermal_iterations), r.maximum_linear_residual_ratio, r.maximum_volume_residual_m3,
            r.maximum_conservation_error_j, r.initial_projection_heat_j, r.final_projection_heat_j, r.pressure_work_roundoff_j, {0, 0}};
        *result = report; *output = candidate.release();
    });
}
ma_status MA_CALL ma_coupled_copy_snapshot(const ma_coupled_world* w, ma_coupled_snapshot* snapshot,
    ma_coupled_cell* cells, uint64_t cell_capacity, double* masses, uint64_t mass_capacity) {
    return guard([&] {
        require(w && snapshot && cells && masses, "Null coupled snapshot argument");
        if (cell_capacity < w->cells.size() || mass_capacity < w->masses.size()) throw BufferError("Coupled snapshot buffer is too small");
        std::copy(w->cells.begin(), w->cells.end(), cells); std::copy(w->masses.begin(), w->masses.end(), masses);
        *snapshot = w->snapshot;
    });
}
}
