#pragma once
// Execution diagnostics of the colony owner's flow stage for the performance
// tools (docs/performance-acceptance.md). Off unless a tool enables them;
// nothing here enters the owner's state, its saves or its audits.
#include "mac_game/region_samples.hpp"
#include "mixed_atmospheres/mac_closed_world.hpp"
#include <cstdint>
#include <vector>
namespace ma::oni::colony {
struct FlowDiagnostics {
    std::uint64_t frame = 0; // the owner frame these belong to (zero: none yet)
    ma::experimental::MacClosedAdvanceReport::GameStages stages;
    std::vector<ma::experimental::game_kernel::region_samples::Sample> regions;
};
void enable_flow_diagnostics(bool) noexcept;
// Gas batches shared by nearby cells (docs/colony-owner.md#gas-batches); on
// unless MA_COLONY_GAS_BATCHES=cell. Controls and comparisons switch it.
void set_gas_batch_spreading(bool) noexcept;
// Profile 12 targets 3 m/s for gas leaving through space-exposed faces.
// MA_COLONY_SPACE_SPEED or this benchmark override can select zero for comparison
// (docs/performance-acceptance.md#game-free-speed-check-2026-11-16).
void set_space_speed(double) noexcept;
// The last completed frame's flow stages and stepped regions.
[[nodiscard]] FlowDiagnostics flow_diagnostics();
}
