#include "mac_compression_fixture.hpp"
#include <iomanip>

namespace {
using namespace ma;
using namespace ma::experimental;
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
MacTransportState source(std::size_t id) {
    const auto& set = ma::testing::setup(); auto s = id == 2 ? set.state(2, 2, true) : set.state(3, id == 0 ? 2 : 3, false, id == 1, id == 1);
    const auto m = mac_face_inertias(set.registry, s, *set.eos);
    if (id == 2) {
        std::vector<MacScaled> j(s.momentum_x.size());
        for (std::size_t f = 0; f < j.size(); ++f) j[f] = MacScaled::from_parts(f % 2 == 0 ? .75 : -.75, -1941);
        store_mac_field(s.momentum_x, s.full_momentum_x, j);
    } else {
        for (std::size_t f = 0; f < s.momentum_x.size(); ++f) if (!ma::testing::compression_fixed_face(s.grid, 0, f))
            s.momentum_x[f] = m.x[f] * (f % s.grid.x_width() == 1 ? 12 : -8);
    }
    return s;
}
void combined(std::size_t id) {
    const auto& set = ma::testing::setup(); const auto s = source(id); const auto held = ma::testing::fingerprint(s); constexpr double dt = 1e-4;
    MacClosedSettings settings; settings.maximum_timestep_s = dt;
    const auto primitive = advance_mac_closed_substep(set.registry, *set.eos, s, dt, settings);
    const auto compression = advance_mac_compression(set.registry, *set.eos, primitive.state, dt);
    const MacClosedWorld owner(set.registry, set.eos, s, settings); const auto result = owner.advanced(dt, 4); const auto& r = result.report;
    check(ma::testing::fingerprint(s) == held && ma::testing::fingerprint(owner.state()) == held && owner.time_s() == 0 && owner.revision() == 1 &&
        result.world.time_s() == dt && result.world.revision() == 2 && r.substeps == 1 && r.attempts == 1 && r.rejected_attempts == 0, "Compression owner atomic state/clock differs");
    check(ma::testing::fingerprint(result.world.state()) == ma::testing::fingerprint(compression.state), "Owner did not apply the declared compression stage");
    check(r.compression_events == compression.report.events.size() && r.compression_events > 0 && r.unresolved_compression_heat_updates == compression.report.unresolved_heat_updates &&
        r.unresolved_compression_momentum_updates == compression.report.unresolved_momentum_updates && r.compression_heat_wide_j.mantissa() == compression.report.heat.mantissa() &&
        r.compression_heat_wide_j.exponent() == compression.report.heat.exponent(), "Owner compression report differs");
    if (id == 2) check(r.compression_heat_wide_j.mantissa() > 0 && r.compression_heat_j == 0 && r.unresolved_compression_heat_updates > 0, "Owner lost below-range heat/update flag");
    ma::testing::export_case(static_cast<int>(id), s, primitive, dt);
    ma::testing::export_compression(id, primitive.state, {result.world.state(), compression.report}, dt, "C");
    std::cout << "OWNER " << id << ' ' << owner.time_s() << ' ' << owner.revision() << ' ' << result.world.time_s() << ' ' << result.world.revision() << ' ' <<
        r.substeps << ' ' << r.attempts << ' ' << r.rejected_attempts << ' ' << r.compression_events << ' ' << r.unresolved_compression_heat_updates << ' ' << r.unresolved_compression_momentum_updates << ' ' << r.compression_heat_j;
    for (const auto& value : {r.compression_heat_wide_j, r.energy_residual_wide_j, r.energy_bound_wide_j, r.full_wall_impulse_x, r.full_wall_impulse_y}) ma::testing::export_compression_scaled(value);
    std::cout << '\n';
}
void controls() {
    const auto& set = ma::testing::setup(); const auto s = source(0); const auto held = ma::testing::fingerprint(s); constexpr double dt = 1e-4;
    MacClosedSettings settings; settings.maximum_timestep_s = dt; settings.compression_viscosity = MacCompressionViscosity::none;
    const auto primitive = advance_mac_closed_substep(set.registry, *set.eos, s, dt, settings);
    const MacClosedWorld disabled(set.registry, set.eos, s, settings); const auto result = disabled.advanced(dt, 4);
    check(ma::testing::fingerprint(result.world.state()) == ma::testing::fingerprint(primitive.state) && result.report.compression_events == 0 &&
        result.report.compression_heat_wide_j.is_zero(), "Disabled reference mode applied compression");
    settings.compression_viscosity = MacCompressionViscosity::quadratic; const MacClosedWorld owner(set.registry, set.eos, s, settings);
    bool rollback = false; try { static_cast<void>(owner.advanced(2 * dt, 1)); } catch (const std::runtime_error&) { rollback = true; }
    check(rollback && owner.time_s() == 0 && owner.revision() == 1 && ma::testing::fingerprint(owner.state()) == held, "Later failure published a compressed candidate");
    settings.compression_viscosity = static_cast<MacCompressionViscosity>(-1); bool invalid = false;
    try { static_cast<void>(MacClosedWorld(set.registry, set.eos, s, settings)); } catch (const std::invalid_argument&) { invalid = true; }
    check(invalid && ma::testing::fingerprint(s) == held, "Invalid compression mode accepted or changed source");
    std::cout << "OCONTROL 1 1 1\n";
}
}
int main() { try {
    require_mac_energy_environment(); std::cout << std::setprecision(17); const auto& set = ma::testing::setup();
    for (const auto& m : set.eos->reference().materials()) std::cout << "MATERIAL " << set.registry.index(m.id) << ' ' << static_cast<int>(m.phase) << ' ' << m.molar_mass_kg_per_mol << ' ' << m.cv_j_per_kg_k << ' ' << m.energy_offset_j_per_kg << ' ' << m.zero_pressure_density_kg_per_m3 << ' ' << m.reference_bulk_modulus_pa << '\n';
    combined(0); std::cout << "PASS gas owner compression and wall accounts\n";
    combined(1); std::cout << "PASS mixed-liquid owner compression and actual U\n";
    combined(2); std::cout << "PASS full-range owner heat and unresolved increments\n";
    controls(); std::cout << "PASS disabled reference and atomic later failure\n"; return 0;
} catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; } }
