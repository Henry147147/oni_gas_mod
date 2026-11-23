#pragma once
#include "mixed_atmospheres/mac_closed_world.hpp"
#include <cstdint>
namespace ma::experimental {
// What portion of the colony's fluid law this owner frame schedules.
enum class MacGameFlowFrame { normal, held, sampled };
struct MacGameKernelAccess {
  static MacFaceInertias inertias(const SpeciesRegistry &,
                                  const MacTransportState &);
  static void validate(const MacClosedWorld &, MacTransportState &);
  static ReferenceEquilibriumState reading(const MacClosedWorld &,
                                           const MacMaterialCell &);
  // reading() into out's storage; a presentation reading leaves out the
  // entropy and specific energies (zero and empty), every other field equal.
  static void reading_into(const MacClosedWorld &, const MacMaterialCell &,
                           ReferenceEquilibriumState &out, bool presentation);
  static void initialize(const SpeciesRegistry &, const FluidThermodynamics &,
                         MacTransportState &, std::span<const double>,
                         const MacClosedSettings &);
  static MacClosedAdvance advance(const MacClosedWorld &, double, std::size_t);
  // Call-scoped owner boundary law; ordinary advance remains unguarded.
  static MacClosedAdvance advance_guarded(const MacClosedWorld &, double,
                                         std::size_t,
                                         std::span<const std::uint8_t>,
                                         MacGameFlowFrame = MacGameFlowFrame::normal);
  static MacTraceSink trace(const MacClosedWorld &, std::span<const std::uint8_t> eligible = {});
  // In place when no other world shares the source's state; the result is
  // that of the copying form.
  static MacTraceSink trace(MacClosedWorld &&, std::span<const std::uint8_t> eligible = {});
  // PRIVATE execution-only preparation for the owner heat retry loop.
  // Copy only shared physical state, leaving revision/time/totals/metadata intact.
  // owned_chunks (one flag per cell chunk, optional): chunks to copy now
  // rather than at their first write.
  static bool detach_heat_state(MacClosedWorld &, std::span<const std::uint8_t> owned_chunks = {});
  static MacClosedWorld flow_return(const MacClosedWorld &, std::span<const MacFlowReturn>, long double *);
  static MacClosedWorld flow_return(MacClosedWorld &&, std::span<const MacFlowReturn>, long double *);
  // MacClosedWorld::with_carried_passives, in place when no other world
  // shares the source's state; the result is the copying form's.
  static MacClosedWorld carried_passives(MacClosedWorld &&);
  // Diagnostics: the stage times of this thread's last game advance.
  static MacClosedAdvanceReport::GameStages &last_stages() noexcept {
    thread_local MacClosedAdvanceReport::GameStages stages;
    return stages;
  }
  // Space venting (mac_game/flow_local.hpp, Flow::set_sinks): the cells the
  // owner empties every frame and the speed cap of their faces, for this
  // thread's following game advances. Empty or zero: off. Never saved.
  struct Sinks { std::vector<std::uint8_t> cells; double speed = 0; };
  static Sinks &sinks() noexcept {
    thread_local Sinks value;
    return value;
  }
  // Face forces (mac_game/pressure_be.hpp, docs/face-forces.md) for this
  // thread's following game advances, each of which reports the work every
  // force did (MacClosedAdvanceReport::game_face_force_work_j). Empty: none,
  // and then an advance computes exactly what it did before forces existed.
  // Never saved.
  static std::vector<MacGameFaceForce> &face_forces() noexcept {
    thread_local std::vector<MacGameFaceForce> value;
    return value;
  }
  // PRIVATE test observations; inline, immutable, no extra object fields.
  static const void* heat_totals_identity(const MacClosedWorld &source) noexcept { return source.game_totals_.get(); }
  static const void* heat_activity_identity(const MacClosedWorld &source) noexcept { return source.game_activity_.get(); }
  static MacClosedWorld heat(const MacClosedWorld &,
                             std::span<const MacCellHeat>);
  // The edit (its index in the batch) at which this thread's last game heat
  // batch failed, or SIZE_MAX when it did not fail at an edit: the first
  // edit that fails when the batch's edits run one by one.
  static std::size_t &heat_failure() noexcept {
    thread_local std::size_t index = SIZE_MAX;
    return index;
  }
  static MacFluidTransferResult transfers(const MacClosedWorld &,
                                          std::span<const MacFluidTransfer>,
                                          MacFluidTransferTopology,
                                          bool until_refused = false);
  // In place when no other world shares the source's state; the result is
  // that of the copying forms, and a refused edit leaves the source as it was.
  static MacClosedWorld heat(MacClosedWorld &&, std::span<const MacCellHeat>);
  static MacFluidTransferResult transfers(MacClosedWorld &&,
                                          std::span<const MacFluidTransfer>,
                                          MacFluidTransferTopology,
                                          bool until_refused = false);
  static MacClosedWorld terrain(const MacClosedWorld &,
                                std::span<const std::size_t>, bool open);
  static MacClosedWorld faces(const MacClosedWorld &, std::vector<std::uint8_t>,
                              std::vector<std::uint8_t>);
};
} // namespace ma::experimental
