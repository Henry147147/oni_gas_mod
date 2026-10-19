// Convergence study of the separation gate's resting-slab reference
// (MAC_STRONG_FLOW.md, acceptance criteria). Not MAC code and not a gate run.
// Usage: ma_riemann_slab_reference N refine... (each refine even, ascending)
// For each refinement it prints the central reference cells, then the gate-metric
// differences between consecutive refinements and against the finest one:
// window density/pressure/velocity L1 (normalized as the gate's errors) and the
// larger relative central density/pressure difference.
#include "mac_riemann_reference.hpp"
#include <iomanip>
#include <iostream>
#include <string>

namespace {
using namespace ma::testing::riemann_reference;
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
}
int main(int argc, char** argv) {
    try {
        check(argc >= 3, "Usage: ma_riemann_slab_reference N refine...");
        const auto gate = static_cast<std::size_t>(std::stoul(argv[1]));
        // Gate nitrogen with a 1e-28 helium mass ratio (registry values in its MATERIAL records).
        const double rgas = 8.31446261815324 * (1 / 0.028013400000000001 + 1e-28 / 0.0040026020000000001) / (1 + 1e-28);
        const double g = 1 + rgas / ((743.19694795514863 + 1e-28 * 3115.7356059500198) / (1 + 1e-28));
        const double unit = std::sqrt(100000.), sound = std::sqrt(g) * unit, H = 1 / static_cast<double>(gate);
        const double duration = .03 / unit, gate_dt = .25 * H / ((1 + 4.) * sound);
        const State left{1, -4 * sound, 100000}, right{1, 4 * sound, 100000};
        std::vector<std::size_t> refinements; std::vector<SlabProfile> profiles;
        for (int a = 2; a < argc; ++a) {
            refinements.push_back(static_cast<std::size_t>(std::stoul(argv[a])));
            profiles.push_back(resting_slab(left, right, g, gate, refinements.back(), duration, gate_dt));
            const auto& p = profiles.back();
            std::cout << std::setprecision(10) << "SLABREF N " << gate << " refine " << refinements.back() << " central rho " << p.rho[gate / 2 - 1]
                      << ' ' << p.rho[gate / 2] << " p " << p.p[gate / 2 - 1] << ' ' << p.p[gate / 2] << '\n';
        }
        const auto compare = [&](std::size_t a, std::size_t b) {
            double rho = 0, p = 0, u = 0, crho = 0, cp = 0;
            for (std::size_t i = 0; i < gate; ++i) {
                const double x = static_cast<double>(i) * H;
                if (x >= .25 && x + H <= .75) { rho += H * std::abs(profiles[a].rho[i] - profiles[b].rho[i]) / .5;
                    p += H * std::abs(profiles[a].p[i] - profiles[b].p[i]) / (100000 * .5); }
                if (x >= .25 && x < .75) u += H * std::abs(profiles[a].u[i] - profiles[b].u[i]) / (sound * .5);
                if (i + 1 == gate / 2 || i == gate / 2) {
                    crho = std::max(crho, std::abs(profiles[a].rho[i] - profiles[b].rho[i]) / profiles[b].rho[i]);
                    cp = std::max(cp, std::abs(profiles[a].p[i] - profiles[b].p[i]) / profiles[b].p[i]);
                }
            }
            std::cout << std::setprecision(6) << "SLABDIFF N " << gate << " refine " << refinements[a] << " vs " << refinements[b] << " L1 rho " << rho
                      << " p " << p << " u " << u << " central rho " << crho << " p " << cp << '\n';
        };
        for (std::size_t k = 1; k < profiles.size(); ++k) compare(k - 1, k);
        for (std::size_t k = 0; k + 2 < profiles.size(); ++k) compare(k, profiles.size() - 1);
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
