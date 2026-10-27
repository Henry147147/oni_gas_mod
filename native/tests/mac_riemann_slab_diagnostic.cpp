// Diagnostic for the strong-flow separation failure (MAC_STRONG_FLOW.md,
// "Start-up resolution of the resting slab"). It never stops on a gate criterion
// and is not a gate run: both modes change the MAC grid or start, never the gate.
// Usage: ma_mac_riemann_slab_diagnostic N fine K [mat=1 mom=1 comp=2 heat=2]
//        ma_mac_riemann_slab_diagnostic N restart F [mat=1 mom=1 comp=2 heat=2]
//   fine K     runs the MAC on N*K cells with timestep gate_dt/K from the same
//              physical start: the resting slab [1/2 - 1/(2N), 1/2 + 1/(2N)] keeps
//              each fine dual's at-rest mass fraction (K = 1 is the gate start).
//   restart F  starts the gate-grid MAC at F*duration from the R = 8 resting-slab
//              reference projected onto the gate grid (cell mass and internal
//              energy; face momentum of the R fine cells of each dual) and runs
//              the remaining time with the gate timestep.
// Both compare the end state with the R = 8 resting-slab reference on the gate
// grid using the gate's separation metrics, and split the velocity error by
// distance from the centre (< .05, < .1, < .18, rest).
#include "mac_fixture.hpp"
#include "mac_riemann_reference.hpp"
#include <array>
#include <cstdlib>
#include <iomanip>
#include <string>

