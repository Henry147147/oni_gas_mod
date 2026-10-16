#include "mixed_atmospheres/experimental/canonical_material.hpp"
#include <bit>
#include <stdexcept>
#include <type_traits>

namespace ma::experimental {
namespace {
// Deliberately a complete diagnostic representation, not a restart decoder.
// Tagged fields and explicit lengths avoid native padding, pointers and locale.
// Binary64 values are copied as little-endian bits, including signed zero.
class Bytes {
public:
    void integer(std::uint64_t value) {
        room(8);
        for (unsigned i = 0; i < 8; ++i) {
            data.push_back(static_cast<std::byte>(value & 255U)); value >>= 8;
        }
    }
    void text(std::string_view value) {
        integer(value.size()); room(value.size());
        for (const unsigned char byte : value) data.push_back(static_cast<std::byte>(byte));
    }
    std::vector<std::byte> data;
private:
    void room(std::size_t count) const {
        constexpr std::size_t maximum = 2 * 1024 * 1024;
        if (count > maximum - data.size()) throw std::length_error("Canonical diagnostic byte limit exceeded");
    }
};
void emit(Bytes& w, double value) { w.integer(std::bit_cast<std::uint64_t>(value)); }
void emit(Bytes& w, const std::string& value) { w.text(value); }
void emit(Bytes&, const SpeciesDefinition&);
void emit(Bytes&, const ReferenceMaterial&);
void emit(Bytes&, const ReferenceConservedCell&);
void emit(Bytes&, const ReferenceEquilibriumState&);
void emit(Bytes&, const CanonicalReferencePiece&);
void emit(Bytes&, const CanonicalPieceReading&);
void emit(Bytes&, const CanonicalOwnerReading&);
void emit(Bytes&, const RetainedScalar&);
void emit(Bytes&, const RetainedScalarDifference&);
void emit(Bytes&, const CanonicalGeometryMeasure&);
void emit(Bytes&, const CanonicalPrimaryValues&);
template<class T> requires (std::is_integral_v<T> || std::is_enum_v<T>)
void emit(Bytes& w, T value) { w.integer(static_cast<std::uint64_t>(value)); }
template<class T, std::size_t N> void emit(Bytes& w, const std::array<T, N>& values) {
    w.integer(N); for (const auto& value : values) emit(w, value);
}
template<class T> void emit(Bytes& w, const std::vector<T>& values) {
    w.integer(values.size()); for (const auto& value : values) emit(w, value);
}
#define FIELD(name) do { w.text(#name); emit(w, r.name); } while (false)
void emit(Bytes& w, const RetainedScalar& r) {
    w.text("high"); emit(w, r.high()); w.text("low"); emit(w, r.low());
}
void emit(Bytes& w, const RetainedScalarDifference& r) {
    FIELD(value); FIELD(expansion_terms); FIELD(expansion_count); FIELD(roundoff_remainders);
}
void emit(Bytes& w, const CanonicalGeometryMeasure& r) {
    FIELD(length_m); FIELD(volume_terms_m3); FIELD(volume_term_count);
    FIELD(projected_volume_m3); FIELD(projection_residual_terms_m3);
}
void emit(Bytes& w, const CanonicalPrimaryValues& r) {
    FIELD(relative_positions_m); FIELD(momentum); FIELD(internal_energy_j);
    FIELD(origin_m); FIELD(completed_time_seconds);
}
void emit(Bytes& w, const SpeciesDefinition& r) {
    FIELD(id); FIELD(name); FIELD(phase);
    w.text("atoms"); w.integer(r.atoms.size());
    for (const auto& [element, count] : r.atoms) { w.text(element); emit(w, count); }
    FIELD(molar_mass); FIELD(cp); FIELD(density); FIELD(conductivity);
    FIELD(formation_enthalpy); FIELD(max_mole_fraction);
}
void emit(Bytes& w, const ReferenceMaterial& r) {
    FIELD(id); FIELD(phase); FIELD(molar_mass_kg_per_mol); FIELD(cv_j_per_kg_k);
    FIELD(energy_offset_j_per_kg); FIELD(minimum_temperature_k); FIELD(maximum_temperature_k);
    FIELD(zero_pressure_density_kg_per_m3); FIELD(reference_bulk_modulus_pa);
    FIELD(maximum_density_ratio); FIELD(provenance);
}
void emit(Bytes& w, const ReferenceRootOptions& r) {
    FIELD(relative_pressure_tolerance); FIELD(relative_volume_tolerance); FIELD(maximum_iterations);
}
void emit(Bytes& w, const ReferenceConservedCell& r) {
    FIELD(masses_kg); FIELD(internal_energy_j); FIELD(volume_m3);
}
void emit(Bytes& w, const ReferenceEquilibriumState& r) {
    FIELD(temperature_k); FIELD(mechanical_pressure_pa); FIELD(gas_pressure_pa);
    FIELD(gas_volume_m3); FIELD(liquid_volume_m3); FIELD(vacuum_volume_m3);
    FIELD(reference_energy_j); FIELD(thermal_energy_j); FIELD(elastic_energy_j);
    FIELD(entropy_j_per_k); FIELD(helmholtz_energy_j); FIELD(intrinsic_heat_capacity_j_per_k);
    FIELD(equilibrium_heat_capacity_at_cell_volume_j_per_k); FIELD(volume_residual_m3); FIELD(energy_residual_j);
    FIELD(partial_volumes_m3); FIELD(specific_internal_energies_j_per_kg);
    FIELD(pressure_iterations); FIELD(has_gas); FIELD(has_vacuum);
}
void emit(Bytes& w, const CanonicalBoundary& r) {
    FIELD(kind); FIELD(velocity_m_per_s); FIELD(exterior_pressure_pa);
}
void emit(Bytes& w, const CanonicalReferencePiece& r) {
    FIELD(left); FIELD(right); FIELD(owner); FIELD(density_left); FIELD(density_right);
}
void emit(Bytes& w, const CanonicalPieceReading& r) {
    FIELD(geometry); FIELD(reference_geometry);
    FIELD(jacobian); FIELD(mass_kg); FIELD(first_mass_moment); FIELD(second_mass_moment);
    FIELD(momentum); FIELD(position_velocity_product); FIELD(kinetic_energy_j);
    FIELD(density_left); FIELD(density_right); FIELD(masses_kg); FIELD(consistent_mass); FIELD(local_momentum);
}
void emit(Bytes& w, const CanonicalOwnerReading& r) {
    FIELD(masses_kg); FIELD(mass_kg); FIELD(first_mass_moment); FIELD(second_mass_moment);
    FIELD(momentum); FIELD(position_velocity_product); FIELD(resolved_kinetic_energy_j);
    FIELD(bulk_kinetic_energy_j); FIELD(mechanical_variance_j);
}
void emit(Bytes& w, const CanonicalInitializationLedger& r) {
    FIELD(caloric_energy_j); FIELD(explicitly_added_elastic_energy_j);
    FIELD(represented_elastic_energy_change_j); FIELD(energy_addition_roundoff_j);
    FIELD(requested_kinetic_energy_j); FIELD(represented_kinetic_energy_j);
    FIELD(kinetic_arithmetic_difference_j); FIELD(maximum_quadrature_ratio);
    FIELD(maximum_thermodynamic_ratio); FIELD(maximum_linear_ratio);
    FIELD(requested_velocity_m_per_s); FIELD(momentum_product_terms); FIELD(momentum_sum_remainders);
}
void emit(Bytes& w, const CanonicalStepReport& r) {
    FIELD(seconds); FIELD(represented_seconds); FIELD(force_residual_ratio); FIELD(linear_residual_ratio);
    FIELD(maximum_quadrature_ratio); FIELD(maximum_thermodynamic_ratio); FIELD(maximum_work_oracle_ratio);
    FIELD(old_kinetic_energy_j); FIELD(new_kinetic_energy_j); FIELD(energy_error_j); FIELD(energy_budget_j);
    FIELD(accounting_remainder_j); FIELD(accounting_bound_j); FIELD(momentum_residual_work_j);
    FIELD(kinematic_defect_work_j); FIELD(kinetic_defect_j); FIELD(trial_residual_work_j);
    FIELD(exterior_work_left_j); FIELD(exterior_work_right_j); FIELD(reaction_work_left_j); FIELD(reaction_work_right_j);
    FIELD(exterior_impulse_left); FIELD(exterior_impulse_right); FIELD(reaction_impulse_left); FIELD(reaction_impulse_right);
    FIELD(momentum_error); FIELD(momentum_accounting_remainder); FIELD(momentum_accounting_bound);
    FIELD(first_moment_change); FIELD(first_moment_error); FIELD(first_moment_accounting_remainder); FIELD(first_moment_bound);
    FIELD(position_velocity_change); FIELD(position_velocity_accounting_remainder); FIELD(position_velocity_bound);
    FIELD(relative_position_velocity_change); FIELD(origin_position_velocity_change);
    FIELD(position_velocity_compatibility_change); FIELD(origin_compatibility_change); FIELD(total_homogeneous_entropy_change);
    FIELD(newton_updates); FIELD(backtracks); FIELD(factorizations); FIELD(constitutive_charges); FIELD(prescribed_displacements_exact);
    FIELD(requested_displacement); FIELD(represented_displacement); FIELD(scaled_residual); FIELD(force_residual); FIELD(force_scales);
    FIELD(old_style_trial_residual); FIELD(compatibility0); FIELD(compatibility1); FIELD(decoded_product0); FIELD(decoded_product1);
    FIELD(delta_velocity); FIELD(mass_delta_velocity); FIELD(requested_impulse); FIELD(represented_impulse);
    FIELD(free_addition_defect); FIELD(constrained_linearity_defect); FIELD(constrained_join_defect); FIELD(constrained_impulse_defect);
    FIELD(constrained_linearity_bound); FIELD(constrained_join_bound); FIELD(constrained_impulse_bound);
    FIELD(pressure_force); FIELD(exterior_force); FIELD(reaction); FIELD(actual_force); FIELD(momentum_residual); FIELD(kinematic_defect);
    FIELD(requested_volume_change); FIELD(represented_volume_change); FIELD(incidence_volume_change); FIELD(volume_storage_difference);
    FIELD(balanced_energy_change); FIELD(represented_energy_change); FIELD(stored_energy_defect); FIELD(secant_defect);
    FIELD(volume_work_defect); FIELD(owner_work_defect); FIELD(work_pressure); FIELD(work_stiffness);
    FIELD(entropy_change); FIELD(entropy_cache_difference); FIELD(eos_represented_volume_change); FIELD(eos_represented_energy_change);
    FIELD(cap_impulse_left); FIELD(cap_impulse_right); FIELD(cap_work_left); FIELD(cap_work_right);
    FIELD(endpoint_volume_resolves); FIELD(endpoint_energy_resolves);
}
void emit(Bytes& w, const CanonicalSnapshot& r) {
    FIELD(primary); FIELD(owner_geometry);
    w.text("registry"); emit(w, r.registry->all());
    w.text("profile_version"); emit(w, r.thermodynamics->reference().profile_version());
    w.text("materials"); emit(w, r.thermodynamics->reference().materials());
    w.text("root_options"); emit(w, ReferenceRootOptions{});
    FIELD(model_metadata); FIELD(diagnostic_model_identity); FIELD(reference_nodes_m);
    FIELD(positions_m); FIELD(velocities_m_per_s); FIELD(momentum); FIELD(consistent_mass);
    FIELD(reference_pieces); FIELD(pieces); FIELD(owner_ids); FIELD(owner_left); FIELD(owner_right);
    FIELD(cells); FIELD(thermodynamics_readings); FIELD(owners); FIELD(left); FIELD(right);
    FIELD(area_m2); FIELD(origin_high_m); FIELD(origin_low_m); FIELD(completed_time_seconds);
    FIELD(resolved_kinetic_energy_j); FIELD(cumulative_energy_error_j); FIELD(completed_steps);
    FIELD(initialization_energy); FIELD(initialization); FIELD(last_step);
}
#undef FIELD
}
std::vector<std::byte> canonical_material_diagnostic_bytes(const CanonicalReadView& view) {
    Bytes output; output.text("ma-canonical-material-diagnostics-v2");
    emit(output, view.snapshot()); return std::move(output.data);
}
}
