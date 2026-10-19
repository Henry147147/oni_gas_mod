// Sharp gas/liquid interfaces (MacMaterialReconstruction::interface_plic): a
// liquid square translating through air at uniform velocity and pressure.
// PLIC keeps the liquid in its own cells, with exact conservation and no
// spurious acceleration at the density jump. Limited-linear is reported for
// comparison only.
#include "mac_kernel_fixture.hpp"
#include <iomanip>

namespace {
using namespace ma;
using namespace ma::experimental;
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
struct Measure { std::size_t mixed = 0; double mist = 0, liquid = 0, velocity_error = 0; };
Measure translate(MacMaterialReconstruction reconstruction, std::size_t steps) {
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
    for (std::size_t k = 0; k < steps; ++k) world = world.advanced(.01, 4096).world;
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
        // Measured at commit: 85 / 229 mixed cells and 1.6e-3 / 5.3e-3 mist. A
        // perfect representation of the shifted square has 24 mixed cells; the
        // remainder is diagonal (corner) transport, which face parcels cannot route.
        check(2 * sharp.mixed <= smooth.mixed && 2 * sharp.mist <= smooth.mist, "Interface reconstruction is not sharper than limited-linear");
        check(sharp.mixed <= 100 && sharp.mist <= 2e-3, "Interface reconstruction smeared the liquid square");
        check(sharp.velocity_error <= 1e-9, "Uniform translation accelerated at the gas/liquid interface");
        std::cout << "PASS translating liquid square stays sharp, conservative and uniform\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
