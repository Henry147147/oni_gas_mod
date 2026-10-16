#include "canonical_material_internal.hpp"

namespace ma::experimental::canonical_detail {

ReferenceMetrics reference_metrics(Kernel& k, const Data& d, const CanonicalReferencePiece& piece) {
    ReferenceMetrics result;
    result.geometry = measure_geometry(k, retained_value(k, d.reference_nodes_m[piece.left]),
        retained_value(k, d.reference_nodes_m[piece.right]), d.area_m2);
    const double measure = result.geometry.projected_volume_m3;
    Sum rho_left(k, 1), rho_right(k, 2);
    for (std::size_t s = 0; s < 4; ++s) {
        result.masses[s] = k.mul(measure, sum(k, {k.mul(.5, piece.density_left[s]), k.mul(.5, piece.density_right[s])}));
        k.scalar(Field::species_mass, result.masses[s], static_cast<std::uint32_t>(s));
        k.require((piece.density_left[s] == 0 && piece.density_right[s] == 0) || result.masses[s] > 0,
            Code::numerical_precision, "Positive reference constituent lost its integrated mass");
        rho_left.add(piece.density_left[s]); rho_right.add(piece.density_right[s]);
    }
    const double left = rho_left.get(), right = rho_right.get(), factor = k.div(measure, 12);
    result.consistent = {k.mul(factor, sum(k, {k.mul(3, left), right})),
        k.mul(factor, sum(k, {left, right})), k.mul(factor, sum(k, {left, k.mul(3, right)}))};
    return result;
}
double owner_volume(Kernel& k, const Data& d, std::size_t owner) {
    return measure_geometry(k, d.primary.relative_positions_m[d.owner_left[owner]],
        d.primary.relative_positions_m[d.owner_right[owner]], d.area_m2).projected_volume_m3;
}
Vec volume_change(Kernel& k, const Data& d, const Vec& displacement) {
    Vec result(7);
    for (std::size_t i = 0; i < 7; ++i)
        result[i] = projected(k, scaled(k, terms({displacement[d.owner_right[i]], -displacement[d.owner_left[i]]}), d.area_m2));
    return result;
}
Vec exterior_force(Kernel& k, const Data& d) {
    Vec result(d.positions_m.size());
    result.front() = k.mul(d.area_m2, d.left.exterior_pressure_pa);
    result.back() = -k.mul(d.area_m2, d.right.exterior_pressure_pa);
    return result;
}
std::array<double, 7> pressure_components(Kernel& k, const Data& d, std::span<const double> pressure, std::size_t node) {
    k.require(pressure.size() == 7, Code::unsupported_configuration, "Canonical pressure owner count differs");
    std::array<double, 7> result{};
    for (std::size_t i = 0; i < 7; ++i) {
        if (node == d.owner_left[i]) result[i] = -k.mul(d.area_m2, pressure[i]);
        else if (node == d.owner_right[i]) result[i] = k.mul(d.area_m2, pressure[i]);
    }
    return result;
}
Vec pressure_force(Kernel& k, const Data& d, std::span<const double> pressure) {
    Vec result(d.positions_m.size());
    for (std::size_t j = 0; j < result.size(); ++j) {
        k.row_scope(static_cast<std::uint32_t>(j)); k.row(Field::work_pressure, pressure);
        const auto parts = pressure_components(k, d, pressure, j); Sum value(k);
        for (double part : parts) value.add(part);
        result[j] = value.get();
    }
    return result;
}

void update_readings(Kernel& k, Data& d, double& maximum_quadrature_ratio) {
    std::array<std::array<Accumulated, 10>, 7> totals{};
    std::array<Accumulated, 7> volumes{};
    d.pieces.clear(); d.pieces.reserve(d.reference_pieces.size());
    const auto species_count = d.thermodynamics->reference().materials().size();
    const double gauss_half_width = k.div(.5, k.sqrt(3));
    const std::array<double, 2> points{difference(k, .5, gauss_half_width), sum(k, {.5, gauss_half_width})};
    for (const auto& reference : d.reference_pieces) {
        const auto a = reference.left, b = reference.right, owner = reference.owner;
        k.scope(Stage::reference_integration, static_cast<std::uint32_t>(a), static_cast<std::uint32_t>(owner));
        k.row(Field::reference_density, reference.density_left, 0); k.row(Field::reference_density, reference.density_right, 1);
        k.row(Field::velocity1, d.velocities_m_per_s);
        k.scalar(Field::reference_position, d.reference_nodes_m[a], 0); k.scalar(Field::reference_position, d.reference_nodes_m[b], 1);
        k.scalar(Field::position, d.positions_m[a], 0); k.scalar(Field::position, d.positions_m[b], 1);
        const double before = difference(k, d.reference_nodes_m[b], d.reference_nodes_m[a]);
        const auto geometry = measure_geometry(k, d.primary.relative_positions_m[a], d.primary.relative_positions_m[b], d.area_m2);
        const double after = sum(k, {geometry.length_m.value.high(), geometry.length_m.value.low(),
            geometry.length_m.roundoff_remainders[0], geometry.length_m.roundoff_remainders[1]});
        k.require(before > 0 && after > 0, Code::unsupported_topology, "Canonical fine nodes crossed");
        volumes[owner].add(k, geometry.projected_volume_m3, 12);
        CanonicalPieceReading view; view.geometry = geometry;
        view.jacobian = k.div(after, before); k.scalar(Field::jacobian, view.jacobian);
        const auto metrics = reference_metrics(k, d, reference);
        view.reference_geometry = metrics.geometry;
        view.masses_kg = metrics.masses; view.consistent_mass = metrics.consistent;
        k.row(Field::species_mass, view.masses_kg); k.row(Field::matrix_coefficients, view.consistent_mass);
        for (std::size_t s = 0; s < 4; ++s) {
            view.density_left[s] = k.div(reference.density_left[s], view.jacobian);
            view.density_right[s] = k.div(reference.density_right[s], view.jacobian);
            k.require((reference.density_left[s] == 0 || view.density_left[s] > 0) &&
                (reference.density_right[s] == 0 || view.density_right[s] > 0),
                Code::numerical_precision, "Positive pushed constituent density was erased");
        }
        k.row(Field::reference_density, view.density_left, 2); k.row(Field::reference_density, view.density_right, 3);
        const auto& c = view.consistent_mass;
        const double w0 = sum(k, {c[0], c[1]}), w1 = sum(k, {c[1], c[2]});
        const double xa = physical_position(k, d, a);
        const double xb = physical_position(k, d, b);
        const double ua = d.velocities_m_per_s[a], ub = d.velocities_m_per_s[b];
        view.mass_kg = sum(k, {w0, w1});
        view.first_mass_moment = sum(k, {k.mul(w0, xa), k.mul(w1, xb)});
        view.second_mass_moment = sum(k, {k.mul(k.mul(c[0], xa), xa), k.mul(k.mul(k.mul(2, c[1]), xa), xb), k.mul(k.mul(c[2], xb), xb)});
        view.local_momentum = {sum(k, {k.mul(c[0], ua), k.mul(c[1], ub)}), sum(k, {k.mul(c[1], ua), k.mul(c[2], ub)})};
        view.momentum = sum(k, {k.mul(w0, ua), k.mul(w1, ub)});
        view.position_velocity_product = sum(k, {k.mul(xa, view.local_momentum[0]), k.mul(xb, view.local_momentum[1])});
        view.kinetic_energy_j = k.mul(.5, sum(k, {k.mul(ua, view.local_momentum[0]), k.mul(ub, view.local_momentum[1])}));
        k.require(view.kinetic_energy_j >= 0, Code::numerical_precision, "Negative piece consistent kinetic energy");

        std::array<Accumulated, 4> independent_masses{};
        std::array<Accumulated, 3> independent_matrix{};
        for (double t : points) {
            const double other = difference(k, 1, t); Sum density(k, 11);
            for (std::size_t s = 0; s < 4; ++s) {
                const double rho = sum(k, {k.mul(other, view.density_left[s]), k.mul(t, view.density_right[s])});
                const double mass = k.mul(k.mul(k.mul(.5, d.area_m2), after), rho);
                independent_masses[s].add(k, mass, static_cast<std::uint32_t>(4 + s)); density.add(rho);
            }
            const double mass = k.mul(k.mul(k.mul(.5, d.area_m2), after), density.get());
            independent_matrix[0].add(k, k.mul(k.mul(mass, other), other), 8);
            independent_matrix[1].add(k, k.mul(k.mul(mass, t), other), 9);
            independent_matrix[2].add(k, k.mul(k.mul(mass, t), t), 10);
        }
        for (std::size_t s = 0; s < 4; ++s) {
            const double independent = independent_masses[s].get(k, static_cast<std::uint32_t>(4 + s));
            const double error = difference(k, independent, view.masses_kg[s]);
            k.scalar(Field::species_mass, view.masses_kg[s], static_cast<std::uint32_t>(s));
            k.scalar(Field::matrix_products, independent, static_cast<std::uint32_t>(s));
            k.within(error, independent, 128 * epsilon, "Independent pushed-species quadrature differs from reference mass");
            maximum_quadrature_ratio = std::max(maximum_quadrature_ratio, k.ratio(error, independent));
        }
        for (std::size_t p = 0; p < 3; ++p) {
            const double independent = independent_matrix[p].get(k, static_cast<std::uint32_t>(8 + p));
            const double error = difference(k, independent, c[p]);
            k.scalar(Field::matrix_products, independent, static_cast<std::uint32_t>(p));
            k.within(error, independent, 128 * epsilon, "Independent current consistent matrix differs from reference matrix");
            maximum_quadrature_ratio = std::max(maximum_quadrature_ratio, k.ratio(error, independent));
        }
        auto& owner_totals = totals[owner];
        for (std::size_t s = 0; s < 4; ++s) owner_totals[s].add(k, view.masses_kg[s], static_cast<std::uint32_t>(100 + owner * 10 + s));
        const std::array<double, 6> properties{view.mass_kg, view.first_mass_moment, view.second_mass_moment,
            view.momentum, view.position_velocity_product, view.kinetic_energy_j};
        for (std::size_t p = 0; p < properties.size(); ++p)
            owner_totals[p + 4].add(k, properties[p], static_cast<std::uint32_t>(104 + owner * 10 + p));
        d.pieces.push_back(view);
    }
    Sum owner_kinetic(k, 0), owner_momentum(k, 1);
    for (std::size_t i = 0; i < 7; ++i) {
        k.scope(Stage::reference_integration, canonical_absent_index, static_cast<std::uint32_t>(i));
        k.within(difference(k, volumes[i].get(k, 12), d.cells[i].volume_m3), d.cells[i].volume_m3,
            32 * epsilon, "Fine volumes do not partition the retained owner volume");
        CanonicalOwnerReading owner; Sum mass(k, 2);
        for (std::size_t s = 0; s < 4; ++s) {
            owner.masses_kg[s] = totals[i][s].get(k, static_cast<std::uint32_t>(100 + i * 10 + s));
            const double stored = s < species_count ? d.cells[i].masses_kg[s] : 0;
            k.scalar(Field::species_mass, owner.masses_kg[s], static_cast<std::uint32_t>(s));
            k.require(owner.masses_kg[s] == stored, Code::numerical_precision, "Owner species differ from retained fine reference measures");
            mass.add(owner.masses_kg[s]);
        }
        owner.mass_kg = mass.get(); k.require(owner.mass_kg > 0, Code::unsupported_topology, "Canonical owner has no material");
        const double matrix_mass = totals[i][4].get(k, static_cast<std::uint32_t>(104 + i * 10));
        k.within(difference(k, matrix_mass, owner.mass_kg), std::max(matrix_mass, owner.mass_kg), 128 * epsilon,
            "Owner matrix mass differs from constituent inventory mass");
        owner.first_mass_moment = totals[i][5].get(k, static_cast<std::uint32_t>(105 + i * 10));
        owner.second_mass_moment = totals[i][6].get(k, static_cast<std::uint32_t>(106 + i * 10));
        owner.momentum = totals[i][7].get(k, static_cast<std::uint32_t>(107 + i * 10));
        owner.position_velocity_product = totals[i][8].get(k, static_cast<std::uint32_t>(108 + i * 10));
        owner.resolved_kinetic_energy_j = totals[i][9].get(k, static_cast<std::uint32_t>(109 + i * 10));
        owner.bulk_kinetic_energy_j = owner.momentum == 0 ? 0 : k.mul(.5, k.mul(owner.momentum, k.div(owner.momentum, owner.mass_kg)));
        owner.mechanical_variance_j = difference(k, owner.resolved_kinetic_energy_j, owner.bulk_kinetic_energy_j);
        k.scalar(Field::resolved_kinetic_energy, owner.resolved_kinetic_energy_j);
        k.scalar(Field::mechanical_variance, owner.mechanical_variance_j);
        k.within(std::min(0.0, owner.mechanical_variance_j), std::max(owner.resolved_kinetic_energy_j, owner.bulk_kinetic_energy_j),
            64 * epsilon, "Owner resolved kinetic energy is below the reading bridge's bulk bound");
        d.owners[i] = owner; owner_kinetic.add(owner.resolved_kinetic_energy_j); owner_momentum.add(owner.momentum);
    }
    k.scope(Stage::reference_integration);
    double product_ratio = 0; d.resolved_kinetic_energy_j = kinetic(k, d, product_ratio);
    const double split = owner_kinetic.get();
    k.within(difference(k, split, d.resolved_kinetic_energy_j), sum(k, {std::abs(split), std::abs(d.resolved_kinetic_energy_j)}),
        128 * epsilon, "Owner kinetic partition differs from the full consistent quadratic");
    Sum canonical_momentum(k, 2), compatibility_scale(k, 3);
    for (std::size_t j = 0; j < d.momentum.size(); ++j) {
        canonical_momentum.add(d.primary.momentum[j].high()); canonical_momentum.add(d.primary.momentum[j].low());
        compatibility_scale.add(std::abs(d.primary.momentum[j].high())); compatibility_scale.add(std::abs(d.primary.momentum[j].low()));
        const auto terms = physical_row_products(k, d.consistent_mass, d.velocities_m_per_s, j);
        for (double term : terms) compatibility_scale.add(std::abs(term));
    }
    k.within(difference(k, owner_momentum.get(), canonical_momentum.get()), compatibility_scale.get(), 256 * epsilon,
        "Derived owner momentum differs from actual canonical momentum beyond compatibility arithmetic");
}
}
