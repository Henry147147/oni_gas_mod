#include "oni_colony.hpp"
#include "oni_catalogue.hpp"
#include "oni_colony_import.hpp"
#include "oni_radial_consumption.hpp"
#include "oni_sha256.hpp"
#include "mixed_atmospheres/mac_closed_world.hpp"
#include "mixed_atmospheres/mac_kernel.hpp"
#include "mixed_atmospheres/mac_primary.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cfenv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <xmmintrin.h>

static_assert(sizeof(MAOniColonySpecies) == 40 && sizeof(MAOniColonyReading) == 64 && sizeof(MAOniColonyConstituent) == 24);
static_assert(sizeof(MAOniColonyMaterial) == 168 && sizeof(MAOniColonyConfig) == 56 && sizeof(MAOniColonyCell) == 16 &&
    sizeof(MAOniColonyStatus) == 584 && sizeof(MAOniColonyCellView) == 24 && sizeof(MAOniColonyTransfer) == 40 &&
    sizeof(MAOniColonyReceipt) == 40 && sizeof(MAOniColonyHeat) == 24, "Colony ABI layout changed");

namespace ma::oni::colony {
namespace {
using namespace experimental;
constexpr std::uint32_t none32 = std::numeric_limits<std::uint32_t>::max();
constexpr double phase_relaxation_time_s = 1;
// Immiscible liquids settle with a 2 s time constant: a 1 mm droplet rises or
// sinks through another liquid at its Stokes speed, about half a metre per
// second for oil in water, so it crosses a cell in about two seconds.
constexpr double settling_time_s = 2;
constexpr double reservoir_release_kg_per_s = 50;
void require(bool ok, const char* why) { if (!ok) throw std::invalid_argument(why); }

// Unity can use FTZ/DAZ and reduced x87 precision: select the solver's
// arithmetic on this thread and restore the caller's on exit.
struct ArithmeticScope {
    std::fenv_t saved{}; unsigned int sse = _mm_getcsr();
    ArithmeticScope() {
        if (std::feholdexcept(&saved) != 0) throw std::runtime_error("Cannot save native floating-point environment");
        if (std::fesetround(FE_TONEAREST) != 0) { (void)std::fesetenv(&saved); _mm_setcsr(sse); throw std::runtime_error("Cannot select nearest rounding"); }
        _mm_setcsr((_mm_getcsr() & ~0xe040U) | 0x1f80U);
#if defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
        if constexpr (std::numeric_limits<long double>::digits > std::numeric_limits<double>::digits) {
            unsigned short control = 0; __asm__ volatile("fnstcw %0" : "=m"(control));
            control = static_cast<unsigned short>(control | 0x0300U); __asm__ volatile("fldcw %0" : : "m"(control));
        }
#endif
    }
    ~ArithmeticScope() { (void)std::fesetenv(&saved); _mm_setcsr(sse); }
    ArithmeticScope(const ArithmeticScope&) = delete; ArithmeticScope& operator=(const ArithmeticScope&) = delete;
};

struct Published {
    MAOniColonyStatus status{};
    std::vector<MAOniColonyCellView> views;
    std::shared_ptr<const MacClosedWorld> world;
    std::shared_ptr<const Catalogue> catalogue;
};
// Conserved-quantity ledger: initial inventory plus every booked flow.
struct Ledger {
    std::vector<long double> initial, injected, extracted, sunk;
    std::vector<long double> converted; // phase change: + to the product species
    long double initial_energy = 0, heat = 0, energy_in = 0, energy_out = 0, energy_sunk = 0;
};
struct Session {
    MAOniColonyConfig config{};
    std::vector<MAOniColonyMaterial> materials;
    std::shared_ptr<const Catalogue> catalogue;
    std::vector<std::uint8_t> space; // per owned cell
    std::vector<ReservoirEntry> reservoir;
    std::vector<MacSaturationPair> pairs; // liquid/vapour pairs that boil and condense
    std::shared_ptr<const MacClosedWorld> world;
    Ledger ledger;
    MAOniColonyStatus status{};
};

std::mutex lifecycle; // start, restore, stop, advance and checkpoint
std::mutex inputs;    // queued transfers and heat, receipts
std::mutex publication;
std::atomic<bool> enabled{};
std::unique_ptr<Session> session;
std::shared_ptr<const Published> latest;
// Geometry and ownership are published before enabled and unchanged while it is set.
MAOniColonyConfig geometry{};
std::unique_ptr<std::atomic<std::uint8_t>[]> owned;
std::size_t owned_count = 0;
std::array<std::atomic<std::uint64_t>, MA_ONI_HOOK_COUNT> suppressed{};
std::vector<MAOniColonyTransfer> pending_transfers;
std::vector<MAOniColonyHeat> pending_heat;
std::vector<MAOniColonyTerrain> pending_terrain;
std::vector<MAOniColonyReceipt> receipts;
std::uint64_t receipt_sequence = 0, acknowledged = 0, sessions = 0;
std::atomic<std::uint64_t> start_thread{};
// Native shadow: per owned cell, what write_shadow put there this frame.
struct ShadowCell { std::uint16_t element = 0, before = 0; bool written = false; float mass = 0, temperature = 0; };
// A cell native froze: its liquid leaves the owner and the cell closes.
struct Freeze { std::uint32_t public_cell, native_index; double mass_kg; };
std::vector<Freeze> pending_freezes; // under inputs
std::vector<ShadowCell> shadow; // native worker only
struct NativeExchange { double heat_j = 0, mass_in_kg = 0, mass_out_kg = 0; std::uint64_t changes = 0; };
NativeExchange exchanged; // under inputs

// Solver thread (docs/colony-owner.md#pacing). ONI's game thread waits for
// each native frame right after posting it, so a positive frame only hands its
// timestep here and returns. Frames that arrive while a step runs add their
// time to the next step (at most 10 s waits), so the owner never drops game
// time; pacing in the mod normally holds the game clock instead.
void run_frame(std::uint64_t ticket, double seconds, const char* refusal, std::uint64_t thread) noexcept;
struct Solver {
    std::mutex m; std::condition_variable wake, idle;
    std::thread thread;
    bool quit = false, running = false;
    const char* refusal = nullptr; // a frame the owner must refuse
    std::uint64_t ticket = 0; double seconds = 0; // waiting work
    std::atomic<bool> busy{};
    void loop() {
        const auto id = static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
        std::unique_lock l(m);
        for (;;) {
            wake.wait(l, [&] { return quit || seconds > 0 || refusal; });
            if (quit) return;
            const auto t = ticket; const double dt = seconds; const auto r = refusal;
            seconds = 0; refusal = nullptr; running = true;
            l.unlock(); run_frame(t, dt, r, id); l.lock();
            running = false;
            if (seconds == 0) busy.store(false, std::memory_order_release);
            idle.notify_all();
        }
    }
    // Waits until no step runs or waits; `discard` drops waiting work first.
    void settle(bool discard) {
        std::unique_lock l(m);
        if (discard) { seconds = 0; refusal = nullptr; }
        idle.wait(l, [&] { return !running && seconds == 0 && !refusal; });
        busy.store(false, std::memory_order_release);
    }
    ~Solver() { { std::lock_guard l(m); quit = true; } wake.notify_all(); if (thread.joinable()) thread.join(); }
};
Solver solver; // declared after the session state, so destroyed before it

MAOniColonyStatus empty_status() { MAOniColonyStatus s{}; s.struct_size = sizeof(s); s.abi_version = MA_ONI_COLONY_ABI_VERSION; return s; }
void write_error(MAOniColonyStatus& s, const char* text) { std::snprintf(s.error, sizeof(s.error), "%s", text); }

void validate(const MAOniColonyConfig& c) {
    require(c.struct_size == sizeof(c) && c.abi_version == MA_ONI_COLONY_ABI_VERSION, "Invalid colony configuration ABI");
    require((c.flags & ~MA_ONI_COLONY_PHYSICS_CONDUCTION) == 0, "Unknown colony physics flag");
    require(c.grid_width >= 3 && c.grid_height >= 3 && c.grid_width <= 65535 && c.grid_height <= 65535, "Invalid colony grid");
    require(c.width >= 2 && c.height >= 2 && c.width <= MA_ONI_COLONY_MAXIMUM_SIDE && c.height <= MA_ONI_COLONY_MAXIMUM_SIDE &&
        static_cast<std::uint64_t>(c.width) * c.height <= MA_ONI_COLONY_MAXIMUM_CELLS, "Colony rectangle is outside its size bounds");
    require(static_cast<std::uint64_t>(c.offset_x) + c.width <= c.grid_width && static_cast<std::uint64_t>(c.offset_y) + c.height <= c.grid_height,
        "Colony rectangle leaves the grid");
    require(std::isfinite(c.gravity_m_per_s2) && c.gravity_m_per_s2 >= 0 && c.gravity_m_per_s2 <= 100, "Invalid colony gravity");
    require(std::isfinite(c.maximum_timestep_s) && c.maximum_timestep_s > 0 && c.maximum_timestep_s <= .1, "Invalid colony maximum timestep");
}
std::shared_ptr<const Catalogue> catalogue_of(std::span<const MAOniColonyMaterial> materials) {
    require(!materials.empty() && materials.size() <= 4096, "A colony needs 1..4096 materials");
    std::vector<GameFluidElement> elements;
    for (const auto& m : materials) {
        require(m.struct_size == sizeof(m) && m.abi_version == MA_ONI_COLONY_ABI_VERSION && (m.phase == 1 || m.phase == 2) && m.reserved == 0,
            "Invalid colony material record");
        require(std::memchr(m.id, 0, sizeof(m.id)) != nullptr, "Colony material id is not terminated");
        GameFluidElement e; e.id = m.id; e.hash = m.element_hash; e.native_index = m.native_index; e.gas = m.phase == 1;
        e.molar_mass_kg_per_mol = m.molar_mass_kg_per_mol; e.specific_heat_j_per_kg_k = m.specific_heat_j_per_kg_k;
        e.conductivity_w_per_m_k = m.conductivity_w_per_m_k; e.density_kg_per_m3 = m.density_kg_per_m3;
        e.low_temperature_k = m.low_temperature_k; e.high_temperature_k = m.high_temperature_k;
        e.low_target = m.low_target; e.high_target = m.high_target; e.low_byproduct = m.low_byproduct; e.high_byproduct = m.high_byproduct;
        e.low_byproduct_fraction = m.low_byproduct_fraction; e.high_byproduct_fraction = m.high_byproduct_fraction;
        elements.push_back(std::move(e));
    }
    return std::make_shared<const Catalogue>(build_catalogue(std::move(elements)));
}
// Each pair's critical temperature from the catalogue (no liquid above it).
std::vector<MacSaturationPair> with_critical(const Catalogue& cat, std::vector<MacSaturationPair> pairs) {
    for (auto& p : pairs) for (const auto& c : cat.pairs)
        if (c.liquid == p.liquid && c.vapour == p.vapour && c.critical_k > 0) p.critical_temperature_k = c.critical_k;
    return pairs;
}
MacClosedSettings settings_of(const MAOniColonyConfig& c) {
    MacClosedSettings s; s.maximum_timestep_s = c.maximum_timestep_s; s.numerical_kernel = MacNumericalKernel::production;
    s.gravity_m_per_s2 = c.gravity_m_per_s2; s.thermal_conduction = (c.flags & MA_ONI_COLONY_PHYSICS_CONDUCTION) != 0;
    s.vacuum = MacVacuumRepresentation::void_phase; s.reconstruction = MacMaterialReconstruction::interface_plic;
    s.subcell_void = MacSubcellVoidPolicy::expand_unresolved_gas;
    s.step_control = MacStepControl::predictive;
    return s;
}
std::size_t rect_index(const MAOniColonyConfig& c, std::uint32_t public_cell) {
    const auto x = public_cell % c.grid_width, y = public_cell / c.grid_width;
    require(public_cell < c.grid_width * c.grid_height && x >= c.offset_x && x < c.offset_x + c.width && y >= c.offset_y && y < c.offset_y + c.height,
        "Cell is outside the colony rectangle");
    return (x - c.offset_x) + static_cast<std::size_t>(y - c.offset_y) * c.width;
}
std::vector<long double> totals(const MacClosedWorld& w) {
    std::vector<long double> out(w.registry().size());
    for (const auto& cell : w.state().cells) for (std::size_t k = 0; k < out.size(); ++k) out[k] += cell.mixture.masses[k];
    return out;
}
long double energy_of(const MacClosedWorld& w) {
    const auto& st = w.state(); MacEnergySum total;
    for (const auto& cell : st.cells) account_mac_cell_internal_energy(total, cell);
    const auto inertia = mac_face_inertias(w.registry(), st, w.thermodynamics());
    const auto jx = mac_field_values(st.momentum_x, st.full_momentum_x), jy = mac_field_values(st.momentum_y, st.full_momentum_y);
    for (std::size_t i = 0; i < jx.size(); ++i) if (inertia.x[i] > 0) total.add(MacEnergy::kinetic(inertia.x[i], jx[i]));
    for (std::size_t i = 0; i < jy.size(); ++i) if (inertia.y[i] > 0) total.add(MacEnergy::kinetic(inertia.y[i], jy[i]));
    long double value = 0; for (std::size_t k = 0; k < total.size(); ++k) value += mac_energy_long_double(total.component(k));
    const double g = w.settings().gravity_m_per_s2;
    for (std::size_t i = 0; i < st.cells.size(); ++i)
        value += static_cast<long double>(st.cells[i].mixture.mass()) * g * ((static_cast<double>(i / st.grid.width) + .5) * st.grid.spacing_m);
    return value;
}
long double parcel_energy(const MacFluidParcel& p) {
    return static_cast<long double>(p.internal_energy_j) + mac_energy_long_double(p.resolved_kinetic_energy) + mac_energy_long_double(p.potential_energy);
}
std::vector<MAOniColonyCellView> project(const Session& s, const MacClosedWorld& w) {
    const auto& st = w.state(); const auto& registry = w.registry(); const auto& c = *s.catalogue;
    std::vector<MAOniColonyCellView> views(st.cells.size());
    for (std::size_t i = 0; i < st.cells.size(); ++i) {
        auto& v = views[i];
        if (st.grid.walls[i]) continue;
        v.flags = MA_ONI_COLONY_VIEW_OWNED; v.element_index = s.config.vacuum_index;
        const auto& m = st.cells[i].mixture;
        if (!(m.mass() > 0)) continue;
        const auto reading = w.thermodynamics().evaluate(m, mac_material_volume(st, i));
        double gas = 0, liquid = 0, best_gas = 0, best_liquid = 0; std::size_t gas_k = 0, liquid_k = 0;
        for (std::size_t k = 0; k < registry.size(); ++k) {
            const double mass = m.masses[k]; if (mass == 0) continue;
            if (registry[k].phase == Phase::gas) { gas += mass; if (mass > best_gas) { best_gas = mass; gas_k = k; } }
            else { liquid += mass; if (mass > best_liquid) { best_liquid = mass; liquid_k = k; } }
        }
        const bool liquid_dominant = reading.liquid_volume_m3 >= .5 * st.grid.volume_m3() || gas == 0;
        v.element_index = c.materials[liquid_dominant ? liquid_k : gas_k].game.native_index;
        v.flags |= (gas > 0 ? MA_ONI_COLONY_VIEW_GAS : 0U) | (liquid > 0 ? MA_ONI_COLONY_VIEW_LIQUID : 0U);
        v.mass_kg = static_cast<float>(liquid_dominant ? liquid : gas);
        v.temperature_k = static_cast<float>(reading.temperature_k);
        v.pressure_pa = static_cast<float>(reading.mechanical_pressure_pa);
        v.liquid_fraction = static_cast<float>(reading.liquid_volume_m3 / st.grid.volume_m3());
    }
    return views;
}
void publish(const Session& s, std::vector<MAOniColonyCellView> views) {
    auto p = std::make_shared<Published>();
    p->status = s.status; p->views = std::move(views); p->world = s.world; p->catalogue = s.catalogue;
    std::lock_guard lock(publication); latest = std::move(p);
}
void publish(const Session& s) { publish(s, project(s, *s.world)); }
void fault(Session& s, const char* why) noexcept {
    s.status.state = 2; write_error(s.status, why);
    try { auto p = std::make_shared<Published>(); p->status = s.status;
        { std::lock_guard lock(publication); if (latest) { p->views = latest->views; p->world = latest->world; p->catalogue = latest->catalogue; } latest = std::move(p); } }
    catch (...) {}
}
void activate(std::unique_ptr<Session> next, std::uint64_t thread) {
    require(!enabled.load(std::memory_order_acquire), "A colony owner is already active");
    // Execution only: the kernel's results never depend on its thread count.
    // Leave two cores for the game's own threads.
    auto options = mac_kernel_default_options();
    options.threads = std::clamp(std::thread::hardware_concurrency(), 3U, 18U) - 2;
    set_mac_kernel_default_options(options);
    next->status = empty_status(); next->status.state = 1; next->status.session = ++sessions; next->status.start_thread = thread;
    start_thread.store(thread, std::memory_order_relaxed);
    next->status.species = static_cast<std::uint32_t>(next->catalogue->registry.size()); next->status.time_seconds = next->world->time_s();
    for (const auto& r : next->reservoir) next->status.reservoir_kg += r.mass_kg;
    geometry = next->config; owned_count = next->world->state().grid.walls.size();
    owned = std::make_unique<std::atomic<std::uint8_t>[]>(owned_count);
    for (std::size_t i = 0; i < owned_count; ++i) owned[i].store(next->world->state().grid.walls[i] ? 0 : 1, std::memory_order_relaxed);
    for (auto& v : suppressed) v.store(0, std::memory_order_relaxed);
    { std::lock_guard lock(inputs); pending_transfers.clear(); pending_heat.clear(); pending_terrain.clear(); pending_freezes.clear(); receipts.clear();
      receipt_sequence = acknowledged = 0; exchanged = {}; }
    publish(*next);
    session = std::move(next);
    enabled.store(true, std::memory_order_release);
}

// Nearest open cell (breadth first, within `radius`) and its order.
std::vector<std::size_t> nearby(const MacTransportState& st, std::size_t centre, std::uint32_t radius) {
    const auto w = st.grid.width, h = st.grid.height; std::vector<std::size_t> order;
    if (st.grid.walls[centre] == 0) order.push_back(centre);
    std::vector<std::pair<std::size_t, std::uint32_t>> queue{{centre, 0}}; std::vector<std::uint8_t> seen(st.cells.size()); seen[centre] = 1;
    for (std::size_t q = 0; q < queue.size(); ++q) {
        const auto [i, d] = queue[q]; if (d == radius) continue;
        const auto x = i % w, y = i / w;
        for (const auto n : {x > 0 ? i - 1 : SIZE_MAX, x + 1 < w ? i + 1 : SIZE_MAX, y > 0 ? i - w : SIZE_MAX, y + 1 < h ? i + w : SIZE_MAX}) {
            if (n == SIZE_MAX || seen[n]) continue;
            seen[n] = 1;
            if (st.grid.walls[n]) continue;
            order.push_back(n); queue.emplace_back(n, d + 1);
        }
    }
    return order;
}
}

// Moves material between cells: extract each move's mass, then inject each
// target's parcels at the temperature whose internal energy is their U+K+G
// less the potential at the target. Both directions are booked, so the ledger
// closes whatever the rounding.
struct Move { std::size_t from = 0, to = 0, species = 0; double mass = 0; };
MacClosedWorld move_material(MacClosedWorld world, std::span<const Move> moves, Ledger& L, MAOniColonyStatus& st) {
    if (moves.empty()) return world;
    const auto& registry = world.registry(); const double g = world.settings().gravity_m_per_s2;
    struct Target { std::vector<double> masses; long double energy = 0, heat = 0, mass = 0; };
    std::map<std::size_t, Target> targets;
    for (std::size_t first = 0; first < moves.size(); first += 4096) {
        std::vector<MacFluidTransfer> edits;
        for (std::size_t k = first; k < std::min(moves.size(), first + 4096); ++k) edits.push_back({moves[k].from, moves[k].species, false, moves[k].mass, 0});
        auto result = world.with_fluid_transfers(edits, MacFluidTransferTopology::occupied_mixture);
        world = std::move(result.world);
        for (std::size_t k = 0; k < result.parcels.size(); ++k) {
            const auto& p = result.parcels[k]; auto& t = targets[moves[first + k].to];
            if (t.masses.empty()) t.masses.assign(registry.size(), 0);
            t.masses[p.species_index] += p.mass_kg; t.energy += parcel_energy(p); t.heat += p.mass_kg * p.temperature_k; t.mass += p.mass_kg;
            L.extracted[p.species_index] += p.mass_kg; L.energy_out += parcel_energy(p);
        }
    }
    std::vector<MacFluidTransfer> inject;
    for (const auto& [to, t] : targets) {
        if (!(t.mass > 0)) continue;
        long double offset = 0, capacity = 0;
        for (std::size_t k = 0; k < t.masses.size(); ++k) if (t.masses[k] > 0) {
            offset += t.masses[k] * registry[k].internal_energy(0); capacity += t.masses[k] * registry[k].cv();
        }
        const long double potential = t.mass * g * ((static_cast<double>(to / world.state().grid.width) + .5) * world.state().grid.spacing_m);
        double temperature = static_cast<double>((t.energy - potential - offset) / capacity);
        if (!std::isfinite(temperature) || !(temperature > 1)) temperature = static_cast<double>(t.heat / t.mass);
        for (std::size_t k = 0; k < t.masses.size(); ++k) if (t.masses[k] > 0) inject.push_back({to, k, true, t.masses[k], temperature});
    }
    for (std::size_t first = 0; first < inject.size(); first += 4096) {
        const std::span<const MacFluidTransfer> batch(inject.data() + first, std::min<std::size_t>(4096, inject.size() - first));
        auto result = world.with_fluid_transfers(batch, MacFluidTransferTopology::occupied_mixture);
        world = std::move(result.world);
        for (const auto& p : result.parcels) { L.injected[p.species_index] += p.mass_kg; L.energy_in += parcel_energy(p); }
    }
    (void)st;
    return world;
}
// Terrain edits at the start of a frame (docs/colony-owner.md#terrain).
MacClosedWorld apply_terrain(Session& s, MacClosedWorld world, std::span<const MAOniColonyTerrain> edits) {
    const auto& st0 = world.state(); const auto w = st0.grid.width, h = st0.grid.height;
    std::vector<std::size_t> opening, closing;
    std::vector<std::uint8_t> touched(st0.cells.size());
    for (const auto& e : edits) {
        const auto i = rect_index(s.config, e.public_cell);
        if (touched[i]) continue;
        touched[i] = 1;
        if (e.kind == MA_ONI_COLONY_OPEN && st0.grid.walls[i]) opening.push_back(i);
        if (e.kind == MA_ONI_COLONY_CLOSE && !st0.grid.walls[i]) closing.push_back(i);
    }
    const auto neighbours = [&](std::size_t i) {
        std::vector<std::size_t> out; const auto x = i % w, y = i / w;
        for (const auto n : {x > 0 ? i - 1 : SIZE_MAX, x + 1 < w ? i + 1 : SIZE_MAX, y > 0 ? i - w : SIZE_MAX, y + 1 < h ? i + w : SIZE_MAX})
            if (n != SIZE_MAX) out.push_back(n);
        return out;
    };
    if (!closing.empty()) {
        // Displace each closing cell's contents equally into its open neighbours.
        std::vector<Move> moves; std::vector<std::uint8_t> closes(st0.cells.size());
        for (const auto i : closing) closes[i] = 1;
        for (const auto i : closing) {
            const auto& m = world.state().cells[i].mixture; if (!(m.mass() > 0)) continue;
            std::vector<std::size_t> open;
            for (const auto n : neighbours(i)) if (!world.state().grid.walls[n] && !closes[n] && !s.space[n]) open.push_back(n);
            if (open.empty()) {
                // A sealed cell's contents go to the reservoir.
                std::vector<MacFluidTransfer> drain;
                for (std::size_t k = 0; k < m.masses.size(); ++k) if (m.masses[k] > 0) drain.push_back({i, k, false, m.masses[k], 0});
                auto result = world.with_fluid_transfers(drain, MacFluidTransferTopology::occupied_mixture);
                world = std::move(result.world);
                for (const auto& p : result.parcels) {
                    s.ledger.extracted[p.species_index] += p.mass_kg; s.ledger.energy_out += parcel_energy(p);
                    s.reservoir.push_back({i, p.species_index, p.mass_kg, p.temperature_k}); s.status.reservoir_kg += p.mass_kg;
                }
                continue;
            }
            for (std::size_t k = 0; k < m.masses.size(); ++k) if (m.masses[k] > 0) {
                double left = m.masses[k];
                for (std::size_t q = 0; q < open.size(); ++q) {
                    const double share = q + 1 == open.size() ? left : m.masses[k] / static_cast<double>(open.size());
                    moves.push_back({i, open[q], k, share}); left -= share;
                }
            }
        }
        world = move_material(std::move(world), moves, s.ledger, s.status);
        world = world.with_closed_cells(closing);
        for (const auto i : closing) owned[i].store(0, std::memory_order_relaxed);
        s.status.closed_cells += closing.size();
    }
    if (!opening.empty()) {
        world = world.with_opened_cells(opening);
        for (const auto i : opening) owned[i].store(1, std::memory_order_relaxed);
        // Refill each opened cell from its gas neighbours at their pressure:
        // with m such neighbours each gives 1/(m+1) of its gas.
        std::vector<Move> moves; const auto& registry = world.registry();
        std::vector<std::uint8_t> opens(world.state().cells.size()); for (const auto i : opening) opens[i] = 1;
        for (const auto i : opening) {
            std::vector<std::size_t> donors;
            for (const auto n : neighbours(i)) {
                if (world.state().grid.walls[n] || opens[n]) continue;
                const auto& m = world.state().cells[n].mixture;
                for (std::size_t k = 0; k < m.masses.size(); ++k) if (m.masses[k] > 0 && registry[k].phase == Phase::gas) { donors.push_back(n); break; }
            }
            for (const auto n : donors) {
                const auto& m = world.state().cells[n].mixture;
                for (std::size_t k = 0; k < m.masses.size(); ++k) if (m.masses[k] > 0 && registry[k].phase == Phase::gas)
                    moves.push_back({n, i, k, m.masses[k] / static_cast<double>(donors.size() + 1)});
            }
        }
        world = move_material(std::move(world), moves, s.ledger, s.status);
        s.status.opened_cells += opening.size();
    }
    return world;
}

// Audited conversions (docs/colony-owner.md#conversions): a fluid past one of
// its game transitions whose target is another fluid outside its own
// liquid/vapour pair (polluted water boiling to steam, crude oil cracking to
// petroleum) converts at the cell's temperature, 1 - exp(-dt / 1 s) of it per
// step. A solid byproduct (salt from salt water) leaves the owner and is
// spawned by the game as debris through a byproduct receipt. Mass moves by
// booked transfers; the energy difference is booked with them, as the game's
// own transitions keep the temperature.
MacClosedWorld convert_identities(Session& s, MacClosedWorld world, double seconds) {
    const auto& cat = *s.catalogue; const auto& st = world.state();
    struct Rule { std::size_t from, to; std::int32_t byproduct; double fraction; bool high; double threshold; };
    std::vector<Rule> rules;
    for (std::size_t k = 0; k < cat.materials.size(); ++k) {
        const auto& g = cat.materials[k].game;
        for (const bool high : {true, false}) {
            const auto target = high ? g.high_target : g.low_target;
            if (target == 0) continue;
            std::size_t to;
            try { to = cat.species_of_hash(target); } catch (...) { continue; } // a solid target: native freezing
            bool pair = false;
            for (const auto& p : s.pairs) pair = pair || (p.liquid == k && p.vapour == to) || (p.vapour == k && p.liquid == to);
            if (pair || to == k) continue;
            rules.push_back({k, to, high ? g.high_byproduct : g.low_byproduct, high ? g.high_byproduct_fraction : g.low_byproduct_fraction,
                high, high ? g.high_temperature_k : g.low_temperature_k});
        }
    }
    if (rules.empty()) return world;
    const double share = 1 - std::exp(-seconds / phase_relaxation_time_s);
    std::vector<MacFluidTransfer> edits; struct Product { std::size_t cell, species; double mass; std::int32_t byproduct; double byproduct_kg; };
    std::vector<Product> products;
    for (std::size_t i = 0; i < st.cells.size() && edits.size() < 4000; ++i) {
        if (st.grid.walls[i] || s.space[i]) continue;
        const auto& m = st.cells[i].mixture;
        bool candidate = false; for (const auto& r : rules) candidate = candidate || m.masses[r.from] > 0;
        if (!candidate) continue;
        double t = 0;
        try { t = cat.eos->evaluate(m, mac_material_volume(st, i)).temperature_k; } catch (...) { continue; }
        for (const auto& r : rules) {
            if (!(m.masses[r.from] > 0) || (r.high ? !(t > r.threshold) : !(t < r.threshold))) continue;
            // A conversion too small to change the cell's masses in binary64
            // waits (traces stay unconverted).
            const double converted = m.masses[r.from] * share;
            if (!(converted > 1e-9 * m.mass())) continue;
            const double byproduct = r.byproduct != 0 ? converted * std::clamp(r.fraction, 0.0, 1.0) : 0;
            edits.push_back({i, r.from, false, converted, 0});
            products.push_back({i, r.to, converted - byproduct, r.byproduct, byproduct});
        }
    }
    if (edits.empty()) return world;
    // The cell's temperature before conversion is the products' temperature.
    std::vector<double> temperature(edits.size());
    for (std::size_t q = 0; q < edits.size(); ++q)
        temperature[q] = cat.eos->evaluate(st.cells[edits[q].cell_index].mixture, mac_material_volume(st, edits[q].cell_index)).temperature_k;
    auto& L = s.ledger;
    auto taken = world.with_fluid_transfers(edits, MacFluidTransferTopology::occupied_mixture);
    world = std::move(taken.world);
    std::vector<MacFluidTransfer> given;
    for (std::size_t q = 0; q < products.size(); ++q) {
        const auto& parcel = taken.parcels[q];
        L.extracted[parcel.species_index] += parcel.mass_kg; L.energy_out += parcel_energy(parcel);
        s.status.converted_kg += parcel.mass_kg;
        if (products[q].mass > 0) given.push_back({products[q].cell, products[q].species, true, products[q].mass, temperature[q]});
        if (products[q].byproduct_kg > 0) {
            std::lock_guard lock(inputs);
            MAOniColonyReceipt r{}; r.sequence = ++receipt_sequence; r.completed_frame = s.status.completed_frames + 1;
            r.callback = products[q].byproduct; r.kind = MA_ONI_COLONY_BYPRODUCT;
            r.public_cell = static_cast<std::uint32_t>((s.config.offset_x + products[q].cell % s.config.width) +
                (s.config.offset_y + products[q].cell / s.config.width) * s.config.grid_width);
            r.mass_kg = static_cast<float>(products[q].byproduct_kg); r.temperature_k = static_cast<float>(temperature[q]);
            receipts.push_back(r);
        }
    }
    if (!given.empty()) {
        auto result = world.with_fluid_transfers(given, MacFluidTransferTopology::occupied_mixture);
        world = std::move(result.world);
        for (const auto& parcel : result.parcels) { L.injected[parcel.species_index] += parcel.mass_kg; L.energy_in += parcel_energy(parcel); }
    }
    return world;
}

bool active() noexcept { return enabled.load(std::memory_order_acquire); }
bool busy() noexcept { return solver.busy.load(std::memory_order_acquire); }
void wait() noexcept { solver.settle(false); }

void start(const MAOniColonyConfig& config, std::span<const MAOniColonyMaterial> materials, std::span<const MAOniColonyCell> cells, std::uint64_t thread) {
    std::lock_guard lock(lifecycle);
    const ArithmeticScope arithmetic;
    validate(config);
    require(cells.size() == static_cast<std::size_t>(config.width) * config.height, "The colony inventory must cover the rectangle");
    auto next = std::make_unique<Session>();
    next->config = config; next->materials.assign(materials.begin(), materials.end()); next->catalogue = catalogue_of(materials);
    const auto& cat = *next->catalogue;
    std::vector<ImportCell> imported(cells.size()); next->space.assign(cells.size(), 0);
    for (std::size_t i = 0; i < cells.size(); ++i) {
        const auto& c = cells[i];
        require(c.kind <= MA_ONI_COLONY_SPACE && std::isfinite(c.mass_kg) && c.mass_kg >= 0 && std::isfinite(c.temperature_k) && c.temperature_k >= 0,
            "Invalid colony import cell");
        if (c.kind == MA_ONI_COLONY_SOLID) { imported[i] = {ImportCellKind::solid, import_vacuum, 0, 0}; continue; }
        if (c.kind == MA_ONI_COLONY_SPACE) { imported[i] = {ImportCellKind::space, import_vacuum, 0, 0}; next->space[i] = 1; continue; }
        if (c.native_index == config.vacuum_index || c.mass_kg == 0) { imported[i] = {ImportCellKind::fluid, import_vacuum, 0, 0}; continue; }
        require(cat.has_native(c.native_index), "A fluid cell holds an element outside the colony catalogue");
        require(c.temperature_k > 0, "A fluid cell has no positive temperature");
        imported[i] = {ImportCellKind::fluid, static_cast<std::uint32_t>(cat.species_of_native(c.native_index)), c.mass_kg, c.temperature_k};
    }
    auto settled = settle_import(cat, config.width, config.height, imported, config.gravity_m_per_s2);
    next->reservoir = std::move(settled.reservoir);
    next->world = std::make_shared<const MacClosedWorld>(cat.registry, cat.eos, std::move(settled.state), settings_of(config));
    next->ledger.initial = totals(*next->world); next->ledger.initial_energy = energy_of(*next->world);
    for (auto* v : {&next->ledger.injected, &next->ledger.extracted, &next->ledger.sunk, &next->ledger.converted}) v->assign(cat.registry.size(), 0);
    next->pairs = with_critical(cat, mac_saturation_pairs(cat.registry, *cat.eos));
    activate(std::move(next), thread);
}

void stop() noexcept {
    enabled.store(false, std::memory_order_release);
    solver.settle(true);
    std::lock_guard lock(lifecycle);
    session.reset();
    std::lock_guard p(publication); latest.reset();
}

MAOniColonyStatus status() {
    std::lock_guard lock(publication);
    auto s = latest ? latest->status : empty_status();
    std::lock_guard q(inputs); s.receipts_pending = receipts.size();
    s.native_heat_j = exchanged.heat_j; s.native_mass_in_kg = exchanged.mass_in_kg; s.native_mass_out_kg = exchanged.mass_out_kg;
    s.native_changes = exchanged.changes;
    return s;
}
void copy(MAOniColonyStatus& out, MAOniColonyCellView* views, std::uint32_t capacity) {
    std::shared_ptr<const Published> p; { std::lock_guard lock(publication); p = latest; }
    require(p != nullptr, "No colony owner is active");
    if (views) {
        require(capacity >= p->views.size(), "Colony view buffer is too small");
        std::copy(p->views.begin(), p->views.end(), views);
    }
    out = p->status;
    std::lock_guard q(inputs);
    out.native_heat_j = exchanged.heat_j; out.native_mass_in_kg = exchanged.mass_in_kg; out.native_mass_out_kg = exchanged.mass_out_kg;
    out.native_changes = exchanged.changes;
}
std::uint32_t composition(std::uint32_t public_cell, std::uint32_t* indices, double* masses, std::uint32_t capacity) {
    std::shared_ptr<const Published> p; { std::lock_guard lock(publication); p = latest; }
    require(p != nullptr && p->world != nullptr, "No colony owner is active");
    const auto i = rect_index(geometry, public_cell);
    const auto& m = p->world->state().cells[i].mixture; std::uint32_t count = 0;
    for (std::size_t k = 0; k < m.masses.size(); ++k) if (m.masses[k] > 0) {
        if (count < capacity) { indices[count] = p->catalogue->materials[k].game.native_index; masses[count] = m.masses[k]; }
        ++count;
    }
    return count;
}
std::uint32_t species(MAOniColonySpecies* out, std::uint32_t capacity) {
    std::shared_ptr<const Published> p; { std::lock_guard lock(publication); p = latest; }
    require(p != nullptr && p->catalogue != nullptr, "No colony owner is active");
    const auto& cat = *p->catalogue; const auto& materials = cat.eos->reference().materials();
    const auto n = static_cast<std::uint32_t>(cat.materials.size());
    if (!out) return n;
    require(capacity >= n, "Colony species buffer is too small");
    for (std::uint32_t k = 0; k < n; ++k) {
        const auto& m = materials[k];
        out[k] = {cat.materials[k].game.native_index, cat.materials[k].game.gas ? 1U : 2U, m.molar_mass_kg_per_mol, m.cv_j_per_kg_k,
            m.zero_pressure_density_kg_per_m3, m.energy_offset_j_per_kg};
    }
    return n;
}
std::uint64_t readings(std::vector<MAOniColonyReading>& out, std::vector<MAOniColonyConstituent>& parts) {
    std::shared_ptr<const Published> p; { std::lock_guard lock(publication); p = latest; }
    require(p != nullptr && p->world != nullptr, "No colony owner is active");
    const ArithmeticScope arithmetic;
    const auto& w = *p->world; const auto& st = w.state(); const auto& eos = w.thermodynamics();
    out.clear(); parts.clear();
    for (std::size_t i = 0; i < st.cells.size(); ++i) {
        const auto& m = st.cells[i].mixture;
        if (st.grid.walls[i] || !(m.mass() > 0)) continue;
        const double volume = mac_material_volume(st, i);
        const auto e = eos.evaluate(m, volume);
        MAOniColonyReading r{};
        r.public_cell = static_cast<std::uint32_t>((geometry.offset_x + i % geometry.width) + (geometry.offset_y + i / geometry.width) * geometry.grid_width);
        r.first_constituent = static_cast<std::uint32_t>(parts.size());
        r.temperature_k = e.temperature_k; r.mechanical_pressure_pa = e.mechanical_pressure_pa; r.gas_pressure_pa = e.gas_pressure_pa;
        r.internal_energy_j = m.energy; r.occupied_volume_m3 = volume;
        for (std::size_t k = 0; k < m.masses.size(); ++k) if (m.masses[k] > 0) {
            parts.push_back({static_cast<std::uint32_t>(k), 0, m.masses[k], e.partial_volumes_m3[k]});
            if (!p->catalogue->materials[k].game.gas) r.liquid_volume_m3 += e.partial_volumes_m3[k];
        }
        r.constituents = static_cast<std::uint32_t>(parts.size()) - r.first_constituent;
        out.push_back(r);
    }
    return p->status.completed_frames;
}

void queue_transfers(std::span<const MAOniColonyTransfer> edits) {
    require(active(), "No colony owner is active");
    for (const auto& e : edits) {
        const auto kind = e.kind & ~MA_ONI_COLONY_CONSUMER;
        require(kind >= MA_ONI_COLONY_INJECT && kind <= MA_ONI_COLONY_EXTRACT_LIQUID && (kind != MA_ONI_COLONY_INJECT || kind == e.kind) &&
            e.reserved == 0 && e.radius <= 64 &&
            std::isfinite(e.mass_kg) && e.mass_kg >= 0 && std::isfinite(e.temperature_k) && e.temperature_k >= 0, "Invalid colony transfer");
        (void)rect_index(geometry, e.public_cell);
    }
    std::lock_guard lock(inputs);
    require(pending_transfers.size() + edits.size() <= 1u << 20, "Too many queued colony transfers");
    pending_transfers.insert(pending_transfers.end(), edits.begin(), edits.end());
}
void queue_heat(std::span<const MAOniColonyHeat> edits) {
    require(active(), "No colony owner is active");
    for (const auto& e : edits) {
        require(std::isfinite(e.joules) && std::isfinite(e.maximum_temperature_k) && e.reserved == 0, "Invalid colony heat edit");
        (void)rect_index(geometry, e.public_cell);
    }
    std::lock_guard lock(inputs);
    require(pending_heat.size() + edits.size() <= 1u << 20, "Too many queued colony heat edits");
    pending_heat.insert(pending_heat.end(), edits.begin(), edits.end());
}
void queue_terrain(std::span<const MAOniColonyTerrain> edits) {
    require(active(), "No colony owner is active");
    for (const auto& e : edits) {
        require(e.kind == MA_ONI_COLONY_OPEN || e.kind == MA_ONI_COLONY_CLOSE, "Invalid colony terrain edit");
        (void)rect_index(geometry, e.public_cell);
    }
    std::lock_guard lock(inputs);
    require(pending_terrain.size() + edits.size() <= 1u << 20, "Too many queued colony terrain edits");
    pending_terrain.insert(pending_terrain.end(), edits.begin(), edits.end());
}
std::uint32_t copy_receipts(MAOniColonyReceipt* out, std::uint32_t capacity) {
    std::lock_guard lock(inputs);
    const auto count = static_cast<std::uint32_t>(std::min<std::size_t>(capacity, receipts.size()));
    if (out) std::copy(receipts.begin(), receipts.begin() + count, out);
    return static_cast<std::uint32_t>(receipts.size());
}
void ack_receipts(std::uint64_t through) {
    std::lock_guard lock(inputs);
    require(through >= acknowledged && through <= receipt_sequence, "Colony receipt acknowledgement is out of order");
    std::erase_if(receipts, [&](const MAOniColonyReceipt& r) { return r.sequence <= through; });
    acknowledged = through;
}

bool owns(int native_cell) noexcept {
    if (!enabled.load(std::memory_order_acquire) || native_cell < 0) return false;
    const auto pitch = geometry.grid_width + 2, position = static_cast<std::uint32_t>(native_cell);
    const auto nx = position % pitch, ny = position / pitch;
    if (!nx || !ny) return false;
    const auto x = nx - 1, y = ny - 1;
    if (x < geometry.offset_x || x - geometry.offset_x >= geometry.width || y < geometry.offset_y || y - geometry.offset_y >= geometry.height) return false;
    return owned[(x - geometry.offset_x) + static_cast<std::size_t>(y - geometry.offset_y) * geometry.width].load(std::memory_order_relaxed) != 0;
}

void write_shadow(const NativeCells& n) noexcept {
    if (!enabled.load(std::memory_order_acquire)) return;
    std::shared_ptr<const Published> p; { std::lock_guard lock(publication); p = latest; }
    if (!p || !p->catalogue || p->views.size() != owned_count || p->status.state != 1 || n.pitch != geometry.grid_width + 2) return;
    const auto& cat = *p->catalogue; const auto vacuum = geometry.vacuum_index;
    try { shadow.assign(owned_count, ShadowCell{}); } catch (...) { return; }
    for (std::size_t i = 0; i < owned_count; ++i) {
        const std::size_t c = (geometry.offset_x + i % geometry.width + 1) + (geometry.offset_y + i / geometry.width + 1) * static_cast<std::size_t>(n.pitch);
        const auto present = n.element[0][c];
        shadow[i].before = present;
        if (!owned[i].load(std::memory_order_relaxed)) continue;
        // A solid native placed this frame (a tile being built) stays native.
        if (present != vacuum && !cat.has_native(present)) continue;
        const auto& v = p->views[i]; const bool empty = !(v.mass_kg > 0);
        auto& sh = shadow[i];
        sh.element = static_cast<std::uint16_t>(empty ? vacuum : v.element_index); sh.written = true;
        sh.mass = empty ? 0.f : v.mass_kg; sh.temperature = empty ? 0.f : v.temperature_k;
        for (int b = 0; b < 2; ++b) { n.element[b][c] = sh.element; n.mass[b][c] = sh.mass; n.temperature[b][c] = sh.temperature; }
    }
}

void capture_shadow(const NativeCells& n) noexcept {
    if (!enabled.load(std::memory_order_acquire) || shadow.size() != owned_count || n.pitch != geometry.grid_width + 2) return;
    std::shared_ptr<const Published> p; { std::lock_guard lock(publication); p = latest; }
    if (!p || !p->catalogue) return;
    try {
        const auto& cat = *p->catalogue; const auto vacuum = geometry.vacuum_index;
        const auto heat_capacity = [&](std::uint16_t e) { return cat.materials[cat.species_of_native(e)].game.specific_heat_j_per_kg_k; };
        std::vector<MAOniColonyHeat> heat; std::vector<MAOniColonyTransfer> moves; std::vector<MAOniColonyTerrain> opened;
        std::vector<Freeze> froze; NativeExchange sum;
        const auto fluid = [&](std::uint16_t e) { return e == vacuum || cat.has_native(e); };
        for (std::size_t i = 0; i < owned_count; ++i) {
            const auto& sh = shadow[i];
            const std::size_t c = (geometry.offset_x + i % geometry.width + 1) + (geometry.offset_y + i / geometry.width + 1) * static_cast<std::size_t>(n.pitch);
            const auto e = n.element[1][c]; const float m = n.mass[1][c], t = n.temperature[1][c];
            const auto cell = static_cast<std::uint32_t>((geometry.offset_x + i % geometry.width) + (geometry.offset_y + i / geometry.width) * geometry.grid_width);
            if (!sh.written) {
                // A native solid that melted (or sublimated away) this frame:
                // the owner opens the cell and takes its fluid.
                if (owned[i].load(std::memory_order_relaxed) || fluid(sh.before) || !fluid(e)) continue;
                if (!(std::isfinite(m) && m >= 0 && std::isfinite(t) && t >= 0)) continue;
                opened.push_back({cell, MA_ONI_COLONY_OPEN});
                if (e != vacuum && m > 0 && t > 0) { moves.push_back({cell, e, MA_ONI_COLONY_INJECT, 0, -1, 0, m, t}); sum.mass_in_kg += m; }
                ++sum.changes; continue;
            }
            if (e == sh.element && m == sh.mass && t == sh.temperature) continue;
            if (!fluid(e)) {
                // Native froze the cell: its liquid leaves and the cell closes.
                if (sh.element != vacuum && sh.mass > 0) { froze.push_back({cell, sh.element, sh.mass}); sum.mass_out_kg += sh.mass; }
                ++sum.changes; continue;
            }
            if (!(std::isfinite(m) && m >= 0 && std::isfinite(t) && t >= 0)) continue;
            ++sum.changes;
            if (e == sh.element && m == sh.mass) {
                // ONI's own heat exchange (tiles, buildings, items, pipes) with the shadow.
                if (e == vacuum || !(m > 0)) continue;
                const double joules = static_cast<double>(m) * heat_capacity(e) * (static_cast<double>(t) - sh.temperature);
                heat.push_back({cell, 0, joules, 0}); sum.heat_j += joules;
                continue;
            }
            // Native actors (element consumers and emitters) changed the mass.
            const auto take = [&](std::uint16_t element, double mass) {
                moves.push_back({cell, element, MA_ONI_COLONY_EXTRACT, 0, -1, 0, mass, 0}); sum.mass_out_kg += mass; };
            const auto give = [&](std::uint16_t element, double mass, double temperature) {
                moves.push_back({cell, element, MA_ONI_COLONY_INJECT, 0, -1, 0, mass, temperature}); sum.mass_in_kg += mass; };
            if (e == sh.element) {
                const double delta = static_cast<double>(m) - sh.mass;
                if (delta < 0) take(e, -delta);
                else {
                    // The emitted parcel's temperature, from the mixed endpoint.
                    const double emitted = (static_cast<double>(m) * t - static_cast<double>(sh.mass) * sh.temperature) / delta;
                    give(e, delta, emitted > 0 && std::isfinite(emitted) ? emitted : t);
                }
            } else {
                if (sh.element != vacuum && sh.mass > 0) take(sh.element, sh.mass);
                if (e != vacuum && m > 0 && t > 0) give(e, m, t);
            }
        }
        std::lock_guard q(inputs);
        pending_heat.insert(pending_heat.end(), heat.begin(), heat.end());
        pending_terrain.insert(pending_terrain.end(), opened.begin(), opened.end());
        pending_transfers.insert(pending_transfers.end(), moves.begin(), moves.end());
        pending_freezes.insert(pending_freezes.end(), froze.begin(), froze.end());
        exchanged.heat_j += sum.heat_j; exchanged.mass_in_kg += sum.mass_in_kg; exchanged.mass_out_kg += sum.mass_out_kg; exchanged.changes += sum.changes;
    } catch (...) {}
}

bool allow_native_transition(int native_cell, std::uint16_t element, float temperature) noexcept {
    if (!owns(native_cell)) return true;
    std::shared_ptr<const Published> p; { std::lock_guard lock(publication); p = latest; }
    if (!p || !p->catalogue) return false;
    const auto& cat = *p->catalogue;
    if (!cat.has_native(element)) return true; // a solid native placed this frame
    try {
        const auto& game = cat.materials[cat.species_of_native(element)].game;
        const std::int32_t target = temperature < game.low_temperature_k ? game.low_target : temperature > game.high_temperature_k ? game.high_target : 0;
        if (target == 0) return false;
        // A fluid target is the owner's (phase relaxation); a solid one native's.
        (void)cat.species_of_hash(target);
        return false;
    } catch (...) { return true; }
}

bool suppress(std::uint32_t hook, int native_cell) noexcept {
    if (!enabled.load(std::memory_order_acquire) || native_cell < 0) return false;
    const auto pitch = geometry.grid_width + 2, position = static_cast<std::uint32_t>(native_cell);
    const auto nx = position % pitch, ny = position / pitch;
    if (!nx || !ny) return false;
    const auto x = nx - 1, y = ny - 1;
    if (x < geometry.offset_x || x - geometry.offset_x >= geometry.width || y < geometry.offset_y || y - geometry.offset_y >= geometry.height) return false;
    // Native transitions, sublimation and post-processing act on native
    // solids; their effect on owned cells is captured through the shadow.
    if (!owned[(x - geometry.offset_x) + static_cast<std::size_t>(y - geometry.offset_y) * geometry.width].load(std::memory_order_relaxed)) return false;
    if (hook < suppressed.size()) suppressed[hook].fetch_add(1, std::memory_order_relaxed);
    return true;
}

void advance(std::uint64_t ticket, double seconds, std::uint64_t thread) noexcept {
    if (!enabled.load(std::memory_order_acquire)) return;
    try {
        std::unique_lock l(solver.m);
        const char* why = !(seconds > 0 && seconds <= 1) ? "Native frame timestep is invalid" :
            thread == start_thread.load(std::memory_order_relaxed) ? "A colony frame came from the game thread instead of a native worker" : nullptr;
        if (why) { solver.refusal = why; seconds = 0; }
        solver.idle.wait(l, [&] { return solver.quit || solver.seconds + seconds <= 10; });
        if (solver.quit) return;
        solver.ticket = ticket; solver.seconds += seconds;
        solver.busy.store(true, std::memory_order_release);
        if (!solver.thread.joinable()) solver.thread = std::thread([] { solver.loop(); });
        solver.wake.notify_one();
    } catch (...) {
        std::lock_guard lock(lifecycle);
        if (session) fault(*session, "The colony solver thread could not take a frame");
    }
}

namespace {
void run_frame(std::uint64_t ticket, double seconds, const char* refusal, std::uint64_t thread) noexcept {
    std::lock_guard lock(lifecycle);
    if (!session || session->status.state != 1) return;
    auto& s = *session;
    const char* doing = "frame input"; // named in a fault
    try {
        const ArithmeticScope arithmetic;
        const auto started = std::chrono::steady_clock::now();
        if (refusal) throw std::runtime_error(refusal);
        if (!ticket || ticket <= s.status.native_ticket || !std::isfinite(seconds) || seconds <= 0 || seconds > 10)
            throw std::runtime_error("Native frame ticket or timestep is invalid");
        std::vector<MAOniColonyTransfer> transfers; std::vector<MAOniColonyHeat> heat; std::vector<MAOniColonyTerrain> terrain;
        std::vector<Freeze> freezes;
        { std::lock_guard q(inputs); transfers.swap(pending_transfers); heat.swap(pending_heat); terrain.swap(pending_terrain); freezes.swap(pending_freezes); }
        auto world = *s.world;
        const auto& cat = *s.catalogue; const auto& registry = cat.registry;
        auto& L = s.ledger;
        auto mark = std::chrono::steady_clock::now(); double stage[6]{};
        const auto lap = [&](int k) { const auto now = std::chrono::steady_clock::now(); stage[k] = std::chrono::duration<double, std::milli>(now - mark).count(); mark = now; };
        // Frozen cells: the liquid native turned solid leaves, then the cell closes.
        doing = "freezing";
        if (!freezes.empty()) {
            std::vector<MacFluidTransfer> taken; const auto& st = world.state();
            for (const auto& f : freezes) {
                const auto i = rect_index(s.config, f.public_cell);
                if (st.grid.walls[i] || !cat.has_native(f.native_index)) continue;
                // Native froze the cell's whole projected liquid (binary32 of the
                // owner's mass): all of it leaves, so no rounding residue remains.
                const auto k = cat.species_of_native(f.native_index);
                const auto& held = st.cells[i].mixture;
                const double mass = held.masses[k];
                // Traces beside the liquid would keep only rounding residue of
                // the cell's energy: they leave with it, the liquid last, so the
                // last removal takes exactly what is stored. A real headspace
                // stays and is displaced when the cell closes.
                if (held.mass() - mass <= 1e-6 * held.mass())
                    for (std::size_t q = 0; q < held.masses.size(); ++q)
                        if (q != k && held.masses[q] > 0 && taken.size() < 4096) taken.push_back({i, q, false, held.masses[q], 0});
                if (mass > 0 && taken.size() < 4096) taken.push_back({i, k, false, mass, 0});
                terrain.insert(terrain.begin(), MAOniColonyTerrain{f.public_cell, MA_ONI_COLONY_CLOSE});
            }
            if (!taken.empty()) {
                auto result = world.with_fluid_transfers(taken, MacFluidTransferTopology::occupied_mixture);
                world = std::move(result.world);
                for (const auto& parcel : result.parcels) {
                    L.extracted[parcel.species_index] += parcel.mass_kg; L.energy_out += parcel_energy(parcel); s.status.extracted_kg += parcel.mass_kg;
                }
            }
        }
        doing = "terrain";
        if (!terrain.empty()) world = apply_terrain(s, std::move(world), terrain);
        lap(0); doing = "heat";
        // Heat edits.
        for (std::size_t first = 0; first < heat.size(); first += 4096) {
            std::vector<MacCellHeat> edits;
            for (std::size_t k = first; k < std::min(heat.size(), first + 4096); ++k) {
                const auto i = rect_index(s.config, heat[k].public_cell);
                if (world.state().grid.walls[i] || !(world.state().cells[i].mixture.mass() > 0) || heat[k].joules == 0) continue;
                edits.push_back({i, heat[k].joules, heat[k].maximum_temperature_k > 0 ? heat[k].maximum_temperature_k : 1e5});
                L.heat += heat[k].joules;
            }
            if (!edits.empty()) world = world.with_cell_heat(edits);
            s.status.committed_heat += edits.size();
        }
        lap(1); doing = "reservoir";
        // Reservoir release: liquid booked at import (it did not fit its
        // sealed pocket) or from a sealed closing seeps back once there is room
        // near its anchor, at most reservoir_release_kg_per_s, at its own
        // temperature. The anchor's open cells within three take it, the cell
        // with the least liquid first.
        if (!s.reservoir.empty()) {
            const auto& st = world.state(); std::vector<MacFluidTransfer> released; std::vector<std::size_t> from;
            const auto& eos_materials = cat.eos->reference().materials();
            for (std::size_t e = 0; e < s.reservoir.size() && released.size() < 4096; ++e) {
                auto& entry = s.reservoir[e];
                if (!(entry.mass_kg > 0)) continue;
                const auto order = nearby(st, entry.region_cell, 3);
                std::size_t best = SIZE_MAX; double best_volume = 2;
                for (const auto i : order) {
                    if (s.space[i]) continue;
                    double liquid = 0; const auto& m = st.cells[i].mixture;
                    for (std::size_t k = 0; k < m.masses.size(); ++k) if (!cat.materials[k].game.gas && m.masses[k] > 0)
                        liquid += m.masses[k] / eos_materials[k].zero_pressure_density_kg_per_m3;
                    if (liquid < best_volume) { best_volume = liquid; best = i; }
                }
                const bool gas = cat.materials[entry.species].game.gas;
                if (best == SIZE_MAX || (!gas && best_volume > .9)) continue;
                double mass = std::min(entry.mass_kg, reservoir_release_kg_per_s * seconds);
                if (!gas) mass = std::min(mass, (.95 - best_volume) * eos_materials[entry.species].zero_pressure_density_kg_per_m3);
                if (!(mass > 0)) continue;
                released.push_back({best, entry.species, true, mass, entry.temperature_k}); from.push_back(e);
            }
            if (!released.empty()) {
                auto result = world.with_fluid_transfers(released, MacFluidTransferTopology::occupied_mixture);
                world = std::move(result.world);
                for (std::size_t q = 0; q < result.parcels.size(); ++q) {
                    const auto& parcel = result.parcels[q];
                    L.injected[parcel.species_index] += parcel.mass_kg; L.energy_in += parcel_energy(parcel);
                    s.reservoir[from[q]].mass_kg -= parcel.mass_kg; s.status.reservoir_kg -= parcel.mass_kg;
                    s.status.released_kg += parcel.mass_kg;
                }
                std::erase_if(s.reservoir, [](const ReservoirEntry& r) { return !(r.mass_kg > 1e-9); });
            }
        }
        doing = "transfers";
        // Transfers: plan against the current state, then commit in batches.
        std::vector<MacFluidTransfer> edits; struct Pending { std::size_t first, count; const MAOniColonyTransfer* source; };
        std::vector<Pending> plans;
        for (const auto& t : transfers) {
            const auto centre = rect_index(s.config, t.public_cell);
            const auto& st = world.state();
            const auto kind = t.kind & ~MA_ONI_COLONY_CONSUMER;
            if (kind == MA_ONI_COLONY_EXTRACT_GAS || kind == MA_ONI_COLONY_EXTRACT_LIQUID) {
                // A phase's mixture (a pump or an any-gas consumer): the cells'
                // totals of that phase, nearest first, each cell's share split
                // over its constituents by mass.
                const bool gas = kind == MA_ONI_COLONY_EXTRACT_GAS;
                const auto order = nearby(st, centre, t.radius);
                std::vector<double> available(st.cells.size());
                for (const auto i : order) for (std::size_t k = 0; k < cat.materials.size(); ++k)
                    if (cat.materials[k].game.gas == gas) available[i] += st.cells[i].mixture.masses[k];
                const auto plan = radial::allocate_order(available, order, static_cast<double>(static_cast<float>(t.mass_kg)));
                const auto first = edits.size();
                for (const auto& a : plan.allocations) {
                    const auto& m = st.cells[a.cell].mixture;
                    for (std::size_t k = 0; k < cat.materials.size(); ++k)
                        if (cat.materials[k].game.gas == gas && m.masses[k] > 0) {
                            // All of a cell's phase leaves together, traces included.
                            const double share = a.mass >= available[a.cell] ? m.masses[k] : a.mass * (m.masses[k] / available[a.cell]);
                            if (share > 0) edits.push_back({a.cell, k, false, share, 0});
                        }
                }
                plans.push_back({first, edits.size() - first, &t});
                continue;
            }
            if (!cat.has_native(t.native_index)) { plans.push_back({edits.size(), 0, &t}); continue; }
            const auto k = cat.species_of_native(t.native_index);
            if (kind == MA_ONI_COLONY_INJECT) {
                const auto order = nearby(st, centre, 2);
                if (order.empty() || !(t.mass_kg > 0) || !(t.temperature_k > 0)) { plans.push_back({edits.size(), 0, &t}); continue; }
                plans.push_back({edits.size(), 1, &t}); edits.push_back({order.front(), k, true, t.mass_kg, t.temperature_k});
            } else {
                const auto order = nearby(st, centre, t.radius);
                std::vector<double> available(st.cells.size());
                for (const auto i : order) available[i] = st.cells[i].mixture.masses[k];
                const auto plan = radial::allocate_order(available, order, static_cast<double>(static_cast<float>(t.mass_kg)));
                plans.push_back({edits.size(), plan.allocations.size(), &t});
                for (const auto& a : plan.allocations) edits.push_back({a.cell, k, false, a.mass, 0});
            }
        }
        std::vector<MacFluidParcel> parcels; parcels.reserve(edits.size());
        for (std::size_t first = 0; first < edits.size(); first += 4096) {
            const std::span<const MacFluidTransfer> batch(edits.data() + first, std::min<std::size_t>(4096, edits.size() - first));
            auto result = world.with_fluid_transfers(batch, MacFluidTransferTopology::occupied_mixture);
            world = std::move(result.world);
            parcels.insert(parcels.end(), result.parcels.begin(), result.parcels.end());
        }
        if (!plans.empty()) {
            std::lock_guard q(inputs);
            const auto receipt = [&](const MAOniColonyTransfer& t, std::uint32_t native_index, long double mass, long double heat_capacity_t) {
                MAOniColonyReceipt r{}; r.sequence = ++receipt_sequence; r.completed_frame = s.status.completed_frames + 1;
                r.callback = t.callback; r.kind = t.kind; r.native_index = native_index; r.public_cell = t.public_cell;
                r.mass_kg = static_cast<float>(mass); r.temperature_k = mass > 0 ? static_cast<float>(heat_capacity_t / mass) : 0;
                receipts.push_back(r);
            };
            for (const auto& p : plans) {
                std::map<std::size_t, std::pair<long double, long double>> by_species; // mass, sum m T
                for (std::size_t e = p.first; e < p.first + p.count; ++e) {
                    const auto& parcel = parcels[e];
                    auto& entry = by_species[parcel.species_index];
                    entry.first += parcel.mass_kg; entry.second += parcel.mass_kg * parcel.temperature_k;
                    (parcel.injected ? L.injected : L.extracted)[parcel.species_index] += parcel.mass_kg;
                    (parcel.injected ? L.energy_in : L.energy_out) += parcel_energy(parcel);
                    (parcel.injected ? s.status.injected_kg : s.status.extracted_kg) += parcel.mass_kg;
                }
                ++s.status.committed_transfers;
                if (p.source->callback < 0) continue;
                const auto kind = p.source->kind & ~MA_ONI_COLONY_CONSUMER;
                const bool phase = kind == MA_ONI_COLONY_EXTRACT_GAS || kind == MA_ONI_COLONY_EXTRACT_LIQUID;
                if (!phase) {
                    long double mass = 0, heat_capacity_t = 0;
                    for (const auto& [k, entry] : by_species) { mass += entry.first; heat_capacity_t += entry.second; }
                    receipt(*p.source, p.source->native_index, mass, heat_capacity_t);
                } else if (by_species.empty()) receipt(*p.source, geometry.vacuum_index, 0, 0);
                // A mixture leaves one receipt per constituent.
                else for (const auto& [k, entry] : by_species) receipt(*p.source, cat.materials[k].game.native_index, entry.first, entry.second);
            }
        }
        lap(2); doing = "flow";
        // Flow. A debugging request (MA_COLONY_FAULT_DUMP=path) writes the
        // world before a failing flow step and the material records there.
        // MA_COLONY_DUMP_FRAME=n also writes the world before frame n's flow.
        const auto dump = [&](const char* path) {
            if (std::FILE* f = path ? std::fopen(path, "wb") : nullptr) {
                const auto image = world.checkpoint(); const auto count = static_cast<std::uint32_t>(s.materials.size());
                std::fwrite(&count, 4, 1, f); std::fwrite(s.materials.data(), sizeof(MAOniColonyMaterial), count, f);
                std::fwrite(&seconds, 8, 1, f); std::fwrite(image.data(), 1, image.size(), f); std::fclose(f);
            }
        };
        if (const char* frame = std::getenv("MA_COLONY_DUMP_FRAME"); frame && s.status.completed_frames + 1 == std::strtoull(frame, nullptr, 10))
            dump(std::getenv("MA_COLONY_FAULT_DUMP"));
        std::optional<MacClosedAdvance> flowed;
        try { flowed.emplace(world.advanced(seconds, 65536, MacExecutionPolicy::production)); }
        catch (...) { dump(std::getenv("MA_COLONY_FAULT_DUMP")); throw; }
        auto advanced = std::move(*flowed);
        world = std::move(advanced.world);
        // Phase change (the production kernel has none): each cell holding a
        // liquid/vapour pair relaxes toward saturation at fixed U and volume.
        doing = "phase change";
        if (!s.pairs.empty()) {
            const auto before = totals(world);
            try {
                // First-order relaxation with a 1 s time constant: a cell moves
                // 1 - exp(-dt / 1 s) of the way to equilibrium per frame, so a
                // superheated pool boils over seconds instead of flashing.
                auto relaxed = world.with_phase_relaxation(s.pairs, 1 - std::exp(-seconds / phase_relaxation_time_s));
                if (relaxed.report.evaporated_kg + relaxed.report.condensed_kg > 0) {
                    const auto after = totals(relaxed.world);
                    for (std::size_t k = 0; k < after.size(); ++k) L.converted[k] += after[k] - before[k];
                    s.status.evaporated_kg += relaxed.report.evaporated_kg; s.status.condensed_kg += relaxed.report.condensed_kg;
                }
                world = std::move(relaxed.world);
            } catch (const std::domain_error&) { ++s.status.skipped_relaxations; }
        }
        doing = "settling";
        try {
            auto settled = world.with_liquid_settling(std::min(.25, 1 - std::exp(-seconds / settling_time_s)));
            s.status.settled_kg += settled.report.moved_kg; world = std::move(settled.world);
        } catch (const std::domain_error&) { ++s.status.skipped_relaxations; }
        doing = "conversions";
        world = convert_identities(s, std::move(world), seconds);
        lap(3); doing = "space sink";
        // Open sky: whatever reaches a space cell leaves through the sink.
        std::vector<MacFluidTransfer> sink;
        for (std::size_t i = 0; i < s.space.size(); ++i) if (s.space[i]) {
            const auto& m = world.state().cells[i].mixture;
            for (std::size_t k = 0; k < m.masses.size(); ++k) if (m.masses[k] > 0) sink.push_back({i, k, false, m.masses[k], 0});
        }
        for (std::size_t first = 0; first < sink.size(); first += 4096) {
            const std::span<const MacFluidTransfer> batch(sink.data() + first, std::min<std::size_t>(4096, sink.size() - first));
            auto result = world.with_fluid_transfers(batch, MacFluidTransferTopology::occupied_mixture);
            world = std::move(result.world);
            for (const auto& parcel : result.parcels) { L.sunk[parcel.species_index] += parcel.mass_kg; L.energy_sunk += parcel_energy(parcel); s.status.space_sink_kg += parcel.mass_kg; }
        }
        lap(4); doing = "ledger";
        // Ledgers close: every species and U+K+G against the booked flows.
        const auto now = totals(world);
        double mass_error = 0;
        for (std::size_t k = 0; k < now.size(); ++k) {
            const long double expected = L.initial[k] + L.injected[k] - L.extracted[k] - L.sunk[k] + L.converted[k];
            const long double scale = L.initial[k] + L.injected[k] + L.extracted[k] + L.sunk[k] + std::abs(L.converted[k]);
            if (scale > 0) mass_error = std::max(mass_error, static_cast<double>(std::abs(now[k] - expected) / scale));
            else if (now[k] != 0) throw std::runtime_error("The colony created an unbooked constituent");
        }
        const long double expected_energy = L.initial_energy + L.heat + L.energy_in - L.energy_out - L.energy_sunk;
        const long double energy_scale = std::abs(L.initial_energy) + std::abs(L.heat) + std::abs(L.energy_in) + std::abs(L.energy_out) + std::abs(L.energy_sunk);
        const double energy_error = static_cast<double>(std::abs(energy_of(world) - expected_energy) / energy_scale);
        s.status.maximum_relative_mass_error = std::max(s.status.maximum_relative_mass_error, mass_error);
        s.status.maximum_relative_energy_error = std::max(s.status.maximum_relative_energy_error, energy_error);
        if (mass_error > 1e-8 || energy_error > 1e-6) throw std::runtime_error("The colony exceeded its mass/energy ledger bound");
        (void)registry;
        s.world = std::make_shared<const MacClosedWorld>(std::move(world));
        ++s.status.completed_frames; s.status.native_ticket = ticket; s.status.solver_thread = thread;
        s.status.substeps += advanced.report.substeps; s.status.rejected_attempts += advanced.report.rejected_attempts;
        s.status.time_seconds = s.world->time_s(); s.status.last_frame_seconds = seconds;
        s.status.last_advance_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        s.status.maximum_advance_ms = std::max(s.status.maximum_advance_ms, s.status.last_advance_ms);
        auto views = project(s, *s.world);
        lap(5); std::copy(std::begin(stage), std::end(stage), s.status.stage_ms);
        publish(s, std::move(views));
    } catch (const std::exception& error) { fault(s, (std::string(doing) + ": " + error.what()).c_str()); }
      catch (...) { fault(s, "Unknown colony advance failure"); }
}
}

// MACOLN01 checkpoint: header, configuration and material records (exact
// bytes), reservoir, ledger, the MACCLS01 physical image and a SHA-256.
namespace {
struct Writer {
    std::vector<std::uint8_t> bytes;
    void raw(const void* p, std::size_t n) { const auto* b = static_cast<const std::uint8_t*>(p); bytes.insert(bytes.end(), b, b + n); }
    void u32(std::uint32_t v) { raw(&v, 4); }
    void u64(std::uint64_t v) { raw(&v, 8); }
    void f64(double v) { raw(&v, 8); }
    void ld(long double v) { f64(static_cast<double>(v)); f64(static_cast<double>(v - static_cast<long double>(static_cast<double>(v)))); }
};
struct Reader {
    std::span<const std::uint8_t> bytes; std::size_t at = 0;
    void raw(void* p, std::size_t n) { require(n <= bytes.size() - at, "Truncated colony checkpoint"); std::memcpy(p, bytes.data() + at, n); at += n; }
    std::uint32_t u32() { std::uint32_t v; raw(&v, 4); return v; }
    std::uint64_t u64() { std::uint64_t v; raw(&v, 8); return v; }
    double f64() { double v; raw(&v, 8); return v; }
    long double ld() { const double hi = f64(), lo = f64(); return static_cast<long double>(hi) + lo; }
};
constexpr char magic[8] = {'M', 'A', 'C', 'O', 'L', 'N', '0', '1'};
// Version 5 adds the inputs queued at the save; version 4 the phase-change
// conversion ledger; version 3 stored the continuing counters explicitly
// (version 2 the raw status struct).
constexpr std::uint32_t checkpoint_version = 5;
}
std::vector<std::uint8_t> checkpoint() {
    solver.settle(false);
    std::lock_guard lock(lifecycle);
    require(session != nullptr, "No colony owner is active");
    require(session->status.state == 1, "A faulted colony owner cannot be saved");
    const ArithmeticScope arithmetic;
    const auto& s = *session; Writer w;
    w.raw(magic, 8); w.u32(checkpoint_version); w.raw(&s.config, sizeof(s.config));
    w.u32(static_cast<std::uint32_t>(s.materials.size())); w.raw(s.materials.data(), s.materials.size() * sizeof(MAOniColonyMaterial));
    w.u32(static_cast<std::uint32_t>(s.space.size())); w.raw(s.space.data(), s.space.size());
    w.u32(static_cast<std::uint32_t>(s.reservoir.size()));
    for (const auto& r : s.reservoir) { w.u64(r.region_cell); w.u64(r.species); w.f64(r.mass_kg); w.f64(r.temperature_k); }
    const auto& L = s.ledger;
    for (const auto* v : {&L.initial, &L.injected, &L.extracted, &L.sunk, &L.converted}) for (const auto x : *v) w.ld(x);
    for (const auto x : {L.initial_energy, L.heat, L.energy_in, L.energy_out, L.energy_sunk}) w.ld(x);
    // Counters that continue across a save; the status layout itself is not stored.
    for (const auto v : {s.status.completed_frames, s.status.substeps, s.status.rejected_attempts, s.status.committed_transfers,
                         s.status.committed_heat, s.status.opened_cells, s.status.closed_cells}) w.u64(v);
    for (const auto v : {s.status.maximum_relative_mass_error, s.status.maximum_relative_energy_error, s.status.space_sink_kg,
                         s.status.injected_kg, s.status.extracted_kg, s.status.evaporated_kg, s.status.condensed_kg}) w.f64(v);
    // Inputs queued for the next step are in flight at the save. Callbacks do
    // not survive a load: an extraction answering one (a breath, a consumer)
    // is dropped and asked again after the load; an injection (exhaled gas, an
    // emitter's output) and native-side changes without callbacks are kept.
    std::vector<MAOniColonyTransfer> transfers; std::vector<MAOniColonyHeat> heat; std::vector<MAOniColonyTerrain> terrain; std::vector<Freeze> freezes;
    {
        std::lock_guard q(inputs);
        for (auto t : pending_transfers) {
            const bool answered = t.callback >= 0 || (t.kind & MA_ONI_COLONY_CONSUMER) != 0;
            if (answered && (t.kind & ~MA_ONI_COLONY_CONSUMER) != MA_ONI_COLONY_INJECT) continue;
            t.callback = -1; t.kind &= ~MA_ONI_COLONY_CONSUMER; transfers.push_back(t);
        }
        heat = pending_heat; terrain = pending_terrain; freezes = pending_freezes;
    }
    w.u32(static_cast<std::uint32_t>(transfers.size())); w.raw(transfers.data(), transfers.size() * sizeof(MAOniColonyTransfer));
    w.u32(static_cast<std::uint32_t>(heat.size())); w.raw(heat.data(), heat.size() * sizeof(MAOniColonyHeat));
    w.u32(static_cast<std::uint32_t>(terrain.size())); w.raw(terrain.data(), terrain.size() * sizeof(MAOniColonyTerrain));
    w.u32(static_cast<std::uint32_t>(freezes.size()));
    for (const auto& f : freezes) { w.u32(f.public_cell); w.u32(f.native_index); w.f64(f.mass_kg); }
    const auto image = s.world->checkpoint();
    w.u64(image.size()); w.raw(image.data(), image.size());
    const auto digest = hash_memory(w.bytes.data(), w.bytes.size());
    w.raw(digest.data(), digest.size());
    return std::move(w.bytes);
}
void restore(const MAOniColonyConfig& config, std::span<const MAOniColonyMaterial> materials, std::span<const std::uint8_t> image, std::uint64_t thread) {
    std::lock_guard lock(lifecycle);
    const ArithmeticScope arithmetic;
    validate(config);
    require(image.size() > 8 + 64, "Colony checkpoint is too small");
    const auto digest = hash_memory(image.data(), image.size() - 64);
    require(std::memcmp(digest.data(), image.data() + image.size() - 64, 64) == 0, "Colony checkpoint checksum differs");
    Reader r{image.first(image.size() - 64)};
    char head[8]; r.raw(head, 8); require(std::memcmp(head, magic, 8) == 0 && r.u32() == checkpoint_version, "Unsupported colony checkpoint");
    MAOniColonyConfig saved{}; r.raw(&saved, sizeof(saved));
    require(std::memcmp(&saved, &config, sizeof(config)) == 0, "Colony checkpoint geometry or physics differs from the loaded game");
    const auto count = r.u32(); require(count == materials.size(), "Colony checkpoint catalogue differs from the running game");
    std::vector<MAOniColonyMaterial> stored(count); r.raw(stored.data(), count * sizeof(MAOniColonyMaterial));
    require(std::memcmp(stored.data(), materials.data(), count * sizeof(MAOniColonyMaterial)) == 0, "Colony checkpoint catalogue differs from the running game");
    auto next = std::make_unique<Session>();
    next->config = config; next->materials = std::move(stored); next->catalogue = catalogue_of(next->materials);
    const auto cells = r.u32(); require(cells == static_cast<std::size_t>(config.width) * config.height, "Colony checkpoint space mask differs");
    next->space.resize(cells); r.raw(next->space.data(), cells);
    const auto entries = r.u32();
    for (std::uint32_t k = 0; k < entries; ++k) {
        ReservoirEntry e; e.region_cell = r.u64(); e.species = r.u64(); e.mass_kg = r.f64(); e.temperature_k = r.f64();
        require(e.species < next->catalogue->registry.size(), "Colony checkpoint reservoir names an unknown species"); next->reservoir.push_back(e);
    }
    const auto K = next->catalogue->registry.size(); auto& L = next->ledger;
    for (auto* v : {&L.initial, &L.injected, &L.extracted, &L.sunk, &L.converted}) { v->resize(K); for (auto& x : *v) x = r.ld(); }
    next->pairs = with_critical(*next->catalogue, mac_saturation_pairs(next->catalogue->registry, *next->catalogue->eos));
    for (auto* x : {&L.initial_energy, &L.heat, &L.energy_in, &L.energy_out, &L.energy_sunk}) *x = r.ld();
    MAOniColonyStatus saved_status{};
    for (auto* v : {&saved_status.completed_frames, &saved_status.substeps, &saved_status.rejected_attempts, &saved_status.committed_transfers,
                    &saved_status.committed_heat, &saved_status.opened_cells, &saved_status.closed_cells}) *v = r.u64();
    for (auto* v : {&saved_status.maximum_relative_mass_error, &saved_status.maximum_relative_energy_error, &saved_status.space_sink_kg,
                    &saved_status.injected_kg, &saved_status.extracted_kg, &saved_status.evaporated_kg, &saved_status.condensed_kg}) *v = r.f64();
    const auto read_array = [&](auto& out) {
        const auto n = r.u32(); require(n <= 1u << 20, "Colony checkpoint holds too many queued inputs");
        out.resize(n); r.raw(out.data(), n * sizeof(out[0]));
    };
    std::vector<MAOniColonyTransfer> transfers; std::vector<MAOniColonyHeat> heat; std::vector<MAOniColonyTerrain> terrain; std::vector<Freeze> freezes;
    read_array(transfers); read_array(heat); read_array(terrain);
    freezes.resize(r.u32()); require(freezes.size() <= 1u << 20, "Colony checkpoint holds too many queued inputs");
    for (auto& f : freezes) { f.public_cell = r.u32(); f.native_index = r.u32(); f.mass_kg = r.f64(); }
    const auto physical = r.u64(); require(physical == r.bytes.size() - r.at, "Colony checkpoint physical image length differs");
    next->world = std::make_shared<const MacClosedWorld>(MacClosedWorld::from_checkpoint(next->catalogue->registry, next->catalogue->eos,
        r.bytes.subspan(r.at, static_cast<std::size_t>(physical))));
    activate(std::move(next), thread);
    // Ledger bounds and counters continue from the saved session.
    auto& st = session->status;
    st.completed_frames = saved_status.completed_frames; st.substeps = saved_status.substeps; st.rejected_attempts = saved_status.rejected_attempts;
    st.committed_transfers = saved_status.committed_transfers; st.committed_heat = saved_status.committed_heat;
    st.maximum_relative_mass_error = saved_status.maximum_relative_mass_error; st.maximum_relative_energy_error = saved_status.maximum_relative_energy_error;
    st.space_sink_kg = saved_status.space_sink_kg; st.injected_kg = saved_status.injected_kg; st.extracted_kg = saved_status.extracted_kg;
    st.opened_cells = saved_status.opened_cells; st.closed_cells = saved_status.closed_cells;
    st.evaporated_kg = saved_status.evaporated_kg; st.condensed_kg = saved_status.condensed_kg;
    {
        std::lock_guard q(inputs);
        pending_transfers = std::move(transfers); pending_heat = std::move(heat); pending_terrain = std::move(terrain); pending_freezes = std::move(freezes);
    }
    publish(*session);
}
}
