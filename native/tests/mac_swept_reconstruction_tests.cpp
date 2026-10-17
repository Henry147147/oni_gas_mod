#include "mac_fixture.hpp"
#include <array>
#include <fstream>
#include <iomanip>
#include <iterator>

namespace {
using namespace ma;
using namespace ma::experimental;
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
void near(double actual, double expected) {
    check(std::isfinite(actual) && std::abs(actual - expected) <=
        256 * std::numeric_limits<double>::epsilon() * (std::abs(actual) + std::abs(expected)),
        "Swept reconstruction affine comparison failed");
}
struct AffineSetup {
    SpeciesRegistry registry;
    std::shared_ptr<const FluidThermodynamics> eos;
    std::size_t nitrogen = 0, helium = 0;
    AffineSetup() {
        const auto& original = ma::testing::setup();
        for (auto species : original.registry.all()) {
            if (species.id == "nitrogen") species.formation_enthalpy = species.cp * reference_temperature * species.molar_mass;
            registry.add(std::move(species));
        }
        auto definitions = original.eos->reference().materials();
        for (auto& material : definitions) material.energy_offset_j_per_kg = registry[registry.index(material.id)].internal_energy(0);
        eos = std::make_shared<const FluidThermodynamics>(registry, "mac-swept-affine-controls-v1", std::move(definitions));
        nitrogen = registry.index("nitrogen"); helium = registry.index("helium");
    }
    void fill(MacMaterialCell& cell, double volume, double x, double y) const {
        cell.mixture = MixtureInventory(registry.size());
        cell.mixture.masses[nitrogen] = volume; cell.mixture.masses[helium] = 0x1p-100 * volume;
        cell.mixture.energy = 1048576 * volume;
        cell.passive_quantities = {{17, volume * (2 + .125 * x + .0625 * y)},
            {18, volume * (3 - .03125 * x + .125 * y)}, {UINT64_C(0x80000000), 0x1p-200 * volume}};
    }
};
double passive(const MacMaterialCell& cell, std::uint64_t id) {
    const auto found = std::find_if(cell.passive_quantities.begin(), cell.passive_quantities.end(),
        [&](const PassiveQuantity& value) { return value.identity == id; });
    return found == cell.passive_quantities.end() ? 0 : found->quantity;
}
void export_plan(int control, const AffineSetup& setup, const MacTransportState& source, const MacMassFluxes& q, const MacParcelPlan& plan) {
    const auto& g = source.grid;
    std::cout << "SGRID " << control << ' ' << g.width << ' ' << g.height << ' ' << g.spacing_m << ' ' << g.depth_m << ' ' << g.periodic_x << ' ' << g.periodic_y << '\n';
    const auto print_cell = [&](const MacMaterialCell& cell) {
        std::cout << ' ' << cell.mixture.masses[setup.nitrogen] << ' ' << cell.mixture.masses[setup.helium] << ' ' << cell.mixture.energy
            << ' ' << passive(cell, 17) << ' ' << passive(cell, 18) << ' ' << passive(cell, UINT64_C(0x80000000));
    };
    for (std::size_t i = 0; i < source.cells.size(); ++i) {
        std::cout << "SCELL " << control << ' ' << i << ' ' << plan.source_volumes_m3[i] << ' ' << plan.retained_volumes_m3[i] << ' ' << plan.limiter[i];
        print_cell(source.cells[i]); print_cell(plan.retained[i]); std::cout << '\n';
    }
    for (std::size_t axis = 0; axis < 2; ++axis) {
        const auto& values = axis == 0 ? q.x : q.y;
        for (std::size_t f = 0; f < values.size(); ++f) std::cout << "SSWEEP " << control << ' ' << axis << ' ' << f << ' ' << values[f] << '\n';
    }
    for (const auto& parcel : plan.parcels) {
        const auto contents = mac_parcel_contents_at(parcel, parcel.volume_m3);
        std::cout << "SPARC " << control << ' ' << parcel.axis << ' ' << parcel.face << ' ' << parcel.donor << ' ' << parcel.target << ' ' << parcel.volume_m3;
        print_cell(contents); std::cout << '\n';
    }
}
void parcel_check(int control, const AffineSetup& setup, const MacParcelPlan& plan, std::size_t donor,
    std::size_t axis, std::size_t face, double x, double y) {
    const auto found = std::find_if(plan.parcels.begin(), plan.parcels.end(), [&](const MacPrimalParcel& p) {
        return p.donor == donor && p.axis == axis && p.face == face;
    });
    check(found != plan.parcels.end() && plan.limiter[donor] == 1 && plan.unresolved_stencil_cells == 0, "Swept affine donor was flattened or limited");
    const auto c = mac_parcel_contents_at(*found, found->volume_m3);
    near(c.mixture.masses[setup.nitrogen], found->volume_m3);
    near(c.mixture.masses[setup.helium], found->volume_m3 * 0x1p-100);
    near(c.mixture.energy, found->volume_m3 * 1048576);
    near(passive(c, 17), found->volume_m3 * (2 + .125 * x + .0625 * y));
    near(passive(c, 18), found->volume_m3 * (3 - .03125 * x + .125 * y));
    near(passive(c, UINT64_C(0x80000000)), found->volume_m3 * 0x1p-200);
    std::cout << "SAFFINE " << control << ' ' << donor << ' ' << axis << ' ' << face << ' ' << x << ' ' << y << '\n';
}
void axial_affine(const AffineSetup& setup) {
    constexpr std::array<double, 7> displacement{0, 1. / 64, 4. / 64, 5. / 64, 7. / 64, 4. / 64, 0};
    for (int axis = 0; axis < 2; ++axis) for (int sign : {-1, 1}) for (int scaled_grid = 0; scaled_grid < 2; ++scaled_grid) {
        const int control = axis * 4 + (sign > 0 ? 2 : 0) + scaled_grid;
        auto source = ma::testing::setup().state(axis == 0 ? 6U : 2U, axis == 0 ? 2U : 6U, false);
        auto& g = source.grid; g.periodic_x = axis == 1; g.periodic_y = axis == 0;
        g.spacing_m = scaled_grid ? .25 : 1; g.depth_m = scaled_grid ? 2 : 1; const double base = g.volume_m3();
        source.momentum_x.assign(g.x_width() * g.height, 0); source.momentum_y.assign(g.width * g.y_height(), 0);
        MacMassFluxes q{std::vector<double>(source.momentum_x.size()), std::vector<double>(source.momentum_y.size())};
        std::vector<double> volumes(source.cells.size());
        for (std::size_t y = 0; y < g.height; ++y) for (std::size_t x = 0; x < g.width; ++x) {
            const auto n = axis == 0 ? x : y, i = x + y * g.width;
            const double centre = static_cast<double>(n) + .5 + .5 * sign * (displacement[n] + displacement[n + 1]);
            volumes[i] = base * (1 + sign * (displacement[n + 1] - displacement[n]));
            // The transverse periodic coordinate is constant, so this is an
            // affine field on its periodic covering without a seam in the data.
            setup.fill(source.cells[i], volumes[i], axis == 0 ? centre : 0, axis == 1 ? centre : 0);
        }
        if (axis == 0) for (std::size_t y = 0; y < g.height; ++y) for (std::size_t x = 0; x < g.x_width(); ++x)
            q.x[g.x_face(x, y)] = sign * base * displacement[x];
        if (axis == 1) for (std::size_t y = 0; y < g.y_height(); ++y) for (std::size_t x = 0; x < g.width; ++x)
            q.y[g.y_face(x, y)] = sign * base * displacement[y];
        const auto held = ma::testing::fingerprint(source);
        const auto plan = reconstruct_mac_swept_parcels(setup.registry, *setup.eos, source, volumes, q);
        const auto donor_n = sign > 0 ? std::size_t(2) : std::size_t(3);
        const auto donor = axis == 0 ? donor_n : donor_n * g.width;
        const auto face = axis == 0 ? g.x_face(3, 0) : g.y_face(0, 3);
        const double slab = 3 + .5 * sign * displacement[3];
        parcel_check(control, setup, plan, donor, static_cast<std::size_t>(axis), face, axis == 0 ? slab : 0, axis == 1 ? slab : 0);
        check(ma::testing::fingerprint(source) == held, "Swept axial reconstruction mutated its source");
        export_plan(control, setup, source, q, plan);
    }
}
void oblique_affine(const AffineSetup& setup) {
    for (int shear = 0; shear < 2; ++shear) for (int sign : {-1, 1}) {
        const int control = 8 + 2 * shear + (sign > 0 ? 1 : 0);
        auto source = ma::testing::setup().state(8, 8, false); const auto& g = source.grid;
        MacMassFluxes q{std::vector<double>(source.momentum_x.size()), std::vector<double>(source.momentum_y.size())};
        const auto u = [&](std::size_t y) { return sign * (shear ? (2 + static_cast<double>(y)) / 128 : .0625); };
        const auto v = [&](std::size_t x) { return sign * (shear ? (static_cast<double>(x) - 1) / 256 : .03125); };
        for (std::size_t y = 0; y < g.height; ++y) for (std::size_t x = 1; x < g.width; ++x) q.x[g.x_face(x, y)] = u(y);
        for (std::size_t y = 1; y < g.height; ++y) for (std::size_t x = 0; x < g.width; ++x) q.y[g.y_face(x, y)] = v(x);
        std::vector<double> volumes(source.cells.size());
        for (std::size_t y = 0; y < g.height; ++y) for (std::size_t x = 0; x < g.width; ++x) {
            const auto i = x + y * g.width;
            volumes[i] = 1 + q.x[g.x_face(x + 1, y)] - q.x[g.x_face(x, y)] + q.y[g.y_face(x, y + 1)] - q.y[g.y_face(x, y)];
            double cx = static_cast<double>(x) + .5, cy = static_cast<double>(y) + .5;
            if (x > 0 && y > 0 && x + 1 < g.width && y + 1 < g.height) {
                cx += u(y) + .25 * v(x) * (u(y + 1) - u(y - 1));
                cy += v(x) + .25 * u(y) * (v(x + 1) - v(x - 1));
            }
            setup.fill(source.cells[i], volumes[i], cx, cy);
        }
        const auto held = ma::testing::fingerprint(source);
        const auto plan = reconstruct_mac_swept_parcels(setup.registry, *setup.eos, source, volumes, q);
        const auto donor = 3 + 3 * g.width, face_x = g.x_face(sign > 0 ? 4U : 3U, 3), face_y = g.y_face(3, sign > 0 ? 4U : 3U);
        const auto xf = sign > 0 ? std::size_t(4) : std::size_t(3), yf = sign > 0 ? std::size_t(4) : std::size_t(3);
        parcel_check(control, setup, plan, donor, 0, face_x, static_cast<double>(xf) + .5 * u(3), 3.5 + .25 * (v(xf - 1) + v(xf)));
        parcel_check(control, setup, plan, donor, 1, face_y, 3.5 + .25 * (u(yf - 1) + u(yf)), static_cast<double>(yf) + .5 * v(3));
        check(ma::testing::fingerprint(source) == held, "Swept oblique reconstruction mutated its source");
        export_plan(control, setup, source, q, plan);
    }
}
void rejection_and_identity() {
    const auto& setup = ma::testing::setup(); const auto original = setup.state(4, 3, false, true, true);
    const auto held = ma::testing::fingerprint(original); std::vector<double> volumes(original.cells.size(), 1); volumes[5] = 0;
    const MacMassFluxes zero{std::vector<double>(original.momentum_x.size()), std::vector<double>(original.momentum_y.size())};
    const auto plan = reconstruct_mac_swept_parcels(setup.registry, *setup.eos, original, volumes, zero);
    const auto same = advance_mac_parcel_transport(setup.registry, original, *setup.eos, plan);
    check(plan.parcels.empty() && ma::testing::fingerprint(same.state) == held, "Swept zero-flux/wall identity changed state");
    for (int control = 0; control < 4; ++control) {
        auto source = original; auto bad_volumes = volumes; auto q = zero;
        if (control == 0) bad_volumes[0] *= 1.01;
        if (control == 1) q.x[source.grid.x_face(0, 0)] = .125;
        if (control == 2) q.x[source.grid.x_face(1, 1)] = .125;
        if (control == 3) {
            for (std::size_t i = 0; i < source.cells.size(); ++i) if (!source.grid.walls[i])
                source.cells[i].passive_quantities = {{17, std::numeric_limits<double>::denorm_min()}};
            q.x[source.grid.x_face(1, 0)] = .125; bad_volumes[0] += .125; bad_volumes[1] -= .125;
        }
        const auto before = ma::testing::fingerprint(source); bool caught = false;
        try { (void)reconstruct_mac_swept_parcels(setup.registry, *setup.eos, source, bad_volumes, q); }
        catch (const FluidFatalError& error) { caught = control != 3 || error.code() == FluidFatalCode::numerical_precision; }
        catch (const std::exception&) { caught = control != 3; }
        check(caught && before == ma::testing::fingerprint(source), "Swept malformed geometry or positive underflow was silently accepted");
    }
    MacClosedSettings invalid; invalid.reconstruction = static_cast<MacMaterialReconstruction>(3); bool caught = false;
    try { invalid.validate(); } catch (const std::invalid_argument&) { caught = true; }
    check(caught && ma::testing::fingerprint(original) == held, "Swept selector admission or source atomicity failed");
}
MacClosedWorld checkpoint_owner(bool wet, MacShockHeatRegularization heat, MacCompressionViscosity compression) {
    const auto& setup = ma::testing::setup(); auto source = setup.state(4, 3, false, wet, true);
    const auto inertia = mac_face_inertias(setup.registry, source, *setup.eos);
    source.momentum_x[source.grid.x_face(2, 0)] = .1 * inertia.x[source.grid.x_face(2, 0)];
    source.momentum_y[source.grid.y_face(3, 1)] = -.025 * inertia.y[source.grid.y_face(3, 1)];
    MacClosedSettings settings; settings.reconstruction = MacMaterialReconstruction::swept_first_moment;
    settings.gravity_m_per_s2 = 9.81; settings.thermal_conduction = !wet; settings.shock_heat = heat; settings.compression_viscosity = compression;
    return MacClosedWorld(setup.registry, setup.eos, std::move(source), settings).advanced(.002).world;
}
void owner_and_checkpoint() {
    const auto& setup = ma::testing::setup();
    for (const bool wet : {false, true}) for (const auto heat : {MacShockHeatRegularization::none, MacShockHeatRegularization::compression_activated,
        MacShockHeatRegularization::pressure_sensed_acoustic, MacShockHeatRegularization::pressure_sensed_series})
        for (const auto compression : {MacCompressionViscosity::none, MacCompressionViscosity::quadratic, MacCompressionViscosity::acoustic_quadratic}) {
            const auto source = checkpoint_owner(wet, heat, compression); const auto image = source.checkpoint();
            const auto restored = MacClosedWorld::from_checkpoint(setup.registry, setup.eos, image);
            check(restored.checkpoint() == image && restored.settings().reconstruction == MacMaterialReconstruction::swept_first_moment,
                "Swept selector or complete checkpoint state did not round trip");
            const auto expected = source.advanced(.002), resumed = restored.advanced(.002);
            check(expected.world.checkpoint() == resumed.world.checkpoint() && source.checkpoint() == image &&
                expected.report.substeps == resumed.report.substeps && expected.report.rejected_attempts == resumed.report.rejected_attempts,
                "Swept checkpoint continuation or source atomicity differs");
            auto options = source.settings(); options.gravity_m_per_s2 = 0;
            const auto direct = advance_mac_closed_substep(setup.registry, *setup.eos, source.state(), 1e-5, options);
            const auto plan = reconstruct_mac_swept_parcels(setup.registry, *setup.eos, direct.pressure_state, direct.material_volumes_m3, direct.volume_fluxes_m3);
            const auto result = advance_mac_parcel_transport(setup.registry, direct.pressure_state, *setup.eos, plan, source.settings().momentum_reconstruction);
            check(direct.reconstruction && ma::testing::fingerprint(result.state) == ma::testing::fingerprint(direct.state),
                "Closed owner did not dispatch the swept material reconstruction");
            std::cout << "SCHECKPOINT " << wet << ' ' << static_cast<int>(heat) << ' ' << static_cast<int>(compression) << ' '
                << source.time_s() << ' ' << resumed.world.time_s() << ' ' << expected.report.substeps << ' ' << expected.report.rejected_attempts << '\n';
        }
}
}
int main(int argc, char** argv) {
    try {
        std::cout << std::setprecision(17);
        if (argc == 3) {
            const auto& setup = ma::testing::setup(); const std::string operation = argv[1];
            if (operation == "--write-checkpoint") {
                const auto image = checkpoint_owner(false, MacShockHeatRegularization::pressure_sensed_series, MacCompressionViscosity::acoustic_quadratic).checkpoint();
                std::ofstream stream(argv[2], std::ios::binary); stream.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
                check(stream.good(), "Could not write swept cross-platform checkpoint");
            } else if (operation == "--read-checkpoint") {
                std::ifstream stream(argv[2], std::ios::binary); check(stream.good(), "Could not read swept cross-platform checkpoint");
                const std::vector<std::uint8_t> image{std::istreambuf_iterator<char>(stream), {}};
                check(MacClosedWorld::from_checkpoint(setup.registry, setup.eos, image).checkpoint() == image, "Swept cross-platform checkpoint changed bytes");
            } else throw std::invalid_argument("Unknown swept checkpoint operation");
            std::cout << "PASS swept cross-platform checkpoint bytes\n"; return 0;
        }
        check(argc == 1, "Invalid swept reconstruction arguments"); const AffineSetup setup;
        axial_affine(setup); std::cout << "PASS moved-centroid axial affine parcels, both signs/axes and scaled geometry\n";
        oblique_affine(setup); std::cout << "PASS oblique translation and shear affine parcels with transverse CTU centroids\n";
        rejection_and_identity(); std::cout << "PASS swept zero-flux wall identity, invalid geometry/selector and positive trace underflow rollback\n";
        owner_and_checkpoint(); std::cout << "PASS swept owner dispatch and exact checkpoint continuation for 24 gas/liquid compression/heat combinations\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
