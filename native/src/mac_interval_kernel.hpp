#pragma once
#include "mixed_atmospheres/mac_closed_world.hpp"
#include "mac_pressure_multigrid.hpp"
#include <mutex>

namespace ma::experimental {
namespace interval_detail { struct TransportTopology; struct IntervalWorkspace; }
struct MacIntervalCandidate { MacTransportState state; MacClosedAdvanceReport report; };
struct MacIntervalCache {
    MacIntervalCache();
    ~MacIntervalCache();
    // The grid is owned with its immutable primal/dual face connectivity.
    std::once_flag transport_once;
    std::shared_ptr<const interval_detail::TransportTopology> transport;
    std::once_flag multigrid_once;
    std::shared_ptr<const interval_detail::PressureMultigridTopology> multigrid;
    // Related immutable owners share one reusable numerical workspace. A
    // solver leases it exclusively; coefficients are replaced before use.
    std::mutex multigrid_workspace_mutex;
    std::unique_ptr<interval_detail::PressureMultigrid> multigrid_workspace;
    std::mutex workspace_mutex;
    std::unique_ptr<interval_detail::IntervalWorkspace> workspace;
};
// An absent result requests the unchanged reference interval from its original
// source. No partial state or numerical history escapes a declined fast path.
// Consumed attempts are retained even on decline and share the owner's budget.
[[nodiscard]] std::optional<MacIntervalCandidate> advance_mac_interval_kernel(
    const SpeciesRegistry&, const FluidThermodynamics&, const MacTransportState&,
    const MacClosedSettings&, double seconds, std::size_t attempt_budget, std::size_t& consumed_attempts, MacIntervalCache*);
// The retained implementation's stage checks do not replace the opted-in
// interval contract. Energy is reconciled by the owner's existing exact total.
void validate_mac_interval_fallback(const MacTransportState&, const MacTransportState&, MacClosedAdvanceReport&);
}
