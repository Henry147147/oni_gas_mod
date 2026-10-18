#pragma once
#include "mixed_atmospheres/mac_closed_world.hpp"

namespace ma::experimental {
struct MacIntervalCandidate { MacTransportState state; MacClosedAdvanceReport report; };
// An absent result requests the unchanged reference interval from its original
// source. No partial state or numerical history escapes a declined fast path.
// Consumed attempts are retained even on decline and share the owner's budget.
[[nodiscard]] std::optional<MacIntervalCandidate> advance_mac_interval_kernel(
    const SpeciesRegistry&, const FluidThermodynamics&, const MacTransportState&,
    const MacClosedSettings&, double seconds, std::size_t attempt_budget, std::size_t& consumed_attempts);
// The retained implementation's stage checks do not replace the opted-in
// interval contract. Energy is reconciled by the owner's existing exact total.
void validate_mac_interval_fallback(const MacTransportState&, const MacTransportState&, MacClosedAdvanceReport&);
}
