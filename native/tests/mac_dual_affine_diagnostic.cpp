#include "mac_fixture.hpp"
#include <array>
#include <iomanip>

namespace {
using namespace ma;
using namespace ma::experimental;
void check(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
void emit(MacScaled value) { std::cout << ' ' << value.mantissa() << ' ' << value.exponent(); }
void run(int id, int axis, int control, MacMomentumReconstruction mode) {
    const auto& set = ma::testing::setup(); constexpr std::size_t n = 10;
    auto s = set.state(axis == 0 ? n : 2, axis == 0 ? 2 : n, false);
    auto& g = s.grid; g.periodic_x = axis == 1; g.periodic_y = axis == 0;
    const auto ni = set.registry.index("nitrogen"), he = set.registry.index("helium");
    const double a = control == 2 ? 1 : .25, b = control == 2 ? 0 : .125;
    std::array<double, n + 1> q{};
    for (std::size_t f = 1; f < n; ++f)
        q[f] = control == 3 ? 0 : control == 1 ? .125 : static_cast<double>(f - 1) / 64;
    MacMassFluxes flux{std::vector<double>(g.x_width() * g.height), std::vector<double>(g.width * g.y_height())};
    std::vector<double> volumes(s.cells.size());
    for (std::size_t i = 0; i < s.cells.size(); ++i) {
        const auto coordinate = axis == 0 ? i % g.width : i / g.width;
        volumes[i] = 1 + q[coordinate + 1] - q[coordinate];
        std::vector<double> masses(set.registry.size()); masses[ni] = volumes[i]; masses[he] = volumes[i] * 1e-28;
        s.cells[i].mixture = set.eos->initialize_at_temperature(masses, 300, volumes[i]).mixture;
        s.cells[i].passive_quantities = {{17, volumes[i]}, {UINT64_C(0x80000000), volumes[i] * 1e-30}};
    }
    const auto inertia = mac_face_inertias(set.registry, s, *set.eos, volumes);
    s.momentum_x.assign(inertia.x.size(), 0); s.momentum_y.assign(inertia.y.size(), 0);
    for (std::size_t other = 0; other < 2; ++other) for (std::size_t f = 0; f <= n; ++f) {
        const auto face = axis == 0 ? g.x_face(f, other) : g.y_face(other, f);
        (axis == 0 ? flux.x : flux.y)[face] = q[f];
        if (f != 0 && f != n) (axis == 0 ? s.momentum_x : s.momentum_y)[face] =
            (axis == 0 ? inertia.x : inertia.y)[face] * (a + b * (static_cast<double>(f) + q[f]));
    }
    const auto held = ma::testing::fingerprint(s);
    const auto plan = reconstruct_mac_swept_parcels(set.registry, *set.eos, s, volumes, flux);
    const auto advanced = advance_mac_parcel_transport(set.registry, s, *set.eos, plan, mode);
    check(ma::testing::fingerprint(s) == held, "Diagnostic transport changed its source");
    std::cout << "DCASE " << id << ' ' << axis << ' ' << control << ' ' << static_cast<int>(mode) << ' ' << a << ' ' << b << '\n';
    for (std::size_t f = 0; f <= n; ++f) std::cout << "DSWEEP " << id << ' ' << f << ' ' << q[f] << '\n';
    const auto& report = axis == 0 ? advanced.report.x : advanced.report.y;
    const auto actual = axis == 0 ? mac_field_values(advanced.state.momentum_x, advanced.state.full_momentum_x) :
        mac_field_values(advanced.state.momentum_y, advanced.state.full_momentum_y);
    for (std::size_t other = 0; other < 2; ++other) for (std::size_t f = 4; f <= 6; ++f) {
        const auto face = axis == 0 ? g.x_face(f, other) : g.y_face(other, f);
        MacScaledSum candidate; candidate.add(mode == MacMomentumReconstruction::donor_cell ? actual[face] : report.upwind_momentum[face]);
        double minimum_limiter = 1;
        for (const auto& event : report.corrections) {
            const auto& edge = report.fluxes[event.dual_edge];
            if (edge.left == face) candidate.add(event.pair.requested_impulse.negated());
            if (edge.right == face) candidate.add(event.pair.requested_impulse);
            if (edge.left == face || edge.right == face) minimum_limiter = std::min(minimum_limiter, event.pair.limiter.require_representable());
        }
        const auto expected = MacScaled::from_double(report.new_inertia[face] * (a + b * static_cast<double>(f)));
        std::cout << "DNODE " << id << ' ' << other << ' ' << f << ' ' << face << ' '
            << static_cast<double>(f - 1) + q[f - 1] << ' ' << static_cast<double>(f) + q[f] << ' '
            << static_cast<double>(f + 1) + q[f + 1] << ' ' << report.old_inertia[face] << ' ' << report.new_inertia[face];
        emit(actual[face]); emit(candidate.value()); emit(expected);
        std::cout << ' ' << minimum_limiter << '\n';
    }
    std::cout << "DUNCHANGED " << id << " 1\n";
    ma::testing::export_transport(id, s, advanced, plan);
}
}
int main() { try {
    require_mac_energy_environment(); std::cout << std::setprecision(17);
    const auto& set = ma::testing::setup();
    for (const auto& m : set.eos->reference().materials()) std::cout << "DMATERIAL " << set.registry.index(m.id) << ' '
        << (m.phase == ReferencePhase::ideal_gas ? 0 : 1) << ' ' << m.molar_mass_kg_per_mol << ' ' << m.cv_j_per_kg_k << ' '
        << m.energy_offset_j_per_kg << ' ' << m.zero_pressure_density_kg_per_m3 << ' ' << m.reference_bulk_modulus_pa << '\n';
    int id = 0;
    for (int axis = 0; axis < 2; ++axis) for (int control = 0; control < 4; ++control)
        for (const auto mode : {MacMomentumReconstruction::donor_cell, MacMomentumReconstruction::limited_linear, MacMomentumReconstruction::quadratic_limited_linear})
            run(id++, axis, control, mode);
    std::cout << "DCOMPLETE " << id << '\n'; return 0;
} catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; } }
