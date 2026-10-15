#pragma once
#include "occupied_staggered.hpp"
#include <cstdint>

namespace ma::experimental {
// Instantaneous rezoning only. Coordinates use the state's existing origin;
// outer nodes must match exactly. IDs describe a grid mapping, not physics.
struct StaggeredRezonePlan {
    std::vector<double> node_relative_positions_m;
    std::vector<std::int64_t> target_cell_ids;
};
struct StaggeredCartesianGrid {
    double origin_relative_high_m = 0, origin_relative_low_m = 0, spacing_m = 1;
    std::int64_t first_cell = 0, end_cell = 1;
};
struct StaggeredMechanicalPacket { double mass_kg = 0, momentum_kg_m_per_s = 0, kinetic_j = 0; };
struct StaggeredPrimalRecord {
    std::size_t donor = 0, target = 0;
    double length_m = 0, centroid_relative_m = 0, nonreference_energy_j = 0, potential_j = 0;
    std::vector<double> masses_kg;
};
struct StaggeredDualRecord {
    std::size_t donor_corner = 0, target_corner = 0;
    double length_m = 0;
    StaggeredMechanicalPacket packet;
};
struct StaggeredRemapCellReport {
    StaggeredMechanicalPacket raw_left, raw_right, normalized_left, normalized_right, left, right;
    double raw_minus_primal_mass_kg = 0, quadrature_mass_bound_kg = 0;
    double normalization_delta_mass_kg = 0, normalization_delta_momentum_kg_m_per_s = 0, normalization_delta_kinetic_j = 0;
    double normalization_momentum_bound_kg_m_per_s = 0, normalization_kinetic_bound_j = 0;
    double lambda = 1, rebalance_transfer_mass_kg = 0;
    double rebalance_momentum_residual_kg_m_per_s = 0, rebalance_kinetic_residual_j = 0;
    double mixing_heat_j = 0, arithmetic_energy_j = 0, wall_heat_j = 0, gravity_relocation_j = 0;
    double transported_nonreference_energy_j = 0, stored_u_roundoff_j = 0;
};
struct StaggeredRemapNodeReport {
    double inertia_kg = 0, provisional_momentum_kg_m_per_s = 0, provisional_kinetic_j = 0;
    double transported_kinetic_j = 0, mixing_heat_j = 0, mixing_arithmetic_bound_j = 0;
    double wall_impulse_kg_m_per_s = 0, wall_heat_j = 0;
};
struct StaggeredRemapReport {
    bool identity = false;
    std::vector<std::int64_t> target_cell_ids;
    std::vector<StaggeredPrimalRecord> primal_records;
    std::vector<StaggeredDualRecord> dual_records;
    std::vector<StaggeredRemapCellReport> cells;
    std::vector<StaggeredRemapNodeReport> nodes;
    std::vector<double> species_mass_residual_kg, species_mass_bound_kg;
    double gather_momentum_residual_kg_m_per_s = 0, gather_kinetic_residual_j = 0;
    double momentum_residual_kg_m_per_s = 0, momentum_arithmetic_bound_kg_m_per_s = 0;
    double energy_residual_j = 0, energy_arithmetic_bound_j = 0;
    double entropy_change_j_per_k = 0, entropy_arithmetic_bound_j_per_k = 0;
    double mixing_heat_j = 0, arithmetic_energy_j = 0, gravity_relocation_j = 0;
    double wall_impulse_kg_m_per_s = 0, wall_heat_j = 0;
    double geometry_volume_residual_m3 = 0, absolute_geometry_volume_residual_m3 = 0;
    std::size_t unresolved_u_updates = 0;
};
struct StaggeredRemapCandidate {
    OccupiedStaggeredState state;
    StaggeredRemapReport report;
    std::vector<std::int64_t> target_cell_ids;
};
[[nodiscard]] StaggeredRezonePlan plan_staggered_cartesian_rezone(const OccupiedStaggeredState&, const StaggeredCartesianGrid&);
[[nodiscard]] StaggeredRemapCandidate rezone_occupied_staggered(const OccupiedStaggeredState&, const StaggeredRezonePlan&);
// No pressure/body solve, retry loop or time increment; complete candidate then
// noexcept state commit. The returned report owns the target mapping as input.
[[nodiscard]] StaggeredRemapReport rezone_occupied_staggered_into(OccupiedStaggeredState&, const StaggeredRezonePlan&);
}
