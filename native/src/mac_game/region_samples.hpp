#pragma once
// Execution diagnostic for the performance tools, off unless a tool enables
// it: what each stepped region of the calling thread's last game advance
// cost. Nothing here is read by the solver, saved or audited, so arithmetic
// and checkpoint bytes are unchanged whether it is on or off.
#include <atomic>
#include <cstddef>
#include <vector>
namespace ma::experimental::game_kernel::region_samples {
struct Sample {
  // The region's lowest owner-grid cell, its bounding box and its fluid cells.
  std::size_t representative = 0, x = 0, y = 0, width = 0, height = 0, cells = 0;
  // Game seconds stepped and the wall time of the region's tick (on one
  // worker unless the region had inner workers).
  double seconds = 0, ms = 0;
  std::size_t substeps = 0, attempts = 0, refinements = 0, iterations = 0;
  double pressure_ms = 0, transport_ms = 0, layering_ms = 0, interface_ms = 0,
         heat_ms = 0, phase_ms = 0, audit_ms = 0;
};
inline std::atomic<bool> &enabled() noexcept {
  static std::atomic<bool> on{false};
  return on;
}
// One entry per stepped region and interval of this thread's last advance.
inline std::vector<Sample> &last() noexcept {
  thread_local std::vector<Sample> samples;
  return samples;
}
} // namespace ma::experimental::game_kernel::region_samples
