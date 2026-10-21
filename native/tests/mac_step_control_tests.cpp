// Predictive step control (MAC_KERNEL.md, "Step control"): a jet of air
// through a one-cell gap into vacuum, the colony's settings, under both
// controllers. Predictive control must spend far fewer rejected attempts,
// conserve the same species, and persist as checkpoint version 13.
#include "mac_fixture.hpp"
#include <iomanip>

namespace {
using namespace ma::testing;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class F> void rejects(F&& f, const char* message) {
    bool rejected = false;
    try { f(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, message);
}
MacClosedSettings settings(MacStepControl control) {
    MacClosedSettings s;
    s.numerical_kernel = MacNumericalKernel::production; s.maximum_timestep_s = .01;
    s.vacuum = MacVacuumRepresentation::void_phase; s.reconstruction = MacMaterialReconstruction::interface_plic;
    s.subcell_void = MacSubcellVoidPolicy::expand_unresolved_gas; s.step_control = control;
    return s;
}
// 8x10 box: air at 100 kPa below row 5, a gap at x = 3, vacuum above.
MacTransportState jet() {
    const auto& s = setup(); constexpr std::size_t w = 8, h = 10;
    MacTransportState st; st.grid = {w, h, 1, 1, false, false, std::vector<std::uint8_t>(w * h)};
    st.occupied_volumes_m3.assign(w * h, 0);
    for (std::size_t y = 0; y < h; ++y) for (std::size_t x = 0; x < w; ++x) {
        const auto i = x + y * w;
        st.grid.walls[i] = x == 0 || x + 1 == w || y == 0 || (y == 5 && x != 3);
        const bool gas = !st.grid.walls[i] && y < 5;
        st.cells.push_back({gas ? s.mixture(1, 100000, 300) : MixtureInventory(s.registry.size()), {}});
        if (gas) st.occupied_volumes_m3[i] = 1;
    }
    const auto inertia = mac_face_inertias(s.registry, st, *s.eos);
    st.momentum_x.assign(inertia.x.size(), 0); st.momentum_y.assign(inertia.y.size(), 0);
    return st;
}
std::vector<long double> species(const MacTransportState& st) {
    std::vector<long double> total(st.cells.front().mixture.masses.size());
    for (const auto& c : st.cells) for (std::size_t k = 0; k < total.size(); ++k) total[k] += c.mixture.masses[k];
    return total;
}
void controllers() {
    const auto& s = setup(); const auto start = jet();
    std::size_t rejected[2] = {}, substeps[2] = {};
    for (const auto control : {MacStepControl::doubling, MacStepControl::predictive}) {
        MacClosedWorld world(s.registry, s.eos, start, settings(control));
        for (int frame = 0; frame < 3; ++frame) {
            auto next = world.advanced(.2, 65536, MacExecutionPolicy::production); world = std::move(next.world);
            rejected[static_cast<int>(control)] += next.report.rejected_attempts;
            substeps[static_cast<int>(control)] += next.report.substeps;
        }
        const auto a = species(start), b = species(world.state());
        for (std::size_t k = 0; k < a.size(); ++k) require(std::abs(a[k] - b[k]) <= 1e-12L * a[k] + 1e-300L, "Jet lost species mass");
        const auto image = world.checkpoint();
        require(image[8] == (control == MacStepControl::predictive ? 13 : 12), "Step control selected the wrong checkpoint version");
        const auto restored = MacClosedWorld::from_checkpoint(s.registry, s.eos, image);
        require(restored.settings().step_control == control && restored.checkpoint() == image, "Saved step control changed on restore");
        require(restored.advanced(.05, 65536, MacExecutionPolicy::production).world.checkpoint() ==
            world.advanced(.05, 65536, MacExecutionPolicy::production).world.checkpoint(), "Step-control checkpoint lost exact continuation");
        if (control == MacStepControl::predictive) {
            // The selector follows the subcell policy (mac_subcell_void_tests).
            std::uint32_t identity = 0;
            for (std::size_t i = 0; i < 4; ++i) identity |= static_cast<std::uint32_t>(image[12 + i]) << (8 * i);
            const std::size_t at = 16 + identity + 16 + 26 + 89 + 4;
            require(image.at(at) == 1, "Step control has an unexpected wire position");
            for (const std::uint8_t invalid : {std::uint8_t{0}, std::uint8_t{2}}) {
                auto corrupt = image; corrupt[at] = invalid;
                std::uint64_t hash = UINT64_C(14695981039346656037);
                for (std::size_t i = 0; i + 8 < corrupt.size(); ++i) hash = (hash ^ corrupt[i]) * UINT64_C(1099511628211);
                for (std::size_t i = 0; i < 8; ++i) corrupt[corrupt.size() - 8 + i] = static_cast<std::uint8_t>(hash >> (8 * i));
                rejects([&] { (void)MacClosedWorld::from_checkpoint(s.registry, s.eos, corrupt); }, "Noncanonical saved step control was accepted");
            }
            auto reference = settings(control); reference.numerical_kernel = MacNumericalKernel::reference;
            rejects([&] { reference.validate(); }, "Predictive control was admitted on the reference kernel");
        }
    }
    std::cout << "STEP doubling rejected=" << rejected[0] << " substeps=" << substeps[0]
              << " predictive rejected=" << rejected[1] << " substeps=" << substeps[1] << '\n';
    require(rejected[1] * 4 < rejected[0], "Predictive control did not cut CFL rejections");
}
}
int main() {
    try {
        std::cout << std::setprecision(17);
        controllers();
        std::cout << "PASS predictive step control: fewer rejections, conserved species, version-13 continuation\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
