#pragma once
#include "mixed_atmospheres/mac_closed_world.hpp"
namespace ma::experimental {
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
                                         std::span<const std::uint8_t>);
  static MacTraceSink trace(const MacClosedWorld &, std::span<const std::uint8_t> eligible = {});
  // PRIVATE execution-only preparation for the owner heat retry loop.
  // Copy only shared physical state, leaving revision/time/totals/metadata intact.
  // owned_chunks (one flag per cell chunk, optional): chunks to copy now
  // rather than at their first write.
  static bool detach_heat_state(MacClosedWorld &, std::span<const std::uint8_t> owned_chunks = {});
  // Diagnostics: the stage times of this thread's last game advance.
  static MacClosedAdvanceReport::GameStages &last_stages() noexcept {
    thread_local MacClosedAdvanceReport::GameStages stages;
    return stages;
  }
  // PRIVATE test observations; inline, immutable, no extra object fields.
  static const void* heat_totals_identity(const MacClosedWorld &source) noexcept { return source.game_totals_.get(); }
  static const void* heat_activity_identity(const MacClosedWorld &source) noexcept { return source.game_activity_.get(); }
  static MacClosedWorld heat(const MacClosedWorld &,
                             std::span<const MacCellHeat>);
  static MacFluidTransferResult transfers(const MacClosedWorld &,
                                          std::span<const MacFluidTransfer>,
                                          MacFluidTransferTopology);
  // In place when no other world shares the source's state; the result is
  // that of the copying forms, and a refused edit leaves the source as it was.
  static MacClosedWorld heat(MacClosedWorld &&, std::span<const MacCellHeat>);
  static MacFluidTransferResult transfers(MacClosedWorld &&,
                                          std::span<const MacFluidTransfer>,
                                          MacFluidTransferTopology);
  static MacClosedWorld terrain(const MacClosedWorld &,
                                std::span<const std::size_t>, bool open);
  static MacClosedWorld faces(const MacClosedWorld &, std::vector<std::uint8_t>,
                              std::vector<std::uint8_t>);
};
} // namespace ma::experimental
