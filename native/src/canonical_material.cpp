#include "canonical_material_internal.hpp"
#include <new>
#include <type_traits>

namespace ma::experimental::canonical_detail {
namespace {
bool text_valid(std::string_view value, std::size_t maximum, bool whitespace = false) {
    if (value.empty() || value.size() > maximum) return false;
    for (std::size_t i = 0; i < value.size();) {
        const auto lead = static_cast<unsigned char>(value[i++]);
        if (lead < 128) {
            if (lead == 0 || (lead < 32 && !(whitespace && (lead == '\n' || lead == '\r' || lead == '\t'))) || lead == 127) return false;
            continue;
        }
        unsigned count, code;
        if (lead >= 0xc2 && lead <= 0xdf) { count = 1; code = lead & 31U; }
        else if (lead >= 0xe0 && lead <= 0xef) { count = 2; code = lead & 15U; }
        else if (lead >= 0xf0 && lead <= 0xf4) { count = 3; code = lead & 7U; }
        else return false;
        if (value.size() - i < count) return false;
        for (unsigned j = 0; j < count; ++j) {
            const auto byte = static_cast<unsigned char>(value[i++]);
            if ((byte & 0xc0U) != 0x80U) return false;
            code = (code << 6) | (byte & 63U);
        }
        if ((count == 1 && code < 128) || (count == 2 && code < 2048) || (count == 3 && code < 65536) ||
            (code >= 0xd800 && code <= 0xdfff) || code > 0x10ffff) return false;
    }
    return true;
}
void validate_boundary(Kernel& k, const CanonicalBoundary& boundary) {
    k.finite(boundary.velocity_m_per_s, Field::velocity0); k.nonnegative(boundary.exterior_pressure_pa, Field::exterior_force);
    k.require(boundary.kind == CanonicalBoundaryKind::free || boundary.kind == CanonicalBoundaryKind::fixed ||
        boundary.kind == CanonicalBoundaryKind::prescribed_velocity, Code::unsupported_configuration, "Unknown canonical boundary kind");
    k.require(boundary.kind == CanonicalBoundaryKind::prescribed_velocity || boundary.velocity_m_per_s == 0,
        Code::unsupported_configuration, "Unused/free or fixed boundary velocity must be zero");
}
SpeciesRegistry validate_registry(Kernel& k, const CanonicalInitialization& in) {
    SpeciesRegistry result;
    for (std::size_t i = 0; i < in.registry.size(); ++i) {
        const auto& species = in.registry[i]; k.scope(Stage::model_validation);
        k.scalar(Field::molar_mass, species.molar_mass); k.scalar(Field::heat_capacity, species.cp);
        k.scalar(Field::reference_density, species.density); k.scalar(Field::energy_offset, species.formation_enthalpy);
        k.require(text_valid(species.id, 256) && text_valid(species.name, 256), Code::unsupported_configuration, "Invalid bounded registry identity/name");
        k.require(species.atoms.size() != 0 && species.atoms.size() <= 16, Code::unsupported_configuration, "Registry atom-count bounds differ");
        for (const auto& [element, count] : species.atoms) {
            k.require(text_valid(element, 32), Code::unsupported_configuration, "Invalid bounded element identity");
            k.positive(count, Field::matrix_operands);
        }
        k.require(species.phase == Phase::gas || species.phase == Phase::liquid, Code::unsupported_configuration, "Canonical registry contains an unsupported phase");
        k.positive(species.molar_mass, Field::molar_mass); k.positive(species.cp, Field::heat_capacity);
        k.nonnegative(species.density, Field::reference_density); k.nonnegative(species.conductivity, Field::arithmetic_result);
        k.finite(species.formation_enthalpy, Field::energy_offset); k.nonnegative(species.max_mole_fraction, Field::arithmetic_result);
        k.require(species.max_mole_fraction <= 1, Code::unsupported_configuration, "Registry hazard fraction exceeds one");
        const auto index = result.add(species);
        k.scalar(Field::molar_mass, result[index].molar_mass, 1);
        k.require(result[index].molar_mass == species.molar_mass, Code::unsupported_configuration,
            "Registry molecular mass differs from its actual supplied atom definition");
    }
    return result;
}
void validate_profiles(Kernel& k, const CanonicalInitialization& in) {
    for (std::size_t i = 0; i < in.materials.size(); ++i) {
        const auto& material = in.materials[i]; k.scope(Stage::model_validation);
        k.require(text_valid(material.id, 256) && text_valid(material.provenance, 4096, true),
            Code::unsupported_configuration, "Invalid bounded profile identity/provenance");
        k.positive(material.molar_mass_kg_per_mol, Field::molar_mass);
        k.positive(material.cv_j_per_kg_k, Field::heat_capacity); k.finite(material.energy_offset_j_per_kg, Field::energy_offset);
        k.positive(material.minimum_temperature_k, Field::temperature_minimum);
        k.positive(material.maximum_temperature_k, Field::temperature_maximum);
        k.nonnegative(material.zero_pressure_density_kg_per_m3, Field::reference_density);
        k.nonnegative(material.reference_bulk_modulus_pa, Field::bulk_modulus);
        k.positive(material.maximum_density_ratio, Field::density_ratio_limit);
        k.require(material.phase == ReferencePhase::ideal_gas || material.phase == ReferencePhase::elastic_liquid,
            Code::unsupported_configuration, "Unknown canonical profile phase");
    }
}
Data initialize_data(Kernel& k, const CanonicalInitialization& in) {
    k.scope(Stage::initialization); k.deadline();
    const auto n = in.positions_m.size(), species_count = in.materials.size();
    k.require(n >= 8 && n <= 33 && in.reference_nodes_m.size() == n && in.velocities_m_per_s.size() == n &&
        in.pieces.size() + 1 == n && in.pieces.size() <= 32 && species_count >= 1 && species_count <= 4 && in.registry.size() == species_count,
        Code::unsupported_configuration, "Canonical initialization shape or capacity differs");
    k.require(in.energy == CanonicalInitialEnergy::stored_internal_energy || in.energy == CanonicalInitialEnergy::temperature,
        Code::unsupported_configuration, "Unknown canonical initialization energy policy");
    k.require(text_valid(in.profile_version, 256) && text_valid(in.expected_model_metadata, 65536, true),
        Code::unsupported_configuration, "Invalid bounded model version or complete metadata");
    k.positive(in.area_m2, Field::occupied_volume); k.nonnegative(in.completed_time_seconds, Field::completed_time);
    k.step_context(0, in.completed_time_seconds); validate_boundary(k, in.left); validate_boundary(k, in.right);
    for (std::size_t i = 0; i < 7; ++i) {
        k.scope(Stage::initialization, canonical_absent_index, static_cast<std::uint32_t>(i));
        k.finite(in.stored_internal_energy_j[i], Field::stored_internal_energy); k.nonnegative(in.temperatures_k[i], Field::temperature);
        k.require(in.energy == CanonicalInitialEnergy::temperature ? in.stored_internal_energy_j[i] == 0 : in.temperatures_k[i] == 0,
            Code::unsupported_configuration, "Inactive canonical initialization energy array is nonzero");
        for (std::size_t j = 0; j < i; ++j) k.require(in.owner_ids[i] != in.owner_ids[j], Code::unsupported_configuration, "Duplicate canonical owner identity");
    }
    auto registry = validate_registry(k, in); validate_profiles(k, in);
    Data d; d.registry = std::make_shared<const SpeciesRegistry>(std::move(registry));
    k.scope(Stage::model_validation);
    d.thermodynamics = std::make_shared<const FluidThermodynamics>(*d.registry, in.profile_version, in.materials);
    d.thermodynamics->validate_registry(*d.registry);
    d.model_metadata = d.thermodynamics->reference().metadata();
    k.require(d.model_metadata.size() <= 65536 && d.model_metadata == in.expected_model_metadata,
        Code::unsupported_configuration, "Complete canonical thermodynamic model metadata differs");
    d.diagnostic_model_identity = in.diagnostic_model_identity;
    d.reference_nodes_m = in.reference_nodes_m; d.positions_m.resize(n); d.velocities_m_per_s = in.velocities_m_per_s;
    d.reference_pieces = in.pieces; d.owner_ids = in.owner_ids; d.area_m2 = in.area_m2; d.completed_time_seconds = in.completed_time_seconds;
    d.left = in.left; d.right = in.right; d.initialization_energy = in.energy; d.consistent_mass = zeros(n);
    d.owner_left.fill(n); d.owner_right.fill(0);
    for (std::size_t j = 0; j < n; ++j) {
        k.scope(Stage::initialization, static_cast<std::uint32_t>(j));
        k.finite(in.positions_m[j], Field::position); k.finite(in.reference_nodes_m[j], Field::reference_position);
        k.finite(in.velocities_m_per_s[j], Field::velocity0);
        if (j != 0) k.require(in.positions_m[j] > in.positions_m[j - 1] && in.reference_nodes_m[j] > in.reference_nodes_m[j - 1],
            Code::unsupported_topology, "Initial canonical nodes are not strictly ordered");
        if (constrained(d, j)) k.require(in.velocities_m_per_s[j] == boundary_velocity(d, j),
            Code::unsupported_configuration, "Initial velocity conflicts with the prescribed endpoint");
    }
    const auto anchor = in.left.kind == CanonicalBoundaryKind::fixed ? std::size_t{0} :
        in.right.kind == CanonicalBoundaryKind::fixed ? n - 1 : n / 2;
    d.origin_high_m = in.positions_m[anchor];
    for (std::size_t j = 0; j < n; ++j) {
        d.positions_m[j] = difference(k, in.positions_m[j], d.origin_high_m);
        k.require(sum(k, {d.origin_high_m, d.positions_m[j]}) == in.positions_m[j],
            Code::numerical_precision, "Canonical initial relative coordinates cannot reconstruct the supplied geometry");
    }
    std::array<std::array<Accumulated, 4>, 7> masses{};
    std::size_t previous_owner = 0;
    for (std::size_t e = 0; e < in.pieces.size(); ++e) {
        const auto& piece = in.pieces[e];
        k.scope(Stage::reference_integration);
        k.require(piece.left == e && piece.right == e + 1 && piece.owner < 7 && (e != 0 || piece.owner == 0) &&
            (piece.owner == previous_owner || piece.owner == previous_owner + 1),
            Code::unsupported_topology, "Canonical reference-piece/owner incidence differs");
        previous_owner = piece.owner; d.owner_left[piece.owner] = std::min(d.owner_left[piece.owner], piece.left); d.owner_right[piece.owner] = piece.right;
        k.scope(Stage::reference_integration, static_cast<std::uint32_t>(piece.left), static_cast<std::uint32_t>(piece.owner));
        k.row(Field::reference_density, piece.density_left, 0); k.row(Field::reference_density, piece.density_right, 1);
        for (std::size_t s = 0; s < 4; ++s) {
            k.nonnegative(piece.density_left[s], Field::reference_density); k.nonnegative(piece.density_right[s], Field::reference_density);
            if (s >= species_count) k.require(piece.density_left[s] == 0 && piece.density_right[s] == 0,
                Code::unsupported_configuration, "Positive fine constituent has no supplied profile");
        }
        const auto metrics = reference_metrics(k, d, piece); Sum piece_mass(k);
        for (std::size_t s = 0; s < 4; ++s) {
            piece_mass.add(metrics.masses[s]); masses[piece.owner][s].add(k, metrics.masses[s], static_cast<std::uint32_t>(20 + s));
        }
        k.require(piece_mass.get() > 0, Code::unsupported_topology, "Canonical fine reference piece is empty");
        d.consistent_mass[piece.left][piece.left] = sum(k, {d.consistent_mass[piece.left][piece.left], metrics.consistent[0]});
        const double cross = sum(k, {d.consistent_mass[piece.left][piece.right], metrics.consistent[1]});
        d.consistent_mass[piece.left][piece.right] = cross; d.consistent_mass[piece.right][piece.left] = cross;
        d.consistent_mass[piece.right][piece.right] = sum(k, {d.consistent_mass[piece.right][piece.right], metrics.consistent[2]});
    }
    k.require(previous_owner == 6, Code::unsupported_topology, "Canonical reference does not cover all seven owners");
    k.owner_nodes(d.owner_left, d.owner_right); k.baseline(Field::velocity0, in.velocities_m_per_s);
    const auto& profiles = d.thermodynamics->reference().materials();
    for (std::size_t i = 0; i < 7; ++i) {
        k.scope(Stage::initialization, canonical_absent_index, static_cast<std::uint32_t>(i));
        k.require(d.owner_left[i] < d.owner_right[i] && (i == 0 || d.owner_left[i] == d.owner_right[i - 1]),
            Code::unsupported_topology, "Canonical owner partition contains a gap");
        auto& cell = d.cells[i]; cell.masses_kg.resize(species_count);
        for (std::size_t s = 0; s < species_count; ++s) cell.masses_kg[s] = masses[i][s].get(k, static_cast<std::uint32_t>(20 + s));
        cell.volume_m3 = owner_volume(k, d, i); k.positive(cell.volume_m3, Field::occupied_volume);
        k.row(Field::species_mass, cell.masses_kg);
        if (in.energy == CanonicalInitialEnergy::stored_internal_energy) cell.internal_energy_j = in.stored_internal_energy_j[i];
        else {
            k.positive(in.temperatures_k[i], Field::temperature); Vec registry_masses(species_count, 0);
            for (std::size_t s = 0; s < species_count; ++s) registry_masses[d.registry->index(profiles[s].id)] = cell.masses_kg[s];
            k.charge(8);
            const auto initialized = d.thermodynamics->initialize_at_temperature(registry_masses, in.temperatures_k[i], cell.volume_m3);
            const auto retained = d.thermodynamics->to_reference(initialized.mixture, cell.volume_m3);
            k.require(retained.masses_kg == cell.masses_kg, Code::numerical_precision, "Temperature initialization changed canonical constituent masses");
            cell.internal_energy_j = retained.internal_energy_j;
            d.initialization.caloric_energy_j[i] = initialized.caloric_reference_and_thermal_energy_j;
            d.initialization.explicitly_added_elastic_energy_j[i] = initialized.initialized_elastic_energy_j;
            const double represented = difference(k, cell.internal_energy_j, initialized.caloric_reference_and_thermal_energy_j);
            d.initialization.represented_elastic_energy_change_j[i] = represented;
            d.initialization.energy_addition_roundoff_j[i] = difference(k, represented, initialized.initialized_elastic_energy_j);
            Sum offset(k, 1), thermal(k, 2);
            for (std::size_t s = 0; s < species_count; ++s) {
                offset.add(k.mul(cell.masses_kg[s], profiles[s].energy_offset_j_per_kg));
                thermal.add(k.mul(k.mul(cell.masses_kg[s], profiles[s].cv_j_per_kg_k), in.temperatures_k[i]));
            }
            const double available = difference(k, cell.internal_energy_j, offset.get());
            k.scalar(Field::stored_internal_energy, cell.internal_energy_j);
            k.require(available > 0 && difference(k, available, initialized.initialized_elastic_energy_j) > 0,
                Code::numerical_precision, "Temperature initialization erased required thermal energy");
            const double required = sum(k, {thermal.get(), initialized.initialized_elastic_energy_j});
            k.within(difference(k, available, required), required, 64 * epsilon, "Initialization U does not represent its thermal/elastic ledger");
        }
        d.thermodynamics_readings[i] = evaluate_owner(k, d, cell, i);
    }
    k.scope(Stage::canonical_decode); double linear_ratio = 0;
    d.initialization.requested_kinetic_energy_j = kinetic(k, d, linear_ratio);
    d.momentum = multiply(k, d.consistent_mass, in.velocities_m_per_s, linear_ratio); k.baseline(Field::pi1, d.momentum);
    d.velocities_m_per_s = decode_velocity(k, d, linear_ratio);
    double quadrature = 0; update_readings(k, d, quadrature);
    d.initialization.represented_kinetic_energy_j = d.resolved_kinetic_energy_j;
    d.initialization.kinetic_arithmetic_difference_j = difference(k, d.resolved_kinetic_energy_j, d.initialization.requested_kinetic_energy_j);
    k.within(d.initialization.kinetic_arithmetic_difference_j,
        sum(k, {std::abs(d.resolved_kinetic_energy_j), std::abs(d.initialization.requested_kinetic_energy_j)}),
        256 * epsilon, "Initial canonical decoding changes kinetic energy beyond arithmetic resolution");
    d.initialization.maximum_quadrature_ratio = quadrature; d.initialization.maximum_linear_ratio = linear_ratio;
    CanonicalStepReport verification;
    for (std::size_t i = 0; i < 7; ++i) check_thermodynamics(k, d, d, i, verification);
    d.initialization.maximum_thermodynamic_ratio = verification.maximum_thermodynamic_ratio;
    return d;
}
}
}

