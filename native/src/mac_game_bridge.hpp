#pragma once
#include "mixed_atmospheres/mac_closed_world.hpp"
namespace ma::experimental {
struct MacGameKernelAccess {
  static MacFaceInertias inertias(const SpeciesRegistry &,
                                  const MacTransportState &);
  static void validate(const MacClosedWorld &, MacTransportState &);
  static ReferenceEquilibriumState reading(const MacClosedWorld &,
                                           const MacMaterialCell &);
  static void initialize(const SpeciesRegistry &, const FluidThermodynamics &,
                         MacTransportState &, std::span<const double>,
                         const MacClosedSettings &);
  static MacClosedAdvance advance(const MacClosedWorld &, double, std::size_t);
  static MacTraceSink trace(const MacClosedWorld &);
  static MacClosedWorld heat(const MacClosedWorld &,
                             std::span<const MacCellHeat>);
  static MacFluidTransferResult transfers(const MacClosedWorld &,
                                          std::span<const MacFluidTransfer>,
                                          MacFluidTransferTopology);
  static MacClosedWorld terrain(const MacClosedWorld &,
                                std::span<const std::size_t>, bool open);
  static MacClosedWorld faces(const MacClosedWorld &, std::vector<std::uint8_t>,
                              std::vector<std::uint8_t>);
};
} // namespace ma::experimental
