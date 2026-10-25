#include "oni_colony.hpp"
#include "mac_kernel_detail.hpp"
#include "mac_phase_flow.hpp"
#include "oni_catalogue.hpp"
#include "oni_colony_import.hpp"
#include "oni_radial_consumption.hpp"
#include "oni_precise_receipt.hpp"
#include "oni_parcel_admission.hpp"
#include "oni_release_capture.hpp"
#include "oni_transfer_capture.hpp"
#include "oni_world_biology.hpp"
#include "oni_sha256.hpp"
#include "mixed_atmospheres/mac_closed_world.hpp"
#include "mixed_atmospheres/mac_kernel.hpp"
#include "mixed_atmospheres/mac_primary.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cfenv>
#include <chrono>
#if defined(__linux__)
#include <sched.h>
#endif
#include <cmath>
#include <condition_variable>
#include <cstddef>
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
#include <unordered_map>
#include <xmmintrin.h>

static_assert(sizeof(MAOniColonySpecies) == 40 && sizeof(MAOniColonyReading) == 64 && sizeof(MAOniColonyConstituent) == 24);
static_assert(sizeof(MAOniColonyPassive)==16);
static_assert(sizeof(MAOniColonyActorJob)==24 && offsetof(MAOniColonyActorJob,token)==16);
static_assert(sizeof(MAOniColonyParcelContext)==72 && offsetof(MAOniColonyParcelContext,session)==16 &&
    offsetof(MAOniColonyParcelContext,cell_width_m)==48);
static_assert(sizeof(MAOniColonySampleCell)==24 && sizeof(MAOniColonySamplePart)==16);
static_assert(sizeof(MAOniColonyMaterial) == 168 && sizeof(MAOniColonyConfig) == 56 && sizeof(MAOniColonyCell) == 24 &&
    sizeof(MAOniColonyStatus) == 600 && sizeof(MAOniColonyCellView) == 32 && sizeof(MAOniColonyTransfer) == 48 &&
    sizeof(MAOniColonyReceipt) == 48 && sizeof(MAOniColonyHeat) == 24, "Colony ABI layout changed");

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
bool stage_logging_enabled() noexcept {
    static const bool enabled=[] {
        const auto* value=std::getenv("MA_COLONY_STAGE_LOG");
        return value && std::strcmp(value,"1")==0;
    }();
    return enabled;
}

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
    MAOniColonyConfig config{};
    double booked_mass_kg=0, booked_heat_j=0;
    MAOniColonyStatus status{};
    std::vector<MAOniColonyCellView> views;
    // The presentation's readings, in cell order, made with the views.
    std::vector<MAOniColonyReading> readings;
    std::vector<MAOniColonyConstituent> parts;
    std::shared_ptr<const MacClosedWorld> world;
    std::shared_ptr<const Catalogue> catalogue;
    // World germ accounts at this frame (docs/colony-owner.md#germs).
    MAOniColonyBiology biology{};
    std::vector<MAOniColonyBiologyAccount> germ_accounts;
};
// Conserved-quantity ledger: initial inventory plus every booked flow.
struct Ledger {
    std::vector<long double> initial, injected, extracted, sunk;
    std::vector<long double> converted; // phase change: + to the product species
    // Gross phase change per species (evaporated plus condensed), the scale of
    // a species whose only flow is phase change. Not saved: a restored owner
    // starts it at |converted|.
    std::vector<long double> converted_gross;
    long double initial_energy = 0, heat = 0, energy_in = 0, energy_out = 0, energy_sunk = 0;
    // The saved game profile's trace sink (MacTraceSinkReport): removed mass
    // per species, U+K+G and canonical momentum, booked apart from the space sink.
    std::vector<long double> trace;
    long double energy_trace = 0, trace_momentum_x = 0, trace_momentum_y = 0;
};
struct Session {
    MAOniColonyConfig config{};
    std::vector<MAOniColonyMaterial> materials;
    std::shared_ptr<const Catalogue> catalogue;
    std::vector<std::uint8_t> space; // per owned cell
    std::vector<std::uint8_t> blocking; // per owned cell: passes gas, not liquid (airflow tile)
    std::vector<ReservoirEntry> reservoir;
    std::vector<MacSaturationPair> pairs; // liquid/vapour pairs that boil and condense
    std::shared_ptr<const MacClosedWorld> world;
    Ledger ledger;
    MAOniColonyStatus status{};
    // World germ evolution (docs/colony-owner.md#germs), saved from owner
    // version 10: 0 legacy (native code evolves the projected germ on the
    // shadow), 1 the owner evolves every identity.
    std::uint32_t germ_mode = 0;
    biology::Clocks clocks;
    biology::Accounts accounts;
    std::uint64_t biology_frames = 0;
    std::map<std::uint64_t, double> germs_held; // per identity after the last biology stage
    std::uint64_t germ_cells = 0;
    double biology_error = 0;
    detail::ReleaseCapture release_capture; // diagnostics only, never serialized
};

