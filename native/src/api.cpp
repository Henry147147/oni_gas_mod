#include "mixed_atmospheres/api.h"
#include "mixed_atmospheres/grid.hpp"
#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

struct ma_world { ma::GasWorld value; explicit ma_world(ma::GasWorld world) : value(std::move(world)) {} };
namespace {
thread_local std::string last_error;
struct BufferError : std::runtime_error { using runtime_error::runtime_error; };
template<class Function> ma_status guard(Function&& function) noexcept {
    try { function(); last_error.clear(); return MA_OK; }
    catch (const BufferError& e) { last_error = e.what(); return MA_BUFFER_TOO_SMALL; }
    catch (const std::out_of_range& e) { last_error = e.what(); return MA_INVALID_ARGUMENT; }
    catch (const std::invalid_argument& e) { last_error = e.what(); return MA_INVALID_ARGUMENT; }
    catch (const std::domain_error& e) { last_error = e.what(); return MA_NUMERICAL_ERROR; }
    catch (const std::exception& e) { last_error = e.what(); return MA_INTERNAL_ERROR; }
    catch (...) { last_error = "Unknown native error"; return MA_INTERNAL_ERROR; }
}
void require(bool condition, const char* message) { if (!condition) throw std::invalid_argument(message); }
void capacity(uint64_t actual, std::size_t expected) { if (actual < expected) throw BufferError("Output buffer is too small"); }
ma::AtmosphereSettings options(const ma_atmosphere_settings& s) {
    require(s.hard_mode <= 1 && s.reserved == 0, "Invalid atmosphere flags");
    return {s.hard_mode != 0, s.minimum_oxygen_fraction, s.maximum_oxygen_fraction,
            s.minimum_oxygen_partial_pressure, s.maximum_co2_fraction, s.maximum_co2_partial_pressure};
}
}

extern "C" {
uint32_t MA_CALL ma_abi_version(void) { return 1; }
const char* MA_CALL ma_last_error(void) { return last_error.c_str(); }
ma_status MA_CALL ma_create_world(const ma_world_config* c, ma_world** output) {
    if (output) *output = nullptr;
    return guard([&] {
        require(c && output, "Null world creation argument");
        require(c->conduction <= 1 && c->open_top <= 1, "Invalid world flags");
        auto world = std::make_unique<ma_world>(ma::GasWorld(c->width, c->height, c->cell_size, c->depth,
            {c->gravity, c->diffusivity, c->conduction != 0, c->open_top != 0}));
        *output = world.release();
    });
}
void MA_CALL ma_destroy_world(ma_world* world) { delete world; }
ma_status MA_CALL ma_get_config(const ma_world* world, ma_world_config* output) {
    return guard([&] {
        require(world && output, "Null config argument");
        const auto& w = world->value; const auto& s = w.settings();
        *output = {static_cast<uint32_t>(w.width()), static_cast<uint32_t>(w.height()), w.cell_size(), w.depth(),
                   s.gravity, s.diffusivity, s.conduction ? 1U : 0U, s.open_top ? 1U : 0U};
    });
}
ma_status MA_CALL ma_species_count(const ma_world* world, uint32_t* output) {
    return guard([&] { require(world && output, "Null species count argument"); *output = static_cast<uint32_t>(world->value.registry().size()); });
}
ma_status MA_CALL ma_species_id(const ma_world* world, uint32_t species, char* buffer, uint64_t size, uint64_t* required) {
    return guard([&] {
        require(world && required, "Null species identifier argument");
        const auto& id = world->value.registry()[species].id;
        *required = id.size() + 1;
        if (!buffer && size == 0) return;
        require(buffer != nullptr, "Null species output buffer"); capacity(size, *required);
        std::memcpy(buffer, id.c_str(), id.size() + 1);
    });
}
ma_status MA_CALL ma_set_cell(ma_world* world, uint32_t x, uint32_t y, const double* masses, uint32_t count, double t, double ux, double uy) {
    return guard([&] {
        require(world && masses, "Null cell input");
        const auto& r = world->value.registry(); require(count == r.size(), "Wrong component count");
        ma::MixtureInventory mixture(count);
        std::copy(masses, masses + count, mixture.masses.begin());
        mixture.set_temperature(r, t);
        world->value.set_cell(x, y, mixture, ux, uy);
    });
}
ma_status MA_CALL ma_set_wall(ma_world* world, uint32_t x, uint32_t y, uint32_t wall) {
    return guard([&] { require(world && wall <= 1, "Invalid wall argument"); world->value.set_wall(x, y, wall != 0); });
}
ma_status MA_CALL ma_get_cell(const ma_world* world, uint32_t x, uint32_t y, ma_sample* output, double* masses, uint32_t size) {
    return guard([&] {
        require(world && output && masses, "Null cell output"); capacity(size, world->value.registry().size());
        const auto& c = world->value.cell(x, y); const auto& r = world->value.registry();
        const double m = c.mixture.mass();
        *output = {m, c.mixture.temperature(r), c.wall ? 0 : c.mixture.pressure(r, world->value.volume()), c.mixture.energy,
                   m == 0 ? 0 : c.momentum_x / m, m == 0 ? 0 : c.momentum_y / m, c.wall ? 1U : 0U, 0};
        std::copy(c.mixture.masses.begin(), c.mixture.masses.end(), masses);
    });
}
ma_status MA_CALL ma_assess_atmosphere(const ma_world* world, uint32_t x, uint32_t y, const ma_atmosphere_settings* settings, ma_breathability* output) {
    return guard([&] {
        require(world && settings && output, "Null atmosphere argument");
        const auto result = ma::assess_atmosphere(world->value.registry(), world->value.cell(x, y).mixture,
                                                 world->value.volume(), options(*settings));
        *output = {result.pressure, result.oxygen_fraction, result.oxygen_partial_pressure,
                   result.co2_fraction, result.co2_partial_pressure, result.failures, 0};
    });
}
ma_status MA_CALL ma_step(ma_world* world, double seconds, uint64_t budget, ma_step_result* result) {
    if (result) *result = {0, 0};
    return guard([&] { require(world && result, "Null step argument"); const auto r = world->value.step(seconds, budget); *result = {r.seconds, r.substeps}; });
}
ma_status MA_CALL ma_totals(const ma_world* world, double* masses, uint32_t size, double* energy, double* escaped, double* escaped_energy) {
    return guard([&] {
        require(world && masses && energy && escaped && escaped_energy, "Null totals argument"); capacity(size, world->value.registry().size());
        const auto result = world->value.totals();
        std::copy(result.masses.begin(), result.masses.end(), masses);
        std::copy(world->value.escaped_masses().begin(), world->value.escaped_masses().end(), escaped);
        *energy = result.energy(); *escaped_energy = world->value.escaped_energy();
    });
}
ma_status MA_CALL ma_save(const ma_world* world, uint8_t* buffer, uint64_t size, uint64_t* required) {
    return guard([&] {
        require(world && required, "Null save argument"); const auto bytes = world->value.save(); *required = bytes.size();
        if (!buffer && size == 0) return;
        require(buffer != nullptr, "Null save buffer"); capacity(size, bytes.size()); std::memcpy(buffer, bytes.data(), bytes.size());
    });
}
ma_status MA_CALL ma_load(const uint8_t* buffer, uint64_t size, ma_world** output) {
    if (output) *output = nullptr;
    return guard([&] {
        require(buffer && output, "Null load argument"); require(size <= 1024ULL * 1024 * 1024, "Save too large");
        auto world = std::make_unique<ma_world>(ma::GasWorld::load({buffer, static_cast<std::size_t>(size)}));
        *output = world.release();
    });
}
}
