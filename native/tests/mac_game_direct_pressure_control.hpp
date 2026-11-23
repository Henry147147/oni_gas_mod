#pragma once
#include "mac_fixture.hpp"
#include "mac_game_convective_control.hpp"
#include "mac_game_surface_control.hpp"
#include "../src/mac_game/flow_local.hpp"
#include "../src/mac_game/pressure_be.hpp"
#include "mixed_atmospheres/mac_kernel.hpp"
#include <bit>
#include <cmath>
#include <iostream>

// Profile 12 direct pressure route (native/MAC_GAME_KERNEL.md, "Sparse
// Cholesky pressure solves"; mac_direct_cholesky.hpp), with the game's
// minimum size and fill limit (flow_local.hpp).
// - Kernel: a serpentine system of 5,508 cells is solved by sparse Cholesky
//   to at most a hundredth of the bound the geometric solve would meet; the
//   route falls back to the geometric solve when the grid's fill exceeds the
//   limit, when the system is smaller than the minimum, when the residual
//   misses the bound and when the override is off; its values are the same
//   bytes on 1 and 12 workers.
// - World: a 5,408-cell serpentine cave region takes the route, closes mass
//   and U+K+G, gives the same checkpoint on 1 and 12 workers and continues
//   exactly cold. An open 60 x 60 room (below the minimum, above the limit)
//   and a sealed 208 x 26 room of hot oxygen under cold, the longest open
//   room the minimum admits by size (5,408 cells), keep the geometric
//   multigrid: the fill limit holds an open room off the route, so that an
//   unstable layer keeps the solver noise that seeds its overturning.
namespace ma::testing::direct_pressure_game {
using namespace ma::experimental;
namespace k = ma::experimental::mac_kernel;
inline void check(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
// The override switched for one scope.
struct DirectRoute {
  bool saved = k::pressure_direct_route();
  explicit DirectRoute(bool on) { k::set_pressure_direct_route(on); }
  ~DirectRoute() { k::set_pressure_direct_route(saved); }
};
// Walls of a W x H serpentine: an outer wall and inner walls every 8
// columns, open at the top and the bottom in turn.
inline std::vector<std::uint8_t> serpentine(std::size_t W, std::size_t H) {
  std::vector<std::uint8_t> walls(W * H);
  for (std::size_t i = 0; i < W * H; ++i) {
    const auto x = i % W, y = i / W;
    const bool inner = x % 8 == 0 && x > 0 && x + 1 < W && ((x / 8) % 2 ? y > 2 : y + 3 < H);
    walls[i] = x == 0 || y == 0 || x + 1 == W || y + 1 == H || inner;
  }
  return walls;
}
// A pressure system on `walls`: compliance and weights spread over five
// decades, as between liquid and gas.
struct Kernel {
  MacGrid grid;
  k::Grid geometry;
  k::LinearSystem sys;
  std::vector<double> b;
  explicit Kernel(std::size_t W, std::size_t H, std::vector<std::uint8_t> walls)
      : grid{W, H, 1, 1, false, false, std::move(walls)}, geometry(grid) {
    sys.g = &geometry;
    sys.diag.assign(geometry.N, 0);
    sys.active.assign(geometry.N, 0);
    sys.wx.assign(geometry.NX, 0);
    sys.wy.assign(geometry.NY, 0);
    b.assign(geometry.N, 0);
    std::uint64_t state = 7;
    const auto next = [&] {
      state = state * 6364136223846793005ULL + 1442695040888963407ULL;
      return static_cast<double>(state >> 11) * (1.0 / 9007199254740992.0);
    };
    for (std::size_t i = 0; i < geometry.N; ++i)
      if (!grid.walls[i]) {
        sys.active[i] = 1;
        sys.diag[i] = 1e-8 * std::pow(1e2, next());
        b[i] = next() - .5;
      }
    for (std::size_t f = 0; f < geometry.NX; ++f)
      if (geometry.xl[f] != k::none && geometry.xr[f] != k::none) sys.wx[f] = 1e-6 * std::pow(1e5, next());
    for (std::size_t f = 0; f < geometry.NY; ++f)
      if (geometry.yl[f] != k::none && geometry.yr[f] != k::none) sys.wy[f] = 1e-6 * std::pow(1e5, next());
  }
  struct Run {
    k::LinearSolveResult result;
    std::vector<double> x;
    bool solved_direct = false;
  };
  // One solve on `threads` workers with a fresh cache.
  Run solve(unsigned threads, k::DirectRoute route, double atol, double bound = -1) {
    k::Model model;
    k::Workspace workspace;
    MacClosedSettings settings;
    settings.preconditioner = PressurePreconditioner::incomplete_cholesky;
    settings.game_profile = MacGameProfile::game_kernel_thin_drag;
    MacKernelOptions options;
    options.threads = threads;
    k::Context context{workspace, grid, model, geometry, settings, options, k::Exec{geometry, k::pool_for(threads)}, {}, {}};
    k::ComponentSolveCache cache;
    Run run;
    if (bound >= 0) {
      const auto direct = k::solve_direct(context, cache, sys, b, run.x, bound, route.maximum_fill);
      run.solved_direct = direct.has_value();
      if (direct) run.result = *direct;
      return run;
    }
    // The game's geometric threshold: a fallback of this size is a geometric solve.
    run.result = k::solve_components_cached(context, cache, sys, b, run.x, 0, atol, 400, 4096, 2000, route);
    return run;
  }
  // |b - A x| recomputed in long double from the system.
  long double residual(const std::vector<double> &x) const {
    long double squared = 0;
    for (std::size_t i = 0; i < geometry.N; ++i) {
      if (!sys.active[i]) continue;
      const auto l = geometry.left_face(i), r = geometry.right_face(i), d = geometry.bottom_face(i), u = geometry.top_face(i);
      const auto at = [&](k::Index j) { return j == k::none ? 0.L : static_cast<long double>(x[j]); };
      const long double xi = x[i];
      const long double a = sys.diag[i] * xi + sys.wx[l] * (xi - at(geometry.xl[l])) + sys.wx[r] * (xi - at(geometry.xr[r])) +
                            sys.wy[d] * (xi - at(geometry.yl[d])) + sys.wy[u] * (xi - at(geometry.yr[u]));
      squared += (b[i] - a) * (b[i] - a);
    }
    return std::sqrt(squared);
  }
};
inline bool same(const std::vector<double> &a, const std::vector<double> &b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (std::bit_cast<std::uint64_t>(a[i]) != std::bit_cast<std::uint64_t>(b[i])) return false;
  return true;
}
inline void kernel_controls() {
  namespace game = ma::experimental::game_kernel;
  Kernel cave(138, 48, serpentine(138, 48));
  const std::size_t cells = static_cast<std::size_t>(std::count(cave.sys.active.begin(), cave.sys.active.end(), std::uint8_t{1}));
  long double norm = 0;
  for (const double v : cave.b) norm += static_cast<long double>(v) * v;
  const double atol = 1e-8 * static_cast<double>(std::sqrt(norm));
  check(cells >= game::direct_pressure_cells, "The kernel control's cave is below the route's minimum");
  const k::DirectRoute route{game::direct_pressure_cells, game::direct_pressure_fill};
  // The route is taken and solves far below the bound.
  const auto one = cave.solve(1, route, atol), twelve = cave.solve(12, route, atol);
  check(one.result.direct && one.result.converged && one.result.iterations == 1 && !one.result.geometric,
        "A cave system did not take the direct route");
  check(one.result.residual <= .5 * atol && cave.residual(one.x) <= 1e-2L * .5L * atol,
        "The direct solve did not meet a hundredth of the geometric bound");
  check(same(one.x, twelve.x) && std::bit_cast<std::uint64_t>(one.result.residual) == std::bit_cast<std::uint64_t>(twelve.result.residual),
        "The direct solve differs between 1 and 12 workers");
  // Fallbacks: fill above the limit, a system below the minimum, a missed
  // bound and the override off. The geometric solve then meets the bound.
  const auto capped = cave.solve(1, k::DirectRoute{game::direct_pressure_cells, 1}, atol);
  check(!capped.result.direct && capped.result.geometric && capped.result.converged &&
            cave.residual(capped.x) <= 1.001L * .5L * atol,
        "A grid above the fill limit did not fall back to the geometric solve");
  const auto small = cave.solve(1, k::DirectRoute{cells + 1, game::direct_pressure_fill}, atol);
  check(!small.result.direct && small.result.geometric, "A system below the minimum took the direct route");
  const auto missed = cave.solve(1, route, atol, 0), met = cave.solve(1, route, atol, .5 * atol);
  check(!missed.solved_direct && met.solved_direct && met.result.direct && same(met.x, one.x),
        "The direct route did not refuse a missed bound and keep a met one");
  {
    DirectRoute off(false);
    const auto overridden = cave.solve(1, route, atol);
    check(!overridden.result.direct && overridden.result.geometric, "The override did not switch the direct route off");
  }
  check(k::pressure_direct_route(), "The override was not restored");
  std::cout << "PASS direct pressure route (kernel): " << cells << "-cell cave solved by sparse Cholesky to "
            << static_cast<double>(cave.residual(one.x)) << " against a bound of " << .5 * atol
            << ", the same bytes on 1 and 12 workers; fill limit, minimum size, missed bound and override fall back\n";
}
inline MacClosedSettings settings() {
  MacClosedSettings cfg;
  cfg.numerical_kernel = MacNumericalKernel::game;
  cfg.game_profile = MacGameProfile::game_kernel_thin_drag;
  cfg.vacuum = MacVacuumRepresentation::void_phase;
  cfg.thermal_conduction = true;
  cfg.maximum_timestep_s = static_cast<double>(.2f);
  cfg.gravity_m_per_s2 = 9.81;
  cfg.trace_sink_threshold_kg = 1e-11;
  cfg.game.liquid_law = MacGameLiquidLaw::positive_segments;
  cfg.game.pressure_maximum_iterations = 400;
  cfg.game.heat_maximum_iterations = 800;
  cfg.game.gas_heat_mixing = 8;
  cfg.game.phase_rule = MacPhaseRule::boil_only;
  return cfg;
}
// Air at 300 K on `walls`, with a hot, dense patch in the lower left corner.
inline MacClosedWorld world(std::size_t W, std::size_t H, const std::vector<std::uint8_t> &walls) {
  const auto &f = setup();
  MacTransportState state;
  state.grid = {W, H, 1, 1, false, false, walls};
  const auto nitrogen = f.registry.index("nitrogen"), oxygen = f.registry.index("oxygen");
  std::vector<double> temperatures(W * H, 300);
  for (std::size_t i = 0; i < W * H; ++i) {
    const auto x = i % W, y = i / W;
    MacMaterialCell cell{MixtureInventory(f.registry.size()), {}};
    if (!walls[i]) {
      const bool patch = x <= 5 && y <= 5;
      cell.mixture.masses[nitrogen] = patch ? 1.4 : .9;
      cell.mixture.masses[oxygen] = patch ? .5 : .28;
      if (patch) temperatures[i] = 420;
      cell.passive_quantities = {{17, 1 + static_cast<double>(x % 5)}};
    }
    state.cells.push_back(std::move(cell));
  }
  state.momentum_x.resize((W + 1) * H);
  state.momentum_y.resize(W * (H + 1));
  return MacClosedWorld::game_colony(f.registry, f.eos, std::move(state), temperatures, settings());
}
// A sealed W x H room (walls on its border) of oxygen at 100 kPa, 330 K in
// its lower half and 290 K in its upper half, at rest.
inline MacClosedWorld room(std::size_t W, std::size_t H) {
  const auto &f = setup();
  MacTransportState state;
  state.grid = {W, H, 1, 1, false, false, std::vector<std::uint8_t>(W * H)};
  const auto oxygen = f.registry.index("oxygen");
  std::vector<double> temperatures(W * H, 300);
  for (std::size_t i = 0; i < W * H; ++i) {
    const auto x = i % W, y = i / W;
    state.grid.walls[i] = x == 0 || y == 0 || x + 1 == W || y + 1 == H;
    MacMaterialCell cell{MixtureInventory(f.registry.size()), {}};
    if (!state.grid.walls[i]) {
      temperatures[i] = 2 * y < H ? 330 : 290;
      cell.mixture.masses[oxygen] = 100000 * .032 / (gas_constant * temperatures[i]);
    }
    state.cells.push_back(std::move(cell));
  }
  state.momentum_x.resize((W + 1) * H);
  state.momentum_y.resize(W * (H + 1));
  return MacClosedWorld::game_colony(f.registry, f.eos, std::move(state), temperatures, settings());
}
inline std::size_t cells_of(const MacClosedWorld &w) {
  std::size_t open = 0;
  for (const auto wall : w.state().grid.walls) open += wall == 0;
  return open;
}
inline void world_controls() {
  using convective_game::total_mass;
  const auto saved = mac_kernel_default_options();
  const auto threads = [&](unsigned n) {
    auto options = saved;
    options.threads = n;
    set_mac_kernel_default_options(options);
  };
  // A serpentine cave region of 5,408 cells takes the direct route.
  namespace game = ma::experimental::game_kernel;
  threads(1);
  auto cave = world(130, 50, serpentine(130, 50));
  const auto mass0 = total_mass(cave), energy0 = surface_game::total_energy(cave);
  std::size_t direct = 0, geometric = 0;
  for (unsigned tick = 0; tick < 4; ++tick) {
    auto next = cave.advanced(.2);
    direct += next.report.game_stages.pressure_direct;
    geometric += next.report.game_stages.pressure_geometric;
    cave = std::move(next.world);
  }
  check(direct > 0 && geometric == 0, "The cave region did not take the direct pressure route");
  check(std::abs(total_mass(cave) - mass0) <= 1e-12L * mass0, "The cave region changed mass");
  check(std::abs(surface_game::total_energy(cave) - energy0) <= 1e-12L * std::abs(energy0), "The cave region lost U+K+G");
  const auto image = cave.checkpoint();
  const auto &f = setup();
  const auto cold = MacClosedWorld::from_checkpoint(f.registry, f.eos, image);
  const auto live = cave.advanced(.2).world.checkpoint();
  threads(12);
  const auto restored = cold.advanced(.2);
  const auto twelve = cave.advanced(.2).world.checkpoint();
  threads(1);
  check(restored.report.game_stages.pressure_direct > 0, "The 12-worker cave region did not take the direct route");
  check(live == restored.world.checkpoint() && live == twelve,
        "The cave region's direct route differs between 1 and 12 workers or after a cold restore");
  // An open 60 x 60 room fills at about 930 a cell, above the limit: it
  // keeps the geometric multigrid.
  std::vector<std::uint8_t> frame(62 * 62);
  for (std::size_t i = 0; i < frame.size(); ++i) frame[i] = i % 62 == 0 || i / 62 == 0 || i % 62 == 61 || i / 62 == 61;
  auto open = world(62, 62, frame);
  const auto opened = open.advanced(.2);
  check(opened.report.game_stages.pressure_direct == 0 && opened.report.game_stages.pressure_geometric > 0,
        "An open room above the fill limit left the geometric multigrid");
  // A sealed open room of hot oxygen under cold, just above the minimum by
  // size and as long as the route's sweep admits (1:8, filling at about
  // 717): the fill limit, not its size, keeps it on the geometric multigrid.
  auto layered = room(210, 28);
  std::size_t open_cells = 0;
  for (const auto wall : layered.state().grid.walls) open_cells += wall == 0;
  check(open_cells >= game::direct_pressure_cells, "The layered room is below the route's minimum");
  std::size_t layered_direct = 0, layered_geometric = 0;
  for (unsigned tick = 0; tick < 2; ++tick) {
    auto next = layered.advanced(.2);
    layered_direct += next.report.game_stages.pressure_direct;
    layered_geometric += next.report.game_stages.pressure_geometric;
    layered = std::move(next.world);
  }
  set_mac_kernel_default_options(saved);
  check(layered_direct == 0 && layered_geometric > 0,
        "An open layered room above the minimum left the geometric multigrid");
  std::cout << "PASS direct pressure route (world): " << cells_of(cave) << "-cell cave region " << direct
            << " direct solves in 4 ticks, mass and U+K+G closed, the same checkpoint on 1 and 12 workers and cold; "
            << "an open 3,600-cell room and a sealed " << open_cells
            << "-cell room of hot oxygen under cold keep the geometric multigrid\n";
}
inline void run() {
  kernel_controls();
  world_controls();
}
} // namespace ma::testing::direct_pressure_game
