#pragma once
#include "../src/mac_game/pressure_be.hpp"
#include <iostream>
#include <random>
#include <stdexcept>

namespace ma::testing {
// The cached one-worker component solve must reproduce the general
// component solve bit for bit: serial components (single reductions),
// components of 1024..4095 cells and plan components of at least 4096 cells
// (1024-row reductions), several components at once, cells held at zero
// pressure, refusals and a reused structure with new values.
inline void component_cache_control() {
  namespace k = ma::experimental::game_kernel;
  using ma::experimental::MacGrid;
  const auto check = [](bool yes, const char *why) {
    if (!yes) throw std::runtime_error(why);
  };
  std::mt19937_64 random(2026102801);
  std::uniform_real_distribution<double> unit(0, 1);
  const auto same = [](const std::vector<double> &a, const std::vector<double> &b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
      if (std::bit_cast<std::uint64_t>(a[i]) != std::bit_cast<std::uint64_t>(b[i])) return false;
    return true;
  };
  // 96 x 80: a wall column at x=40 and a wall row at y=30 (left of x=40)
  // split parts of about 1,160, 1,900 and 4,270 active cells; scattered
  // empty cells add small isolated pieces. Some cells hold so little gas
  // that outflow drives them negative, so the corrected solve also runs.
  MacGrid g;
  g.width = 96; g.height = 80;
  g.walls.assign(g.width * g.height, 0);
  for (std::size_t y = 0; y < g.height; ++y) g.walls[40 + y * g.width] = 1;
  for (std::size_t x = 0; x < 40; ++x) g.walls[x + 30 * g.width] = 1;
  std::vector<k::Eos> states(g.width * g.height);
  std::vector<double> jx(g.x_width() * g.height), jy(g.width * g.y_height());
  const auto fill = [&] {
    for (std::size_t i = 0; i < states.size(); ++i) {
      auto &s = states[i];
      s = k::Eos{};
      if (g.walls[i] || unit(random) < .03) continue;
      const bool thin = unit(random) < .05;
      s.mass = thin ? 1e-4 * (1 + unit(random)) : .5 + unit(random);
      s.capacity = 700 * s.mass;
      s.nR = s.mass * 8.314 / .029;
      s.pressure = s.nR * 300;
      s.temperature = 300;
      s.gas_volume = 1;
    }
    const ma::experimental::mac_kernel::Grid kg(g);
    const auto occupied = [&](std::size_t i) {
      return i != ma::experimental::mac_kernel::none && states[i].mass > 0;
    };
    for (std::size_t f = 0; f < jx.size(); ++f)
      jx[f] = occupied(kg.xl[f]) && occupied(kg.xr[f]) ? 40 * (unit(random) - .5) : 0;
    for (std::size_t f = 0; f < jy.size(); ++f)
      jy[f] = occupied(kg.yl[f]) && occupied(kg.yr[f]) ? 40 * (unit(random) - .5) : 0;
  };
  std::size_t corrections = 0;
  for (unsigned round = 0; round < 3; ++round) {
    fill();
    for (const double dt : {.002, .05}) {
      k::PressureSolver one(g, 1, ma::PressurePreconditioner::incomplete_cholesky, 1e-8, 0, nullptr, 400, true);
      k::PressureSolver two(g, 2, ma::PressurePreconditioner::incomplete_cholesky, 1e-8, 0, nullptr, 400, true);
      for (unsigned repeat = 0; repeat < 2; ++repeat) {
        const auto a = one.advance(states, jx, jy, dt, 9.81, true);
        corrections += a.corrections;
        const auto b = two.advance(states, jx, jy, dt, 9.81, true);
        check(a.iterations == b.iterations && a.corrections == b.corrections &&
                  a.converged == b.converged && std::bit_cast<std::uint64_t>(a.residual) == std::bit_cast<std::uint64_t>(b.residual) &&
                  same(a.p, b.p) && same(a.jx, b.jx) && same(a.jy, b.jy) && same(a.qx, b.qx) && same(a.qy, b.qy) &&
                  same(a.damping_x, b.damping_x) && same(a.damping_y, b.damping_y),
              "Cached component pressure solve differs from the general component solve");
      }
    }
  }
  check(corrections > 0, "Cached component control never exercised the corrected solve");
  // A cap the solve cannot meet refuses the same way on both paths.
  fill();
  bool one_refused = false, two_refused = false;
  k::PressureSolver capped_one(g, 1, ma::PressurePreconditioner::incomplete_cholesky, 1e-15, 0, nullptr, 1, true);
  k::PressureSolver capped_two(g, 2, ma::PressurePreconditioner::incomplete_cholesky, 1e-15, 0, nullptr, 1, true);
  try { (void)capped_one.advance(states, jx, jy, .05, 9.81, true); }
  catch (const ma::experimental::mac_kernel::ComponentPressureSolveFailure &) { one_refused = true; }
  try { (void)capped_two.advance(states, jx, jy, .05, 9.81, true); }
  catch (const ma::experimental::mac_kernel::ComponentPressureSolveFailure &) { two_refused = true; }
  check(one_refused && two_refused, "Cached component pressure solve changed a failed-root refusal");
  std::cout << "PASS cached component pressure solve equals the general solve bit for bit (serial, 1024-4095 and plan components, corrections, reuse, refusal)\n";
}
} // namespace ma::testing
