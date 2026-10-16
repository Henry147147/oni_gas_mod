#include "mixed_atmospheres/coupled_api.h"
#include "mixed_atmospheres/fluid_world.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <map>
#include <stdexcept>
#include <vector>

static_assert(sizeof(ma_coupled_config) == 96);
static_assert(sizeof(ma_coupled_species) == 40);
static_assert(sizeof(ma_coupled_cell_input) == 40);
static_assert(sizeof(ma_coupled_cell) == 64);
static_assert(sizeof(ma_coupled_snapshot) == 80);
static_assert(sizeof(ma_coupled_step_result) == 128);
static_assert(sizeof(ma_coupled_passive) == 16);
static_assert(sizeof(ma_coupled_cell_edit) == 48);
static_assert(sizeof(ma_coupled_mutation_budget) == 64);
static_assert(sizeof(ma_coupled_mutation_result) == 128);

struct ma_coupled_world {
    ma_coupled_config config;
    ma::FluidWorld value;
    ma_coupled_snapshot snapshot{};
    std::vector<ma_coupled_cell> cells;
    std::vector<double> masses;
    std::vector<uint64_t> passive_offsets;
    std::vector<ma_coupled_passive> passive_entries;
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
    w.cells.clear(); w.masses.clear(); w.passive_offsets.clear(); w.passive_entries.clear();
    w.cells.reserve(static_cast<std::size_t>(w.config.width) * w.config.height);
    w.masses.reserve(w.cells.capacity() * count);
    w.passive_offsets.reserve(static_cast<std::size_t>(w.config.width) * w.config.height + 1);
    w.passive_offsets.push_back(0);
    for (uint32_t y = 0; y < w.config.height; ++y) for (uint32_t x = 0; x < w.config.width; ++x) {
        const auto& c = w.value.cell(x, y);
        const auto m = c.mixture.mass();
        w.cells.push_back({c.mixture.energy, c.momentum_x, c.momentum_y, w.value.pressure(x, y),
            c.wall || m == 0 ? 0 : c.mixture.temperature(r), c.wall ? 0 : w.value.liquid_fraction(x, y), m, c.wall ? 1U : 0U, 0});
        w.masses.insert(w.masses.end(), c.mixture.masses.begin(), c.mixture.masses.end());
        for (const auto& entry : c.passive_quantities) w.passive_entries.push_back({entry.identity, entry.quantity});
        require(w.passive_entries.size() <= 8000000, "Coupled passive snapshot record budget exceeded");
        w.passive_offsets.push_back(static_cast<uint64_t>(w.passive_entries.size()));
    }
    const auto totals = w.value.totals();
    w.snapshot = {revision, w.value.time(), static_cast<uint64_t>(w.cells.size()), static_cast<uint64_t>(w.masses.size()),
        static_cast<uint32_t>(count), 0, totals.internal_energy, totals.kinetic_energy, totals.potential_energy, {0, 0}};
}
// Only edited rows contribute. This avoids cancellation against an unrelated
// world's total, and keeps a trace's bound independent of bulk constituents.
struct MutationAccount {
    double high = 0, low = 0, scale = 0;
    void add(double value) {
        ma::require_finite(value, "mutation account operand");
        const double sum = high + value, z = sum - high;
        const double error = (high - (sum - z)) + (value - z);
        const double tail = low + error, next = sum + tail, q = next - sum;
        low = (sum - (next - q)) + (tail - q); high = next;
        scale += std::abs(value);
        ma::require_finite(high, "mutation account sum"); ma::require_finite(low, "mutation account tail");
        ma::require_finite(scale, "mutation account scale");
    }
    [[nodiscard]] double value() const { return high + low; }
    [[nodiscard]] double bound() const { return (128 * std::numeric_limits<double>::epsilon()) * scale; }
    [[nodiscard]] double ratio() const {
        const auto error = std::abs(value()), limit = bound();
        if (error == 0) return 0;
        if (!(limit > 0) || error > limit) throw std::domain_error("Coupled mutation does not balance its declared external account");
        return error / limit;
    }
};
void mutation_direction(double actual, double declared) {
    ma::require_finite(declared, "declared mutation change");
    if (declared != 0 && (actual == 0 || std::signbit(actual) != std::signbit(declared)))
        throw std::domain_error("Declared nonzero coupled mutation change is erased or reversed");
}
}
extern "C" {
uint32_t MA_CALL ma_coupled_abi_version(void) { return MA_COUPLED_ABI_VERSION; }
uint32_t MA_CALL ma_coupled_passive_abi_version(void) { return MA_COUPLED_PASSIVE_ABI_VERSION; }
uint32_t MA_CALL ma_coupled_mutation_abi_version(void) { return MA_COUPLED_MUTATION_ABI_VERSION; }
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
    return ma_coupled_create_full(c, cells, cell_count, masses, mass_count, nullptr, 0, nullptr, 0, output);
}
ma_status MA_CALL ma_coupled_create_full(const ma_coupled_config* c, const ma_coupled_cell_input* cells,
    uint64_t cell_count, const double* masses, uint64_t mass_count, const uint64_t* offsets, uint64_t offset_count,
    const ma_coupled_passive* entries, uint64_t entry_count, ma_coupled_world** output) {
    if (output) *output = nullptr;
    return guard([&] {
        require(c && cells && masses && output, "Null coupled initialization argument");
        const auto solver = settings(*c); const auto& r = registry();
        require(c->width > 0 && c->height > 0 && cell_count == static_cast<uint64_t>(c->width) * c->height &&
                cell_count <= 1000000 && mass_count == cell_count * r.size(), "Coupled initialization shape differs");
        require(entry_count <= 8000000 && (entries || entry_count == 0), "Invalid coupled passive entry array");
        if (offsets) {
            require(offset_count == cell_count + 1 && offsets[0] == 0 && offsets[cell_count] == entry_count, "Coupled passive offset shape differs");
            for (uint64_t i = 0; i < cell_count; ++i)
                require(offsets[i] <= offsets[i + 1] && offsets[i + 1] <= entry_count, "Invalid coupled passive offsets");
        } else require(offset_count == 0 && entry_count == 0, "Coupled passive offsets are absent");
        auto candidate = std::make_unique<ma_coupled_world>(*c, solver, r);
        for (uint64_t i = 0; i < cell_count; ++i) {
            const auto& input = cells[i]; require(input.wall <= 1 && input.reserved == 0, "Invalid coupled cell flags");
            ma::MixtureInventory mixture(r.size());
            std::copy_n(masses + i * r.size(), r.size(), mixture.masses.begin()); mixture.energy = input.internal_energy;
            const auto x = static_cast<uint32_t>(i % c->width), y = static_cast<uint32_t>(i / c->width);
            ma::PassiveQuantities passives;
            if (offsets) {
                require(offsets[i + 1] - offsets[i] <= 65537, "Too many coupled cell passive identities");
                for (uint64_t p = offsets[i]; p < offsets[i + 1]; ++p) passives.push_back({entries[p].identity, entries[p].quantity});
                ma::validate_passive_quantities(passives);
            }
            if (input.wall) {
                mixture.validate(r);
                require(mixture.mass() == 0 && mixture.energy == 0 && input.momentum_x == 0 &&
                    input.momentum_y == 0 && input.liquid_pressure == 0 && passives.empty(), "Coupled wall is not empty");
                candidate->value.set_wall(x, y, true);
            } else {
                require(mixture.mass() > 0, "Closed coupled reference does not support vacuum cells");
                candidate->value.set_conserved_cell(x, y, mixture, input.momentum_x, input.momentum_y, input.liquid_pressure, passives);
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
        require(w->passive_entries.empty(), "Use the complete snapshot API for passive metadata");
        if (cell_capacity < w->cells.size() || mass_capacity < w->masses.size()) throw BufferError("Coupled snapshot buffer is too small");
        std::copy(w->cells.begin(), w->cells.end(), cells); std::copy(w->masses.begin(), w->masses.end(), masses);
        *snapshot = w->snapshot;
    });
}
ma_status MA_CALL ma_coupled_passive_count(const ma_coupled_world* w, uint64_t* count) {
    return guard([&] { require(w && count, "Null coupled passive count argument"); *count = static_cast<uint64_t>(w->passive_entries.size()); });
}
ma_status MA_CALL ma_coupled_copy_full_snapshot(const ma_coupled_world* w, ma_coupled_snapshot* snapshot,
    ma_coupled_cell* cells, uint64_t cell_capacity, double* masses, uint64_t mass_capacity,
    uint64_t* offsets, uint64_t offset_capacity, ma_coupled_passive* entries, uint64_t entry_capacity) {
    return guard([&] {
        require(w && snapshot && cells && masses && offsets, "Null coupled complete snapshot argument");
        require(entries || w->passive_entries.empty(), "Null coupled passive snapshot entries");
        if (cell_capacity < w->cells.size() || mass_capacity < w->masses.size() ||
            offset_capacity < w->passive_offsets.size() || entry_capacity < w->passive_entries.size())
            throw BufferError("Coupled complete snapshot buffer is too small");
        std::copy(w->cells.begin(), w->cells.end(), cells); std::copy(w->masses.begin(), w->masses.end(), masses);
        std::copy(w->passive_offsets.begin(), w->passive_offsets.end(), offsets);
        if (!w->passive_entries.empty()) std::copy(w->passive_entries.begin(), w->passive_entries.end(), entries);
        *snapshot = w->snapshot;
    });
}
ma_status MA_CALL ma_coupled_prepare_mutation(const ma_coupled_world* source,
    uint64_t expected_revision, const ma_coupled_cell_edit* edits, uint64_t edit_count,
    const double* masses, uint64_t mass_count, const uint64_t* offsets, uint64_t offset_count,
    const ma_coupled_passive* entries, uint64_t entry_count, const ma_coupled_mutation_budget* budget,
    const double* mass_deltas, uint64_t mass_delta_count,
    const ma_coupled_passive* passive_deltas, uint64_t passive_delta_count,
    ma_coupled_world** output, ma_coupled_mutation_result* result) {
    if (output) *output = nullptr;
    return guard([&] {
        require(source && edits && masses && offsets && budget && mass_deltas && output && result,
            "Null coupled mutation argument");
        require(source->snapshot.revision == expected_revision && expected_revision != std::numeric_limits<uint64_t>::max(),
            "Coupled mutation source revision is stale or exhausted");
        const auto count = source->value.registry().size();
        require(edit_count > 0 && edit_count <= 4096 && mass_count == edit_count * count && mass_delta_count == count,
            "Coupled mutation shape differs");
        require(offset_count == edit_count + 1 && offsets[0] == 0 && offsets[edit_count] == entry_count &&
            entry_count <= 8000000 && (entries || entry_count == 0), "Coupled mutation metadata shape differs");
        require(passive_delta_count <= 65537 && (passive_deltas || passive_delta_count == 0),
            "Invalid coupled mutation metadata account");
        require(budget->reserved[0] == 0 && budget->reserved[1] == 0 && budget->reserved[2] == 0,
            "Invalid coupled mutation budget flags");
        for (uint64_t i = 0; i < edit_count; ++i) {
            require(edits[i].cell_index < source->cells.size() && (i == 0 || edits[i].cell_index > edits[i - 1].cell_index),
                "Coupled mutation cells must be sorted, unique and in range");
            require(edits[i].value.wall == 0 && edits[i].value.reserved == 0 && source->cells[edits[i].cell_index].wall == 0,
                "Coupled mutation cannot change walls or geometry");
            require(offsets[i] <= offsets[i + 1] && offsets[i + 1] <= entry_count && offsets[i + 1] - offsets[i] <= 65537,
                "Invalid coupled mutation metadata offsets");
        }
        auto candidate = std::make_unique<ma_coupled_world>(*source);
        std::vector<MutationAccount> species(count);
        std::map<uint64_t, MutationAccount> passives;
        MutationAccount internal, kinetic, potential, energy, px, py;
        for (uint64_t i = 0; i < edit_count; ++i) {
            const auto& edit = edits[i]; const auto& input = edit.value;
            const auto x = static_cast<uint32_t>(edit.cell_index % source->config.width);
            const auto y = static_cast<uint32_t>(edit.cell_index / source->config.width);
            const auto& old = source->value.cell(x, y);
            ma::MixtureInventory mixture(count); mixture.energy = input.internal_energy;
            std::copy_n(masses + i * count, count, mixture.masses.begin());
            require(mixture.mass() > 0, "Closed coupled mutation cannot create vacuum");
            ma::PassiveQuantities metadata;
            for (uint64_t p = offsets[i]; p < offsets[i + 1]; ++p) metadata.push_back({entries[p].identity, entries[p].quantity});
            candidate->value.set_conserved_cell(x, y, mixture, input.momentum_x, input.momentum_y, input.liquid_pressure, metadata);
            const auto& next = candidate->value.cell(x, y);
            for (std::size_t s = 0; s < count; ++s) { species[s].add(next.mixture.masses[s]); species[s].add(-old.mixture.masses[s]); }
            for (const auto& p : next.passive_quantities) passives[p.identity].add(p.quantity);
            for (const auto& p : old.passive_quantities) passives[p.identity].add(-p.quantity);
            internal.add(next.mixture.energy); internal.add(-old.mixture.energy);
            kinetic.add(next.kinetic_energy()); kinetic.add(-old.kinetic_energy());
            const double phi = source->config.gravity * (static_cast<double>(y) + .5) * source->config.cell_size;
            const double old_pe = old.mixture.mass() * phi, new_pe = next.mixture.mass() * phi;
            potential.add(new_pe); potential.add(-old_pe);
            for (const double value : {next.mixture.energy, -old.mixture.energy, next.kinetic_energy(), -old.kinetic_energy(), new_pe, -old_pe}) energy.add(value);
            px.add(next.momentum_x); px.add(-old.momentum_x); py.add(next.momentum_y); py.add(-old.momentum_y);
        }
        for (std::size_t s = 0; s < count; ++s) {
            mutation_direction(species[s].value(), mass_deltas[s]); species[s].add(-mass_deltas[s]);
        }
        uint64_t previous = 0;
        for (uint64_t i = 0; i < passive_delta_count; ++i) {
            const auto& delta = passive_deltas[i];
            require(delta.identity > previous && delta.identity <= ma::radiation_passive_identity,
                "Coupled mutation metadata account identities must be sorted, unique and known");
            mutation_direction(passives[delta.identity].value(), delta.quantity);
            passives[delta.identity].add(-delta.quantity); previous = delta.identity;
        }
        MutationAccount supplied_energy; supplied_energy.add(budget->material_energy);
        supplied_energy.add(budget->shaft_work); supplied_energy.add(budget->heat);
        mutation_direction(energy.value(), supplied_energy.value());
        mutation_direction(px.value(), budget->momentum_x); mutation_direction(py.value(), budget->momentum_y);
        energy.add(-budget->material_energy); energy.add(-budget->shaft_work); energy.add(-budget->heat);
        px.add(-budget->momentum_x); py.add(-budget->momentum_y);
        // Complete every conservation check before exporting the candidate.
        static_cast<void>(energy.ratio()); static_cast<void>(px.ratio()); static_cast<void>(py.ratio());
        ma_coupled_mutation_result report{}; report.revision = expected_revision + 1; report.edited_cells = edit_count;
        report.internal_energy_change = internal.value(); report.kinetic_energy_change = kinetic.value();
        report.potential_energy_change = potential.value(); report.energy_residual = energy.value(); report.energy_bound = energy.bound();
        report.momentum_x_residual = px.value(); report.momentum_y_residual = py.value();
        report.momentum_x_bound = px.bound(); report.momentum_y_bound = py.bound();
        for (const auto& account : species) {
            report.maximum_species_mass_bound_ratio = std::max(report.maximum_species_mass_bound_ratio, account.ratio());
            report.maximum_species_mass_residual = std::max(report.maximum_species_mass_residual, std::abs(account.value()));
        }
        for (const auto& [identity, account] : passives) {
            static_cast<void>(identity);
            report.maximum_passive_bound_ratio = std::max(report.maximum_passive_bound_ratio, account.ratio());
            report.maximum_passive_residual = std::max(report.maximum_passive_residual, std::abs(account.value()));
        }
        capture(*candidate, report.revision); *result = report; *output = candidate.release();
    });
}
}
