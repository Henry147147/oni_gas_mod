#pragma once
// Colony owner internals (docs/colony-owner.md). The C ABI in oni_colony.h
// forwards here; the native detours call active/suppress/advance.
#include "mixed_atmospheres/oni_colony.h"
#include <cstdint>
#include <span>
#include <vector>
#include <utility>

namespace ma::oni::colony {
void start(const MAOniColonyConfig&, std::span<const MAOniColonyMaterial>, std::span<const MAOniColonyCell>, std::uint64_t thread);
void restore(const MAOniColonyConfig&, std::span<const MAOniColonyMaterial>, std::span<const std::uint8_t>, std::uint64_t thread);
void stop() noexcept;
[[nodiscard]] bool active() noexcept;
[[nodiscard]] bool busy() noexcept;
[[nodiscard]] MAOniColonyStatus status();
void copy(MAOniColonyStatus&, MAOniColonyCellView*, std::uint32_t capacity);
bool copy_changed(std::uint64_t session,std::uint64_t frame,MAOniColonyStatus&,MAOniColonyCellView*,std::uint32_t capacity);
std::uint32_t composition(std::uint32_t public_cell, std::uint32_t* indices, double* masses, std::uint32_t capacity);
void queue_transfers(std::span<const MAOniColonyTransfer>);
void queue_actor_batch(std::span<const MAOniColonyTransfer>,std::span<const MAOniColonyActorJob>,std::span<const MAOniColonyPassive>);
void queue_heat(std::span<const MAOniColonyHeat>);
void queue_terrain(std::span<const MAOniColonyTerrain>);
std::uint32_t copy_receipts(MAOniColonyReceipt*, std::uint32_t capacity);
void ack_receipts(std::uint64_t through);
void enable_precise_receipts(bool enabled);
std::uint32_t copy_precise_receipt(std::uint64_t sequence, std::uint8_t*, std::uint32_t capacity);
std::pair<std::uint32_t,std::uint64_t> queue_pipe_job(std::uint64_t, const MAOniColonyTransfer&, std::span<const MAOniColonyPassive>);
std::pair<std::uint32_t,std::uint64_t> pipe_job_state(std::uint64_t);
std::uint64_t receipt_job(std::uint64_t);
[[nodiscard]] std::vector<std::uint8_t> checkpoint();
std::uint32_t species(MAOniColonySpecies* out, std::uint32_t capacity);
// Readings and constituents of the latest completed frame; returns the frame.
std::uint64_t readings(std::vector<MAOniColonyReading>&, std::vector<MAOniColonyConstituent>&);
void sample(std::span<const std::uint32_t>,MAOniColonyStatus&,std::vector<MAOniColonySampleCell>&,std::vector<MAOniColonySamplePart>&,double* booked_mass=nullptr);
void sample_history(std::uint32_t retained_frames);
void sample_frame(std::uint64_t,std::span<const std::uint32_t>,MAOniColonyStatus&,std::vector<MAOniColonySampleCell>&,
    std::vector<MAOniColonySamplePart>&,double* booked_mass,double* booked_heat);
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
// frame's UpdateData, write() puts the latest completed owner cells into both
// arrays; after it, capture() books every change native code made to them as
// owner heat or transfers.
void write_shadow(const NativeCells&) noexcept;
void capture_shadow(const NativeCells&) noexcept;
[[nodiscard]] bool owns(int native_cell) noexcept;
// A native cell next to an owned one (a dug cell before the owner opens it):
// native gas flow there would move owned gas outside the owner's books.
[[nodiscard]] bool beside_owned(int native_cell) noexcept;
// A native state transition of an owned cell runs only when it freezes the
// cell (a solid target); fluid targets are the owner's phase change.
[[nodiscard]] bool allow_native_transition(int native_cell, std::uint16_t element, float temperature) noexcept;
// Native worker entry points. Never throw; a failure faults the session.
// advance() hands the frame to the solver thread and returns at once.
[[nodiscard]] bool suppress(std::uint32_t hook, int native_cell) noexcept;
void advance(std::uint64_t ticket, double seconds, std::uint64_t thread) noexcept;
// Returns once the solver thread has no step running or waiting.
void wait() noexcept;
}
