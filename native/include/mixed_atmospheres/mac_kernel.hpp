#pragma once
#include "mac_closed_world.hpp"
#include <array>
#include <cstdint>
#include <string>

namespace ma::experimental {
// Production substep kernel for MacClosedWorld (native/MAC_KERNEL.md).
//
// Same physical model, settings and selectors as the reference
// implementation, evaluated with structure-of-arrays binary64 flux-form
// updates under the production contract in MAC_SOLVER.md: conservation by
// construction through shared face fluxes, one interval audit, counted
// rounding, interval entropy check. Selected explicitly with
// MacExecutionPolicy::production or advance_mac_production(); the reference
// path and legacy checkpoint bytes are unchanged; production selection is
// persisted in version 6. Execution options never
// change results: the thread count only distributes fixed blocks, and every
// reduction combines fixed block partials in block order.
struct MacKernelOptions {
    // Worker threads; the result never depends on it.
    unsigned threads = 1;
    // At most one thread per this many fixed row blocks (about 1,024 cells
    // each). The default never runs more threads than blocks: idle workers
    // woken for every parallel region only cost time (4,096 cells, four
    // blocks: 2.0 times one thread with four threads, 1.6 with twelve).
    std::size_t minimum_blocks_per_thread = 1;
    // Numerical choices belong to MacClosedSettings and its checkpoint.
    // Automatic preconditioning uses geometric multigrid on large systems;
    // explicit Jacobi remains available through the saved pressure setting.
    // Diagnostics only: wall-clock stage timing (does not affect results).
    bool collect_stage_timing = false;
    void validate() const;
};
// Production admission (MAC_KERNEL.md, "Coverage and admission"): supported
// selectors, and every stored value finite and at most 2^450 in magnitude.
// Below-range values and full-range canonical momentum are admitted; their
// binary64 rounding is counted in the interval audit.
[[nodiscard]] bool mac_kernel_supports(const MacClosedSettings&);
[[nodiscard]] bool mac_kernel_normal_range(const MacTransportState&);
// Process-wide default for MacExecutionPolicy::production (execution only).
[[nodiscard]] MacKernelOptions mac_kernel_default_options();
void set_mac_kernel_default_options(const MacKernelOptions&);

struct MacKernelAudit {
    // One entry per present constituent, per passive identity, then x and y
    // momentum and U+K+G. Residual is end - start - external; the bound is
    // 128 binary64 epsilons of the interval's absolute operands plus the
    // counted rounding allowance. escalated: the double-double comparison
    // could not decide and exact expansions decided instead.
    std::string quantity;
    double residual = 0, bound = 0, rounding_allowance = 0;
    // Saved-data reviewers recompute the bound from independent endpoints
    // and this counted stage magnitude (entered as a cancelling pair).
    double stage_operand_magnitude = 0, below_range_allowance = 0;
    bool passed = false, escalated = false;
};
struct MacKernelStageTiming {
    double viscosity_ms = 0, conduction_ms = 0, shock_heat_ms = 0, compression_ms = 0,
        eos_ms = 0, pressure_ms = 0, reconstruction_ms = 0, transport_ms = 0, momentum_ms = 0,
        gravity_ms = 0, audit_ms = 0, conversion_ms = 0;
};
struct MacKernelReport {
    std::size_t substeps = 0, attempts = 0, rejected_attempts = 0, audit_retries = 0;
    std::size_t multigrid_solves = 0, multigrid_cycles = 0, multigrid_fallbacks = 0, multigrid_levels = 0, topology_builds = 0;
    std::size_t newton_iterations = 0, linear_iterations = 0, line_search_backtracks = 0;
    std::size_t eos_evaluations = 0, isentropic_evaluations = 0;
    std::size_t reconstruction_backtracks = 0, limited_momentum_corrections = 0;
    std::size_t compression_events = 0, shock_heat_events = 0, conduction_stages = 0, viscosity_modes = 0;
    std::size_t rounded_range_inputs = 0, unresolved_updates = 0;
    std::uint64_t cell_substeps = 0;
    // Threads that executed the interval (MacKernelOptions::threads capped
    // by the grid's row-block count).
    unsigned threads_used = 1;
    // Retained kernel workspace (field banks and scratch) after the interval.
    std::size_t workspace_bytes = 0;
    // The interval ran through the reference implementation because an input
    // or endpoint required full-range or material-boundary refinement.
    bool reference_fallback = false;
    double maximum_volume_closure = 0, minimum_timestep_s = 0, maximum_timestep_s = 0;
    double mixing_heat_j = 0, wall_heat_j = 0, compression_heat_j = 0, shock_heat_transfer_j = 0;
    double wall_impulse_x = 0, wall_impulse_y = 0, body_impulse_y = 0;
    double energy_residual_j = 0, energy_bound_j = 0;
    double entropy_change_j_per_k = 0, entropy_allowance_j_per_k = 0;
    std::vector<MacKernelAudit> audits;
    MacKernelStageTiming timing;
};
struct MacKernelAdvance { MacTransportState state; MacKernelReport report; };
// Interval advance on a validated owner state. start_time_s reproduces the
// reference clock arithmetic (substep sizes are computed from the absolute
// endpoint exactly as the reference does).
[[nodiscard]] MacKernelAdvance advance_mac_kernel(const SpeciesRegistry&, const FluidThermodynamics&,
    const MacTransportState&, const MacClosedSettings&, double start_time_s, double seconds,
    std::size_t attempt_budget, const MacKernelOptions&);
// Owner-level production advance with the same atomicity as advanced():
// returns a new owner one revision later or throws, publishing nothing. Flow
// only: an owner with gas diffusion enabled is rejected here (and by
// advance_mac_kernel); MacClosedWorld::advanced with
// MacExecutionPolicy::production wraps the kernel in the owner's diffusion
// half steps.
[[nodiscard]] MacClosedAdvance advance_mac_production(const MacClosedWorld&, double seconds,
    std::size_t attempt_budget, const MacKernelOptions&);
// The flow interval of a diffusion-composed owner (used by MacClosedWorld).
[[nodiscard]] MacClosedAdvance advance_mac_production_flow(const MacClosedWorld&, double seconds,
    std::size_t attempt_budget, const MacKernelOptions&);
// The kernel report of the most recent advance_mac_production on this thread
// (diagnostic convenience for the benchmark; cleared on failure).
[[nodiscard]] const MacKernelReport& last_mac_production_report();
}
