#pragma once
// Colony owner internals (docs/colony-owner.md). The C ABI in oni_colony.h
// forwards here; the native detours call active/suppress/advance.
#include "mixed_atmospheres/oni_colony.h"
#include <array>
#include <cstdint>
#include <span>
#include <vector>
#include <utility>
#include <memory>

namespace ma::experimental { class MacClosedWorld; }
namespace ma::oni::colony {
void start(const MAOniColonyConfig&, std::span<const MAOniColonyMaterial>, std::span<const MAOniColonyCell>, std::uint64_t thread);
void restore(const MAOniColonyConfig&, std::span<const MAOniColonyMaterial>, std::span<const std::uint8_t>, std::uint64_t thread);
void stop() noexcept;
void fault_from_adapter(const char* reason);
// Native worker form: never throws; an unrecorded fault refuses the next frame.
void fault_native(const char* reason) noexcept;
[[nodiscard]] bool active() noexcept;
[[nodiscard]] bool busy() noexcept;
// True when the owner claims each frame's inputs at submission (activity
// tiers), so messages applied by a later zero-time native frame cannot reach
// a frame that is still running.
[[nodiscard]] bool claims_inputs_at_submission() noexcept;
// Controls: the idle-time private state copy a frame starts from (on by
// default) and how many frames used one.
void set_world_preparation(bool enabled) noexcept;
[[nodiscard]] std::uint64_t prepared_world_frames() noexcept;
[[nodiscard]] MAOniColonyStatus status();
[[nodiscard]] MAOniColonyParcelContext parcel_context();
void copy(MAOniColonyStatus&, MAOniColonyCellView*, std::uint32_t capacity);
bool copy_changed(std::uint64_t session,std::uint64_t frame,MAOniColonyStatus&,MAOniColonyCellView*,std::uint32_t capacity);
std::uint32_t composition(std::uint32_t public_cell, std::uint32_t* indices, double* masses, std::uint32_t capacity);
void queue_transfers(std::span<const MAOniColonyTransfer>);
void queue_actor_batch(std::span<const MAOniColonyTransfer>,std::span<const MAOniColonyActorJob>,std::span<const MAOniColonyPassive>);
void queue_heat(std::span<const MAOniColonyHeat>);
void queue_terrain(std::span<const MAOniColonyTerrain>);
std::uint64_t queue_activity(std::uint64_t session,double seconds,std::span<const std::uint32_t> actors,
    std::span<const std::uint32_t> touched);
void claim_activity_frame(std::uint64_t session,std::uint64_t sequence,double seconds);
void set_activity_tiers(std::uint64_t session,bool enabled);
bool copy_activity(MAOniColonyActivityState&);
bool sample_activity(std::uint64_t session,std::uint64_t frame,std::span<const std::uint32_t> cells,
    MAOniColonyStatus&,std::span<MAOniColonyActivityCell>);
// Actual native body/contact heat only; never waits for lifecycle/solver work.
void wake_native_cell(int native_cell) noexcept;
// The exact catalogue operand used to account a native shadow heat delta.
[[nodiscard]] double native_specific_heat(std::uint16_t native_element) noexcept;
struct NativeHeatProjection {std::uint16_t element;float mass,temperature;bool valid;};
// Immutable origin written by write_shadow, not a native array which later
// native operations may already have changed before a contact is observed.
[[nodiscard]] NativeHeatProjection native_heat_projection(int native_cell) noexcept;
void set_space_cells(std::span<const std::uint32_t>,bool space);
std::uint32_t copy_receipts(MAOniColonyReceipt*, std::uint32_t capacity);
void ack_receipts(std::uint64_t through);
// Byproduct batches (docs/colony-owner.md#byproduct-batches). release: every
// pending batch becomes a receipt of the current frame (after wait(); the save
// barrier). copy: the pending batches as receipts with sequence 0 and the
// frame of their latest byproduct, in key order.
std::uint32_t release_byproducts();
std::uint32_t copy_byproduct_batches(MAOniColonyReceipt*, std::uint32_t capacity);
void enable_precise_receipts(bool enabled);
std::uint32_t copy_precise_receipt(std::uint64_t sequence, std::uint8_t*, std::uint32_t capacity);
std::pair<std::uint32_t,std::uint64_t> queue_pipe_job(std::uint64_t, const MAOniColonyTransfer&, std::span<const MAOniColonyPassive>);
std::pair<std::uint32_t,std::uint64_t> queue_moving_parcel(std::uint64_t session,std::uint64_t token,std::span<const std::uint8_t>);
std::uint32_t copy_moving_outcome(std::uint64_t,std::uint8_t*,std::uint32_t);
std::pair<std::uint32_t,std::uint64_t> pipe_job_state(std::uint64_t);
std::uint64_t receipt_job(std::uint64_t);
// World germ evolution (docs/colony-owner.md#germs): the game's disease table
// (before start/restore), its own cell germ edits, and the germ accounts.
void set_diseases(std::span<const MAOniColonyDisease>, std::span<const MAOniColonyDiseaseGrowth>, std::uint32_t elements);
// Profile-6 options for the next new colony (docs/settings.md): defaults,
// the pre-start setter (null resets) and the running colony's saved values
// (false for a colony that is not a game kernel).
[[nodiscard]] MAOniColonyOptions default_options() noexcept;
// Execution preference, independent of saves. Zero selects physical cores.
[[nodiscard]] unsigned physical_core_count() noexcept;
void set_worker_count(unsigned);
void set_options(const MAOniColonyOptions*);
bool copy_options(MAOniColonyOptions&);
void queue_germs(std::span<const MAOniColonyGermEdit>);
void set_radiation(std::span<const std::uint32_t> cells, std::span<const float> rads);
void germ_accounts(MAOniColonyBiology&, std::vector<MAOniColonyBiologyAccount>&);
// Game-kernel pressure refinements of this session by criterion (diagnostic; never saved):
// pressure solve failure, unconverged pressure, negative or non-finite pressure, swept volume above 0.4 cell.
[[nodiscard]] std::array<std::uint64_t, 4> rejection_counts();
[[nodiscard]] std::vector<std::uint8_t> checkpoint();
// Callback-owned requests across a save (oni_colony.h, owner wrapper 13).
[[nodiscard]] std::vector<MAOniColonyCallbackRecord> checkpoint_callbacks();
std::pair<std::uint32_t,std::uint64_t> callback_rebind_state();
void rebind_callbacks(std::span<const MAOniColonyCallbackRebind>);
// Replay only (ma_oni_colony_replay): a native shadow's frozen-cell inputs.
struct ReplayFreeze { std::uint32_t public_cell, native_index; double mass_kg; };
void queue_replay_freezes(std::span<const ReplayFreeze>);
void queue_replay_activity_native(std::uint64_t session,std::uint64_t ticket,
    std::span<const MAOniColonyTransfer>,std::span<const MAOniColonyHeat>,std::span<const MAOniColonyTerrain>,
    std::span<const ReplayFreeze>,std::span<const MAOniColonyGermEdit>,std::span<const std::uint32_t> touched);
// Latest published owner-held precision inventory (MAPREC01), separate from grid readings.
[[nodiscard]] std::vector<std::uint8_t> precision_inventory();
std::uint32_t species(MAOniColonySpecies* out, std::uint32_t capacity);
// Readings and constituents of the latest completed frame; returns the frame.
std::uint64_t readings(std::vector<MAOniColonyReading>&, std::vector<MAOniColonyConstituent>&);
// Diagnostics: projection blocks, and cells of changed blocks, taken
// unchanged from the previous publication.
std::uint64_t reused_projection_blocks() noexcept;
std::uint64_t reused_projection_cells() noexcept;
// Control switch (MA_COLONY_PROJECTION_FRESH=1 starts it off): false projects
// every cell afresh. Results must not depend on it.
void set_projection_reuse(bool) noexcept;
struct ReadingSnapshot {
    std::shared_ptr<const void> lifetime;
    std::span<const MAOniColonyReading> readings;
    std::span<const MAOniColonyConstituent> parts;
    std::uint64_t frame=0;
};
[[nodiscard]] ReadingSnapshot reading_snapshot();
void sample(std::span<const std::uint32_t>,MAOniColonyStatus&,std::vector<MAOniColonySampleCell>&,std::vector<MAOniColonySamplePart>&,double* booked_mass=nullptr);
bool read_passives(std::uint64_t session,std::uint64_t frame,std::span<const std::uint32_t>,
    MAOniColonyPassiveStamp&,std::vector<MAOniColonyPassiveRow>&);
void sample_history(std::uint32_t retained_frames);
void sample_frame(std::uint64_t,std::span<const std::uint32_t>,MAOniColonyStatus&,std::vector<MAOniColonySampleCell>&,
    std::vector<MAOniColonySamplePart>&,double* booked_mass,double* booked_heat);
[[nodiscard]] MAOniColonyEnergySample energy_components(const experimental::MacClosedWorld&);
[[nodiscard]] MAOniColonyEnergySample region_energy_components(const experimental::MacClosedWorld&,std::span<const std::size_t>);
[[nodiscard]] MAOniColonyEnergySample sample_energy(std::uint64_t session,std::uint64_t frame);
void sample_native_frame(std::uint64_t session,std::uint64_t frame,std::span<const std::uint32_t>,
    MAOniColonyStatus&,std::vector<MAOniColonyNativeSampleCell>&);
[[nodiscard]] MAOniColonyRegionEnergySample sample_region_energy(std::uint64_t session,std::uint64_t frame,
    std::span<const std::uint32_t>);
double liquid_density(std::span<const MAOniColonyMaterial> materials, std::uint32_t native_index, double pressure_pa);
// Control hook: the temperature read for one cell of the latest frame with
// one constituent replaced by trace_kg.
double trace_reading_temperature(std::uint32_t public_cell, std::uint32_t native_index, double trace_kg);
// Control hook: the projection treats this cell's reading as refused (-1 clears).
void refuse_reading(std::int64_t public_cell);
// Control hook: a terrain edit at this cell throws (-1 clears).
void refuse_terrain(std::int64_t public_cell);
// Control hook: closing this public cell displaces into a cell the EOS refuses (-1 clears).
void refuse_displacement(std::int64_t public_cell);
// Control hook: heat edits at this cell are refused (-1 clears).
void refuse_heat(std::int64_t public_cell);
// Control hook: the phase relaxation throws while set.
void refuse_relaxation(bool refused);
// Native cell arrays of one SimData: [0] the frame's old cells, [1] updated.
struct NativeCells {
    std::uint16_t* element[2]{}; float* mass[2]{}; float* temperature[2]{};
    // Germs (docs/colony-owner.md#germs); null when the layout is unknown.
    std::uint8_t* disease[2]{}; std::int32_t* disease_count[2]{};
    std::uint32_t pitch = 0, rows = 0;
};
// Native shadow (docs/colony-owner.md#native-bodies). Before a positive
// frame's UpdateData, write() waits for the preceding owner step and puts its
// completed cells into both arrays; after it, capture() books every native change as
// owner heat or transfers.
void write_shadow(const NativeCells&) noexcept;
struct NativeHeatAdjustment {
    std::int32_t native_cell;
    std::uint16_t element;
    std::uint32_t mass_bits,initial_temperature_bits,final_temperature_bits;
    double joules;
};
void capture_shadow(const NativeCells&,std::uint64_t native_ticket=0,std::span<const NativeHeatAdjustment> paired_heat={}) noexcept;
// Observe a native solid before its transition/postprocess work. Capture
// resolves any displaced projection at the end of the containing update.
void observe_native_solid(const NativeCells&, int source) noexcept;
[[nodiscard]] bool owns(int native_cell) noexcept;
// A native cell next to an owned one (a dug cell before the owner opens it):
// native gas flow there would move owned gas outside the owner's books.
// Solid observations also include diagonal candidates; fluid fences do not.
[[nodiscard]] bool beside_owned(int native_cell, bool diagonals = false) noexcept;
// The imported catalogue contains fluids only. Use the updated native
// element at a postprocess source to distinguish transient fluid from solids.
[[nodiscard]] bool is_fluid_element(std::uint16_t native_element) noexcept;
// A native state transition of an owned cell runs only when it freezes the
// cell (a solid target); fluid targets are the owner's phase change.
[[nodiscard]] bool allow_native_transition(int native_cell, std::uint16_t element, float temperature) noexcept;
// Native worker entry points. Never throw; a failure faults the session.
// advance() hands the frame to the solver thread and returns at once.
[[nodiscard]] bool suppress(std::uint32_t hook, int native_cell) noexcept;
void advance(std::uint64_t ticket, double seconds, std::uint64_t thread) noexcept;
// Returns once the solver thread has no step running or waiting.
void wait() noexcept;
// Lifecycle boundary only, with the native submitter fenced. Apply one batch
// of queued edits without flow, clock or biology growth. Return deferred edits.
std::uint32_t flush_inputs(std::uint32_t* submitted=nullptr);
// A zero-time native update must project lifecycle edits into its shadow too.
[[nodiscard]] bool needs_shadow_refresh() noexcept;
std::pair<std::uint64_t,std::uint32_t> prepare_retirement(std::uint32_t origin,std::uint32_t width,std::uint32_t height);
std::uint32_t copy_retirement(std::uint64_t token,std::uint8_t*,std::uint32_t capacity);
void commit_retirement(std::uint64_t token);
void cancel_retirement(std::uint64_t token);
void finish_retirement(std::uint32_t origin,std::uint32_t width,std::uint32_t height);
}
