// Sharp gas/liquid interfaces (MacMaterialReconstruction::interface_plic): a
// liquid square translating through air at uniform velocity and pressure.
// PLIC keeps the liquid in its own cells, with exact conservation and no
// spurious acceleration at the density jump. Limited-linear is reported for
// comparison only.
#include "mac_kernel_fixture.hpp"
#include <iomanip>
#include <optional>

namespace {
using namespace ma;
using namespace ma::experimental;
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
struct Measure { std::size_t mixed = 0; double mist = 0, liquid = 0, velocity_error = 0; std::optional<MacClosedWorld> world; };
Measure translate(MacMaterialReconstruction reconstruction, std::size_t steps, MacExecutionPolicy policy = MacExecutionPolicy::reference) {
    const auto& s = ma::testing::setup(); const auto water = s.registry.index("water");
    constexpr std::size_t n = 16, lo = 5, hi = 11; constexpr double h = .01, ux = .3, uy = .2;
    const double volume = h * h;
    MacTransportState st; st.grid = {n, n, h, 1, true, true, std::vector<std::uint8_t>(n * n)};
    for (std::size_t y = 0; y < n; ++y) for (std::size_t x = 0; x < n; ++x) {
        MacMaterialCell c{MixtureInventory(s.registry.size()), {}};
        c.mixture = s.mixture(volume, 101325, 300, x >= lo && x < hi && y >= lo && y < hi ? 1. : 0.);
        st.cells.push_back(std::move(c));
    }
    const auto faces = mac_face_inertias(s.registry, st, *s.eos);
    st.momentum_x.assign(faces.x.size(), 0); st.momentum_y.assign(faces.y.size(), 0);
    ma::testing::kernel::set_velocity(s, st, ux, uy);
    MacClosedSettings settings; settings.reconstruction = reconstruction;
    MacClosedWorld world(s.registry, s.eos, st, settings);
    const auto total = [&](const MacClosedWorld& w) { long double m = 0; for (const auto& c : w.state().cells) m += c.mixture.masses[water]; return m; };
    const long double initial = total(world);
    for (std::size_t k = 0; k < steps; ++k) world = world.advanced(.01, 4096, policy).world;
    Measure result; const auto& final = world.state();
    for (const auto& c : final.cells) {
        if (c.mixture.masses[water] == 0) continue;
        const double alpha = s.eos->evaluate(c.mixture, volume).liquid_volume_m3 / volume;
        if (alpha > 1e-6 && alpha < 1 - 1e-6) ++result.mixed;
        if (alpha < .01) result.mist += c.mixture.masses[water];
    }
    result.mist /= static_cast<double>(initial);
    result.liquid = static_cast<double>((total(world) - initial) / initial);
    const auto inertia = mac_face_inertias(s.registry, final, *s.eos);
    for (std::size_t f = 0; f < final.momentum_x.size(); ++f) if (inertia.x[f] > 0)
        result.velocity_error = std::max(result.velocity_error, std::abs(final.momentum_x[f] / inertia.x[f] - ux) / ux);
    for (std::size_t f = 0; f < final.momentum_y.size(); ++f) if (inertia.y[f] > 0)
        result.velocity_error = std::max(result.velocity_error, std::abs(final.momentum_y[f] / inertia.y[f] - uy) / uy);
    result.world.emplace(world);
    return result;
}
void print(const char* name, const Measure& m) {
    std::cout << "IFACE " << name << " mixed=" << m.mixed << " mist=" << m.mist << " liquid_rel=" << m.liquid
              << " velocity_rel=" << m.velocity_error << '\n';
}
}
int main() {
    try {
        std::cout << std::setprecision(17);
        // About 3.6 cells of diagonal travel in 12 steps of 0.01 s.
        const auto sharp = translate(MacMaterialReconstruction::interface_plic, 12);
        const auto smooth = translate(MacMaterialReconstruction::limited_linear, 12);
        print("interface_plic", sharp); print("limited_linear", smooth);
        check(std::abs(sharp.liquid) <= 1e-14, "Interface reconstruction lost or created liquid");
        // Measured: 85-91 / 229 mixed cells and 1.6-1.8e-3 / 5.3e-3 mist; the
        // range is the spread under last-bit changes of the interface normal,
        // which move threshold decisions (accepted splits, two-phase donors). A
        // perfect representation of the shifted square has 24 mixed cells; the
        // remainder is diagonal (corner) transport, which face parcels cannot route.
        check(2 * sharp.mixed <= smooth.mixed && 2 * sharp.mist <= smooth.mist, "Interface reconstruction is not sharper than limited-linear");
        check(sharp.mixed <= 100 && sharp.mist <= 2e-3, "Interface reconstruction smeared the liquid square");
        check(sharp.velocity_error <= 1e-9, "Uniform translation accelerated at the gas/liquid interface");
        std::cout << "PASS translating liquid square stays sharp, conservative and uniform\n";
        // The production kernel runs the same selector. Per-cell relative
        // pressure is ill-conditioned in a nearly full cell (nRT/p over a tiny
        // headspace), so liquid placement is compared by integrals and
        // pressure only in gas cells.
        const auto production = translate(MacMaterialReconstruction::interface_plic, 12, MacExecutionPolicy::production);
        print("interface_plic_production", production);
        const auto& s = ma::testing::setup(); const auto water = s.registry.index("water");
        const auto& a = sharp.world->state(); const auto& b = production.world->state();
        const double volume = a.grid.volume_m3();
        long double total = 0, difference = 0, cxa = 0, cya = 0, cxb = 0, cyb = 0; double temperature = 0, gas_pressure = 0;
        for (std::size_t i = 0; i < a.cells.size(); ++i) {
            const double ma = a.cells[i].mixture.masses[water], mb = b.cells[i].mixture.masses[water];
            const double x = static_cast<double>(i % a.grid.width), y = static_cast<double>(i / a.grid.width);
            total += ma; difference += std::abs(ma - mb); cxa += ma * x; cya += ma * y; cxb += mb * x; cyb += mb * y;
            const auto ea = s.eos->evaluate(a.cells[i].mixture, volume), eb = s.eos->evaluate(b.cells[i].mixture, volume);
            temperature = std::max(temperature, std::abs(ea.temperature_k - eb.temperature_k));
            if (ma == 0 && mb == 0) gas_pressure = std::max(gas_pressure, std::abs(ea.mechanical_pressure_pa - eb.mechanical_pressure_pa) / ea.mechanical_pressure_pa);
        }
        const double placement = static_cast<double>(difference / total);
        const double centroid = static_cast<double>(std::max(std::abs(cxa - cxb), std::abs(cya - cyb)) / total);
        std::cout << "IFACEPROD liquid_L1=" << placement << " centroid_cells=" << centroid << " dT_K=" << temperature << " gas_dp_rel=" << gas_pressure << '\n';
        check(production.world->settings().numerical_kernel == MacNumericalKernel::production, "Production interface run did not select kernel 2");
        check(std::abs(production.liquid) <= 1e-14 && production.mixed <= 100 && production.mist <= 2e-3 && production.velocity_error <= 1e-9,
            "Production interface reconstruction lost its sharpness, conservation or uniform motion");
        check(placement <= .01 && centroid <= .01 && temperature <= .1 && gas_pressure <= .01,
            "Production interface reconstruction departs from the reference");
        std::cout << "PASS production kernel interface reconstruction matches the reference\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
