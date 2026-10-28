#pragma once
#include "../src/mac_game/tick.hpp"
#include <iostream>
#include <stdexcept>

namespace ma::testing {
inline void bounded_pressure_expansion_control() {
  namespace k = ma::experimental::game_kernel;
  using ma::experimental::MacGrid;
  const auto check = [](bool yes, const char *why) {
    if (!yes) throw std::runtime_error(why);
  };
  // Explicit game-like caloric surrogate. ONI's catalogue uses approximately
  // 0.016 kg/mol for its Oxygen element; this is not molecular oxygen data.
  std::vector<k::Species> species(1);
  species[0].molar_mass = .016;
  species[0].cv = 1005;
  species[0].conductivity = .024;
  const auto make_state = [&](const MacGrid &g, auto mass) {
    k::State s;
    const auto N = g.width * g.height;
    s.energy.resize(N);
    s.jx.resize(g.x_width() * g.height);
    s.jy.resize(g.width * g.y_height());
    for (std::size_t i = 0; i < N; ++i) {
      s.masses.offsets.push_back(s.masses.mass.size());
      const double m = mass(i);
      if (m == 0) continue;
      s.masses.species.push_back(0);
      s.masses.mass.push_back(m);
      s.energy[i] = m * species[0].cv * 300;
    }
    s.masses.offsets.push_back(s.masses.mass.size());
    return s;
  };
  const auto make_tick = [&](const MacGrid &g, bool bounded, bool sized = false,
                             bool carried = false) {
    return k::Tick(g, species, {}, 1, 2, 8, false, 0, 1e-8, 0,
                   false, 0, 2, 1, true, 400, 800, true, true, bounded, sized,
                   carried, carried ? 1e-6 : 0.);
  };
  MacGrid g;
  g.width = 32; g.height = 24;
  g.walls.resize(g.width * g.height);
  const auto opened = g.width / 2 + g.width * (g.height / 2);
  for (const double trace : {0., 1e-9}) {
    const auto source = make_state(g, [&](std::size_t i) { return i == opened ? trace : 1.6; });
    auto old_tick = make_tick(g, false);
    auto new_tick = make_tick(g, true);
    auto sized_tick = make_tick(g, true, true);
    auto carried_tick = make_tick(g, true, true, true);
    const auto old = old_tick.advance(source, .2, 0, 1e-11);
    const auto fixed = new_tick.advance(source, .2, 0, 1e-11);
    const auto sized = sized_tick.advance(source, .2, 0, 1e-11);
    const auto carried = carried_tick.advance(source, .2, 0, 1e-11);
    // A much smaller legacy pressure interval independently resolves the
    // newly opened cell. No candidate retry/volume criterion enters this run.
    auto fine_tick = make_tick(g, false);
    auto fine = source;
    for (unsigned n = 0; n < 400; ++n)
      fine = fine_tick.advance(fine, .0005, 0, 1e-11).state;
    const auto reference = fine_tick.evaluate_state(fine);
    double old_error = 0, fixed_error = 0, sized_error = 0, scale = 0;
    for (std::size_t i = 0; i < reference.size(); ++i) {
      old_error += std::abs(old.eos[i].pressure - reference[i].pressure);
      fixed_error += std::abs(fixed.eos[i].pressure - reference[i].pressure);
      sized_error += std::abs(sized.eos[i].pressure - reference[i].pressure);
      scale += reference[i].pressure;
    }
    // Profile 7 sizes the same bounded intervals from the measured swept
    // volume: it meets the same reference with no more pressure work.
    check(sized_error < .01 * scale && sized_error < .1 * old_error,
          "Sized pressure intervals differ from the resolved temporal reference");
    check(sized.pressure_attempts == sized.flow_substeps + sized.pressure_refinements &&
              sized.pressure_attempts <= fixed.pressure_attempts,
          "Sized pressure intervals did not report or reduce their pressure work");
    check(sized.energy_error < 1e-12 && sized.mass_error < 1e-12 &&
              sized.momentum_error < 1e-12 && sized.region_error < 1e-12,
          "Sized pressure intervals broke the physical audit contract");
    // Every inflow into the tiny opened cell comes from a massive donor;
    // profile 8 must retain the bound and the same temporal trajectory here.
    check(carried.state.energy == sized.state.energy &&
              carried.state.masses.mass == sized.state.masses.mass &&
              carried.state.jx == sized.state.jx && carried.state.jy == sized.state.jy,
          "The 1 mg floor ignored a massive inflow into an empty/trace cell");
    check(old_error > .1 * scale,
          "Opening regression no longer exposes the coarse frozen-pressure defect");
    check(fixed_error < .01 * scale && fixed_error < .1 * old_error,
          "Newly opened/near-vacuum gas pressure differs from the resolved temporal reference");
    check(fixed.pressure_refinements > 0 && fixed.flow_substeps > 2 &&
              fixed.pressure_attempts == fixed.flow_substeps + fixed.pressure_refinements,
          "Opening refinement did not report its actual pressure work");
    check(fixed.energy_error < 1e-12 && fixed.mass_error < 1e-12 &&
              fixed.momentum_error < 1e-12 && fixed.region_error < 1e-12,
          "Opening refinement broke the existing physical audit contract");
    if (trace == 0) {
      // Insufficient work for a coarse pressure root requests an earlier
      // recomputation. It must not expose a partly evolved fluid state.
      k::Flow limited(g, species, 1, true, 1e-15, 0, 1, true, true);
      const auto pending = limited.advance(source, .2, 0);
      check(pending.pressure_refinement_requested && pending.state.energy.empty() &&
                pending.eos.size() == source.energy.size() &&
                pending.eos[opened].mass == 0,
            "An unconverged pressure trial reached physical work or lost its source readings");
      k::Flow legacy_limited(g, species, 1, true, 1e-15, 0, 1, true, false);
      bool refused = false;
      try { (void)legacy_limited.advance(source, .2, 0); }
      catch (const std::exception &error) {
        refused = std::string(error.what()).find("did not converge") != std::string::npos;
      }
      check(refused, "A legacy pressure root acquired the new refinement policy");
    }
  }
  // A one-dimensional expansion before either rarefaction/front reaches a
  // wall has no compressing/focusing jet: pressure cannot exceed its initial
  // reservoir value. This oracle does not apply to the 2-D cavity above.
  const auto dilute = make_state(g, [&](std::size_t i) { return i == opened ? 0. : 1e-9; });
  k::Flow dilute_bounded(g, species, 1, true, 1e-8, 0, 400, true, true),
          dilute_floor(g, species, 1, true, 1e-8, 0, 400, true, true, 1e-6);
  check(dilute_bounded.advance(dilute, .2, 0).pressure_refinement_requested &&
            !dilute_floor.advance(dilute, .2, 0).pressure_refinement_requested,
        "The 1 mg floor did not exempt cells and inflows below its mass threshold");
  const auto threshold = make_state(g, [&](std::size_t i) { return i == opened ? 0. : 1e-6; });
  check(dilute_floor.advance(threshold, .2, 0).pressure_refinement_requested,
        "The 1 mg floor exempted an inflow exactly at its inclusive threshold");
  MacGrid line;
  line.width = 128; line.height = 1; line.walls.resize(line.width);
  for (const unsigned profile : {6U, 7U, 8U}) {
    auto one_d = make_tick(line, true, profile >= 7, profile >= 8);
    const auto slab = make_state(line, [](std::size_t i) { return i < 64 ? 1.6 : 0.; });
    const double initial_p = one_d.evaluate_state(slab)[0].pressure;
    const auto front = one_d.advance(slab, .005, 0, 1e-11);
    double maximum = 0;
    for (const auto &e : front.eos) maximum = std::max(maximum, e.pressure);
    check(maximum <= initial_p * 1.001,
          "Pre-reflection one-dimensional vacuum expansion created a pressure spike");
  }
  // Large transport alone does not need a smaller pressure interval. A
  // corner streamfunction gives a closed-boundary, divergence-free velocity
  // field while the remap still needs its local advection microcycles.
  auto circulating = make_state(g, [](std::size_t) { return 1.6; });
  const double pi = std::acos(-1.);
  const auto psi = [&](std::size_t x, std::size_t y) {
    if (x == 0 || y == 0 || x == g.width || y == g.height) return 0.;
    return 60 * std::sin(pi * static_cast<double>(x) / g.width) *
                std::sin(pi * static_cast<double>(y) / g.height);
  };
  for (std::size_t y = 0; y < g.height; ++y)
    for (std::size_t x = 1; x < g.width; ++x)
      circulating.jx[g.x_face(x,y)] = 1.6 * (psi(x,y+1) - psi(x,y));
  for (std::size_t y = 1; y < g.height; ++y)
    for (std::size_t x = 0; x < g.width; ++x)
      circulating.jy[g.y_face(x,y)] = -1.6 * (psi(x+1,y) - psi(x,y));
  k::Flow flow(g, species, 1, true, 1e-8, 0, 400, true, true);
  const auto transported = flow.advance(circulating, .2, 0);
  check(!transported.pressure_refinement_requested && transported.subcycles > 1,
        "Divergence-free fast transport was limited as a compressing gas front");
  check(transported.energy_error < 1e-12 && transported.mass_error < 1e-12 &&
            transported.momentum_error < 1e-12,
        "Fast circulation broke the existing physical audit contract");
  std::cout << "PASS bounded and sized pressure intervals: new and nearly empty cells match a resolved temporal reference; 1D expansion, fast circulation and physical audits\n";
}
} // namespace ma::testing
