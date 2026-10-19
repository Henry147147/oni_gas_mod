#pragma once
#include "mixed_atmospheres/mac_closed_world.hpp"

namespace ma::experimental {
// The strict reference stages also reconcile the selected production interval.
// Energy is reconciled by the owner; inventories, impulses and entropy here.
void validate_mac_interval_fallback(const MacTransportState&, const MacTransportState&, MacClosedAdvanceReport&);
}
