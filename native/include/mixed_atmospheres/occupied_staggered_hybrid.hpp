#pragma once

#include "occupied_staggered_remap.hpp"

// Separate hybrid geometry/material-rezone experiment. No pressure adapter,
// FluidWorld selection, C ABI, save format or game activation.
namespace ma::experimental {
struct HybridSourceIdentity {
    // Snapshot distinguishes alternate accepted branches at the same revision.
    // Exact immutable copies share all three components.
    std::uint64_t lineage = 0, revision = 0, snapshot = 0;
    bool operator==(const HybridSourceIdentity&) const = default;
};
struct HybridMaterialIdentity {
    std::uint64_t lineage = 0, ordinal = 0;
    bool operator==(const HybridMaterialIdentity&) const = default;
};
enum class HybridCellKind {
    source_material, whole_material_slab, left_material_cap, right_material_cap,
    whole_interior_gap, left_transition, interior_cartesian, right_transition,
    unstructured_interior
};
struct HybridCellIdentity {
    HybridCellKind kind = HybridCellKind::source_material;
    // Only source material and protected cap/slab cells have a material ID.
    HybridMaterialIdentity material;
    // Meaningful only for interior_cartesian. Transitions/caps may span bins.
    std::int64_t cartesian_index = 0;
    bool operator==(const HybridCellIdentity&) const = default;
};
struct HybridGeometryLimits {
    std::size_t maximum_material_cells = std::size_t{1} << 20;
    // Sum of positive primal and virtual-corner overlap records, including
    // protected identity records. No mechanical packet is constructed in A0.
    std::size_t maximum_overlap_records = std::size_t{1} << 22;
    // Combined authoritative-source and planned-target coverage records.
    std::size_t maximum_coverage_records = std::size_t{1} << 22;
    bool operator==(const HybridGeometryLimits&) const = default;
};
struct HybridMaterialCap {
    HybridCellIdentity identity;
    std::size_t initial_source_cell = 0;
    std::vector<double> original_species_masses_kg;
};

enum class HybridCoveragePartition { authoritative_source, planned_target };
struct HybridCoverageRecord {
    HybridCoveragePartition partition = HybridCoveragePartition::authoritative_source;
    std::size_t cell_slot = 0;
    std::int64_t cartesian_index = 0;
    double begin_relative_m = 0, end_relative_m = 0;
    double width_m = 0, volume_m3 = 0;
};
struct HybridCoverageBalance {
    HybridCoveragePartition partition = HybridCoveragePartition::authoritative_source;
    std::size_t cell_slot = 0, records = 0;
    double expected_width_m = 0, covered_width_m = 0;
    double width_residual_m = 0, width_arithmetic_bound_m = 0;
    double expected_volume_m3 = 0, covered_volume_m3 = 0;
    double volume_residual_m3 = 0, volume_arithmetic_bound_m3 = 0;
};
class HybridRezonePlan;
struct HybridRezoneCandidate;
class HybridStaggeredState {
public:
    HybridStaggeredState(const HybridStaggeredState&) = default;
    HybridStaggeredState(HybridStaggeredState&&) noexcept = default;
    HybridStaggeredState& operator=(const HybridStaggeredState&);
    HybridStaggeredState& operator=(HybridStaggeredState&&) noexcept = default;
    [[nodiscard]] bool bound() const noexcept { return identity_.lineage != 0 && identity_.snapshot != 0 && canonical_.initialized(); }
    [[nodiscard]] HybridSourceIdentity identity() const noexcept { return identity_; }
    [[nodiscard]] const OccupiedStaggeredState& canonical() const noexcept { return canonical_; }
    [[nodiscard]] const HybridGeometryLimits& limits() const noexcept { return limits_; }
    [[nodiscard]] std::span<const HybridCellIdentity> source_cell_identities() const noexcept { return source_cells_; }
    [[nodiscard]] std::span<const HybridMaterialCap> material_caps() const noexcept { return caps_; }
    [[nodiscard]] bool has_coverage_grid() const noexcept { return has_grid_; }
    [[nodiscard]] const StaggeredCartesianGrid& coverage_grid() const noexcept { return grid_; }
    [[nodiscard]] std::span<const HybridCoverageRecord> authoritative_coverage() const noexcept { return coverage_; }
    [[nodiscard]] std::span<const HybridCoverageBalance> authoritative_coverage_balances() const noexcept { return coverage_balances_; }
private:
    HybridStaggeredState() = default;
    friend HybridStaggeredState bind_material_endcaps(const OccupiedStaggeredState&, const HybridGeometryLimits&);
    friend HybridRezoneCandidate rezone_hybrid_staggered(const HybridStaggeredState&, const HybridRezonePlan&);
    static HybridSourceIdentity next_identity(HybridSourceIdentity);
    OccupiedStaggeredState canonical_;
    HybridSourceIdentity identity_;
    HybridGeometryLimits limits_;
    std::vector<HybridCellIdentity> source_cells_;
    std::vector<HybridMaterialCap> caps_;
    bool has_grid_ = false;
    StaggeredCartesianGrid grid_;
    std::vector<HybridCoverageRecord> coverage_;
    std::vector<HybridCoverageBalance> coverage_balances_;
};

struct HybridPlannedCell {
    HybridCellIdentity identity;
    double begin_relative_m = 0, end_relative_m = 0;
    double width_m = 0, volume_m3 = 0;
    // Geometry only: there is deliberately no inventory/U/J/temperature here.
};
struct HybridProtectedCapMapping {
    HybridMaterialIdentity material;
    std::size_t source_cell = 0, planned_target_cell = 0;
};
struct HybridGeometryDiagnostics {
    std::size_t source_cells = 0, planned_target_cells = 0, eligible_grid_lines = 0;
    std::size_t primal_overlap_records = 0, corner_overlap_records = 0;
    std::size_t source_coverage_records = 0, planned_target_coverage_records = 0;
    bool has_interior_gap = false, no_eligible_grid_line = false;
    bool preferred_cartesian_partition = true;
    double interior_gap_width_m = 0, whole_gap_width_m = 0;
    double left_transition_width_m = 0, right_transition_width_m = 0;
    double minimum_planned_width_m = 0, maximum_planned_width_m = 0;
    // The preferred logical grid is evaluated with compensated coordinates;
    // actual fixed-grid boundaries are its certified represented doubles.
    double maximum_grid_line_roundoff_m = 0, maximum_grid_spacing_residual_m = 0;
};
class HybridRezonePlan {
public:
    HybridRezonePlan(const HybridRezonePlan&) = default;
    HybridRezonePlan(HybridRezonePlan&&) noexcept = default;
    HybridRezonePlan& operator=(const HybridRezonePlan&);
    HybridRezonePlan& operator=(HybridRezonePlan&&) noexcept = default;
    [[nodiscard]] HybridSourceIdentity source_identity() const noexcept { return source_; }
    [[nodiscard]] const HybridGeometryLimits& limits() const noexcept { return limits_; }
    [[nodiscard]] const StaggeredCartesianGrid& preferred_grid() const noexcept { return grid_; }
    [[nodiscard]] const HybridGeometryDiagnostics& diagnostics() const noexcept { return diagnostics_; }
    [[nodiscard]] std::span<const double> node_relative_positions_m() const noexcept { return nodes_; }
    [[nodiscard]] std::span<const HybridPlannedCell> planned_cells() const noexcept { return cells_; }
    [[nodiscard]] std::span<const HybridProtectedCapMapping> protected_caps() const noexcept { return caps_; }
    [[nodiscard]] std::span<const HybridCoverageRecord> source_coverage() const noexcept { return source_coverage_; }
    [[nodiscard]] std::span<const HybridCoverageRecord> planned_target_coverage() const noexcept { return target_coverage_; }
    [[nodiscard]] std::span<const HybridCoverageBalance> source_coverage_balances() const noexcept { return source_balances_; }
    [[nodiscard]] std::span<const HybridCoverageBalance> planned_target_coverage_balances() const noexcept { return target_balances_; }
private:
    HybridRezonePlan() = default;
    friend HybridRezonePlan plan_hybrid_staggered_rezone(const HybridStaggeredState&, const StaggeredCartesianGrid&);
    friend HybridRezonePlan plan_hybrid_material_rezone(const HybridStaggeredState&, std::span<const double>, const StaggeredCartesianGrid&);
    friend void validate_hybrid_rezone_plan(const HybridStaggeredState&, const HybridRezonePlan&);
    static HybridRezonePlan build(const HybridStaggeredState&, const StaggeredCartesianGrid&, std::span<const double>, bool);
    HybridSourceIdentity source_;
    HybridGeometryLimits limits_;
    StaggeredCartesianGrid grid_;
    HybridGeometryDiagnostics diagnostics_;
    std::vector<double> nodes_;
    std::vector<HybridPlannedCell> cells_;
    std::vector<HybridProtectedCapMapping> caps_;
    std::vector<HybridCoverageRecord> source_coverage_, target_coverage_;
    std::vector<HybridCoverageBalance> source_balances_, target_balances_;
};

// Exact canonical copy; arbitrary original interior cells remain explicitly
// source_material. Copies share the same logical immutable source identity;
// independent binds get different identities even for byte-identical input.
[[nodiscard]] HybridStaggeredState bind_material_endcaps(
    const OccupiedStaggeredState&, const HybridGeometryLimits& limits = {});
[[nodiscard]] HybridRezonePlan plan_hybrid_staggered_rezone(
    const HybridStaggeredState&, const StaggeredCartesianGrid&);
// Explicit material target, with exact protected cap positions. Interior cells
// are unstructured, with no Cartesian/minimum-h claim. The grid supplies only
// validated read-only coverage; all A0 capacities/precision checks still apply.
[[nodiscard]] HybridRezonePlan plan_hybrid_material_rezone(
    const HybridStaggeredState&, std::span<const double> target_nodes, const StaggeredCartesianGrid& coverage_grid);
// Authenticates a logical immutable snapshot rather than an object's address.
void validate_hybrid_rezone_plan(const HybridStaggeredState&, const HybridRezonePlan&);

struct HybridCapRezoneReport {
    HybridMaterialIdentity material;
    std::size_t source_cell = 0, target_cell = 0;
    bool right_outer = false;
    StaggeredMechanicalPacket outer_before, outer_after;
    double internal_energy_before_j = 0, internal_energy_after_j = 0, inner_mixing_heat_j = 0;
};
struct HybridRezoneReport {
    HybridSourceIdentity before, after;
    StaggeredRemapReport mechanics;
    std::vector<HybridCapRezoneReport> caps;
    std::vector<HybridCellIdentity> previous_cells, accepted_cells;
    std::vector<double> inserted_interior_nodes_m, retired_interior_nodes_m;
};
struct HybridRezoneCandidate { HybridStaggeredState state; HybridRezoneReport report; };
// Remap only: no material motion, pressure/body step, time increment or retry.
// Even an accepted identity rezone advances the authenticated logical revision.
[[nodiscard]] HybridRezoneCandidate rezone_hybrid_staggered(const HybridStaggeredState&, const HybridRezonePlan&);
[[nodiscard]] HybridRezoneReport rezone_hybrid_staggered_into(HybridStaggeredState&, const HybridRezonePlan&);
}