namespace {
using namespace ma;
using namespace ma::experimental;
namespace reference = ma::testing::riemann_reference;
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
constexpr std::size_t slab_refinement = 8;
struct Options { int mat = 1, mom = 1, comp = 2, heat = 2; };
void run(std::size_t n, bool fine, double parameter, const Options& o) {
    const auto& set = ma::testing::setup(); const auto ni = set.registry.index("nitrogen"), he = set.registry.index("helium");
    const auto& nitrogen = set.registry[ni]; const auto& helium = set.registry[he];
    const double rgas = gas_constant * (1 / nitrogen.molar_mass + 1e-28 / helium.molar_mass) / (1 + 1e-28);
    const double cv = (nitrogen.cv() + 1e-28 * helium.cv()) / (1 + 1e-28), gamma = 1 + rgas / cv;
    const double unit = std::sqrt(100000.), sound = std::sqrt(gamma) * unit, H = 1 / static_cast<double>(n);
    const double duration = .03 / unit, gate_dt = .25 * H / (5 * sound);
    const reference::State left{1, -4 * sound, 100000}, right{1, 4 * sound, 100000};
    const std::size_t k = fine ? static_cast<std::size_t>(parameter) : 1, m = n * k;
    check(k >= 1 && k <= 16 && (fine || (parameter > 0 && parameter < 1)), "Invalid fine factor or restart fraction");
    const double h = 1 / static_cast<double>(m), volume = h * h;
    auto source = set.state(m, 2, false); source.grid.periodic_y = true; source.grid.spacing_m = h;
    source.momentum_x.assign(2 * (m + 1), 0); source.momentum_y.assign(2 * m, 0);
    const auto set_cell = [&](std::size_t x, double rho, double p) {
        for (std::size_t y = 0; y < 2; ++y) {
            auto& cell = source.cells[x + y * m]; std::vector<double> masses(set.registry.size());
            masses[ni] = rho * volume; masses[he] = masses[ni] * 1e-28;
            cell.mixture = set.eos->initialize_at_temperature(masses, p / (rho * rgas), volume).mixture;
            cell.passive_quantities = {{17, cell.mixture.mass()}, {UINT64_C(0x80000000), cell.mixture.mass() * 1e-30}};
        }
    };
    std::vector<double> face_velocity(m + 1);
    double start = 0, discarded = 0, retained = 0;
    if (fine) {
        for (std::size_t x = 0; x < m; ++x) { const auto& s = x < m / 2 ? left : right; set_cell(x, s.rho, s.p); }
        for (std::size_t x = 1; x < m; ++x) {
            const double a = (static_cast<double>(x) - .5) * h, b = a + h, lo = .5 - H / 2, hi = .5 + H / 2;
            const double inside = std::max(0., std::min(b, hi) - std::max(a, lo)) / h;
            face_velocity[x] = (x < m / 2 ? left.u : x > m / 2 ? right.u : 0.) * (1 - inside);
        }
    } else {
        const std::size_t nf = n * slab_refinement; const double hf = 1 / static_cast<double>(nf);
        std::vector<reference::Cell> cells(nf);
        for (std::size_t x = 0; x < nf; ++x) {
            const bool rest = x >= nf / 2 - slab_refinement / 2 && x < nf / 2 + slab_refinement / 2; const auto& s = x < nf / 2 ? left : right;
            cells[x] = reference::conserved({s.rho, rest ? 0. : s.u, s.p}, gamma);
        }
        start = parameter * duration;
        reference::advance(cells, gamma, hf, start, gate_dt / static_cast<double>(slab_refinement));
        for (std::size_t x = 0; x < m; ++x) {
            double mass = 0, internal = 0;
            for (std::size_t q = 0; q < slab_refinement; ++q) {
                const auto& c = cells[x * slab_refinement + q]; mass += c[0] * hf; internal += (c[2] - .5 * c[1] * c[1] / c[0]) * hf;
            }
            set_cell(x, mass / H, (gamma - 1) * internal / H);
        }
        for (std::size_t x = 1; x < m; ++x) {
            double mass = 0, momentum = 0, kinetic = 0;
            for (std::size_t q = x * slab_refinement - slab_refinement / 2; q < x * slab_refinement + slab_refinement / 2; ++q) {
                mass += cells[q][0]; momentum += cells[q][1]; kinetic += .5 * cells[q][1] * cells[q][1] / cells[q][0];
            }
            face_velocity[x] = momentum / mass; retained += .5 * momentum * momentum / mass * hf; discarded += kinetic * hf - .5 * momentum * momentum / mass * hf;
        }
    }
    const auto inertia = mac_face_inertias(set.registry, source, *set.eos);
    for (std::size_t y = 0; y < 2; ++y) for (std::size_t x = 1; x < m; ++x)
        source.momentum_x[source.grid.x_face(x, y)] = inertia.x[source.grid.x_face(x, y)] * face_velocity[x];
    MacClosedSettings settings;
    settings.reconstruction = static_cast<MacMaterialReconstruction>(o.mat);
    settings.momentum_reconstruction = static_cast<MacMomentumReconstruction>(o.mom);
    settings.compression_viscosity = static_cast<MacCompressionViscosity>(o.comp);
    settings.shock_heat = static_cast<MacShockHeatRegularization>(o.heat);
    settings.maximum_timestep_s = gate_dt / static_cast<double>(k);
    const MacClosedWorld world(set.registry, set.eos, source, settings);
    const auto result = world.advanced(duration - start, 65536);
    const auto& final = result.world.state(); const auto final_inertia = mac_face_inertias(set.registry, final, *set.eos);
    const auto slab = reference::resting_slab(left, right, gamma, n, slab_refinement, duration, gate_dt);
    double rho_l1 = 0, p_l1 = 0, central_rho = 0, central_p = 0; std::array<double, 4> u_l1{0, 0, 0, 0};
    for (std::size_t y = 0; y < 2; ++y) for (std::size_t x = 0; x < n; ++x) {
        const double a = static_cast<double>(x) * H;
        if (a < .25 || a + H > .75) continue;
        double rho = 0, p = 0;
        for (std::size_t q = 0; q < k; ++q) {
            const auto& c = final.cells[x * k + q + y * m].mixture;
            rho += c.mass() / volume / static_cast<double>(k); p += set.eos->evaluate(c, volume).mechanical_pressure_pa / static_cast<double>(k);
        }
        rho_l1 += .5 * H * std::abs(rho - slab.rho[x]) / .5; p_l1 += .5 * H * std::abs(p - slab.p[x]) / (100000 * .5);
        if (x == n / 2 || x + 1 == n / 2) {
            central_rho = std::max(central_rho, std::abs(rho - slab.rho[x]) / slab.rho[x]);
            central_p = std::max(central_p, std::abs(p - slab.p[x]) / slab.p[x]);
        }
        const auto f = final.grid.x_face(x * k, y); const double d = std::abs(a - .5);
        u_l1[d < .05 ? 0 : d < .1 ? 1 : d < .18 ? 2 : 3] += .5 * H * std::abs(final.momentum_x[f] / final_inertia.x[f] - slab.u[x]) / (sound * .5);
    }
    std::cout << std::setprecision(10) << "SLABDIAG N " << n << (fine ? " fine " : " restart ") << parameter << " selectors " << o.mat << ',' << o.mom << ','
              << o.comp << ',' << o.heat << " steps " << result.report.substeps << " rejected " << result.report.rejected_attempts
              << " L1 " << rho_l1 << ' ' << p_l1 << ' ' << u_l1[0] + u_l1[1] + u_l1[2] + u_l1[3] << " central " << central_rho << ' ' << central_p
              << " u by |x-.5| <.05/<.1/<.18/rest " << u_l1[0] << ' ' << u_l1[1] << ' ' << u_l1[2] << ' ' << u_l1[3];
    if (!fine) std::cout << " discarded subcell kinetic fraction " << discarded / (retained + discarded);
    std::cout << '\n';
}
}
int main(int argc, char** argv) {
    try {
        check(argc >= 4, "Usage: ma_mac_riemann_slab_diagnostic N fine K|restart F [mat=1 mom=1 comp=2 heat=2]");
        const auto n = static_cast<std::size_t>(std::stoul(argv[1])); const std::string mode = argv[2];
        check(n >= 8 && n % 2 == 0 && (mode == "fine" || mode == "restart"), "Invalid grid or mode");
        Options o;
        for (int a = 4; a < argc; ++a) {
            const std::string text = argv[a]; const auto eq = text.find('='); check(eq != std::string::npos, "Options are key=value");
            const auto key = text.substr(0, eq); const int value = std::atoi(text.c_str() + eq + 1);
            if (key == "mat") o.mat = value; else if (key == "mom") o.mom = value; else if (key == "comp") o.comp = value;
            else if (key == "heat") o.heat = value; else check(false, "Unknown option");
        }
        run(n, mode == "fine", std::atof(argv[3]), o);
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