namespace ma::experimental {
std::optional<CanonicalReadView> CanonicalMaterialState::read_view() const noexcept {
    if (!data_) return std::nullopt;
    return CanonicalReadView(data_);
}
void initialize_canonical_material_into(const CanonicalInitialization& input, CanonicalMaterialState& output,
    CanonicalBudgetContext& budget, CanonicalFailureWitness& witness) {
    using namespace canonical_detail;
    Kernel kernel(budget, witness, true, input.diagnostic_model_identity, 0);
    try {
        auto candidate = initialize_data(kernel, input);
        kernel.scope(Stage::publication); kernel.deadline();
        kernel.require(read_canonical_arithmetic_environment().supported(), Code::numerical_precision, "Canonical arithmetic controls changed during initialization");
        auto complete = std::make_shared<const CanonicalSnapshot>(std::move(candidate));
        static_assert(std::is_nothrow_move_assignable_v<decltype(output.data_)>);
        output.data_ = std::move(complete);
    } catch (const CanonicalFailure&) { throw; }
    catch (const ReferenceError& error) {
        kernel.scalar(Field::constitutive_error_code, static_cast<double>(error.code()));
        kernel.fail(reference_failure_code(error.code()), "Canonical initialization reference model rejected its input");
    }
    catch (const std::bad_alloc&) { kernel.fail(Code::unsupported_configuration, "Canonical initialization allocation failed"); }
    catch (const std::exception&) { kernel.fail(Code::unsupported_configuration, "Canonical initialization registry/model validation failed"); }
}
}
