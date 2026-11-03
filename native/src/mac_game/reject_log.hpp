#pragma once
// Diagnostic only, off by default: MA_COLONY_REJECT_LOG=1 records why each
// pressure-refinement ("rejected attempt") of the game kernel's flow stage
// happened, with the owner-grid cell and the trial interval; =2 also prints
// every event. Nothing here is read by the solver, saved or audited, so
// arithmetic and checkpoint bytes are unchanged whether it is on or off.
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <vector>
namespace ma::experimental::game_kernel::reject_log {
enum class Reason : std::uint8_t {
  pressure_solve_failure,           // the component pressure solve threw ComponentPressureSolveFailure
  pressure_not_converged,           // the pressure solve returned unconverged
  negative_or_nonfinite_pressure,   // the one-pass nonnegative correction left p < 0 or non-finite
  swept_volume                      // a cell's net swept volume exceeded 0.4 cell
};
inline constexpr std::size_t no_cell = std::numeric_limits<std::size_t>::max();
inline const char* name(Reason reason) noexcept {
  switch (reason) {
    case Reason::pressure_solve_failure: return "pressure_solve_failure";
    case Reason::pressure_not_converged: return "pressure_not_converged";
    case Reason::negative_or_nonfinite_pressure: return "negative_or_nonfinite_pressure";
    default: return "swept_volume";
  }
}
struct Event {
  Reason reason;
  std::size_t cell;   // owner-grid cell index (y * width + x), or no_cell
  double ratio;       // swept-volume reasons: largest net swept volume / (0.4 cell volume)
  double trial_dt;    // the interval that was rejected (s)
};
// The region the calling thread is advancing, so a local cell maps to the owner grid.
struct Region { std::size_t x = 0, y = 0, width = 0, source_width = 0; };
inline thread_local Region region;
inline int level() noexcept {
  static const int value = [] { const char* e = std::getenv("MA_COLONY_REJECT_LOG"); return e ? std::atoi(e) : 0; }();
  return value;
}
// Always-on per-criterion event counts (one relaxed increment per refinement, which is
// rare); the owner resets them when a session starts or restores and reports them
// beside rejected_attempts (ma_oni_colony_rejection_counts). Diagnostic only.
inline std::array<std::atomic<std::uint64_t>, 4>& counters() {
  static std::array<std::atomic<std::uint64_t>, 4> value{};
  return value;
}
inline void count(Reason reason) noexcept { counters()[static_cast<std::size_t>(reason)].fetch_add(1, std::memory_order_relaxed); }
inline std::array<std::uint64_t, 4> counts() noexcept {
  std::array<std::uint64_t, 4> out{};
  for (std::size_t i = 0; i < 4; ++i) out[i] = counters()[i].load(std::memory_order_relaxed);
  return out;
}
inline void reset_counts() noexcept { for (auto& c : counters()) c.store(0, std::memory_order_relaxed); }
inline std::mutex& guard() { static std::mutex value; return value; }
inline std::vector<Event>& pending() { static std::vector<Event> value; return value; }
inline void record(Reason reason, std::size_t local_cell, double ratio, double trial_dt) {
  std::size_t cell = local_cell;
  if (local_cell != no_cell && region.width)
    cell = region.x + local_cell % region.width + (region.y + local_cell / region.width) * region.source_width;
  std::lock_guard lock(guard());
  if (pending().size() < (1u << 20)) pending().push_back({reason, cell, ratio, trial_dt});
}
inline std::vector<Event> drain() {
  std::lock_guard lock(guard());
  return std::exchange(pending(), {});
}
}  // namespace ma::experimental::game_kernel::reject_log