std::mutex lifecycle; // start, restore, stop, advance and checkpoint
std::mutex inputs;    // queued transfers and heat, receipts
std::mutex publication;
std::atomic<bool> enabled{};
std::unique_ptr<Session> session;
std::shared_ptr<const Published> latest;
std::uint32_t retained_sample_frames=0;
std::map<std::uint64_t,std::shared_ptr<const Published>> sample_frames; // publication lock; diagnostics only
// Geometry and ownership are published before enabled and unchanged while it is set.
MAOniColonyConfig geometry{};
// Per cell: 0 closed (native), 1 owned, 2 closing (a solid replaced the
// owned fluid and the owner closes the cell at its next step; the shadow and
// native suppression already leave it to ONI), 3 opening (a queued open of a
// closed cell; ONI keeps it until the owner opens it).
std::unique_ptr<std::atomic<std::uint8_t>[]> owned;
std::size_t owned_count = 0;
std::array<std::atomic<std::uint64_t>, MA_ONI_HOOK_COUNT> suppressed{};
std::vector<MAOniColonyTransfer> pending_transfers;
std::vector<MAOniColonyHeat> pending_heat;
std::vector<MAOniColonyTerrain> pending_terrain;
std::vector<MAOniColonyReceipt> receipts;
bool precise_receipts_enabled = false; // adapter opt-in; never part of physical state
std::map<std::uint64_t, std::vector<std::uint8_t>> precise_receipts;
struct PipeJob { MAOniColonyTransfer transfer; PassiveQuantities passives; std::uint64_t last_receipt=0; std::shared_ptr<const std::vector<std::uint8_t>> moving_wire{}; };
std::map<std::uint64_t,PipeJob> pipe_jobs; // queued or committed, until every outcome is acknowledged
std::map<std::size_t,std::uint64_t> pending_job_tokens; // indices in pending_transfers
std::map<std::uint64_t,std::uint64_t> receipt_jobs; // sequence -> durable job token
std::uint64_t accepted_job_token=0;
bool moving_protocol_used=false; // persisted only after explicit admission
std::atomic<std::uint64_t> live_parcel_session{0};
std::map<std::uint64_t,std::vector<std::uint8_t>> moving_outcomes;
std::uint64_t receipt_sequence = 0, acknowledged = 0, sessions = 0;
std::atomic<std::uint64_t> start_thread{};
// Native shadow: per owned cell, what write_shadow put there this frame.
struct ShadowCell { std::uint16_t element = 0, before = 0; bool written = false; float mass = 0, temperature = 0; std::uint32_t germ = 0; std::int32_t germs = 0; };
// Germ changes native code made on owned cells (growth and death, disease
// emitters and consumers, cell edits), applied at the start of the next step.
struct GermEdit { std::uint32_t public_cell, germ; double change; };
std::vector<GermEdit> pending_germs; // under inputs
// World germ evolution (docs/colony-owner.md#germs): the game's disease table
// (lifecycle lock), and the running session's mode for the native worker.
biology::Table disease_table;
std::atomic<std::uint32_t> biology_mode{0};
// Explicit control (MA_COLONY_GERM_ORACLE=1): a new colony keeps the legacy
// mode, and every sealed single-cell culture native code evolves on the shadow
// is compared with the owner's prediction from the same inputs.
bool germ_oracle_requested() noexcept {
    static const bool on = [] { const char* v = std::getenv("MA_COLONY_GERM_ORACLE"); return v && std::strcmp(v, "1") == 0; }();
    return on;
}
struct OracleCell { bool known = false; std::uint32_t germ = 0; float residue = 0; };
struct Oracle {
    std::mutex m;
    std::map<std::uint32_t, OracleCell> cells; // native worker
    std::uint64_t comparisons = 0, mismatches = 0, extinctions = 0;
    double born = 0, died = 0;
    std::string last;
} oracle;
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
void run_frame(std::uint64_t ticket, double seconds, std::uint64_t frames, const char* refusal, std::uint64_t thread) noexcept;
struct Solver {
    std::mutex m; std::condition_variable wake, idle;
    std::thread thread;
    bool quit = false, running = false;
    const char* refusal = nullptr; // a frame the owner must refuse
    std::uint64_t ticket = 0; double seconds = 0; // waiting work
    std::uint64_t frames = 0; // native frames in the waiting work (germs evolve once per frame)
    std::atomic<bool> busy{};
    void loop() {
        const auto id = static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
        std::unique_lock l(m);
        for (;;) {
            wake.wait(l, [&] { return quit || seconds > 0 || refusal; });
            if (quit) return;
            const auto t = ticket; const double dt = seconds; const auto r = refusal; const auto f = frames;
            seconds = 0; frames = 0; refusal = nullptr; running = true;
            l.unlock(); run_frame(t, dt, f, r, id); l.lock();
            running = false;
            if (seconds == 0) busy.store(false, std::memory_order_release);
            idle.notify_all();
        }
    }
    // Waits until no step runs or waits; `discard` drops waiting work first.
    void settle(bool discard) {
        std::unique_lock l(m);
        if (discard) { seconds = 0; frames = 0; refusal = nullptr; }
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
    require((c.flags & ~(MA_ONI_COLONY_PHYSICS_CONDUCTION | MA_ONI_COLONY_GAME_FLOOR | MA_ONI_COLONY_GAME_KERNEL)) == 0, "Unknown colony physics flag");
    require((c.flags&(MA_ONI_COLONY_GAME_FLOOR|MA_ONI_COLONY_GAME_KERNEL))!=(MA_ONI_COLONY_GAME_FLOOR|MA_ONI_COLONY_GAME_KERNEL),"Colony game profiles are exclusive");
    require(c.grid_width >= 3 && c.grid_height >= 3 && c.grid_width <= 65535 && c.grid_height <= 65535, "Invalid colony grid");
    require(c.width >= 2 && c.height >= 2 && c.width <= MA_ONI_COLONY_MAXIMUM_SIDE && c.height <= MA_ONI_COLONY_MAXIMUM_SIDE &&
        static_cast<std::uint64_t>(c.width) * c.height <= MA_ONI_COLONY_MAXIMUM_CELLS, "Colony rectangle is outside its size bounds");
    require(static_cast<std::uint64_t>(c.offset_x) + c.width <= c.grid_width && static_cast<std::uint64_t>(c.offset_y) + c.height <= c.grid_height,
        "Colony rectangle leaves the grid");
    require(std::isfinite(c.gravity_m_per_s2) && c.gravity_m_per_s2 >= 0 && c.gravity_m_per_s2 <= 100, "Invalid colony gravity");
    require(std::isfinite(c.maximum_timestep_s) && c.maximum_timestep_s > 0 && c.maximum_timestep_s <= static_cast<double>(.1f), "Invalid colony maximum timestep");
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
// The game-floor profile's per-constituent trace threshold (at most 1 mg by
// the user's decision). Smallest tested decade clearing the captured faults,
// including both liquid-floor starts; new-colony selection stays opt-in.
constexpr double game_floor_trace_kg = 1e-11;
MacClosedSettings settings_of(const MAOniColonyConfig& c) {
    if(c.flags&MA_ONI_COLONY_GAME_KERNEL) {
        require(!(c.flags&MA_ONI_COLONY_GAME_FLOOR),"Game kernels are exclusive");
        MacClosedSettings game;game.numerical_kernel=MacNumericalKernel::game;game.game_profile=MacGameProfile::game_kernel;
        game.vacuum=MacVacuumRepresentation::void_phase;game.thermal_conduction=(c.flags&MA_ONI_COLONY_PHYSICS_CONDUCTION)!=0;game.gravity_m_per_s2=c.gravity_m_per_s2;
        game.maximum_timestep_s=static_cast<double>(.2f);game.trace_sink_threshold_kg=game_floor_trace_kg;
        const auto selected=[](const char* name){const char* value=std::getenv(name);return value && std::strcmp(value,"1")==0;};
        const bool surface=selected("MA_COLONY_KERNEL3_SURFACE");
        if (surface || selected("MA_COLONY_KERNEL3_POSITIVE")) {
            // The surface selector creates the newest candidate, family 5.
            game.game_profile=surface?MacGameProfile::game_kernel_trace_heat:MacGameProfile::game_kernel_positive_liquid;
            game.game.liquid_law=MacGameLiquidLaw::positive_segments;
            game.game.pressure_maximum_iterations=400;
            game.game.heat_maximum_iterations=800;
        }
        // Profile 5 saves the adopted gas heat mixing (docs/base-game-floor.md, F1 split).
        if (surface) game.game.gas_heat_mixing=8;
        return game;
    }
    MacClosedSettings s; s.maximum_timestep_s = c.maximum_timestep_s; s.numerical_kernel = MacNumericalKernel::production;
    s.gravity_m_per_s2 = c.gravity_m_per_s2; s.thermal_conduction = (c.flags & MA_ONI_COLONY_PHYSICS_CONDUCTION) != 0;
    s.vacuum = MacVacuumRepresentation::void_phase; s.reconstruction = MacMaterialReconstruction::interface_plic;
    s.subcell_void = MacSubcellVoidPolicy::expand_unresolved_gas;
    s.gas_expansion_volume_fraction = .001;
    s.step_control = MacStepControl::regional_predictive;
    s.preconditioner = PressurePreconditioner::incomplete_cholesky;
    if (c.flags & MA_ONI_COLONY_GAME_FLOOR) {
        s.game_profile = MacGameProfile::base_game_floor; s.trace_sink_threshold_kg = game_floor_trace_kg;
        s.compatible_void_pressure_work = true; s.recover_mixed_eos_endpoint = true;
        const char* substep_trace = std::getenv("MA_COLONY_SUBSTEP_TRACE");
        s.trace_sink_per_substep = substep_trace && std::strcmp(substep_trace, "1") == 0;
        const char* fast_phase = std::getenv("MA_COLONY_FAST_PHASE_EOS");
        s.fast_phase_trial_eos = fast_phase && std::strcmp(fast_phase, "1") == 0;
    }
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
    // Walls and zero masses add nothing; skipping them leaves every sum unchanged.
    const auto& st = w.state();
    for (std::size_t i = 0; i < st.cells.size(); ++i) {
        if (st.grid.walls[i]) continue;
        const auto& masses = st.cells[i].mixture.masses;
        for (std::size_t k = 0; k < out.size(); ++k) if (const double m = masses[k]; m != 0) out[k] += m;
    }
    return out;
}
// A cell's reading. A trace far below binary64's normal range (a rounding
// product of the production kernel) has no representable volume in the
// reference EOS; the reading leaves such traces out, as their mass and energy
// are far below the resolution of the cell's.
experimental::ReferenceEquilibriumState evaluate_cell(const FluidThermodynamics& eos, const MixtureInventory& m, double volume) {
    try { return eos.evaluate(m, volume); }
    catch (const std::exception&) {
        auto kept = m; bool dropped = false;
        for (auto& x : kept.masses) if (x > 0 && x < 1e-300) { x = 0; dropped = true; }
        if (!dropped) throw;
        return eos.evaluate(kept, volume);
    }
}
// U+K+G for the ledger (checked to a millionth): U with its stored
// remainder, K from each face's inertia (half the mass of each open cell
// beside it), G at cell centres.
long double energy_of(const MacClosedWorld& w) {
    const auto& st = w.state(); const auto& grid = st.grid; long double value = 0;
    const double g = w.settings().gravity_m_per_s2;
    // Reuse each exact mass; retain the original energy summation order.
    std::vector<double> mass(st.cells.size());
    for (std::size_t i = 0; i < st.cells.size(); ++i) {
        if (grid.walls[i]) continue; // a wall holds no fluid and adds nothing
        const auto& cell = st.cells[i]; mass[i]=cell.mixture.mass();
        value += static_cast<long double>(cell.mixture.energy) + cell.energy_remainder_j.projected + mac_energy_long_double(cell.energy_remainder_j.full);
        value += static_cast<long double>(mass[i]) * g * ((static_cast<double>(i / grid.width) + .5) * grid.spacing_m);
    }
    const auto half = [&](std::size_t cell) { return cell < st.cells.size() && !grid.walls[cell] ? .5L * mass[cell] : 0.L; };
    const auto jx = st.full_momentum_x.empty() ? std::vector<MacScaled>{} : mac_field_values(st.momentum_x, st.full_momentum_x);
    const auto jy = st.full_momentum_y.empty() ? std::vector<MacScaled>{} : mac_field_values(st.momentum_y, st.full_momentum_y);
    for (std::size_t y = 0; y < grid.height; ++y) for (std::size_t x = 0; x < grid.x_width(); ++x) {
        const auto f = grid.x_face(x, y); const long double j = jx.empty() ? st.momentum_x[f] : jx[f].project().value;
        if (j == 0) continue;
        const long double inertia = (x > 0 || grid.periodic_x ? half((x + grid.width - 1) % grid.width + y * grid.width) : 0.L) + (x < grid.width ? half(x + y * grid.width) : 0.L);
        if (inertia > 0) value += .5L * j * j / inertia;
    }
    for (std::size_t y = 0; y < grid.y_height(); ++y) for (std::size_t x = 0; x < grid.width; ++x) {
        const auto f = grid.y_face(x, y); const long double j = jy.empty() ? st.momentum_y[f] : jy[f].project().value;
        if (j == 0) continue;
        const long double inertia = (y > 0 || grid.periodic_y ? half(x + ((y + grid.height - 1) % grid.height) * grid.width) : 0.L) + (y < grid.height ? half(x + y * grid.width) : 0.L);
        if (inertia > 0) value += .5L * j * j / inertia;
    }
    return value;
}
// The most numerous germ among passives (identity <= 256 is a disease index + 1),
// and its count as ONI holds it.
std::pair<std::uint32_t, std::int32_t> dominant_germ(std::span<const PassiveQuantity> passives) {
    std::uint32_t best = 0; double most = 0;
    for (const auto& q : passives) if (q.identity <= 256 && q.quantity > most) { most = q.quantity; best = static_cast<std::uint32_t>(q.identity); }
    if (best == 0 || !(most >= .5)) return {0, 0};
    return {best, static_cast<std::int32_t>(std::min(most, 2147483647.))};
}
long double parcel_energy(const MacFluidParcel& p) {
    return static_cast<long double>(p.internal_energy_j) + mac_energy_long_double(p.resolved_kinetic_energy) + mac_energy_long_double(p.potential_energy);
}
std::atomic<std::size_t> refused_reading{SIZE_MAX}; // control hook: a rect cell whose reading throws
struct Projection {
    std::vector<MAOniColonyCellView> views;
    std::vector<MAOniColonyReading> readings;
    std::vector<MAOniColonyConstituent> parts;
};
// The views ONI sees and the presentation's readings, from one reference EOS
// evaluation per cell on the solver thread; the game thread only copies them.
Projection project(const Session& s, const MacClosedWorld& w) {
    const auto& st = w.state(); const auto& registry = w.registry(); const auto& c = *s.catalogue;
    Projection out; out.views.resize(st.cells.size()); auto& views = out.views;
    constexpr std::size_t block_size = 1024;
    const auto blocks = (st.cells.size() + block_size - 1) / block_size;
    std::vector<std::vector<MAOniColonyReading>> block_readings(blocks);
    std::vector<std::vector<MAOniColonyConstituent>> block_parts(blocks);
    // A cell whose reading the reference EOS refuses keeps its previous view
    // for this frame (the owner's state is unchanged; only its reading waits).
    std::shared_ptr<const Published> previous; { std::lock_guard lock(publication); previous = latest; }
    const bool fallback = previous && previous->views.size() == views.size();
    const auto project_cell = [&](std::size_t i, std::size_t block) {
        auto& v = views[i];
        if (st.grid.walls[i]) return;
        v.flags = MA_ONI_COLONY_VIEW_OWNED; v.element_index = s.config.vacuum_index;
        const auto& m = st.cells[i].mixture;
        if (!(m.mass() > 0)) return;
        std::optional<experimental::ReferenceEquilibriumState> evaluated;
        try {
            if (i == refused_reading.load(std::memory_order_relaxed)) throw std::runtime_error("Reading refused (control)");
            evaluated.emplace(w.settings().numerical_kernel==MacNumericalKernel::game?w.cell_reading(i):evaluate_cell(w.thermodynamics(), m, mac_material_volume(st, i)));
        }
        catch (const std::exception&) { if (w.settings().numerical_kernel==MacNumericalKernel::game || !fallback) throw; v = previous->views[i]; return; }
        if (!std::isfinite(evaluated->temperature_k)) { if (!fallback) throw std::runtime_error("A cell reads a non-finite temperature"); v = previous->views[i]; return; }
        const auto& reading = *evaluated;
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
        // ONI's native heat code asserts on temperatures outside its range; a
        // trace's formal temperature can lie far beyond it (a sub-resolution
        // energy over a vanishing heat capacity). ONI sees the nearest bound.
        v.temperature_k = static_cast<float>(std::clamp(reading.temperature_k, 1.0, 9999.0));
        v.pressure_pa = static_cast<float>(reading.mechanical_pressure_pa);
        v.liquid_fraction = static_cast<float>(reading.liquid_volume_m3 / st.grid.volume_m3());
        const auto germ = dominant_germ(st.cells[i].passive_quantities);
        v.disease = germ.first; v.disease_count = germ.second;
        // A cell without a reading (refused above) keeps its vanilla presentation.
        auto& parts = block_parts[block];
        MAOniColonyReading r{};
        r.public_cell = static_cast<std::uint32_t>((geometry.offset_x + i % geometry.width) + (geometry.offset_y + i / geometry.width) * geometry.grid_width);
        r.first_constituent = static_cast<std::uint32_t>(parts.size());
        r.temperature_k = reading.temperature_k; r.mechanical_pressure_pa = reading.mechanical_pressure_pa; r.gas_pressure_pa = reading.gas_pressure_pa;
        r.internal_energy_j = m.energy; r.occupied_volume_m3 = mac_material_volume(st, i);
        for (std::size_t k = 0; k < m.masses.size(); ++k) if (m.masses[k] > 0) {
            parts.push_back({static_cast<std::uint32_t>(k), 0, m.masses[k], reading.partial_volumes_m3[k]});
            if (!c.materials[k].game.gas) r.liquid_volume_m3 += reading.partial_volumes_m3[k];
        }
        r.constituents = static_cast<std::uint32_t>(parts.size()) - r.first_constituent;
        block_readings[block].push_back(r);
    };
    const auto options = mac_kernel_default_options();
    const auto threads = static_cast<unsigned>(std::min<std::size_t>(options.threads,
        std::max<std::size_t>(1, blocks / options.minimum_blocks_per_thread)));
    mac_kernel::pool_for(threads).run(blocks, [&](std::size_t block) {
        for (std::size_t i = block * block_size; i < std::min(st.cells.size(), (block + 1) * block_size); ++i) project_cell(i, block);
    });
    std::size_t reading_count = 0, part_count = 0;
    for (std::size_t b = 0; b < blocks; ++b) { reading_count += block_readings[b].size(); part_count += block_parts[b].size(); }
    out.readings.reserve(reading_count); out.parts.reserve(part_count);
    for (std::size_t b = 0; b < blocks; ++b) {
        const auto offset = static_cast<std::uint32_t>(out.parts.size());
        for (auto r : block_readings[b]) { r.first_constituent += offset; out.readings.push_back(r); }
        out.parts.insert(out.parts.end(), block_parts[b].begin(), block_parts[b].end());
    }
    return out;
}
void publish(const Session& s, Projection projection) {
    auto p = std::make_shared<Published>();
    p->config=s.config; p->status = s.status; p->views = std::move(projection.views); p->readings = std::move(projection.readings); p->parts = std::move(projection.parts);
    p->world = s.world; p->catalogue = s.catalogue;
    auto& b = p->biology; b.struct_size = sizeof(b); b.abi_version = 1; b.mode = s.germ_mode;
    b.frames = s.biology_frames; b.cultures = s.clocks.size(); b.germ_cells = s.germ_cells; b.audit_error = s.biology_error;
    for (const auto& [key, clock] : s.clocks) if (clock.mode == biology::expected_mode) ++b.expected_cultures;
    for (const auto& [identity, a] : s.accounts) {
        const auto held = s.germs_held.find(identity);
        p->germ_accounts.push_back({identity, static_cast<double>(a.born), static_cast<double>(a.died), static_cast<double>(a.culled),
            static_cast<double>(a.moved), static_cast<double>(a.edited_in), static_cast<double>(a.edited_out), held == s.germs_held.end() ? 0 : held->second});
        b.born += static_cast<double>(a.born); b.died += static_cast<double>(a.died); b.culled += static_cast<double>(a.culled);
        b.moved += static_cast<double>(a.moved); b.edited_in += static_cast<double>(a.edited_in); b.edited_out += static_cast<double>(a.edited_out);
    }
    b.identities = static_cast<std::uint32_t>(p->germ_accounts.size());
    long double booked=0; const auto& L=s.ledger;
    for (std::size_t k=0;k<L.initial.size();++k)
        booked+=L.initial[k]+L.injected[k]-L.extracted[k]-L.sunk[k]-L.trace[k]+L.converted[k];
    p->booked_mass_kg=static_cast<double>(booked);
    p->booked_heat_j=static_cast<double>(L.heat);
    std::lock_guard lock(publication);
    if (retained_sample_frames) {
        sample_frames.emplace(p->status.completed_frames,p);
        while (sample_frames.size()>retained_sample_frames) sample_frames.erase(sample_frames.begin());
    }
    latest = std::move(p);
}
void publish(const Session& s) { publish(s, project(s, *s.world)); }
void fault(Session& s, const char* why) noexcept {
    s.status.state = 2; write_error(s.status, why);
    try { auto p = std::make_shared<Published>(); p->config=s.config; p->status = s.status;
        { std::lock_guard lock(publication); if (latest) { p->views = latest->views; p->readings = latest->readings; p->parts = latest->parts; p->world = latest->world; p->catalogue = latest->catalogue; p->booked_mass_kg=latest->booked_mass_kg; p->booked_heat_j=latest->booked_heat_j;
            p->biology = latest->biology; p->germ_accounts = latest->germ_accounts; } latest = std::move(p); } }
    catch (...) {}
}
// The CPUs this process may run on (its affinity, as when the game is pinned).
unsigned available_cpus() {
#if defined(__linux__)
    cpu_set_t set; CPU_ZERO(&set);
    if (sched_getaffinity(0, sizeof(set), &set) == 0 && CPU_COUNT(&set) > 0) return static_cast<unsigned>(CPU_COUNT(&set));
#endif
    return std::max(1U, std::thread::hardware_concurrency());
}
void activate(std::unique_ptr<Session> next, std::uint64_t thread) {
    require(!enabled.load(std::memory_order_acquire), "A colony owner is already active");
    next->release_capture.configure(std::getenv("MA_COLONY_RELEASE_DUMP"));
    // Execution only: the kernel's results never depend on its thread count.
    // Leave two logical CPUs for the game's own threads. The measured regional
    // workload gains nothing from oversubscribing twelve workers on this host.
    auto options = mac_kernel_default_options();
    options.threads = std::clamp(available_cpus(), 3U, 14U) - 2;
    options.collect_conduction_entropy = false;
    set_mac_kernel_default_options(options);
    next->status = empty_status(); next->status.state = 1; next->status.session = ++sessions; next->status.start_thread = thread;
    start_thread.store(thread, std::memory_order_relaxed);
    next->status.species = static_cast<std::uint32_t>(next->catalogue->registry.size()); next->status.time_seconds = next->world->time_s();
    for (const auto& r : next->reservoir) next->status.reservoir_kg += r.mass_kg;
    geometry = next->config; owned_count = next->world->state().grid.walls.size();
    owned = std::make_unique<std::atomic<std::uint8_t>[]>(owned_count);
    for (std::size_t i = 0; i < owned_count; ++i) owned[i].store(next->world->state().grid.walls[i] ? 0 : 1, std::memory_order_relaxed);
    for (auto& v : suppressed) v.store(0, std::memory_order_relaxed);
    { std::lock_guard lock(inputs); pending_transfers.clear(); pending_heat.clear(); pending_terrain.clear(); pending_freezes.clear(); pending_germs.clear(); receipts.clear();
      precise_receipts.clear(); precise_receipts_enabled = false;
      pipe_jobs.clear(); pending_job_tokens.clear(); receipt_jobs.clear(); accepted_job_token=0;
      receipt_sequence = acknowledged = 0; exchanged = {}; moving_protocol_used=false; moving_outcomes.clear(); }
    { std::lock_guard o(oracle.m); oracle.cells.clear(); oracle.comparisons = oracle.mismatches = oracle.extinctions = 0; oracle.born = oracle.died = 0; oracle.last.clear(); }
    biology_mode.store(next->germ_mode, std::memory_order_relaxed);
    publish(*next);
    session = std::move(next);
    live_parcel_session.store(session->status.session,std::memory_order_release);
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
    struct Target { std::vector<double> masses; long double energy = 0, heat = 0, mass = 0; PassiveQuantities germs; };
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
            add_passive_quantities(t.germs, p.passives, 1);
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
        bool first = true;
        for (std::size_t k = 0; k < t.masses.size(); ++k) if (t.masses[k] > 0) {
            inject.push_back({to, k, true, t.masses[k], temperature});
            if (first) inject.back().passives = t.germs; // germs travel with the moved material
            first = false;
        }
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
// Airflow tiles (docs/colony-owner.md#airflow-tiles): for this step every face
// of a liquid-blocking cell closes while liquid is on either side of it.
double liquid_mass(const MacClosedWorld& world, std::size_t i) {
    const auto& m = world.state().cells[i].mixture; const auto& registry = world.registry(); double total = 0;
    for (std::size_t k = 0; k < m.masses.size(); ++k) if (registry[k].phase == Phase::liquid) total += m.masses[k];
    return total;
}
MacClosedWorld close_blocked_faces(const Session& s, MacClosedWorld world) {
    const auto& grid = world.state().grid; const auto w = grid.width, h = grid.height;
    std::vector<std::uint8_t> x(grid.x_width() * h, 0), y(w * grid.y_height(), 0); bool any = false;
    const auto check = [&](std::size_t a, std::size_t b) {
        if (!(s.blocking[a] || s.blocking[b]) || grid.walls[a] || grid.walls[b]) return false;
        return liquid_mass(world, a) > 0 || liquid_mass(world, b) > 0;
    };
    for (std::size_t row = 0; row < h; ++row) for (std::size_t col = 1; col < w; ++col)
        if (check(col - 1 + row * w, col + row * w)) { x[grid.x_face(col, row)] = 1; any = true; }
    for (std::size_t row = 1; row < h; ++row) for (std::size_t col = 0; col < w; ++col)
        if (check(col + (row - 1) * w, col + row * w)) { y[grid.y_face(col, row)] = 1; any = true; }
    if (!any) { x.clear(); y.clear(); }
    if (x == grid.closed_x && y == grid.closed_y) return world;
    return world.with_closed_faces(std::move(x), std::move(y));
}
// Liquid that reached a blocking cell during a step (its faces close only at
// the next step) moves on to the open neighbour holding the most liquid, or
// the one below.
MacClosedWorld expel_blocked_liquid(Session& s, MacClosedWorld world) {
    const auto& grid = world.state().grid; const auto w = grid.width, h = grid.height; std::vector<struct Move> moves;
    for (std::size_t i = 0; i < s.blocking.size(); ++i) {
        if (!s.blocking[i] || grid.walls[i] || !(liquid_mass(world, i) > 0)) continue;
        const auto x = i % w, yy = i / w; std::size_t best = SIZE_MAX; double most = -1;
        for (const auto n : {yy > 0 ? i - w : SIZE_MAX, x > 0 ? i - 1 : SIZE_MAX, x + 1 < w ? i + 1 : SIZE_MAX, yy + 1 < h ? i + w : SIZE_MAX}) {
            if (n == SIZE_MAX || grid.walls[n] || s.blocking[n] || s.space[n]) continue;
            const double held = liquid_mass(world, n);
            if (held > most) { most = held; best = n; }
        }
        if (best == SIZE_MAX) continue;
        const auto& m = world.state().cells[i].mixture; const auto& registry = world.registry();
        for (std::size_t k = 0; k < m.masses.size(); ++k) if (registry[k].phase == Phase::liquid && m.masses[k] > 0) moves.push_back({i, best, k, m.masses[k]});
    }
    return moves.empty() ? world : move_material(std::move(world), moves, s.ledger, s.status);
}
// Terrain edits at the start of a frame (docs/colony-owner.md#terrain).
std::atomic<std::uint32_t> refused_terrain{UINT32_MAX}; // control hook: a public cell whose terrain edit throws
std::atomic<std::uint32_t> refused_displacement{UINT32_MAX}; // control hook: a closing public cell whose displacement throws
std::atomic<std::size_t> refused_heat{SIZE_MAX};       // control hook: a rect cell whose heat edit is refused
std::atomic<bool> refused_relaxation{false};           // control hook: phase relaxation throws
// `to_reservoir`: a closing cell's contents go to the reservoir instead of
// its neighbours (for a close whose displacement the reference EOS refuses).
MacClosedWorld apply_terrain(Session& s, MacClosedWorld world, std::span<const MAOniColonyTerrain> edits, bool to_reservoir = false) {
    for (const auto& e : edits) if (e.public_cell == refused_terrain.load(std::memory_order_relaxed)) throw std::runtime_error("Terrain edit refused (control)");
    const auto& st0 = world.state(); const auto w = st0.grid.width, h = st0.grid.height;
    std::vector<std::size_t> opening, closing;
    std::vector<std::uint8_t> touched(st0.cells.size());
    std::vector<MacFluidTransfer> cleared;
    for (const auto& e : edits) {
        const auto i = rect_index(s.config, e.public_cell);
        if (e.kind == MA_ONI_COLONY_BLOCK_LIQUID || e.kind == MA_ONI_COLONY_PASS_LIQUID) { s.blocking[i] = e.kind == MA_ONI_COLONY_BLOCK_LIQUID; continue; }
        if (e.kind == MA_ONI_COLONY_CLEAR) {
            if (st0.grid.walls[i]) continue;
            const auto& m = st0.cells[i].mixture;
            for (std::size_t k = 0; k < m.masses.size(); ++k) if (m.masses[k] > 0) cleared.push_back({i, k, false, m.masses[k], 0});
            continue;
        }
        // The first edit that changes the cell takes it; one that would not
        // (an open of an open cell, a close of a wall) does not hide a later one.
        if (touched[i]) continue;
        if (e.kind == MA_ONI_COLONY_OPEN && st0.grid.walls[i]) { touched[i] = 1; opening.push_back(i); }
        if (e.kind == MA_ONI_COLONY_CLOSE && !st0.grid.walls[i]) { touched[i] = 1; closing.push_back(i); }
    }
    if (!cleared.empty()) {
        // Replaced cells lose their fluid (traces first, the bulk last).
        std::stable_sort(cleared.begin(), cleared.end(), [](const MacFluidTransfer& a, const MacFluidTransfer& b) {
            return a.cell_index != b.cell_index ? a.cell_index < b.cell_index : a.mass_kg < b.mass_kg; });
        for (std::size_t first = 0; first < cleared.size(); first += 4096) {
            const std::span<const MacFluidTransfer> batch(cleared.data() + first, std::min<std::size_t>(4096, cleared.size() - first));
            auto result = world.with_fluid_transfers(batch, MacFluidTransferTopology::occupied_mixture);
            world = std::move(result.world);
            for (const auto& p : result.parcels) { s.ledger.extracted[p.species_index] += p.mass_kg; s.ledger.energy_out += parcel_energy(p); s.status.extracted_kg += p.mass_kg; }
        }
    }
    const auto neighbours = [&](std::size_t i) {
        std::vector<std::size_t> out; const auto x = i % w, y = i / w;
        for (const auto n : {x > 0 ? i - 1 : SIZE_MAX, x + 1 < w ? i + 1 : SIZE_MAX, y > 0 ? i - w : SIZE_MAX, y + 1 < h ? i + w : SIZE_MAX})
            if (n != SIZE_MAX) out.push_back(n);
        return out;
    };
    if (!closing.empty()) {
        // Displace each closing cell's contents equally into its open neighbours.
        std::vector<Move> moves; std::vector<std::uint8_t> closes(st0.cells.size()); std::vector<MacFluidTransfer> traces;
        for (const auto i : closing) closes[i] = 1;
        for (const auto i : closing) {
            const auto& m = world.state().cells[i].mixture; if (!(m.mass() > 0)) continue;
            if (!to_reservoir && refused_displacement.load(std::memory_order_relaxed) != UINT32_MAX &&
                rect_index(s.config, refused_displacement.load(std::memory_order_relaxed)) == i) throw std::runtime_error("Displacement refused (control)");
            std::vector<std::size_t> open;
            if (!to_reservoir) for (const auto n : neighbours(i)) if (!world.state().grid.walls[n] && !closes[n] && !s.space[n]) open.push_back(n);
            if (open.empty() && !to_reservoir) {
                // Inside a block closing at once, the contents go to the nearest
                // open cell reached through the block (within 16 steps).
                std::vector<std::size_t> frontier{i}, seen{i}; std::size_t found = SIZE_MAX;
                for (int depth = 0; depth < 16 && found == SIZE_MAX && !frontier.empty(); ++depth) {
                    std::vector<std::size_t> next;
                    for (const auto c : frontier) for (const auto n : neighbours(c)) {
                        if (found != SIZE_MAX || std::find(seen.begin(), seen.end(), n) != seen.end()) continue;
                        seen.push_back(n);
                        if (world.state().grid.walls[n] || s.space[n]) continue;
                        if (!closes[n]) { found = n; break; }
                        next.push_back(n);
                    }
                    frontier = std::move(next);
                }
                if (found != SIZE_MAX) open.push_back(found);
            }
            if (open.empty()) {
                // A sealed cell's contents go to the reservoir.
                std::vector<MacFluidTransfer> drain;
                for (std::size_t k = 0; k < m.masses.size(); ++k) if (m.masses[k] > 0) drain.push_back({i, k, false, m.masses[k], 0});
                std::stable_sort(drain.begin(), drain.end(), [](const MacFluidTransfer& a, const MacFluidTransfer& b) { return a.mass_kg < b.mass_kg; });
                auto result = world.with_fluid_transfers(drain, MacFluidTransferTopology::occupied_mixture);
                world = std::move(result.world);
                for (const auto& p : result.parcels) {
                    s.ledger.extracted[p.species_index] += p.mass_kg; s.ledger.energy_out += parcel_energy(p);
                    s.reservoir.push_back({i, p.species_index, p.mass_kg, p.temperature_k}); s.status.reservoir_kg += p.mass_kg;
                }
                continue;
            }
            // Smallest constituents first: a trace left beside nothing would
            // keep only rounding residue of the cell's energy. Rounding-sized
            // traces (below 1e-12 of the cell) cannot be added to a neighbour
            // holding more of them: they leave, booked as extracted.
            std::vector<std::size_t> held;
            for (std::size_t k = 0; k < m.masses.size(); ++k) if (m.masses[k] > 0) held.push_back(k);
            std::stable_sort(held.begin(), held.end(), [&](std::size_t a, std::size_t b) { return m.masses[a] < m.masses[b]; });
            for (const auto k : held) if (m.masses[k] < 1e-12 * m.mass()) traces.push_back({i, k, false, m.masses[k], 0});
            for (const auto k : held) {
                if (m.masses[k] < 1e-12 * m.mass()) continue;
                double left = m.masses[k];
                for (std::size_t q = 0; q < open.size(); ++q) {
                    const double share = q + 1 == open.size() ? left : m.masses[k] / static_cast<double>(open.size());
                    moves.push_back({i, open[q], k, share}); left -= share;
                }
            }
        }
        for (std::size_t first = 0; first < traces.size(); first += 4096) {
            const std::span<const MacFluidTransfer> batch(traces.data() + first, std::min<std::size_t>(4096, traces.size() - first));
            auto result = world.with_fluid_transfers(batch, MacFluidTransferTopology::occupied_mixture);
            world = std::move(result.world);
            for (const auto& p : result.parcels) { s.ledger.extracted[p.species_index] += p.mass_kg; s.ledger.energy_out += parcel_energy(p); s.status.extracted_kg += p.mass_kg; }
        }
        world = move_material(std::move(world), moves, s.ledger, s.status);
        world = world.with_closed_cells(closing);
        for (const auto i : closing) {
            // A cell whose solid ONI removed while this close was in flight is
            // already queued to open again.
            auto state = owned[i].load(std::memory_order_relaxed);
            while (state != 3 && !owned[i].compare_exchange_weak(state, 0, std::memory_order_relaxed)) {}
        }
        s.status.closed_cells += closing.size();
    }
    if (!opening.empty()) {
        world = world.with_opened_cells(opening);
        for (const auto i : opening) owned[i].store(1, std::memory_order_relaxed);
        if(world.settings().numerical_kernel!=MacNumericalKernel::game){
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
        }
        s.status.opened_cells += opening.size();
    }
    // A queued close or open that did not happen (the cell already had that
    // state, or another edit of the cell came first) returns an open cell to
    // the owner.
    for (const auto& e : edits) if (e.kind == MA_ONI_COLONY_CLOSE || e.kind == MA_ONI_COLONY_OPEN) {
        const auto i = rect_index(s.config, e.public_cell); std::uint8_t pending = e.kind == MA_ONI_COLONY_CLOSE ? 2 : 3;
        if (!world.state().grid.walls[i]) owned[i].compare_exchange_strong(pending, 1, std::memory_order_relaxed);
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
    struct Rule { std::size_t from, to; std::int32_t byproduct; double fraction; bool high; double threshold; bool unseen = false; };
    std::vector<Rule> rules;
    for (std::size_t k = 0; k < cat.materials.size(); ++k) {
        const auto& g = cat.materials[k].game;
        for (const bool high : {true, false}) {
            const auto target = high ? g.high_target : g.low_target;
            if (target == 0) continue;
            std::size_t to;
            try { to = cat.species_of_hash(target); }
            catch (...) {
                // A solid target: ONI freezes the liquid its projection shows;
                // a liquid the projection hides (a puddle under gas, a minority
                // liquid) freezes here, all of it into the solid as debris.
                if (!high && !g.gas) rules.push_back({k, k, target, 1.0, false, g.low_temperature_k, true});
                continue;
            }
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
    // Cells holding a species some rule converts, read in parallel (each
    // reading is the cell's own; the order of use below is the cells').
    struct Candidate { std::size_t cell; double t = 0, liquid_volume = 0; bool read = false; };
    std::vector<Candidate> candidates;
    const auto scan_start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < st.cells.size(); ++i) {
        if (st.grid.walls[i] || s.space[i]) continue;
        const auto& m = st.cells[i].mixture;
        bool candidate = false; for (const auto& r : rules) candidate = candidate || m.masses[r.from] > 0;
        if (candidate) candidates.push_back({i});
    }
    {
        constexpr std::size_t block_size = 256;
        const auto blocks = (candidates.size() + block_size - 1) / block_size;
        const auto options = mac_kernel_default_options();
        const auto threads = static_cast<unsigned>(std::min<std::size_t>(options.threads, std::max<std::size_t>(1, blocks)));
        if (blocks > 0) mac_kernel::pool_for(threads).run(blocks, [&](std::size_t block) {
            for (std::size_t q = block * block_size; q < std::min(candidates.size(), (block + 1) * block_size); ++q) {
                auto& c = candidates[q];
                try { const auto e = world.settings().numerical_kernel==MacNumericalKernel::game?world.cell_reading(c.cell):evaluate_cell(*cat.eos, st.cells[c.cell].mixture, mac_material_volume(st, c.cell)); c.t = e.temperature_k; c.liquid_volume = e.liquid_volume_m3; c.read = true; }
                catch (...) {}
            }
        });
    }
    const auto read_done = std::chrono::steady_clock::now();
    std::vector<double> temperature;
    for (const auto& c : candidates) {
        if (edits.size() >= 4000) break;
        if (!c.read) continue;
        const auto i = c.cell; const auto& m = st.cells[i].mixture;
        const double t = c.t, liquid_volume = c.liquid_volume;
        // The liquid the projection shows (as project() picks it).
        std::size_t shown = SIZE_MAX; double best = 0;
        if (liquid_volume >= .5 * st.grid.volume_m3())
            for (std::size_t k = 0; k < m.masses.size(); ++k) if (!cat.materials[k].game.gas && m.masses[k] > best) { best = m.masses[k]; shown = k; }
        for (const auto& r : rules) {
            if (!(m.masses[r.from] > 0) || (r.high ? !(t > r.threshold) : !(t < r.threshold))) continue;
            if (r.unseen && r.from == shown) continue;
            // A conversion too small to change the cell's masses in binary64
            // waits (traces stay unconverted).
            const double converted = m.masses[r.from] * share;
            if (!(converted > 1e-9 * m.mass())) continue;
            const double byproduct = r.byproduct != 0 ? (r.unseen ? converted : converted * std::clamp(r.fraction, 0.0, 1.0)) : 0;
            edits.push_back({i, r.from, false, converted, 0});
            products.push_back({i, r.to, converted - byproduct, r.byproduct, byproduct});
            // The cell's temperature before conversion is the products' temperature.
            temperature.push_back(t);
        }
    }
    const auto plan_done = std::chrono::steady_clock::now();
    if (edits.empty()) return world;
    auto& L = s.ledger;
    // Sterile products need no extraction receipt to construct their passives.
    // Keep the original take/give order while sharing its state copy/admission.
    const auto planned_gives = static_cast<std::size_t>(std::count_if(products.begin(), products.end(), [](const auto& p) { return p.mass > 0; }));
    const bool sterile = planned_gives <= 4096 && edits.size() <= 4096 - planned_gives &&
        std::all_of(edits.begin(), edits.end(), [&](const auto& e) {
        return world.state().cells[e.cell_index].passive_quantities.empty();
    });
    std::size_t joined_gives = 0;
    auto taken = [&] {
        if (!sterile) return world.with_fluid_transfers(edits, MacFluidTransferTopology::occupied_mixture);
        auto combined = edits;
        for (std::size_t q = 0; q < products.size(); ++q) if (products[q].mass > 0)
            combined.push_back({products[q].cell, products[q].species, true, products[q].mass, temperature[q]});
        joined_gives = combined.size() - edits.size();
        return world.with_fluid_transfers(combined, MacFluidTransferTopology::occupied_mixture);
    }();
    world = std::move(taken.world);
    std::vector<MacFluidTransfer> given;
    for (std::size_t q = 0; q < products.size(); ++q) {
        const auto& parcel = taken.parcels[q];
        L.extracted[parcel.species_index] += parcel.mass_kg; L.energy_out += parcel_energy(parcel);
        s.status.converted_kg += parcel.mass_kg;
        // The product keeps the germs the converted mass carried.
        if (!sterile && products[q].mass > 0) given.push_back({products[q].cell, products[q].species, true, products[q].mass, temperature[q], parcel.passives});
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
    if (joined_gives > 0) {
        // The original two batches each incremented the saved revision.
        world = world.with_passive_edits({});
        for (std::size_t q = edits.size(); q < taken.parcels.size(); ++q) {
            const auto& parcel = taken.parcels[q];
            L.injected[parcel.species_index] += parcel.mass_kg; L.energy_in += parcel_energy(parcel);
        }
    } else if (!given.empty()) {
        auto result = world.with_fluid_transfers(given, MacFluidTransferTopology::occupied_mixture);
        world = std::move(result.world);
        for (const auto& parcel : result.parcels) { L.injected[parcel.species_index] += parcel.mass_kg; L.energy_in += parcel_energy(parcel); }
    }
    const bool stage_log = stage_logging_enabled();
    if (stage_log && s.status.completed_frames % 10 == 9) {
        const auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
        std::fprintf(stderr, "[Mixed Atmospheres] conversions: %zu candidates read %.1f ms, plan %.1f ms, %zu edits applied %.1f ms, joined %zu\n",
            candidates.size(), ms(scan_start, read_done), ms(read_done, plan_done), edits.size(), ms(plan_done, std::chrono::steady_clock::now()), joined_gives);
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
    std::vector<ImportCell> imported(cells.size()); next->space.assign(cells.size(), 0); next->blocking.assign(cells.size(), 0);
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
    // Germs stay in their cell if it still holds fluid after the settle.
    for (std::size_t i = 0; i < cells.size(); ++i) {
        const auto& c = cells[i]; auto& cell = settled.state.cells[i];
        require(c.disease <= 256 && c.disease_count >= 0 && (c.disease != 0 || c.disease_count == 0), "Invalid colony import germs");
        if (c.disease != 0 && c.disease_count > 0 && cell.mixture.mass() > 0) cell.passive_quantities = {{c.disease, static_cast<double>(c.disease_count)}};
    }
    next->reservoir = std::move(settled.reservoir);
    if(config.flags&MA_ONI_COLONY_GAME_KERNEL) {
        auto cfg=settings_of(config);cfg.game.saturation_pairs=with_critical(cat,mac_saturation_pairs(cat.registry,*cat.eos));
        for(auto& pair:cfg.game.saturation_pairs)if(!std::isfinite(pair.critical_temperature_k))pair.critical_temperature_k=1e300;
        std::vector<double> temperatures(settled.state.cells.size());
        for(std::size_t i=0;i<temperatures.size();++i)if(!settled.state.grid.walls[i]&&settled.state.cells[i].mixture.mass()>0)
            temperatures[i]=cat.eos->evaluate(settled.state.cells[i].mixture,mac_material_volume(settled.state,i)).temperature_k;
        next->world=std::make_shared<const MacClosedWorld>(MacClosedWorld::game_colony(cat.registry,cat.eos,std::move(settled.state),temperatures,cfg));
    } else {
    next->world = std::make_shared<const MacClosedWorld>(cat.registry, cat.eos, std::move(settled.state), settings_of(config));
    }
    next->ledger.initial = totals(*next->world); next->ledger.initial_energy = energy_of(*next->world);
    for (auto* v : {&next->ledger.injected, &next->ledger.extracted, &next->ledger.sunk, &next->ledger.converted, &next->ledger.converted_gross,
                    &next->ledger.trace}) v->assign(cat.registry.size(), 0);
    next->pairs = with_critical(cat, mac_saturation_pairs(cat.registry, *cat.eos));
    // New colonies evolve world germs in the owner when the game supplied its
    // disease table; the explicit oracle control keeps the legacy mode.
    next->germ_mode = !disease_table.empty() && !germ_oracle_requested() ? 1 : 0;
    activate(std::move(next), thread);
}

void stop() noexcept {
    enabled.store(false, std::memory_order_release);
    biology_mode.store(0, std::memory_order_relaxed);
    solver.settle(true);
    std::lock_guard lock(lifecycle);
    session.reset();
    std::lock_guard p(publication); latest.reset(); sample_frames.clear(); retained_sample_frames=0;
}

MAOniColonyStatus status() {
    std::lock_guard lock(publication);
    auto s = latest ? latest->status : empty_status();
    std::lock_guard q(inputs); s.receipts_pending = receipts.size();
    s.native_heat_j = exchanged.heat_j; s.native_mass_in_kg = exchanged.mass_in_kg; s.native_mass_out_kg = exchanged.mass_out_kg;
    s.native_changes = exchanged.changes;
    return s;
}
MAOniColonyParcelContext parcel_context() {
    std::shared_ptr<const Published> p; { std::lock_guard lock(publication); p=latest; }
    require(p && p->world,"No colony owner is active");
    const auto& c=p->config;
    const auto spacing=p->world->state().grid.spacing_m;
    return {sizeof(MAOniColonyParcelContext),1,c.flags,0,p->status.session,
        c.grid_width,c.grid_height,c.offset_x,c.offset_y,c.width,c.height,
        spacing,spacing,p->world->settings().gravity_m_per_s2};
}
namespace {
bool copy_published(MAOniColonyStatus& out, MAOniColonyCellView* views, std::uint32_t capacity,
    std::optional<std::pair<std::uint64_t,std::uint64_t>> previous) {
    std::shared_ptr<const Published> p; { std::lock_guard lock(publication); p = latest; }
    require(p != nullptr, "No colony owner is active");
    const bool changed=!previous || previous->first!=p->status.session || previous->second!=p->status.completed_frames;
    if (views) {
        require(capacity >= p->views.size(), "Colony view buffer is too small");
        if (changed) std::copy(p->views.begin(), p->views.end(), views);
    }
    out = p->status;
    std::lock_guard q(inputs);
    out.native_heat_j = exchanged.heat_j; out.native_mass_in_kg = exchanged.mass_in_kg; out.native_mass_out_kg = exchanged.mass_out_kg;
    out.native_changes = exchanged.changes;
    return changed;
}
}
void copy(MAOniColonyStatus& out,MAOniColonyCellView* views,std::uint32_t capacity) {
    (void)copy_published(out,views,capacity,std::nullopt);
}
bool copy_changed(std::uint64_t session_id,std::uint64_t frame,MAOniColonyStatus& out,MAOniColonyCellView* views,std::uint32_t capacity) {
    return copy_published(out,views,capacity,std::pair{session_id,frame});
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
void refuse_relaxation(bool refused) { refused_relaxation.store(refused, std::memory_order_relaxed); }
void refuse_heat(std::int64_t public_cell) {
    refused_heat.store(public_cell < 0 ? SIZE_MAX : rect_index(geometry, static_cast<std::uint32_t>(public_cell)), std::memory_order_relaxed);
}
void refuse_terrain(std::int64_t public_cell) { refused_terrain.store(public_cell < 0 ? UINT32_MAX : static_cast<std::uint32_t>(public_cell), std::memory_order_relaxed); }
void refuse_displacement(std::int64_t public_cell) { refused_displacement.store(public_cell < 0 ? UINT32_MAX : static_cast<std::uint32_t>(public_cell), std::memory_order_relaxed); }
void refuse_reading(std::int64_t public_cell) {
    refused_reading.store(public_cell < 0 ? SIZE_MAX : rect_index(geometry, static_cast<std::uint32_t>(public_cell)), std::memory_order_relaxed);
}
double trace_reading_temperature(std::uint32_t public_cell, std::uint32_t native_index, double trace_kg) {
    std::shared_ptr<const Published> p; { std::lock_guard lock(publication); p = latest; }
    require(p != nullptr && p->world != nullptr, "No colony owner is active");
    const ArithmeticScope arithmetic;
    const auto& w = *p->world; const auto i = rect_index(geometry, public_cell);
    auto m = w.state().cells[i].mixture; m.masses[p->catalogue->species_of_native(native_index)] = trace_kg;
    return evaluate_cell(w.thermodynamics(), m, mac_material_volume(w.state(), i)).temperature_k;
}
// The reference liquid has no thermal expansion: its density depends on
// pressure only.
double liquid_density(std::span<const MAOniColonyMaterial> materials, std::uint32_t native_index, double pressure_pa) {
    require(std::isfinite(pressure_pa) && pressure_pa >= 0, "Invalid liquid density pressure");
    const auto c = catalogue_of(materials);
    const auto k = c->species_of_native(native_index);
    require(!c->materials[k].game.gas, "Liquid density of a gas");
    return 1 / c->eos->reference().liquid_response(k, pressure_pa).specific_volume_m3_per_kg;
}
std::uint64_t readings(std::vector<MAOniColonyReading>& out, std::vector<MAOniColonyConstituent>& parts) {
    std::shared_ptr<const Published> p; { std::lock_guard lock(publication); p = latest; }
    require(p != nullptr && p->world != nullptr, "No colony owner is active");
    out = p->readings; parts = p->parts;
    return p->status.completed_frames;
}
bool read_passives(std::uint64_t expected_session,std::uint64_t expected_frame,std::span<const std::uint32_t> cells,
    MAOniColonyPassiveStamp& stamp,std::vector<MAOniColonyPassiveRow>& rows) {
    require(!cells.empty() && cells.size()<=4096,"Passive observation requires 1..4096 cells");
    for(std::size_t i=1;i<cells.size();++i)require(cells[i]>cells[i-1],"Passive observation cells must be sorted and unique");
    std::shared_ptr<const Published> p;{std::lock_guard lock(publication);p=latest;}
    require(p && p->world,"No colony owner is active");
    if(expected_session && (expected_session!=p->status.session || expected_frame!=p->status.completed_frames))return false;
    const auto& state=p->world->state();std::size_t count=0;
    for(const auto cell : cells) {
        const auto i=rect_index(p->config,cell);const auto size=state.cells[i].passive_quantities.size();
        require(size<=(1u<<20)-count,"Passive observation exceeds the row limit");count+=size;
    }
    std::vector<MAOniColonyPassiveRow> next;next.reserve(count);
    for(const auto cell : cells) {
        const auto i=rect_index(p->config,cell);
        for(const auto& value : state.cells[i].passive_quantities)next.push_back({cell,0,value.identity,value.quantity});
    }
    std::sort(next.begin(),next.end(),[](const auto& a,const auto& b) {
        return a.public_cell<b.public_cell || (a.public_cell==b.public_cell && a.identity<b.identity);
    });
    const MAOniColonyPassiveStamp source{sizeof(MAOniColonyPassiveStamp),1,p->status.session,p->status.completed_frames,p->world->time_s()};
    rows=std::move(next);stamp=source;return true;
}
void sample_history(std::uint32_t frames) {
    require(frames<=64,"Arena history requires 0..64 retained frames");
    std::lock_guard lock(publication);
    require(latest && latest->world && latest->config.width*static_cast<std::uint64_t>(latest->config.height)<=4096,
        "History requires an active small arena owner");
    std::map<std::uint64_t,std::shared_ptr<const Published>> next;
    if (frames) next.emplace(latest->status.completed_frames,latest);
    sample_frames.swap(next); retained_sample_frames=frames;
}
void sample_frame(std::uint64_t frame,std::span<const std::uint32_t> cells,MAOniColonyStatus& source,
    std::vector<MAOniColonySampleCell>& rows,std::vector<MAOniColonySamplePart>& parts,double* booked_mass,double* booked_heat) {
    require(!cells.empty() && cells.size()<=4096,"An arena sample requires 1..4096 cells");
    std::shared_ptr<const Published> p;
    { std::lock_guard lock(publication);
      if (frame==UINT64_MAX) p=latest;
      else if (const auto found=sample_frames.find(frame); found!=sample_frames.end()) p=found->second;
    }
    require(p!=nullptr,"Requested arena frame is not retained");
    require(p && p->world && p->catalogue,"No colony owner is active");
    const ArithmeticScope arithmetic;
    auto ordered=std::vector<std::uint32_t>(cells.begin(),cells.end()); std::sort(ordered.begin(),ordered.end());
    require(std::adjacent_find(ordered.begin(),ordered.end())==ordered.end(),"Duplicate arena sample cell");
    for (const auto cell : cells) (void)rect_index(p->config,cell);
    std::vector<MAOniColonySampleCell> sampled; std::vector<MAOniColonySamplePart> compositions; sampled.reserve(cells.size());
    const auto& state=p->world->state();
    for (const auto cell : cells) {
        const auto index=rect_index(p->config,cell); const auto& mixture=state.cells[index].mixture;
        MAOniColonySampleCell row{}; row.public_cell=cell; row.first_constituent=static_cast<std::uint32_t>(compositions.size());
        if (!state.grid.walls[index]) {
            row.flags=1; row.temperature_k=p->views[index].temperature_k;
            for (std::size_t k=0;k<mixture.masses.size();++k) if (mixture.masses[k]>0)
                compositions.push_back({p->catalogue->materials[k].game.native_index,0,mixture.masses[k]});
            row.constituents=static_cast<std::uint32_t>(compositions.size())-row.first_constituent;
            if (row.constituents) {
                const auto reading=std::lower_bound(p->readings.begin(),p->readings.end(),cell,
                    [](const MAOniColonyReading& r,std::uint32_t c){return r.public_cell<c;});
                if (reading==p->readings.end() || reading->public_cell!=cell) row.flags|=2;
            }
        }
        sampled.push_back(row);
    }
    source=p->status; rows=std::move(sampled); parts=std::move(compositions);
    if (booked_mass) *booked_mass=p->booked_mass_kg;
    if (booked_heat) *booked_heat=p->booked_heat_j;
}
void sample(std::span<const std::uint32_t> cells,MAOniColonyStatus& source,
    std::vector<MAOniColonySampleCell>& rows,std::vector<MAOniColonySamplePart>& parts,double* booked_mass) {
    sample_frame(UINT64_MAX,cells,source,rows,parts,booked_mass,nullptr);
}

namespace {
void validate_transfer(const MAOniColonyTransfer& e) {
        const auto kind = e.kind & ~MA_ONI_COLONY_CONSUMER;
        require(kind >= MA_ONI_COLONY_INJECT && kind <= MA_ONI_COLONY_EXTRACT_LIQUID && (kind != MA_ONI_COLONY_INJECT || kind == e.kind) &&
            e.disease <= 256 && e.radius <= 64 && std::isfinite(e.disease_count) && e.disease_count >= 0 && (e.disease != 0 || e.disease_count == 0) &&
            std::isfinite(e.mass_kg) && e.mass_kg >= 0 && std::isfinite(e.temperature_k) && e.temperature_k >= 0, "Invalid colony transfer");
        (void)rect_index(geometry, e.public_cell);
}
std::pair<std::uint32_t,std::uint64_t> job_state_locked(std::uint64_t token) {
    if (!token || token>accepted_job_token) return {0,0};
    const auto found=pipe_jobs.find(token);
    if (found==pipe_jobs.end()) return {3,0};
    return {found->second.last_receipt?2u:1u,found->second.last_receipt};
}
}
void queue_transfers(std::span<const MAOniColonyTransfer> edits) {
    require(active(), "No colony owner is active");
    for (const auto& e : edits) validate_transfer(e);
    std::lock_guard lock(inputs);
    require(pending_transfers.size() + edits.size() <= 1u << 20, "Too many queued colony transfers");
    pending_transfers.insert(pending_transfers.end(), edits.begin(), edits.end());
}
void queue_actor_batch(std::span<const MAOniColonyTransfer> edits,std::span<const MAOniColonyActorJob> jobs,
    std::span<const MAOniColonyPassive> passives) {
    const ArithmeticScope arithmetic;
    require(active(),"No colony owner is active");
    require(!edits.empty() && edits.size()<=(1u<<20) && jobs.size()<=edits.size(),"Invalid actor batch size");
    for (const auto& edit : edits) validate_transfer(edit);
    std::map<std::uint64_t,PipeJob> staged_jobs;
    std::size_t passive_cursor=0; std::uint32_t previous_index=0; std::uint64_t previous_token=0;
    for (std::size_t n=0;n<jobs.size();++n) {
        const auto& job=jobs[n];
        require(job.reserved==0 && job.transfer_index<edits.size() && (!n || job.transfer_index>previous_index) &&
            job.token && (!n || (previous_token!=UINT64_MAX && job.token==previous_token+1)) &&
            job.first_passive==passive_cursor && job.passive_count<=65537 &&
            passive_cursor<=passives.size() && job.passive_count<=passives.size()-passive_cursor,"Invalid actor job descriptor");
        const auto& transfer=edits[job.transfer_index];
        require(transfer.callback==-1 && transfer.disease==0 && transfer.disease_count==0 &&
            (transfer.kind==MA_ONI_COLONY_INJECT || transfer.kind==MA_ONI_COLONY_EXTRACT_GAS || transfer.kind==MA_ONI_COLONY_EXTRACT_LIQUID) &&
            (transfer.kind==MA_ONI_COLONY_INJECT || job.passive_count==0),"Invalid actor pipe job request");
        PassiveQuantities germs; germs.reserve(job.passive_count); std::uint64_t previous_identity=0;
        for (const auto& passive : passives.subspan(passive_cursor,job.passive_count)) {
            require(passive.identity>previous_identity && std::isfinite(passive.quantity) && passive.quantity>0,"Invalid actor pipe passive");
            germs.push_back({passive.identity,passive.quantity}); previous_identity=passive.identity;
        }
        validate_passive_quantities(germs);
        staged_jobs.emplace(job.token,PipeJob{transfer,std::move(germs)});
        passive_cursor+=job.passive_count; previous_index=job.transfer_index; previous_token=job.token;
    }
    require(passive_cursor==passives.size(),"Unused actor batch passives");
    std::lock_guard lock(inputs);
    require(jobs.empty() || precise_receipts_enabled,"Actor pipe jobs require precise receipts");
    require(pending_transfers.size()<= (1u<<20)-edits.size(),"Too many queued colony transfers");
    require(jobs.empty() || (accepted_job_token!=UINT64_MAX && jobs.front().token==accepted_job_token+1),"Actor pipe tokens must be fresh and contiguous");
    // Allocate every map node and vector capacity before changing the pending
    // input sequence. Integer-key node merges and reserved POD appends cannot
    // allocate or throw; the worker cannot drain until this lock is released.
    std::map<std::size_t,std::uint64_t> staged_indices;
    for (const auto& job : jobs) staged_indices.emplace(pending_transfers.size()+job.transfer_index,job.token);
    pending_transfers.reserve(pending_transfers.size()+edits.size());
    pending_transfers.insert(pending_transfers.end(),edits.begin(),edits.end());
    pending_job_tokens.merge(staged_indices); pipe_jobs.merge(staged_jobs);
    if (!jobs.empty()) accepted_job_token=jobs.back().token;
}
std::pair<std::uint32_t,std::uint64_t> queue_pipe_job(std::uint64_t token,const MAOniColonyTransfer& transfer,std::span<const MAOniColonyPassive> passives) {
    const ArithmeticScope arithmetic;
    require(active(),"No colony owner is active"); validate_transfer(transfer);
    require(token && transfer.callback==-1 && transfer.disease==0 && transfer.disease_count==0 &&
        (transfer.kind==MA_ONI_COLONY_INJECT || transfer.kind==MA_ONI_COLONY_EXTRACT_GAS || transfer.kind==MA_ONI_COLONY_EXTRACT_LIQUID),"Invalid pipe job request");
    require(passives.size()<=65537 && (transfer.kind==MA_ONI_COLONY_INJECT || passives.empty()),"Invalid pipe job passives");
    PassiveQuantities germs; germs.reserve(passives.size()); std::uint64_t previous=0;
    for (const auto& passive : passives) {
        require(passive.identity>previous && std::isfinite(passive.quantity) && passive.quantity>0,"Invalid pipe job passive quantity");
        germs.push_back({passive.identity,passive.quantity}); previous=passive.identity;
    }
    validate_passive_quantities(germs);
    std::lock_guard lock(inputs);
    require(precise_receipts_enabled,"Pipe jobs require precise receipts");
    if (token<=accepted_job_token) {
        if (const auto found=pipe_jobs.find(token); found!=pipe_jobs.end()) {
            require(!found->second.moving_wire && std::memcmp(&found->second.transfer,&transfer,sizeof(transfer))==0 && found->second.passives.size()==germs.size(),"Pipe token reused for a different request");
            for (std::size_t i=0;i<germs.size();++i)
                require(found->second.passives[i].identity==germs[i].identity && found->second.passives[i].quantity==germs[i].quantity,"Pipe token reused for different passives");
        }
        return job_state_locked(token);
    }
    require(accepted_job_token!=std::numeric_limits<std::uint64_t>::max() && token==accepted_job_token+1,"Pipe tokens must be contiguous");
    require(pending_transfers.size()<1u<<20,"Too many queued colony transfers");
    // All potentially allocating operations precede the accepted watermark.
    // Roll back metadata if an allocation refuses the transfer.
    const auto index=pending_transfers.size();
    pipe_jobs.emplace(token,PipeJob{transfer,std::move(germs)});
    try { pending_job_tokens.emplace(index,token); pending_transfers.push_back(transfer); }
    catch (...) { pending_job_tokens.erase(index); pipe_jobs.erase(token); throw; }
    accepted_job_token=token; return {1,0};
}
std::pair<std::uint32_t,std::uint64_t> queue_moving_parcel(std::uint64_t expected_session,std::uint64_t token,std::span<const std::uint8_t> wire) {
    const ArithmeticScope arithmetic;
    require(active(),"No colony owner is active");
    const auto context=parcel_context();
    require(expected_session && context.session==expected_session,"Stale moving parcel session");
    const auto request=detail::decode_moving_parcels(wire);
    for(const auto& item : request) {
        const auto x=item.public_cell%context.grid_width,y=item.public_cell/context.grid_width;
        require(x>=context.offset_x && y>=context.offset_y && x-context.offset_x<context.width && y-context.offset_y<context.height,
            "Moving parcel cell is outside the owner");
    }
    auto payload=std::make_shared<const std::vector<std::uint8_t>>(wire.begin(),wire.end());
    MAOniColonyTransfer marker{};marker.kind=MA_ONI_COLONY_INJECT;marker.callback=-1;
    marker.public_cell=request.front().public_cell;marker.native_index=request.front().native_index;
    std::lock_guard lock(inputs);
    require(active() && expected_session && live_parcel_session.load(std::memory_order_acquire)==expected_session,"Stale moving parcel session");
    require(token && precise_receipts_enabled,"Moving parcels require a token and precise receipts");
    if(token<=accepted_job_token) {
        if(const auto found=pipe_jobs.find(token);found!=pipe_jobs.end())
            require(found->second.moving_wire && *found->second.moving_wire==*payload,"Moving token reused for a different request");
        return job_state_locked(token);
    }
    require(accepted_job_token!=UINT64_MAX && token==accepted_job_token+1,"Moving tokens must be contiguous");
    require(pending_transfers.size()<1u<<20,"Too many queued colony transfers");
    const auto index=pending_transfers.size();
    pipe_jobs.emplace(token,PipeJob{marker,{},0,std::move(payload)});
    try {pending_job_tokens.emplace(index,token);pending_transfers.push_back(marker);}
    catch(...) {pending_job_tokens.erase(index);pipe_jobs.erase(token);throw;}
    accepted_job_token=token;moving_protocol_used=true;return {1,0};
}
std::uint32_t copy_moving_outcome(std::uint64_t sequence,std::uint8_t* output,std::uint32_t capacity) {
    std::lock_guard lock(inputs);
    const auto job=receipt_jobs.find(sequence);
    if(job==receipt_jobs.end() || !pipe_jobs.at(job->second).last_receipt)return 0;
    const auto found=moving_outcomes.find(sequence);if(found==moving_outcomes.end())return 0;
    const auto size=static_cast<std::uint32_t>(found->second.size());
    if(output && capacity>=size)std::copy(found->second.begin(),found->second.end(),output);
    return size;
}
std::pair<std::uint32_t,std::uint64_t> pipe_job_state(std::uint64_t token) {
    require(active(),"No colony owner is active"); std::lock_guard lock(inputs); return job_state_locked(token);
}
std::uint64_t receipt_job(std::uint64_t sequence) {
    std::lock_guard lock(inputs); const auto found=receipt_jobs.find(sequence); return found==receipt_jobs.end()?0:found->second;
}
void set_diseases(std::span<const MAOniColonyDisease> diseases, std::span<const MAOniColonyDiseaseGrowth> growth, std::uint32_t elements) {
    std::lock_guard lock(lifecycle);
    require(!enabled.load(std::memory_order_acquire), "Set the disease table before the colony owner starts");
    require(growth.size() == diseases.size() * elements, "World disease growth table size differs");
    biology::Table table; table.elements = elements;
    for (const auto& d : diseases) {
        require(d.struct_size == sizeof(d) && d.abi_version == 1 && d.reserved == 0, "Invalid disease record");
        biology::Disease rules; rules.strength = d.strength;
        for (int k = 0; k < 4; ++k) { rules.range[k] = d.temperature_range[k]; rules.half_lives[k] = d.temperature_half_lives[k]; }
        table.diseases.push_back(rules);
    }
    for (const auto& g : growth) {
        require(g.minimum_diffusion_ticks <= 255, "Invalid disease diffusion ticks");
        table.growth.push_back({g.under_population_death_rate, g.population_half_life, g.over_population_half_life, g.diffusion_scale,
            g.minimum_count_per_kg, g.maximum_count_per_kg, g.minimum_diffusion_count, static_cast<std::uint8_t>(g.minimum_diffusion_ticks)});
    }
    if (!table.diseases.empty()) biology::validate(table);
    disease_table = std::move(table);
}
void queue_germs(std::span<const MAOniColonyGermEdit> edits) {
    require(active(), "No colony owner is active");
    for (const auto& e : edits) {
        require(biology::disease_identity(e.disease) && std::isfinite(e.change), "Invalid colony germ edit");
        (void)rect_index(geometry, e.public_cell);
    }
    std::lock_guard lock(inputs);
    require(pending_germs.size() + edits.size() <= 1u << 20, "Too many queued colony germ edits");
    for (const auto& e : edits) if (e.change != 0) pending_germs.push_back({e.public_cell, e.disease, e.change});
}
void germ_accounts(MAOniColonyBiology& out, std::vector<MAOniColonyBiologyAccount>& accounts) {
    std::shared_ptr<const Published> p; { std::lock_guard lock(publication); p = latest; }
    require(p != nullptr, "No colony owner is active");
    out = p->biology; out.struct_size = sizeof(out); out.abi_version = 1; accounts = p->germ_accounts;
    std::lock_guard o(oracle.m);
    out.oracle_comparisons = oracle.comparisons; out.oracle_mismatches = oracle.mismatches; out.oracle_extinctions = oracle.extinctions;
    out.oracle_born = oracle.born; out.oracle_died = oracle.died;
    std::snprintf(out.oracle_last, sizeof out.oracle_last, "%s", oracle.last.c_str());
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
// MA_COLONY_TERRAIN_LOG=1: every queued terrain edit with its source (stderr).
void log_terrain(const char* source, std::span<const MAOniColonyTerrain> edits) noexcept {
    static const bool on = std::getenv("MA_COLONY_TERRAIN_LOG") != nullptr;
    if (!on) return;
    for (const auto& e : edits)
        std::fprintf(stderr, "[Mixed Atmospheres] terrain %s: kind %u at %u,%u\n", source, e.kind,
            e.public_cell % geometry.grid_width, e.public_cell / geometry.grid_width);
}
void queue_terrain(std::span<const MAOniColonyTerrain> edits) {
    require(active(), "No colony owner is active");
    for (const auto& e : edits) {
        require(e.kind >= MA_ONI_COLONY_OPEN && e.kind <= MA_ONI_COLONY_CLEAR, "Invalid colony terrain edit");
        (void)rect_index(geometry, e.public_cell);
    }
    std::lock_guard lock(inputs);
    require(pending_terrain.size() + edits.size() <= 1u << 20, "Too many queued colony terrain edits");
    // A queued close leaves the cell to ONI at once (the solid that replaced
    // it is native); the owner displaces its fluid at its next step. A queued
    // open marks the wall as opening, so the shadow does not open it again.
    for (const auto& e : edits) {
        auto& state = owned[rect_index(geometry, e.public_cell)];
        if (e.kind == MA_ONI_COLONY_CLOSE) { std::uint8_t open = 1; state.compare_exchange_strong(open, 2, std::memory_order_relaxed); }
        if (e.kind == MA_ONI_COLONY_OPEN) { std::uint8_t closed = 0; state.compare_exchange_strong(closed, 3, std::memory_order_relaxed); }
    }
    pending_terrain.insert(pending_terrain.end(), edits.begin(), edits.end());
    log_terrain("managed", edits);
}
std::uint32_t copy_receipts(MAOniColonyReceipt* out, std::uint32_t capacity) {
    std::lock_guard lock(inputs);
    std::size_t available=receipts.size();
    if (!receipt_jobs.empty()) {
        available=0;
        for (const auto& receipt : receipts) {
            const auto job=receipt_jobs.find(receipt.sequence);
            if (job!=receipt_jobs.end() && pipe_jobs.at(job->second).last_receipt==0) break;
            ++available;
        }
    }
    const auto count = static_cast<std::uint32_t>(std::min<std::size_t>(capacity, available));
    if (out) std::copy(receipts.begin(), receipts.begin() + count, out);
    return static_cast<std::uint32_t>(available);
}
void ack_receipts(std::uint64_t through) {
    std::lock_guard lock(inputs);
    require(through >= acknowledged && through <= receipt_sequence, "Colony receipt acknowledgement is out of order");
    for (const auto& [sequence,token] : receipt_jobs)
        if (sequence<=through) require(pipe_jobs.at(token).last_receipt!=0,"Cannot acknowledge an unfinished pipe job");
    std::erase_if(receipts, [&](const MAOniColonyReceipt& r) { return r.sequence <= through; });
    precise_receipts.erase(precise_receipts.begin(), precise_receipts.upper_bound(through));
    moving_outcomes.erase(moving_outcomes.begin(),moving_outcomes.upper_bound(through));
    receipt_jobs.erase(receipt_jobs.begin(),receipt_jobs.upper_bound(through));
    std::erase_if(pipe_jobs,[&](const auto& item){return item.second.last_receipt && item.second.last_receipt<=through;});
    acknowledged = through;
}
void enable_precise_receipts(bool enabled_mode) {
    std::lock_guard lock(inputs);
    require(active(), "No colony owner is active");
    if (precise_receipts_enabled == enabled_mode) return;
    require(receipts.empty(), "Cannot change precise receipt mode with outstanding receipts");
    require(pipe_jobs.empty(),"Cannot change precise receipt mode with pending pipe jobs");
    precise_receipts_enabled = enabled_mode;
}
std::uint32_t copy_precise_receipt(std::uint64_t sequence, std::uint8_t* out, std::uint32_t capacity) {
    std::lock_guard lock(inputs);
    if (const auto job=receipt_jobs.find(sequence); job!=receipt_jobs.end() && pipe_jobs.at(job->second).last_receipt==0) return 0;
    const auto found = precise_receipts.find(sequence);
    if (found == precise_receipts.end()) return 0;
    require(found->second.size() <= std::numeric_limits<std::uint32_t>::max(), "Precise receipt exceeds 4 GiB");
    const auto size = static_cast<std::uint32_t>(found->second.size());
    if (out && capacity >= size) std::copy(found->second.begin(), found->second.end(), out);
    return size;
}

bool owns(int native_cell) noexcept {
    if (!enabled.load(std::memory_order_acquire) || native_cell < 0) return false;
    const auto pitch = geometry.grid_width + 2, position = static_cast<std::uint32_t>(native_cell);
    const auto nx = position % pitch, ny = position / pitch;
    if (!nx || !ny) return false;
    const auto x = nx - 1, y = ny - 1;
    if (x < geometry.offset_x || x - geometry.offset_x >= geometry.width || y < geometry.offset_y || y - geometry.offset_y >= geometry.height) return false;
    return owned[(x - geometry.offset_x) + static_cast<std::size_t>(y - geometry.offset_y) * geometry.width].load(std::memory_order_relaxed) == 1;
}

bool beside_owned(int native_cell) noexcept {
    if (!enabled.load(std::memory_order_acquire) || native_cell < 0) return false;
    const auto pitch = static_cast<int>(geometry.grid_width + 2);
    return owns(native_cell - 1) || owns(native_cell + 1) || owns(native_cell - pitch) || owns(native_cell + pitch);
}

void write_shadow(const NativeCells& n) noexcept {
    if (!enabled.load(std::memory_order_acquire)) return;
    std::shared_ptr<const Published> p; { std::lock_guard lock(publication); p = latest; }
    if (!p || !p->catalogue || p->views.size() != owned_count || p->status.state != 1 || n.pitch != geometry.grid_width + 2) return;
    const auto& cat = *p->catalogue; const auto vacuum = geometry.vacuum_index;
    try { shadow.assign(owned_count, ShadowCell{}); } catch (...) { return; }
    std::vector<MAOniColonyTerrain> closes;
    for (std::size_t i = 0; i < owned_count; ++i) {
        const std::size_t c = (geometry.offset_x + i % geometry.width + 1) + (geometry.offset_y + i / geometry.width + 1) * static_cast<std::size_t>(n.pitch);
        const auto present = n.element[0][c];
        shadow[i].before = present;
        if (owned[i].load(std::memory_order_relaxed) != 1) continue;
        // A solid native placed in an owned cell (landing sand, a tile) stays
        // native, and the owner closes the cell at its next step.
        if (present != vacuum && !cat.has_native(present)) {
            std::uint8_t open = 1;
            if (owned[i].compare_exchange_strong(open, 2, std::memory_order_relaxed))
                closes.push_back({static_cast<std::uint32_t>((geometry.offset_x + i % geometry.width) + (geometry.offset_y + i / geometry.width) * geometry.grid_width), MA_ONI_COLONY_CLOSE});
            continue;
        }
        // A trace (under a milligram) is vacuum to ONI's native code: its heat
        // exchange would give so little mass extreme temperatures. The owner
        // keeps it; readers use the owner's own view.
        const auto& v = p->views[i]; const bool empty = !(v.mass_kg >= 1e-6f);
        auto& sh = shadow[i];
        sh.element = static_cast<std::uint16_t>(empty ? vacuum : v.element_index); sh.written = true;
        sh.mass = empty ? 0.f : v.mass_kg; sh.temperature = empty ? 0.f : v.temperature_k;
        for (int b = 0; b < 2; ++b) { n.element[b][c] = sh.element; n.mass[b][c] = sh.mass; n.temperature[b][c] = sh.temperature; }
        if (n.disease[0] && n.disease[1] && n.disease_count[0] && n.disease_count[1]) {
            // The owner evolves world germs itself (mode 1): native code sees
            // none, so only its emitters add germs to the shadow.
            sh.germ = empty || biology_mode.load(std::memory_order_relaxed) == 1 ? 0 : v.disease; sh.germs = sh.germ ? v.disease_count : 0;
            const auto idx = static_cast<std::uint8_t>(sh.germ ? sh.germ - 1 : 255);
            for (int b = 0; b < 2; ++b) { n.disease[b][c] = idx; n.disease_count[b][c] = sh.germs; }
        }
    }
    if (!closes.empty()) {
        std::lock_guard q(inputs);
        try { pending_terrain.insert(pending_terrain.end(), closes.begin(), closes.end()); } catch (...) {}
        log_terrain("shadow close", closes);
    }
}

namespace {
// The germ oracle control (MA_COLONY_GERM_ORACLE=1, legacy mode): native code
// has just evolved a sealed single cell's projected culture on the shadow;
// predict that step from the same inputs (count, element, mass, temperature,
// and the residue native keeps, zero after the cell was last empty) and count
// agreement. A cell is followed only while nothing else can change it.
void observe_oracle(const Catalogue& cat, std::uint32_t cell, const ShadowCell& sh, std::uint16_t e, float m, float t,
    std::uint32_t germ, std::int32_t count, bool sealed) {
    std::lock_guard lock(oracle.m);
    auto& o = oracle.cells[cell];
    if (sh.germ == 0) { o = {true, 0, 0}; return; }
    if (!o.known) return;
    if (!sealed || e != sh.element || !(m > 0) || !cat.has_native(e) || sh.germs < 1 || sh.germ > disease_table.diseases.size() ||
        e >= disease_table.elements) { o.known = false; return; }
    biology::Medium medium; medium.open = true; medium.state = cat.materials[cat.species_of_native(e)].game.gas ? 1 : 2;
    medium.element = e; medium.temperature_k = t; medium.mass_kg = m; medium.parts = {{e, static_cast<double>(m)}};
    const auto step = biology::grow(disease_table, sh.germ - 1, medium, sh.germs, biology::Clock{biology::native_mode, 0, o.residue});
    const bool extinct = !(step.population > 0);
    const bool match = extinct ? germ == 0 : germ == sh.germ && static_cast<double>(count) == step.population;
    ++oracle.comparisons;
    if (extinct) ++oracle.extinctions;
    if (step.change > 0) oracle.born += step.change; else oracle.died -= step.change;
    if (!match) {
        ++oracle.mismatches; o.known = false;
        char text[160];
        std::snprintf(text, sizeof text, "cell %u germ %u: native %u/%d, predicted %.0f from %d at %.3f K, %.6g kg element %u",
            cell, sh.germ, germ, count, step.population, sh.germs, static_cast<double>(t), static_cast<double>(m), static_cast<unsigned>(e));
        oracle.last = text;
        return;
    }
    o.residue = extinct ? 0.f : step.clock.residue;
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
        std::vector<Freeze> froze; std::vector<GermEdit> germs; NativeExchange sum; std::vector<std::pair<std::size_t, std::uint32_t>> undone;
        const auto fluid = [&](std::uint16_t e) { return e == vacuum || cat.has_native(e); };
        const bool oracle_on = germ_oracle_requested() && biology_mode.load(std::memory_order_relaxed) == 0 && !disease_table.empty();
        for (std::size_t i = 0; i < owned_count; ++i) {
            const auto& sh = shadow[i];
            const std::size_t c = (geometry.offset_x + i % geometry.width + 1) + (geometry.offset_y + i / geometry.width + 1) * static_cast<std::size_t>(n.pitch);
            const auto e = n.element[1][c]; const float m = n.mass[1][c], t = n.temperature[1][c];
            const auto cell = static_cast<std::uint32_t>((geometry.offset_x + i % geometry.width) + (geometry.offset_y + i / geometry.width) * geometry.grid_width);
            if (!sh.written) {
                if (!fluid(e)) continue;
                const auto state = owned[i].load(std::memory_order_relaxed);
                if (fluid(sh.before)) {
                    // ONI holds fluid in a cell the owner closed and does not
                    // reopen: a solid placed after its close was queued and
                    // melted within the same native update. The owner opens the
                    // cell again and takes ONI's fluid.
                    if (state != 0 || e == vacuum || !(std::isfinite(m) && m > 0 && std::isfinite(t) && t > 0)) continue;
                } else if (state == 2) {
                    // A solid placed over owned fluid and gone before the owner
                    // closed the cell (unstable sand falling at once): the close is
                    // undone. The fluid native put back is its copy of the owner's.
                    undone.push_back({i, cell}); ++sum.changes; continue;
                } else if (!(std::isfinite(m) && m >= 0 && std::isfinite(t) && t >= 0)) continue;
                // A native solid that melted (or sublimated away) this frame:
                // the owner opens the cell and takes its fluid, once.
                std::uint8_t closed = 0; owned[i].compare_exchange_strong(closed, 3, std::memory_order_relaxed);
                opened.push_back({cell, MA_ONI_COLONY_OPEN});
                if (e != vacuum && m > 0 && t > 0) { moves.push_back({cell, e, MA_ONI_COLONY_INJECT, 0, -1, 0, m, t, 0}); sum.mass_in_kg += m; }
                ++sum.changes; continue;
            }
            if (n.disease[1] && n.disease_count[1] && fluid(e)) {
                const std::uint8_t idx = n.disease[1][c]; const std::int32_t count = n.disease_count[1][c];
                const std::uint32_t germ = idx == 255 || count <= 0 ? 0 : idx + 1u;
                if (oracle_on) {
                    bool sealed = true;
                    for (const auto k : {c - 1, c + 1, c - n.pitch, c + n.pitch}) sealed = sealed && !fluid(n.element[0][k]);
                    observe_oracle(cat, cell, sh, e, m, t, germ, count, sealed);
                }
                if (germ == sh.germ && count != sh.germs && germ != 0) germs.push_back({cell, germ, static_cast<double>(count) - sh.germs});
                else if (germ != sh.germ) {
                    if (sh.germ != 0) germs.push_back({cell, sh.germ, -static_cast<double>(sh.germs)});
                    if (germ != 0) germs.push_back({cell, germ, static_cast<double>(count)});
                }
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
                static const bool heat_log = std::getenv("MA_COLONY_HEAT_LOG") != nullptr;
                if (heat_log)
                    std::fprintf(stderr, "[Mixed Atmospheres] native heat at %u,%u: element %u, %.6g kg, %.6f -> %.6f K, %.6g J\n",
                        cell % geometry.grid_width, cell / geometry.grid_width, static_cast<unsigned>(e), static_cast<double>(m),
                        static_cast<double>(sh.temperature), static_cast<double>(t), joules);
                continue;
            }
            // Native processes changed the mass (the owner runs element
            // consumers and emitters). A take from the gas projection (ONI
            // deleting gas exposed to space) takes the cell's gas mixture by
            // mass; a cell left at no mass gives up all of its gas, traces
            // included (the binary32 projection would leave a residue).
            const auto take = [&](std::uint16_t element, double mass, bool all = false) {
                const bool gas = cat.materials[cat.species_of_native(element)].game.gas;
                moves.push_back({cell, gas ? 0u : element, gas ? MA_ONI_COLONY_EXTRACT_GAS : MA_ONI_COLONY_EXTRACT, 0, -1, 0,
                    all && gas ? 2 * mass + 1 : mass, 0, 0});
                sum.mass_out_kg += mass; };
            const auto give = [&](std::uint16_t element, double mass, double temperature) {
                moves.push_back({cell, element, MA_ONI_COLONY_INJECT, 0, -1, 0, mass, temperature, 0}); sum.mass_in_kg += mass; };
            static const bool mass_log = std::getenv("MA_COLONY_HEAT_LOG") != nullptr;
            if (mass_log)
                std::fprintf(stderr, "[Mixed Atmospheres] native mass at %u,%u: element %u %.6g kg %.4f K -> element %u %.6g kg %.4f K\n",
                    cell % geometry.grid_width, cell / geometry.grid_width, static_cast<unsigned>(sh.element), static_cast<double>(sh.mass),
                    static_cast<double>(sh.temperature), static_cast<unsigned>(e), static_cast<double>(m), static_cast<double>(t));
            if (e == sh.element) {
                const double delta = static_cast<double>(m) - sh.mass;
                if (delta < 0) take(e, -delta, !(m > 0));
                else {
                    // The emitted parcel's temperature, from the mixed endpoint.
                    const double emitted = (static_cast<double>(m) * t - static_cast<double>(sh.mass) * sh.temperature) / delta;
                    give(e, delta, emitted > 0 && std::isfinite(emitted) ? emitted : t);
                }
            } else {
                if (sh.element != vacuum && sh.mass > 0) take(sh.element, sh.mass, true);
                if (e != vacuum && m > 0 && t > 0) give(e, m, t);
            }
        }
        std::lock_guard q(inputs);
        for (const auto& [i, cell] : undone) {
            // Cancel a close not yet taken by the owner; one already taken is
            // followed by an open at the next step.
            const auto before = pending_terrain.size();
            std::erase_if(pending_terrain, [&](const MAOniColonyTerrain& t) { return t.public_cell == cell && t.kind == MA_ONI_COLONY_CLOSE; });
            if (pending_terrain.size() != before) { owned[i].store(1, std::memory_order_relaxed); continue; }
            // The close is in flight or done: the cell opens again without
            // taking ONI's fluid (its copy of the owner's).
            auto state = owned[i].load(std::memory_order_relaxed);
            while ((state == 0 || state == 2) && !owned[i].compare_exchange_weak(state, 3, std::memory_order_relaxed)) {}
            opened.push_back({cell, MA_ONI_COLONY_OPEN});
        }
        pending_germs.insert(pending_germs.end(), germs.begin(), germs.end());
        pending_heat.insert(pending_heat.end(), heat.begin(), heat.end());
        pending_terrain.insert(pending_terrain.end(), opened.begin(), opened.end());
        log_terrain("shadow open", opened);
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
    if (owned[(x - geometry.offset_x) + static_cast<std::size_t>(y - geometry.offset_y) * geometry.width].load(std::memory_order_relaxed) != 1) return false;
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
        solver.ticket = ticket; solver.seconds += seconds; if (seconds > 0) ++solver.frames;
        solver.busy.store(true, std::memory_order_release);
        if (!solver.thread.joinable()) solver.thread = std::thread([] { solver.loop(); });
        solver.wake.notify_one();
    } catch (...) {
        std::lock_guard lock(lifecycle);
        if (session) fault(*session, "The colony solver thread could not take a frame");
    }
}

namespace {
// What the saved game profile's trace sink removed leaves the ledgers as its
// own term. Germs on removed traces leave with them (reported, not booked).
void book_trace(Session& s, const MacTraceSinkReport& r) {
    if (r.constituents == 0) return;
    auto& L = s.ledger;
    require(r.mass_kg.size() == L.trace.size(), "A trace sink report names a different species list");
    long double kg = 0;
    for (std::size_t k = 0; k < r.mass_kg.size(); ++k) { const auto m = mac_energy_long_double(r.mass_kg[k]); L.trace[k] += m; kg += m; }
    const auto joules = mac_energy_long_double(r.internal_energy_j) + mac_energy_long_double(r.kinetic_energy_j) + mac_energy_long_double(r.potential_energy_j);
    L.energy_trace += joules; L.trace_momentum_x += mac_energy_long_double(r.momentum_x); L.trace_momentum_y += mac_energy_long_double(r.momentum_y);
    s.status.trace_sink_kg += static_cast<double>(kg); s.status.trace_sink_j += static_cast<double>(joules);
}
// The profile's sink after the owner's own edits that follow the flow step
// (the flow applies it at both ends of its interval, so traces imported or
// made by the edits before it leave there).
MacClosedWorld sink_traces(Session& s, MacClosedWorld world) {
    if (!(world.settings().trace_sink_threshold_kg > 0)) return world;
    auto sunk = world.with_trace_sink();
    book_trace(s, sunk.report);
    return std::move(sunk.world);
}
// World germ evolution (docs/colony-owner.md#germs): the queued germ edits
// (native emitters acting on the shadow, the game's own cell edits), then one
// frame of the game's world rules per native frame, all applied as one ordered
// passive edit. Media come from the publication this world produced.
MacClosedWorld evolve_germs(Session& s, MacClosedWorld world, std::span<const GermEdit> queued, std::uint64_t frames) {
    const auto& st = world.state(); const auto& cat = *s.catalogue;
    biology::Cells cells; std::map<std::uint64_t, long double> before;
    for (std::size_t i = 0; i < st.cells.size(); ++i) {
        if (st.grid.walls[i]) continue;
        for (const auto& q : st.cells[i].passive_quantities)
            if (biology::disease_identity(q.identity)) { cells[i].push_back({q.identity, q.quantity}); before[q.identity] += q.quantity; }
    }
    const auto previous = s.accounts;
    const auto held_in = [&](std::size_t i, std::uint64_t identity) {
        const auto found = cells.find(i); if (found == cells.end()) return 0.0;
        for (const auto& c : found->second) if (c.identity == identity) return c.quantity;
        return 0.0; };
    std::vector<biology::Edit> edits;
    for (const auto& g : queued) {
        const auto i = rect_index(s.config, g.public_cell);
        if (st.grid.walls[i] || !biology::disease_identity(g.germ)) continue;
        const double held = held_in(i, g.germ);
        const biology::Edit e{i, g.germ, g.change};
        if (!biology::apply(cells, e, st.cells[i].mixture.mass() > 0)) continue;
        edits.push_back(e);
        const double change = held_in(i, g.germ) > 0 ? g.change : -held;
        auto& a = s.accounts[g.germ]; if (change > 0) a.edited_in += change; else a.edited_out -= change;
    }
    std::shared_ptr<const Published> p; { std::lock_guard lock(publication); p = latest; }
    if (frames > 0 && !disease_table.empty() && p && p->views.size() == st.cells.size()) {
        std::unordered_map<std::size_t, biology::Medium> media;
        const std::function<const biology::Medium&(std::size_t)> medium = [&](std::size_t i) -> const biology::Medium& {
            auto [it, inserted] = media.try_emplace(i);
            auto& m = it->second;
            if (!inserted || st.grid.walls[i]) return m;
            const auto& mixture = st.cells[i].mixture; const auto& v = p->views[i];
            m.open = true; m.mass_kg = mixture.mass(); m.temperature_k = v.temperature_k; m.element = v.element_index;
            for (std::size_t k = 0; k < mixture.masses.size(); ++k)
                if (mixture.masses[k] > 0) m.parts.push_back({cat.materials[k].game.native_index, mixture.masses[k]});
            if (m.mass_kg > 0 && cat.has_native(v.element_index)) m.state = cat.materials[cat.species_of_native(v.element_index)].game.gas ? 1 : 2;
            return m;
        };
        for (std::uint64_t f = 0; f < frames; ++f)
            biology::frame(disease_table, st.grid.width, st.grid.height, medium, cells, s.clocks, s.accounts, edits);
        s.biology_frames += frames;
    }
    if (!edits.empty()) {
        std::vector<MacPassiveEdit> passive; passive.reserve(edits.size());
        for (const auto& e : edits) passive.push_back({e.cell, e.identity, e.change});
        world = world.with_passive_edits(passive);
    }
    // Audit: per identity, the world holds what it held plus the stage's
    // booked edits, births, deaths and culled germs (diffusion only moves).
    std::map<std::uint64_t, long double> after; std::uint64_t germ_cells = 0;
    const auto& now = world.state();
    for (std::size_t i = 0; i < now.cells.size(); ++i) {
        if (now.grid.walls[i]) continue;
        bool any = false;
        for (const auto& q : now.cells[i].passive_quantities)
            if (biology::disease_identity(q.identity)) { after[q.identity] += q.quantity; any = true; }
        germ_cells += any ? 1 : 0;
    }
    std::map<std::uint64_t, double> held;
    for (const auto& [identity, a] : s.accounts) {
        const auto old = previous.find(identity);
        const biology::Account o = old == previous.end() ? biology::Account{} : old->second;
        const long double expected = before[identity] + (a.edited_in - o.edited_in) - (a.edited_out - o.edited_out) +
            (a.born - o.born) - (a.died - o.died) - (a.culled - o.culled);
        const long double actual = after[identity];
        const long double scale = std::max({1.0L, std::abs(actual), std::abs(before[identity])});
        s.biology_error = std::max(s.biology_error, static_cast<double>(std::abs(actual - expected) / scale));
        held[identity] = static_cast<double>(actual);
    }
    s.germs_held = std::move(held); s.germ_cells = germ_cells;
    return world;
}
void run_frame(std::uint64_t ticket, double seconds, std::uint64_t frames, const char* refusal, std::uint64_t thread) noexcept {
    std::lock_guard lock(lifecycle);
    if (!session || session->status.state != 1) return;
    auto& s = *session;
    const char* doing = "frame input"; // named in a fault
    // MA_COLONY_STAGE_LOG=1: every tenth frame, each stage's wall time (stderr).
    const bool stage_log = stage_logging_enabled();
    std::vector<std::pair<const char*, double>> steps; auto step_mark = std::chrono::steady_clock::now();
    const auto step = [&](const char* next) {
        if (stage_log) {
            const auto now = std::chrono::steady_clock::now();
            steps.emplace_back(doing, std::chrono::duration<double, std::milli>(now - step_mark).count()); step_mark = now;
        }
        doing = next;
    };
    try {
        const ArithmeticScope arithmetic;
        const auto started = std::chrono::steady_clock::now();
        if (refusal) throw std::runtime_error(refusal);
        if (!ticket || ticket <= s.status.native_ticket || !std::isfinite(seconds) || seconds <= 0 || seconds > 10)
            throw std::runtime_error("Native frame ticket or timestep is invalid");
        std::vector<MAOniColonyTransfer> transfers; std::vector<MAOniColonyHeat> heat; std::vector<MAOniColonyTerrain> terrain;
        std::vector<Freeze> freezes; std::vector<GermEdit> germ_edits;
        std::map<std::size_t,std::uint64_t> job_tokens;
        std::map<std::size_t,PassiveQuantities> job_passives;
        std::map<std::size_t,std::shared_ptr<const std::vector<std::uint8_t>>> moving_jobs;
        std::map<std::uint64_t,std::uint64_t> committed_jobs;
        { std::lock_guard q(inputs); transfers.swap(pending_transfers); heat.swap(pending_heat); terrain.swap(pending_terrain); freezes.swap(pending_freezes);
          germ_edits.swap(pending_germs); job_tokens.swap(pending_job_tokens);
          for (const auto& [index,token] : job_tokens) {
              const auto& job=pipe_jobs.at(token);
              if(job.moving_wire)moving_jobs.emplace(index,job.moving_wire);
              else job_passives.emplace(index,job.passives);
          } }
        auto world = *s.world;
        const auto& cat = *s.catalogue; const auto& registry = cat.registry;
        auto& L = s.ledger;
        auto mark = std::chrono::steady_clock::now(); double stage[6]{};
        const auto lap = [&](int k) { const auto now = std::chrono::steady_clock::now(); stage[k] = std::chrono::duration<double, std::milli>(now - mark).count(); mark = now; };
        step("biology");
        if (s.germ_mode == 1) world = evolve_germs(s, std::move(world), germ_edits, frames);
        // Frozen cells: the liquid native turned solid leaves, then the cell closes.
        step("freezing");
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
        step("terrain");
        if (!terrain.empty()) {
            const auto before_terrain = world; const Session before_session = s;
            try { world = apply_terrain(s, std::move(world), terrain); }
            catch (const std::exception& refused) {
                // One edit at a time: an edit the reference EOS refuses (a
                // displacement into a cell it cannot evaluate) is tried again,
                // a close then sending its cell's contents to the reservoir
                // (the cell is solid in ONI either way); one still refused
                // waits, unbooked, for the next frame. The others apply.
                world = before_terrain; s = before_session;
                std::vector<MAOniColonyTerrain> waiting;
                for (const auto& e : terrain) {
                    bool applied = false;
                    for (const bool to_reservoir : {false, true}) {
                        if (applied || (!to_reservoir && terrain.size() == 1) || (to_reservoir && e.kind != MA_ONI_COLONY_CLOSE)) continue;
                        const auto world_before = world; const Session session_before = s;
                        try { world = apply_terrain(s, std::move(world), std::span(&e, 1), to_reservoir); applied = true; }
                        catch (const std::exception&) { world = world_before; s = session_before; }
                    }
                    if (!applied) waiting.push_back(e);
                }
                if (!waiting.empty()) {
                    std::lock_guard q(inputs); pending_terrain.insert(pending_terrain.end(), waiting.begin(), waiting.end());
                    log_terrain((std::string("waiting (") + refused.what() + ")").c_str(), waiting);
                }
            }
        }
        step("blocked faces");
        world = close_blocked_faces(s, std::move(world));
        step("germs");
        if (s.germ_mode == 0 && !germ_edits.empty()) {
            // Legacy mode: native code's changes to the projected germ.
            std::vector<MacPassiveEdit> edits;
            for (const auto& g : germ_edits) {
                const auto i = rect_index(s.config, g.public_cell);
                if (world.state().grid.walls[i]) continue;
                edits.push_back({i, g.germ, g.change});
                auto& a = s.accounts[g.germ]; if (g.change > 0) a.edited_in += g.change; else a.edited_out -= g.change;
            }
            world = world.with_passive_edits(edits);
        }
        lap(0); step("heat");
        // Heat edits. A batch the solver refuses is split until the refused
        // edits are found (a cell the reference EOS cannot evaluate, as for a
        // liquid trace below binary64's normal range); the rest apply. A
        // refused edit's heat goes to an open neighbour of its cell; with none
        // it stays queued, unbooked, merged per cell, for the next frame.
        std::vector<MAOniColonyHeat> deferred;
        {
            const auto& st0 = world.state(); const auto W = st0.grid.width;
            std::vector<MacCellHeat> edits; std::vector<std::size_t> source;
            for (std::size_t k = 0; k < heat.size(); ++k) {
                const auto i = rect_index(s.config, heat[k].public_cell);
                if (st0.grid.walls[i] || !(st0.cells[i].mixture.mass() > 0) || heat[k].joules == 0) continue;
                edits.push_back({i, heat[k].joules, heat[k].maximum_temperature_k > 0 ? heat[k].maximum_temperature_k : 1e5});
                source.push_back(k); L.heat += heat[k].joules;
            }
            std::vector<std::size_t> refused;
            const auto apply = [&](const auto& self, std::size_t from, std::size_t count) -> void {
                if (count == 0) return;
                const auto hook = refused_heat.load(std::memory_order_relaxed);
                const bool control = std::any_of(edits.begin() + static_cast<std::ptrdiff_t>(from), edits.begin() + static_cast<std::ptrdiff_t>(from + count),
                    [&](const MacCellHeat& e) { return e.cell_index == hook; });
                if (!control) try { world = world.with_cell_heat(std::span(edits.data() + from, count)); return; }
                catch (const std::exception&) {}
                if (count == 1) { refused.push_back(from); return; }
                self(self, from, count / 2); self(self, from + count / 2, count - count / 2);
            };
            for (std::size_t first = 0; first < edits.size(); first += 4096) apply(apply, first, std::min<std::size_t>(4096, edits.size() - first));
            for (const auto q : refused) {
                const auto i = edits[q].cell_index; bool placed = false;
                for (const auto n : {i % W > 0 ? i - 1 : SIZE_MAX, i % W + 1 < W ? i + 1 : SIZE_MAX, i >= W ? i - W : SIZE_MAX, i + W < st0.cells.size() ? i + W : SIZE_MAX}) {
                    if (n == SIZE_MAX || world.state().grid.walls[n] || !(world.state().cells[n].mixture.mass() > 0)) continue;
                    MacCellHeat moved = edits[q]; moved.cell_index = n;
                    try { world = world.with_cell_heat(std::span(&moved, 1)); placed = true; break; } catch (const std::exception&) {}
                }
                if (placed) continue;
                L.heat -= edits[q].joules;
                const auto& h = heat[source[q]];
                auto same = std::find_if(deferred.begin(), deferred.end(), [&](const MAOniColonyHeat& d) { return d.public_cell == h.public_cell; });
                if (same != deferred.end()) same->joules += h.joules; else deferred.push_back(h);
                if (std::getenv("MA_COLONY_LEDGER_LOG")) { std::fprintf(stderr, "[Mixed Atmospheres] heat refused at cell %u (%.6g J), deferred\n", h.public_cell, h.joules); std::fflush(stderr); }
            }
            s.status.committed_heat += edits.size() - deferred.size();
        }
        if (!deferred.empty()) { std::lock_guard q(inputs); pending_heat.insert(pending_heat.end(), deferred.begin(), deferred.end()); }
        lap(1); step("reservoir");
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
        step("transfers");
        // Transfers: plan against the current state, then commit in batches.
        std::vector<MacFluidTransfer> edits; struct Pending { std::size_t first, count; const MAOniColonyTransfer* source; };
        std::vector<Pending> plans;
        // One cell's removals, smallest constituent first. A cell that would
        // keep less than the plan's binary32 quantum (rounding-sized traces of
        // any phase) gives those up too: a trace left beside nothing would
        // keep only rounding residue of the cell's energy.
        const auto remove = [&](std::size_t cell, std::vector<std::pair<std::size_t, double>> take, double quantum) {
            const auto& m = world.state().cells[cell].mixture;
            double kept = m.mass(); for (const auto& [k, x] : take) kept -= x;
            if (kept < quantum)
                for (std::size_t q = 0; q < m.masses.size(); ++q)
                    if (m.masses[q] > 0 && std::none_of(take.begin(), take.end(), [&](const auto& t) { return t.first == q; })) take.push_back({q, m.masses[q]});
            std::stable_sort(take.begin(), take.end(), [&](const auto& a, const auto& b) { return m.masses[a.first] < m.masses[b.first]; });
            for (const auto& [k, x] : take) if (x > 0) edits.push_back({cell, k, false, x, 0});
        };
        for (const auto& t : transfers) {
            if(moving_jobs.contains(static_cast<std::size_t>(&t-transfers.data())))continue;
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
                const double quantum = std::ldexp(1.0, plan.quantum_exponent);
                const auto first = edits.size();
                for (const auto& a : plan.allocations) {
                    const auto& m = st.cells[a.cell].mixture;
                    // A cell the plan empties to within its binary32 quantum
                    // gives up all of its phase together, traces included.
                    const bool whole = available[a.cell] - a.mass < quantum;
                    std::vector<std::pair<std::size_t, double>> take;
                    for (std::size_t k = 0; k < cat.materials.size(); ++k)
                        if (cat.materials[k].game.gas == gas && m.masses[k] > 0) take.push_back({k, whole ? m.masses[k] : a.mass * (m.masses[k] / available[a.cell])});
                    remove(a.cell, std::move(take), quantum);
                }
                plans.push_back({first, edits.size() - first, &t});
                continue;
            }
            if (!cat.has_native(t.native_index)) { plans.push_back({edits.size(), 0, &t}); continue; }
            const auto k = cat.species_of_native(t.native_index);
            if (kind == MA_ONI_COLONY_INJECT) {
                const auto order = nearby(st, centre, 2);
                if (order.empty() && t.callback == -1 && !job_tokens.contains(static_cast<std::size_t>(&t-transfers.data())) &&
                    t.mass_kg > 0 && std::isfinite(t.mass_kg) && t.temperature_k > 0 && std::isfinite(t.temperature_k)) {
                    // Nothing open within two of an addition no one is told
                    // about (a machine's output, a dumped item): it waits in
                    // the reservoir at the cell and seeps back when there is
                    // room, rather than being lost.
                    s.reservoir.push_back({centre, k, t.mass_kg, t.temperature_k}); s.status.reservoir_kg += t.mass_kg;
                    plans.push_back({edits.size(), 0, &t}); continue;
                }
                if (order.empty() || !(t.mass_kg > 0) || !(t.temperature_k > 0)) { plans.push_back({edits.size(), 0, &t}); continue; }
                plans.push_back({edits.size(), 1, &t}); edits.push_back({order.front(), k, true, t.mass_kg, t.temperature_k});
                const auto input=job_passives.find(static_cast<std::size_t>(&t-transfers.data()));
                if (input!=job_passives.end()) edits.back().passives=input->second;
                else if (t.disease != 0 && t.disease_count >= 1) edits.back().passives = {{t.disease, t.disease_count}};
            } else {
                const auto order = nearby(st, centre, t.radius);
                std::vector<double> available(st.cells.size());
                for (const auto i : order) available[i] = st.cells[i].mixture.masses[k];
                const auto plan = radial::allocate_order(available, order, static_cast<double>(static_cast<float>(t.mass_kg)));
                const double quantum = std::ldexp(1.0, plan.quantum_exponent);
                const auto first = edits.size();
                for (const auto& a : plan.allocations) {
                    const auto& m = st.cells[a.cell].mixture;
                    if (available[a.cell] - a.mass >= quantum) { edits.push_back({a.cell, k, false, a.mass, 0}); continue; }
                    // The constituent leaves whole; what its phase keeps below
                    // the quantum (rounding residue) leaves with it.
                    std::vector<std::pair<std::size_t, double>> take{{k, m.masses[k]}};
                    double rest = 0;
                    for (std::size_t q = 0; q < cat.materials.size(); ++q) if (q != k && cat.materials[q].game.gas == cat.materials[k].game.gas) rest += m.masses[q];
                    if (rest < quantum)
                        for (std::size_t q = 0; q < cat.materials.size(); ++q)
                            if (q != k && cat.materials[q].game.gas == cat.materials[k].game.gas && m.masses[q] > 0) take.push_back({q, m.masses[q]});
                    remove(a.cell, std::move(take), quantum);
                }
                plans.push_back({first, edits.size() - first, &t});
            }
        }
        for (const auto& p : plans)
            if (job_tokens.contains(static_cast<std::size_t>(p.source-transfers.data())))
                for (std::size_t e=p.first;e<p.first+p.count;++e) edits[e].observe_passives=true;
        std::vector<MacFluidParcel> parcels; parcels.reserve(edits.size());
        for (std::size_t first = 0; first < edits.size(); first += 4096) {
            const std::span<const MacFluidTransfer> batch(edits.data() + first, std::min<std::size_t>(4096, edits.size() - first));
            try {
                auto result = world.with_fluid_transfers(batch, MacFluidTransferTopology::occupied_mixture);
                world = std::move(result.world);
                parcels.insert(parcels.end(), result.parcels.begin(), result.parcels.end());
            } catch (const std::exception&) {
                // One by one: an edit too small to change its cell in binary64
                // (a few nanograms on a full liquid cell) is dropped as a zero
                // parcel; any other failing edit faults, naming its cell.
                for (const auto& e : batch) {
                    std::string failure;
                    try {
                        auto one = world.with_fluid_transfers(std::span(&e, 1), MacFluidTransferTopology::occupied_mixture);
                        world = std::move(one.world); parcels.push_back(one.parcels.front()); continue;
                    } catch (const std::exception& single) { failure = single.what(); }
                    if (e.mass_kg <= 1e-9 * world.state().cells[e.cell_index].mixture.mass()) {
                        MacFluidParcel zero{}; zero.cell_index = e.cell_index; zero.species_index = e.species_index; zero.injected = e.inject;
                        if (e.observe_passives) zero.passives_before=zero.passives_after=world.state().cells[e.cell_index].passive_quantities;
                        parcels.push_back(zero); continue;
                    }
                    const auto& m = world.state().cells[e.cell_index].mixture; const auto W = world.state().grid.width;
                    std::string where = " (" + std::string(e.inject ? "injecting " : "extracting ") + std::to_string(e.mass_kg) + " kg " + cat.materials[e.species_index].game.id +
                        " at " + std::to_string(s.config.offset_x + e.cell_index % W) + "," + std::to_string(s.config.offset_y + e.cell_index / W) + " holding";
                    for (std::size_t k = 0; k < m.masses.size(); ++k) if (m.masses[k] > 0) { char b[64]; std::snprintf(b, sizeof b, " %s %.3g", cat.materials[k].game.id.c_str(), m.masses[k]); where += b; }
                    if (const auto* path=std::getenv("MA_COLONY_TRANSFER_DUMP"); path && *path) {
                        bool captured=false;
                        try { const auto image=world.checkpoint(); captured=detail::capture_transfer(path,s.materials,image,seconds,e,s.status.completed_frames+1,s.config.flags); }
                        catch (...) {}
                        std::fprintf(stderr,"[Mixed Atmospheres] failed transfer capture %s at frame %llu: %s\n",
                            captured?"saved":"failed",static_cast<unsigned long long>(s.status.completed_frames+1),path);
                        std::fflush(stderr);
                    }
                    throw std::runtime_error(failure + where + ")");
                }
            }
        }
        if (!plans.empty()) {
            std::lock_guard q(inputs);
            const auto receipt = [&](const MAOniColonyTransfer& t, std::uint32_t native_index, long double mass, long double heat_capacity_t,
                                     const PassiveQuantities& germs, const Pending& p, std::optional<std::size_t> only_species) {
                MAOniColonyReceipt r{}; r.sequence = ++receipt_sequence; r.completed_frame = s.status.completed_frames + 1;
                r.callback = t.callback; r.kind = t.kind; r.native_index = native_index; r.public_cell = t.public_cell;
                r.mass_kg = static_cast<float>(mass); r.temperature_k = mass > 0 ? static_cast<float>(heat_capacity_t / mass) : 0;
                const auto germ = dominant_germ(germs); r.disease = germ.first; r.disease_count = germ.second;
                receipts.push_back(r);
                const auto job=job_tokens.find(static_cast<std::size_t>(p.source-transfers.data()));
                if (job!=job_tokens.end()) {
                    receipt_jobs.emplace(r.sequence,job->second); committed_jobs[job->second]=r.sequence;
                }
                if (precise_receipts_enabled) {
                    std::size_t count = 0;
                    for (std::size_t e = p.first; e < p.first + p.count; ++e)
                        if (!only_species || parcels[e].species_index == *only_species) ++count;
                    detail::PreciseReceiptWriter wire; wire.header(r.sequence, r.completed_frame, count,job!=job_tokens.end());
                    for (std::size_t e = p.first; e < p.first + p.count; ++e) {
                        const auto& parcel = parcels[e];
                        if (only_species && parcel.species_index != *only_species) continue;
                        const auto cell = static_cast<std::uint32_t>(parcel.cell_index);
                        const auto public_cell = (s.config.offset_x + cell % s.config.width) +
                            (s.config.offset_y + cell / s.config.width) * s.config.grid_width;
                        // The solver returns extracted passives on the parcel;
                        // a successful injection adds its full supplied list.
                        // Its parcel deliberately leaves that list empty.
                        const auto carried=parcel.injected && parcel.mass_kg>0 ?
                            std::span<const PassiveQuantity>(edits[e].passives) : std::span<const PassiveQuantity>(parcel.passives);
                        wire.parcel(parcel, cat.materials[parcel.species_index].game.native_index, public_cell,carried);
                    }
                    precise_receipts.emplace(r.sequence, std::move(wire.bytes));
                }
            };
            for (const auto& p : plans) {
                std::map<std::size_t, std::pair<long double, long double>> by_species; // mass, sum m T
                std::map<std::size_t, PassiveQuantities> germs_of; PassiveQuantities all_germs;
                for (std::size_t e = p.first; e < p.first + p.count; ++e) {
                    const auto& parcel = parcels[e];
                    auto& entry = by_species[parcel.species_index];
                    entry.first += parcel.mass_kg; entry.second += parcel.mass_kg * parcel.temperature_k;
                    if (!parcel.injected && !parcel.passives.empty()) {
                        add_passive_quantities(germs_of[parcel.species_index], parcel.passives, 1); add_passive_quantities(all_germs, parcel.passives, 1);
                    }
                    (parcel.injected ? L.injected : L.extracted)[parcel.species_index] += parcel.mass_kg;
                    (parcel.injected ? L.energy_in : L.energy_out) += parcel_energy(parcel);
                    (parcel.injected ? s.status.injected_kg : s.status.extracted_kg) += parcel.mass_kg;
                }
                ++s.status.committed_transfers;
                if (p.source->callback < 0 && !job_tokens.contains(static_cast<std::size_t>(p.source-transfers.data()))) continue;
                const auto kind = p.source->kind & ~MA_ONI_COLONY_CONSUMER;
                const bool phase = kind == MA_ONI_COLONY_EXTRACT_GAS || kind == MA_ONI_COLONY_EXTRACT_LIQUID;
                if (!phase) {
                    long double mass = 0, heat_capacity_t = 0;
                    for (const auto& [k, entry] : by_species) { mass += entry.first; heat_capacity_t += entry.second; }
                    receipt(*p.source, p.source->native_index, mass, heat_capacity_t, all_germs, p, {});
                } else if (by_species.empty()) receipt(*p.source, geometry.vacuum_index, 0, 0, {}, p, {});
                // A mixture leaves one receipt per constituent.
                else for (const auto& [k, entry] : by_species) receipt(*p.source, cat.materials[k].game.native_index, entry.first, entry.second, germs_of[k], p, k);
            }
        }
        const auto moving_started=std::chrono::steady_clock::now();
        std::size_t moving_items=0,moving_accepted=0,moving_refused=0;
        if(!moving_jobs.empty())step("moving parcels");
        for(const auto& [index,payload] : moving_jobs) {
            const auto requests=detail::decode_moving_parcels(*payload);moving_items+=requests.size();
            const auto token=job_tokens.at(index);
            detail::MovingParcelOutcome outcome;
            {std::lock_guard q(inputs);require(receipt_sequence!=UINT64_MAX,"Receipt sequence exhausted");outcome.sequence=receipt_sequence+1;}
            outcome.frame=s.status.completed_frames+1;outcome.token=token;outcome.session=s.status.session;
            std::vector<MacMovingParcel> batch;batch.reserve(requests.size());
            for(const auto& item : requests) {
                if(!cat.has_native(item.native_index)) {outcome.refusal=1;outcome.reason="Unknown moving parcel material";break;}
                MacMovingParcel parcel;parcel.cell_index=rect_index(s.config,item.public_cell);parcel.species_index=cat.species_of_native(item.native_index);
                parcel.mass_kg=item.mass_kg;parcel.internal_energy_j=item.internal_energy_j;parcel.kinetic_energy_j=item.kinetic_energy_j;
                parcel.potential_energy_j=item.potential_energy_j;parcel.momentum_x=item.momentum_x;parcel.momentum_y=item.momentum_y;parcel.passives=item.passives;
                batch.push_back(std::move(parcel));
            }
            std::optional<MacParcelDepositResult> candidate;
            if(!outcome.refusal) {candidate.emplace(world.with_moving_parcels(batch));detail::append_core_outcome(outcome,*candidate,requests);}
            std::vector<long double> masses;long double energy=0;
            if(!outcome.refusal) {
                try {
                    masses.resize(batch.size());
                    const auto total=[](const MacEnergySum& sum) {
                        long double value=0;for(std::size_t n=0;n<sum.size();++n)value+=mac_energy_long_double(sum.component(n));
                        if(!std::isfinite(value))throw std::invalid_argument("Moving parcel exceeds owner reporting range");
                        return value;
                    };
                    for(std::size_t n=0;n<outcome.grid.size();++n) {
                        masses[n]=total(outcome.grid[n].mass_kg);
                        energy+=total(outcome.grid[n].internal_energy_j)+total(outcome.grid[n].kinetic_energy_j)+total(outcome.grid[n].potential_energy_j);
                    }
                    if(!std::isfinite(energy))throw std::invalid_argument("Moving parcel exceeds owner reporting range");
                } catch(const std::exception&) {outcome.refusal=16;outcome.reason="Moving parcel exceeds owner reporting range";outcome.consumed.clear();outcome.grid.clear();}
            }
            std::vector<std::uint8_t> encoded;
            try {encoded=detail::encode_moving_outcome(outcome);}
            catch(const std::invalid_argument&) {
                outcome.refusal=17;outcome.reason="Moving parcel receipt capacity";outcome.consumed.clear();outcome.grid.clear();
                encoded=detail::encode_moving_outcome(outcome);
            }
            MAOniColonyReceipt receipt{};receipt.sequence=outcome.sequence;receipt.completed_frame=outcome.frame;
            receipt.callback=-1;receipt.kind=MA_ONI_COLONY_MOVING_PARCEL;receipt.public_cell=requests.front().public_cell;
            receipt.native_index=requests.front().native_index;
            // Reserve publication storage before committing the physical edit.
            {std::lock_guard q(inputs);
                receipts.reserve(receipts.size()+1);moving_outcomes.emplace(receipt.sequence,std::move(encoded));
                receipt_jobs.emplace(receipt.sequence,token);committed_jobs.emplace(token,receipt.sequence);
                receipts.push_back(receipt);receipt_sequence=receipt.sequence;
            }
            if(!outcome.refusal) {
                for(std::size_t n=0;n<batch.size();++n) {L.injected[batch[n].species_index]+=masses[n];s.status.injected_kg+=static_cast<double>(masses[n]);}
                L.energy_in+=energy;world=std::move(candidate->world);++moving_accepted;
            } else ++moving_refused;
            ++s.status.committed_transfers;
        }
        if(stage_log && !moving_jobs.empty()) {
            const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-moving_started).count();
            std::fprintf(stderr,"[Mixed Atmospheres] moving parcels frame %llu: batches %zu items %zu accepted %zu refused %zu ms %.6f\n",
                static_cast<unsigned long long>(s.status.completed_frames+1),moving_jobs.size(),moving_items,moving_accepted,moving_refused,ms);
        }
        // The profile's trace sink before the flow is the flow's own (advanced()
        // applies it at both ends of its interval).
        lap(2); step("flow");
        // Flow. A debugging request (MA_COLONY_FAULT_DUMP=path) writes the
        // world before a failing flow step and the material records there.
        // MA_COLONY_DUMP_FRAME=n also writes the world before frame n's flow.
        const auto dump = [&](const char* path) {
            if (std::FILE* f = path ? std::fopen(path, "wb") : nullptr) {
                const auto image = world.checkpoint(); const auto count = static_cast<std::uint32_t>(s.materials.size());
                const bool ok = std::fwrite(&count, 4, 1, f)==1 && std::fwrite(s.materials.data(), sizeof(MAOniColonyMaterial), count, f)==count &&
                    std::fwrite(&seconds, 8, 1, f)==1 && std::fwrite(image.data(), 1, image.size(), f)==image.size();
                return std::fclose(f)==0 && ok;
            }
            return false;
        };
        if (s.release_capture.ready(s.config,world.state().grid.walls)) {
            const auto temporary=s.release_capture.path+".tmp";
            require(dump(temporary.c_str()),"Could not write the requested release capture");
            require(std::rename(temporary.c_str(),s.release_capture.path.c_str())==0,"Could not publish the requested release capture");
            s.release_capture.captured();
            std::fprintf(stderr,"[Mixed Atmospheres] release capture before frame %llu at %.17g s: %s\n",
                static_cast<unsigned long long>(s.status.completed_frames+1),world.time_s(),s.release_capture.path.c_str());
        }
        if (const char* frame = std::getenv("MA_COLONY_DUMP_FRAME"); frame && s.status.completed_frames + 1 == std::strtoull(frame, nullptr, 10))
            dump(std::getenv("MA_COLONY_FAULT_DUMP"));
        std::optional<MacPhaseFlow> flowed;
        // MA_COLONY_SLOW_DUMP=s also writes the world before the first flow
        // step still running after s seconds (a watchdog, so a step that
        // never finishes is caught too).
        static std::atomic<bool> slow_dumped{false};
        std::mutex watch; std::condition_variable flow_done; bool finished = false; std::thread watchdog;
        if (const char* slow = std::getenv("MA_COLONY_SLOW_DUMP"); slow && !slow_dumped.load())
            watchdog = std::thread([&, limit = std::atof(slow)] {
                std::unique_lock held(watch);
                if (!flow_done.wait_for(held, std::chrono::duration<double>(limit), [&] { return finished; }) && !slow_dumped.exchange(true))
                    dump(std::getenv("MA_COLONY_FAULT_DUMP"));
            });
        const auto stop_watchdog = [&] {
            { std::lock_guard held(watch); finished = true; }
            flow_done.notify_all(); if (watchdog.joinable()) watchdog.join();
        };
        try { flowed.emplace(advance_mac_phase_flow(world, seconds, 65536, s.pairs)); }
        catch (...) { stop_watchdog(); dump(std::getenv("MA_COLONY_FAULT_DUMP")); throw; }
        stop_watchdog();
        auto advanced = std::move(*flowed);
        book_trace(s, advanced.trace_sink);
        for (std::size_t k = 0; k < advanced.converted.size(); ++k) {
            L.converted[k] += advanced.converted[k]; L.converted_gross[k] += advanced.converted_gross[k];
        }
        s.status.evaporated_kg += advanced.evaporated_kg; s.status.condensed_kg += advanced.condensed_kg;
        world = std::move(advanced.world);
        // Phase change (the production kernel has none): each cell holding a
        // liquid/vapour pair relaxes toward saturation at fixed U and volume.
        step("phase change");
        if (world.settings().numerical_kernel!=MacNumericalKernel::game && !s.pairs.empty()) {
            const auto before = totals(world);
            try {
                // First-order relaxation with a 1 s time constant: a cell moves
                // 1 - exp(-dt / 1 s) of the way to equilibrium per frame, so a
                // superheated pool boils over seconds instead of flashing.
                if (refused_relaxation.load(std::memory_order_relaxed)) throw std::runtime_error("Phase relaxation refused (control)");
                auto relaxed = world.with_phase_relaxation(s.pairs, 1 - std::exp(-seconds / phase_relaxation_time_s));
                if (relaxed.report.evaporated_kg + relaxed.report.condensed_kg > 0) {
                    const auto after = totals(relaxed.world);
                    for (std::size_t k = 0; k < after.size(); ++k) { L.converted[k] += after[k] - before[k]; L.converted_gross[k] += std::abs(after[k] - before[k]); }
                    s.status.evaporated_kg += relaxed.report.evaporated_kg; s.status.condensed_kg += relaxed.report.condensed_kg;
                }
                world = std::move(relaxed.world);
            } catch (const std::domain_error&) { ++s.status.skipped_relaxations; }
            catch (const std::exception& error) {
                // A cell the reference EOS cannot evaluate stops the frame's
                // relaxation (the frame skips it, as for a domain error). The
                // first such world is dumped for replay (MA_COLONY_FAULT_DUMP).
                ++s.status.skipped_relaxations;
                static std::atomic<bool> dumped{false};
                if (!dumped.exchange(true)) {
                    if (const char* path = std::getenv("MA_COLONY_FAULT_DUMP")) dump((std::string(path) + ".relaxation").c_str());
                    if (std::getenv("MA_COLONY_LEDGER_LOG")) { std::fprintf(stderr, "[Mixed Atmospheres] phase relaxation skipped: %s\n", error.what()); std::fflush(stderr); }
                }
            }
        }
        step("settling");
        if(world.settings().numerical_kernel!=MacNumericalKernel::game) try {
            auto settled = world.with_liquid_settling(std::min(.25, 1 - std::exp(-seconds / settling_time_s)));
            s.status.settled_kg += settled.report.moved_kg; world = std::move(settled.world);
        } catch (const std::domain_error&) { ++s.status.skipped_relaxations; }
        step("airflow tiles");
        world = expel_blocked_liquid(s, std::move(world));
        step("conversions");
        world = convert_identities(s, std::move(world), seconds);
        step("trace sink");
        world = sink_traces(s, std::move(world));
        lap(3); step("space sink");
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
        lap(4); step("ledger");
        // Ledgers close: every species and U+K+G against the booked flows.
        const auto now = totals(world);
        double mass_error = 0; std::size_t worst = 0;
        for (std::size_t k = 0; k < now.size(); ++k) {
            const long double expected = L.initial[k] + L.injected[k] - L.extracted[k] - L.sunk[k] - L.trace[k] + L.converted[k];
            const long double scale = L.initial[k] + L.injected[k] + L.extracted[k] + L.sunk[k] + L.trace[k] + L.converted_gross[k];
            if (scale > 0) { const auto e = static_cast<double>(std::abs(now[k] - expected) / scale); if (e > mass_error) { mass_error = e; worst = k; } }
            else if (now[k] != 0) throw std::runtime_error("The colony created an unbooked constituent");
        }
        // MA_COLONY_LEDGER_LOG=1: report each frame that raises the worst
        // relative mass error above 1e-12, with its species (stderr).
        if (mass_error > 1e-12 && mass_error > s.status.maximum_relative_mass_error && std::getenv("MA_COLONY_LEDGER_LOG")) {
            const long double expected = L.initial[worst] + L.injected[worst] - L.extracted[worst] - L.sunk[worst] - L.trace[worst] + L.converted[worst];
            std::fprintf(stderr, "[Mixed Atmospheres] ledger frame %llu (%s): %s relative mass error %.3g (held %.17Lg, booked %.17Lg; injected %.6Lg extracted %.6Lg sunk %.6Lg converted %.6Lg)\n",
                static_cast<unsigned long long>(s.status.completed_frames + 1), doing, cat.materials[worst].game.id.c_str(), mass_error, now[worst], expected,
                L.injected[worst], L.extracted[worst], L.sunk[worst], L.converted[worst]);
            std::fflush(stderr);
        }
        const long double expected_energy = L.initial_energy + L.heat + L.energy_in - L.energy_out - L.energy_sunk - L.energy_trace;
        const long double energy_scale = std::abs(L.initial_energy) + std::abs(L.heat) + std::abs(L.energy_in) + std::abs(L.energy_out) +
            std::abs(L.energy_sunk) + std::abs(L.energy_trace);
        const double energy_error = static_cast<double>(std::abs(energy_of(world) - expected_energy) / energy_scale);
        s.status.maximum_relative_mass_error = std::max(s.status.maximum_relative_mass_error, mass_error);
        s.status.maximum_relative_energy_error = std::max(s.status.maximum_relative_energy_error, energy_error);
        if (mass_error > 1e-8 || energy_error > 1e-6) throw std::runtime_error("The colony exceeded its mass/energy ledger bound");
        (void)registry;
        s.world = std::make_shared<const MacClosedWorld>(std::move(world));
        ++s.status.completed_frames; s.status.native_ticket = ticket; s.status.solver_thread = thread;
        s.status.substeps += advanced.substeps; s.status.rejected_attempts += advanced.rejected_attempts;
        s.status.time_seconds = s.world->time_s(); s.status.last_frame_seconds = seconds;
        s.status.last_advance_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        s.status.maximum_advance_ms = std::max(s.status.maximum_advance_ms, s.status.last_advance_ms);
        step("projection");
        auto projection = project(s, *s.world);
        lap(5); std::copy(std::begin(stage), std::end(stage), s.status.stage_ms);
        step("publication");
        if (stage_log && s.status.completed_frames % 10 == 0) {
            std::string line = "[Mixed Atmospheres] stages frame " + std::to_string(s.status.completed_frames) + ":";
            char item[96];
            for (const auto& [name, ms] : steps) { std::snprintf(item, sizeof item, " %s %.1f", name, ms); line += item; }
            std::snprintf(item, sizeof item, "; heat %zu transfers %zu terrain %zu germs %zu", heat.size(), transfers.size(), terrain.size(), germ_edits.size()); line += item;
            std::fprintf(stderr, "%s\n", line.c_str()); std::fflush(stderr);
        }
        publish(s, std::move(projection));
        // Pipe outcomes become readable only after the physical frame is
        // committed and published; a fault cannot credit an uncommitted edit.
        if (!committed_jobs.empty()) { std::lock_guard q(inputs);
          for (const auto& [token,last] : committed_jobs) pipe_jobs.at(token).last_receipt=last; }
    } catch (const std::exception& error) { fault(s, (std::string(doing) + ": " + error.what()).c_str()); }
      catch (...) { fault(s, "Unknown colony advance failure"); }
}
}

// MACOLN01 checkpoint: header, configuration and material records (exact
// bytes), reservoir, ledger, the MACCLS01 physical image and a SHA-256.
namespace {
struct Writer {
    std::vector<std::uint8_t> bytes;
    void raw(const void* p, std::size_t n) { if (n == 0) return; const auto* b = static_cast<const std::uint8_t*>(p); bytes.insert(bytes.end(), b, b + n); }
    void u8(std::uint8_t v) { raw(&v, 1); }
    void u32(std::uint32_t v) { raw(&v, 4); }
    void u64(std::uint64_t v) { raw(&v, 8); }
    void f32(float v) { raw(&v, 4); }
    void f64(double v) { raw(&v, 8); }
    void ld(long double v) { f64(static_cast<double>(v)); f64(static_cast<double>(v - static_cast<long double>(static_cast<double>(v)))); }
};
struct Reader {
    std::span<const std::uint8_t> bytes; std::size_t at = 0;
    void raw(void* p, std::size_t n) { require(n <= bytes.size() - at, "Truncated colony checkpoint"); if (n != 0) std::memcpy(p, bytes.data() + at, n); at += n; }
    std::uint8_t u8() { std::uint8_t v; raw(&v, 1); return v; }
    std::uint32_t u32() { std::uint32_t v; raw(&v, 4); return v; }
    std::uint64_t u64() { std::uint64_t v; raw(&v, 8); return v; }
    float f32() { float v; raw(&v, 4); return v; }
    double f64() { double v; raw(&v, 8); return v; }
    long double ld() { const double hi = f64(), lo = f64(); return static_cast<long double>(hi) + lo; }
};
constexpr char magic[8] = {'M', 'A', 'C', 'O', 'L', 'N', '0', '1'};
// Version 10 adds world germ evolution (owner mode 1): after the version 9 job
// list, u32 mode, u64 biology frames, the culture clocks (u32 count; u32 cell
// index, u64 identity, u8 mode, u8 ticks, f32 residue each), the queued germ
// edits (u32 count; u32 public cell, u32 identity, f64 change each), the germ
// accounts (u32 count; u64 identity and six long-double pairs each: born,
// died, culled, moved, edited in, edited out) and f64 audit error. Legacy-mode
// owners keep writing version 8/9.
// Version 9 adds canonical moving payloads per durable job, only after explicit
// moving admission. Unused facilities retain exact version8 output.
// Version 8 adds durable pending pipe jobs and their full passives/token watermark.
// Version 7 adds the trace-sink ledger; version 6 the liquid-blocking cells; version 5 the inputs queued at the save; version 4 the phase-change
// conversion ledger; version 3 stored the continuing counters explicitly
// (version 2 the raw status struct).
constexpr std::uint32_t checkpoint_version = 10;
}
std::vector<std::uint8_t> checkpoint() {
    solver.settle(false);
    std::lock_guard lock(lifecycle);
    require(session != nullptr, "No colony owner is active");
    require(session->status.state == 1, "A faulted colony owner cannot be saved");
    const ArithmeticScope arithmetic;
    const auto& s = *session; Writer w;
    // Keep the queue and selected wrapper framing one coherent save snapshot.
    std::unique_lock input_snapshot(inputs);
    const std::uint32_t saved_version=s.germ_mode==1?10:moving_protocol_used?9:8;
    w.raw(magic, 8); w.u32(saved_version); w.raw(&s.config, sizeof(s.config));
    w.u32(static_cast<std::uint32_t>(s.materials.size())); w.raw(s.materials.data(), s.materials.size() * sizeof(MAOniColonyMaterial));
    w.u32(static_cast<std::uint32_t>(s.space.size())); w.raw(s.space.data(), s.space.size());
    w.u32(static_cast<std::uint32_t>(s.blocking.size())); w.raw(s.blocking.data(), s.blocking.size());
    w.u32(static_cast<std::uint32_t>(s.reservoir.size()));
    for (const auto& r : s.reservoir) { w.u64(r.region_cell); w.u64(r.species); w.f64(r.mass_kg); w.f64(r.temperature_k); }
    const auto& L = s.ledger;
    for (const auto* v : {&L.initial, &L.injected, &L.extracted, &L.sunk, &L.converted}) for (const auto x : *v) w.ld(x);
    for (const auto x : {L.initial_energy, L.heat, L.energy_in, L.energy_out, L.energy_sunk}) w.ld(x);
    for (const auto x : L.trace) w.ld(x);
    for (const auto x : {L.energy_trace, L.trace_momentum_x, L.trace_momentum_y}) w.ld(x);
    w.f64(s.status.trace_sink_kg); w.f64(s.status.trace_sink_j);
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
    struct SavedJob { std::uint32_t index; std::uint64_t token; PassiveQuantities passives; std::shared_ptr<const std::vector<std::uint8_t>> moving_wire{}; };
    std::vector<SavedJob> saved_jobs; std::uint64_t saved_job_token=0;
    {
        for (const auto& [token,job] : pipe_jobs) { (void)token; require(job.last_receipt==0,"Drain committed pipe outcomes before saving"); }
        saved_job_token=accepted_job_token;
        for (std::size_t i=0;i<pending_transfers.size();++i) {
            auto t=pending_transfers[i];
            const bool answered = t.callback >= 0 || (t.kind & MA_ONI_COLONY_CONSUMER) != 0;
            if (answered && (t.kind & ~MA_ONI_COLONY_CONSUMER) != MA_ONI_COLONY_INJECT) continue;
            if (const auto job=pending_job_tokens.find(i); job!=pending_job_tokens.end())
                saved_jobs.push_back({static_cast<std::uint32_t>(transfers.size()),job->second,pipe_jobs.at(job->second).passives,pipe_jobs.at(job->second).moving_wire});
            t.callback = -1; t.kind &= ~MA_ONI_COLONY_CONSUMER; transfers.push_back(t);
        }
        require(saved_jobs.size()==pipe_jobs.size(),"Pending pipe jobs are missing from the save queue");
        heat = pending_heat; terrain = pending_terrain; freezes = pending_freezes;
    }
    w.u32(static_cast<std::uint32_t>(transfers.size())); w.raw(transfers.data(), transfers.size() * sizeof(MAOniColonyTransfer));
    w.u32(static_cast<std::uint32_t>(heat.size())); w.raw(heat.data(), heat.size() * sizeof(MAOniColonyHeat));
    w.u32(static_cast<std::uint32_t>(terrain.size())); w.raw(terrain.data(), terrain.size() * sizeof(MAOniColonyTerrain));
    w.u32(static_cast<std::uint32_t>(freezes.size()));
    for (const auto& f : freezes) { w.u32(f.public_cell); w.u32(f.native_index); w.f64(f.mass_kg); }
    w.u64(saved_job_token); w.u32(static_cast<std::uint32_t>(saved_jobs.size()));
    for (const auto& job : saved_jobs) {
        w.u32(job.index); w.u64(job.token); w.u32(static_cast<std::uint32_t>(job.passives.size()));
        for (const auto& passive : job.passives) { w.u64(passive.identity); w.f64(passive.quantity); }
        if(saved_version>=9) {
            w.u32(job.moving_wire?static_cast<std::uint32_t>(job.moving_wire->size()):0);
            if(job.moving_wire)w.raw(job.moving_wire->data(),job.moving_wire->size());
        }
    }
    if (saved_version >= 10) {
        w.u32(s.germ_mode); w.u64(s.biology_frames);
        w.u32(static_cast<std::uint32_t>(s.clocks.size()));
        for (const auto& [key, clock] : s.clocks) {
            w.u32(static_cast<std::uint32_t>(key.first)); w.u64(key.second); w.u8(clock.mode); w.u8(clock.ticks); w.f32(clock.residue);
        }
        w.u32(static_cast<std::uint32_t>(pending_germs.size()));
        for (const auto& g : pending_germs) { w.u32(g.public_cell); w.u32(g.germ); w.f64(g.change); }
        w.u32(static_cast<std::uint32_t>(s.accounts.size()));
        for (const auto& [identity, a] : s.accounts) {
            w.u64(identity);
            for (const auto x : {a.born, a.died, a.culled, a.moved, a.edited_in, a.edited_out}) w.ld(x);
        }
        w.f64(s.biology_error);
    }
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
    char head[8]; r.raw(head, 8); require(std::memcmp(head, magic, 8) == 0, "Unsupported colony checkpoint");
    const auto version = r.u32(); require(version >= 6 && version <= checkpoint_version, "Unsupported colony checkpoint");
    MAOniColonyConfig saved{}; r.raw(&saved, sizeof(saved));
    require(std::memcmp(&saved, &config, sizeof(config)) == 0, "Colony checkpoint geometry or physics differs from the loaded game");
    const auto count = r.u32(); require(count == materials.size(), "Colony checkpoint catalogue differs from the running game");
    std::vector<MAOniColonyMaterial> stored(count); r.raw(stored.data(), count * sizeof(MAOniColonyMaterial));
    require(std::memcmp(stored.data(), materials.data(), count * sizeof(MAOniColonyMaterial)) == 0, "Colony checkpoint catalogue differs from the running game");
    auto next = std::make_unique<Session>();
    next->config = config; next->materials = std::move(stored); next->catalogue = catalogue_of(next->materials);
    const auto cells = r.u32(); require(cells == static_cast<std::size_t>(config.width) * config.height, "Colony checkpoint space mask differs");
    next->space.resize(cells); r.raw(next->space.data(), cells);
    require(r.u32() == cells, "Colony checkpoint blocking mask differs"); next->blocking.resize(cells); r.raw(next->blocking.data(), cells);
    const auto entries = r.u32();
    for (std::uint32_t k = 0; k < entries; ++k) {
        ReservoirEntry e; e.region_cell = r.u64(); e.species = r.u64(); e.mass_kg = r.f64(); e.temperature_k = r.f64();
        require(e.species < next->catalogue->registry.size(), "Colony checkpoint reservoir names an unknown species"); next->reservoir.push_back(e);
    }
    const auto K = next->catalogue->registry.size(); auto& L = next->ledger;
    for (auto* v : {&L.initial, &L.injected, &L.extracted, &L.sunk, &L.converted}) { v->resize(K); for (auto& x : *v) x = r.ld(); }
    L.converted_gross.resize(K); for (std::size_t k = 0; k < K; ++k) L.converted_gross[k] = std::abs(L.converted[k]);
    next->pairs = with_critical(*next->catalogue, mac_saturation_pairs(next->catalogue->registry, *next->catalogue->eos));
    for (auto* x : {&L.initial_energy, &L.heat, &L.energy_in, &L.energy_out, &L.energy_sunk}) *x = r.ld();
    L.trace.assign(K, 0);
    double trace_kg = 0, trace_j = 0;
    if (version >= 7) {
        for (auto& x : L.trace) x = r.ld();
        for (auto* x : {&L.energy_trace, &L.trace_momentum_x, &L.trace_momentum_y}) *x = r.ld();
        trace_kg = r.f64(); trace_j = r.f64();
    }
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
    std::map<std::size_t,std::uint64_t> restored_tokens; std::map<std::uint64_t,PipeJob> restored_jobs; std::uint64_t restored_watermark=0;
    if (version>=8) {
        restored_watermark=r.u64(); const auto job_count=r.u32(); require(job_count<=transfers.size(),"Too many pending pipe jobs");
        std::size_t previous_index=0;
        for (std::uint32_t i=0;i<job_count;++i) {
            const auto index=r.u32(); const auto token=r.u64(); const auto n=r.u32();
            require(index<transfers.size() && (i==0 || index>previous_index) && token && token<=restored_watermark &&
                !restored_jobs.contains(token) && n<=65537 && n<=(r.bytes.size()-r.at)/16,"Invalid saved pipe job index/token/passives");
            previous_index=index; const auto& t=transfers[index]; (void)rect_index(config,t.public_cell);
            require(t.callback==-1 && t.disease==0 && t.disease_count==0 && t.radius<=64 &&
                std::isfinite(t.mass_kg) && t.mass_kg>=0 && std::isfinite(t.temperature_k) && t.temperature_k>=0 &&
                (t.kind==MA_ONI_COLONY_INJECT || t.kind==MA_ONI_COLONY_EXTRACT_GAS || t.kind==MA_ONI_COLONY_EXTRACT_LIQUID) &&
                (t.kind==MA_ONI_COLONY_INJECT || n==0),"Invalid saved pipe transfer");
            PipeJob job{t,{}}; job.passives.reserve(n); std::uint64_t previous=0;
            for (std::uint32_t j=0;j<n;++j) {
                const auto identity=r.u64(); const auto quantity=r.f64();
                require(identity>previous && std::isfinite(quantity) && quantity>0,"Invalid saved pipe passive quantity");
                previous=identity; job.passives.push_back({identity,quantity});
            }
            validate_passive_quantities(job.passives);
            if(version>=9) {
                const auto bytes=r.u32();require(bytes<=detail::maximum_parcel_wire_bytes && bytes<=r.bytes.size()-r.at,"Invalid saved moving payload size");
                if(bytes) {
                    const auto wire=r.bytes.subspan(r.at,bytes);const auto request=detail::decode_moving_parcels(wire);
                    for(const auto& item : request)(void)rect_index(config,item.public_cell);
                    require(t.kind==MA_ONI_COLONY_INJECT && t.mass_kg==0 && t.temperature_k==0 && t.radius==0 && job.passives.empty() &&
                        t.public_cell==request.front().public_cell && t.native_index==request.front().native_index,"Saved moving marker differs from payload");
                    job.moving_wire=std::make_shared<const std::vector<std::uint8_t>>(wire.begin(),wire.end());r.at+=bytes;
                }
            }
            restored_tokens.emplace(index,token); restored_jobs.emplace(token,std::move(job));
        }
    }
    std::vector<GermEdit> restored_germs;
    if (version >= 10) {
        next->germ_mode = r.u32(); require(next->germ_mode == 1, "Unsupported saved world germ mode");
        require(!disease_table.empty(), "This save evolves world germs and needs the game's disease table");
        next->biology_frames = r.u64();
        const auto clocks = r.u32(); require(clocks <= (r.bytes.size() - r.at) / 18, "Invalid saved germ clock count");
        std::pair<std::size_t, std::uint64_t> previous_key{0, 0};
        for (std::uint32_t k = 0; k < clocks; ++k) {
            const std::size_t cell = r.u32(); const auto identity = r.u64();
            biology::Clock clock; clock.mode = r.u8(); clock.ticks = r.u8(); clock.residue = r.f32();
            const std::pair key{cell, identity};
            require(cell < cells && biology::disease_identity(identity) && (k == 0 || key > previous_key) && clock.mode <= biology::expected_mode &&
                clock.ticks <= biology::maximum_ticks && std::isfinite(clock.residue) && std::abs(clock.residue) < 1 &&
                (clock.mode == biology::native_mode || clock.residue == 0), "Invalid saved germ clock");
            next->clocks.emplace(key, clock); previous_key = key;
        }
        const auto germs = r.u32(); require(germs <= 1u << 20 && germs <= (r.bytes.size() - r.at) / 16, "Invalid saved germ edit count");
        for (std::uint32_t k = 0; k < germs; ++k) {
            GermEdit g; g.public_cell = r.u32(); g.germ = r.u32(); g.change = r.f64();
            (void)rect_index(config, g.public_cell);
            require(biology::disease_identity(g.germ) && std::isfinite(g.change) && g.change != 0, "Invalid saved germ edit");
            restored_germs.push_back(g);
        }
        const auto identities = r.u32(); require(identities <= 256, "Invalid saved germ account count");
        std::uint64_t previous_identity = 0;
        for (std::uint32_t k = 0; k < identities; ++k) {
            const auto identity = r.u64();
            require(biology::disease_identity(identity) && identity > previous_identity, "Invalid saved germ account identity");
            biology::Account a;
            for (auto* x : {&a.born, &a.died, &a.culled, &a.moved, &a.edited_in, &a.edited_out}) { *x = r.ld(); require(std::isfinite(static_cast<double>(*x)), "Invalid saved germ account"); }
            next->accounts.emplace(identity, a); previous_identity = identity;
        }
        next->biology_error = r.f64(); require(std::isfinite(next->biology_error) && next->biology_error >= 0, "Invalid saved germ audit");
    }
    const auto physical = r.u64(); require(physical == r.bytes.size() - r.at, "Colony checkpoint physical image length differs");
    next->world = std::make_shared<const MacClosedWorld>(MacClosedWorld::from_checkpoint(next->catalogue->registry, next->catalogue->eos,
        r.bytes.subspan(r.at, static_cast<std::size_t>(physical))));
    require(((config.flags&MA_ONI_COLONY_GAME_KERNEL)!=0)==(next->world->settings().numerical_kernel==MacNumericalKernel::game),"Saved core kernel differs from saved colony flags");
    activate(std::move(next), thread);
    // Ledger bounds and counters continue from the saved session.
    auto& st = session->status;
    st.completed_frames = saved_status.completed_frames; st.substeps = saved_status.substeps; st.rejected_attempts = saved_status.rejected_attempts;
    st.committed_transfers = saved_status.committed_transfers; st.committed_heat = saved_status.committed_heat;
    st.maximum_relative_mass_error = saved_status.maximum_relative_mass_error; st.maximum_relative_energy_error = saved_status.maximum_relative_energy_error;
    st.space_sink_kg = saved_status.space_sink_kg; st.injected_kg = saved_status.injected_kg; st.extracted_kg = saved_status.extracted_kg;
    st.opened_cells = saved_status.opened_cells; st.closed_cells = saved_status.closed_cells;
    st.evaporated_kg = saved_status.evaporated_kg; st.condensed_kg = saved_status.condensed_kg;
    st.trace_sink_kg = trace_kg; st.trace_sink_j = trace_j;
    {
        std::lock_guard q(inputs);
        pending_transfers = std::move(transfers); pending_heat = std::move(heat); pending_terrain = std::move(terrain); pending_freezes = std::move(freezes);
        pending_germs = std::move(restored_germs);
        pending_job_tokens=std::move(restored_tokens); pipe_jobs=std::move(restored_jobs); accepted_job_token=restored_watermark;
        // Pending durable jobs must publish full outcomes even before the
        // managed client has finished rebuilding its port objects.
        precise_receipts_enabled=!pipe_jobs.empty();moving_protocol_used=version>=9;
        // A wall queued to open is opening, as before the save.
        for (const auto& e : pending_terrain) if (e.kind == MA_ONI_COLONY_OPEN) {
            std::uint8_t closed = 0; owned[rect_index(geometry, e.public_cell)].compare_exchange_strong(closed, 3, std::memory_order_relaxed);
        }
    }
    publish(*session);
}
}
