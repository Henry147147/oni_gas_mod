#pragma once
#include "../src/mac_game/layering.hpp"
#include "../src/mac_game/surface.hpp"
#include <iostream>

namespace ma::testing {
inline void carried_momentum_control() {
  namespace k = ma::experimental::game_kernel;
  namespace execution = ma::experimental::mac_kernel;
  using ma::experimental::MacGrid;
  const auto check = [](bool ok, const char *why) {
    if (!ok) throw std::runtime_error(why);
  };
  std::vector<k::Species> species(3);
  for (unsigned key : {0U, 1U}) {
    species[key].gas = false;
    species[key].positive_liquid = true;
    species[key].rho0 = key == 0 ? 800 : 1600;
    species[key].cv = 2000;
    species[key].stiffness = 8e7;
  }
  const auto state = [&](const MacGrid &g, std::size_t donor,
                         std::size_t receiver, bool layering, bool vacuum) {
    k::State s;
    s.energy.resize(g.width * g.height);
    s.jx.resize(g.x_width() * g.height);
    s.jy.resize(g.width * g.y_height());
    for (std::size_t i = 0; i < s.energy.size(); ++i) {
      s.masses.offsets.push_back(s.masses.mass.size());
      const auto add = [&](unsigned key, double mass) {
        s.masses.species.push_back(key); s.masses.mass.push_back(mass);
        s.energy[i] += mass * species[key].cv * 300;
      };
      const bool from = i == donor || i == donor + 14 * g.width,
                 to = i == receiver || i == receiver + 14 * g.width;
      if (from) add(0, layering ? 400 : 200);
      if (to) add(layering ? 1 : 0, layering ? 800 : 600);
      if (!vacuum || from || to) add(2, 1);
      s.passives.offsets.push_back(s.passives.mass.size());
      if (!vacuum || from || to) {
        s.passives.species.push_back(17);
        s.passives.mass.push_back(.25);
      }
    }
    s.masses.offsets.push_back(s.masses.mass.size());
    s.passives.offsets.push_back(s.passives.mass.size());
    return s;
  };
  const auto readings = [&](const MacGrid &g, const k::State &s) {
    std::vector<k::Eos> result(s.energy.size());
    for (std::size_t i = 0; i < result.size(); ++i) {
      const auto begin = s.masses.offsets[i], count = s.masses.offsets[i+1] - begin;
      result[i] = k::evaluate_selected(species, count,
          [&](std::size_t a) { return s.masses.species[begin+a]; },
          [&](std::size_t a) { return s.masses.mass[begin+a]; }, s.energy[i],
          g.spacing_m * g.spacing_m * g.depth_m);
    }
    return result;
  };
  const auto equal = [](const k::State &a, const k::State &b) {
    return a.energy == b.energy && a.jx == b.jx && a.jy == b.jy &&
        a.masses.offsets == b.masses.offsets && a.masses.species == b.masses.species &&
        a.masses.mass == b.masses.mass && a.passives.offsets == b.passives.offsets &&
        a.passives.species == b.passives.species && a.passives.mass == b.passives.mass;
  };
  for (const bool layering : {false, true}) for (const bool walls : {false, true})
      for (const bool vacuum : {false, true}) for (const int direction : {-1, 1}) {
    MacGrid g;
    g.width = 6; g.height = 48; g.walls.resize(g.width * g.height);
    const std::size_t donor = 2 + 20 * g.width,
                      receiver = layering ? static_cast<std::size_t>(
                          static_cast<long>(donor) + direction * static_cast<long>(g.width)) : donor + 1;
    auto s = state(g, donor, receiver, layering, vacuum);
    // A shedding liquid cell has momentum on both axes; the receiving
    // same-side faces optionally terminate at an explicit impermeable mask.
    const auto source_x = g.x_face(2, 20), source_y = g.y_face(2, 21),
               target_x = g.x_face(receiver % g.width, receiver / g.width),
               target_y = g.y_face(receiver % g.width, receiver / g.width + 1);
    s.jx[source_x] = 70; s.jy[source_y] = -100;
    s.jx[source_x + 14 * g.x_width()] = -40;
    s.jy[source_y + 14 * g.width] = 55;
    if (walls) {
      g.closed_x.resize(s.jx.size()); g.closed_y.resize(s.jy.size());
      // The exchange itself must remain open.
      if (!layering) {
        g.closed_y[target_y] = 1;
        g.closed_y[target_y + 14 * g.width] = 1;
      } else {
        g.closed_x[target_x] = 1;
        g.closed_x[target_x + 14 * g.x_width()] = 1;
      }
    }
    const execution::Grid grid(g);
    const k::Topology topology(grid);
    k::RegionAudit audit(grid, topology, species.size());
    const double gravity = layering ? direction * 9.81 : 0;
    audit.capture(s, gravity, 1);
    const auto eos = readings(g, s);
    execution::Pool pool(4);
    k::State serial, parallel;
    double wall_x = 0, wall_y = 0;
    if (layering) {
      auto next = k::layer(g, species, s, eos, .2, gravity, &grid, nullptr, true, &audit);
      check(next.faces > 0, "Carried layering control made no exchange");
      wall_x = next.wall_x; wall_y = next.wall_y; serial = std::move(next.state);
      parallel = k::layer(g, species, s, eos, .2, gravity, &grid, &pool, true).state;
    } else {
      auto next = k::sharpen(g, species, s, eos, .2, gravity, 0, nullptr, &grid,
                             nullptr, true, &audit);
      check(next.faces > 0, "Carried sharpening control made no correction");
      check(next.clusters >= 2, "Carried sharpening worker control needs independent clusters");
      wall_x = next.wall_x; wall_y = next.wall_y; serial = std::move(next.state);
      parallel = k::sharpen(g, species, s, eos, .2, gravity, 0, &pool, &grid,
                              nullptr, true).state;
    }
    check(equal(serial, parallel), "Carried momentum depends on worker count");
    audit.capture(serial, gravity, -1);
    check(audit.finish() < 0x1p-45, "Carried momentum failed the exact region audit");
    check(std::abs(serial.jx[source_x]) < std::abs(s.jx[source_x]),
          "Liquid exchange stranded momentum on the shedding face");
    if (walls) check(wall_x != 0 || wall_y != 0,
                     "Carried momentum reaching a closed face omitted its wall impulse");
    else check(wall_x == 0 && wall_y == 0 && serial.jx[target_x] != s.jx[target_x],
               "Carried momentum did not reach its receiving open face");
    (void)readings(g, serial);
  }
  // Two unstable oil/water pairs share the intervening vertical face in
  // their momentum footprints. Its two parcel updates must accumulate using
  // the original face velocity, independently of parcel/worker scheduling.
  for (const int direction : {-1, 1}) {
    MacGrid g; g.width = 6; g.height = 8; g.walls.resize(g.width * g.height);
    k::State s; s.energy.resize(g.walls.size());
    s.jx.resize(g.x_width() * g.height); s.jy.resize(g.width * g.y_height());
    for (std::size_t i = 0; i < s.energy.size(); ++i) {
      s.masses.offsets.push_back(s.masses.mass.size());
      const auto x = i % g.width, y = i / g.width;
      if (x == 2 && y >= 1 && y <= 4) {
        const unsigned key = direction > 0 ? static_cast<unsigned>((y+1)%2)
                                            : static_cast<unsigned>(y%2);
        const double mass = .5 * species[key].rho0;
        s.masses.species.push_back(key); s.masses.mass.push_back(mass);
        s.energy[i] += mass * species[key].cv * 300;
      }
      s.masses.species.push_back(2); s.masses.mass.push_back(1);
      s.energy[i] += species[2].cv * 300;
    }
    s.masses.offsets.push_back(s.masses.mass.size());
    const auto shared = g.y_face(2,3); s.jy[shared] = 100;
    s.jx[g.x_face(2,2)] = 70; s.jx[g.x_face(2,3)] = -40;
    const execution::Grid grid(g); const k::Topology topology(grid);
    k::RegionAudit audit(grid,topology,species.size());
    const double gravity = direction * 9.81;
    audit.capture(s,gravity,1);
    const auto eos = readings(g,s);
    const auto serial = k::layer(g,species,s,eos,.2,gravity,&grid,nullptr,true,&audit);
    execution::Pool pool(4);
    const auto parallel = k::layer(g,species,s,eos,.2,gravity,&grid,&pool,true);
    check(serial.faces == 2 && equal(serial.state,parallel.state),
          "Adjacent carried layering exchanges changed their worker result");
    check(serial.state.jy[shared] != s.jy[shared],
          "Adjacent layering control did not update its shared momentum face");
    audit.capture(serial.state,gravity,-1);
    check(audit.finish() < 0x1p-45,
          "Adjacent layering momentum footprints broke the region audit");
  }
  std::cout << "PASS profile8 sharpening/layering carry both momentum components, closed-face impulses, exact region audits and worker equality\n";
}
} // namespace ma::testing
