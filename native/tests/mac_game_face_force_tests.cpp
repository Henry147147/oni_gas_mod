// Face forces of the game kernel (docs/face-forces.md): the external force
// another mod's fan puts on one cell face,
//   F = A * stall * clamp(1 - u / free_speed, 0, 1).
// Each control drives faces of a small profile 12 world through the kernel's
// request (MacGameKernelAccess::face_forces) and checks the fan curve and the
// accounts: mass is exact, and U+K+G changes by exactly the work the advance
// reports for the forces.
#include "mac_fixture.hpp"
#include "mac_game_surface_control.hpp"
#include "../src/mac_game_bridge.hpp"
#include "mixed_atmospheres/mac_kernel.hpp"
#include <cmath>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace ma;
using namespace ma::experimental;
using ma::testing::setup;
constexpr double frame = static_cast<double>(.2f);

void check(bool ok, const std::string &message) {
  if (!ok) throw std::runtime_error(message);
}
MacClosedSettings settings(MacGameProfile profile = MacGameProfile::game_kernel_thin_drag) {
  MacClosedSettings cfg;
  cfg.numerical_kernel = MacNumericalKernel::game;
  cfg.game_profile = profile;
  cfg.vacuum = MacVacuumRepresentation::void_phase;
  cfg.thermal_conduction = true;
  cfg.maximum_timestep_s = frame;
  cfg.gravity_m_per_s2 = 9.81;
  cfg.trace_sink_threshold_kg = 1e-11;
  cfg.game.liquid_law = MacGameLiquidLaw::positive_segments;
  cfg.game.pressure_maximum_iterations = 400;
  cfg.game.heat_maximum_iterations = 800;
  cfg.game.gas_heat_mixing = 8;
  cfg.game.phase_rule = MacPhaseRule::boil_only;
  return cfg;
}
// A world of W by H cells, walls where `wall(x, y)`, each open cell holding
// what `fill(x, y, cell)` puts there, at 300 K.
template <class Wall, class Fill>
MacClosedWorld world_of(std::size_t W, std::size_t H, Wall &&wall, Fill &&fill,
                        MacClosedSettings cfg = settings()) {
  const auto &f = setup();
  MacTransportState state;
  state.grid = {W, H, 1, 1, false, false, std::vector<std::uint8_t>(W * H)};
  for (std::size_t i = 0; i < W * H; ++i) {
    const auto x = i % W, y = i / W;
    MacMaterialCell cell{MixtureInventory(f.registry.size()), {}};
    state.grid.walls[i] = wall(x, y) ? 1 : 0;
    if (!state.grid.walls[i]) fill(x, y, cell);
    state.cells.push_back(std::move(cell));
  }
  state.momentum_x.resize((W + 1) * H);
  state.momentum_y.resize(W * (H + 1));
  return MacClosedWorld::game_colony(f.registry, f.eos, std::move(state),
                                     std::vector<double>(W * H, 300), cfg);
}
long double mass_of(const MacClosedWorld &w) {
  long double total = 0;
  for (const auto &c : w.state().cells) total += c.mixture.mass();
  return total;
}
struct Run {
  MacClosedWorld world;
  std::vector<long double> work;  // per force, summed
  long double total = 0;          // all forces
  double last = 0, largest_frame = 0, smallest_frame = 0;
};
// Advances `frames` game frames with `forces` driven, checking after every
// frame that mass is exact and that U+K+G rose by the reported work.
Run drive(MacClosedWorld world, const std::vector<MacGameFaceForce> &forces, unsigned frames,
          const char *name) {
  Run out{std::move(world), std::vector<long double>(forces.size()), 0, 0, 0, 0};
  MacGameKernelAccess::face_forces() = forces;
  const auto mass0 = mass_of(out.world);
  for (unsigned n = 0; n < frames; ++n) {
    const auto before = ma::testing::surface_game::total_energy(out.world);
    auto next = out.world.advanced(frame);
    const auto &report = next.report;
    check(report.game_face_force_work_j.size() == forces.size(),
          std::string(name) + ": the advance did not report every force");
    long double sum = 0, sunk = 0;
    for (std::size_t k = 0; k < forces.size(); ++k) {
      out.work[k] += report.game_face_force_work_j[k];
      sum += report.game_face_force_work_j[k];
    }
    check(std::abs(sum - report.game_face_force_work_total_j) <= 1e-12L * (std::abs(sum) + 1e-300L),
          std::string(name) + ": the reported total is not the sum of the forces");
    const auto &t = report.trace_sink;
    sunk = mac_energy_long_double(t.internal_energy_j) + mac_energy_long_double(t.kinetic_energy_j) +
           mac_energy_long_double(t.potential_energy_j);
    const auto after = ma::testing::surface_game::total_energy(next.world);
    // The kernel audits this to 1e-12 of its operands with double-double
    // sums; a long double recount of the state is good to about 1e-11.
    if (!(std::abs(after + sunk - before - sum) <= 2e-11L * std::abs(before))) {
      char text[256];
      std::snprintf(text, sizeof text, "%s: U+K+G changed by %.17Lg J in frame %u, the forces' work was %.17Lg J",
                    name, after + sunk - before, n, sum);
      throw std::runtime_error(text);
    }
    out.total += sum;
    out.last = static_cast<double>(sum);
    out.largest_frame = std::max(out.largest_frame, out.last);
    out.smallest_frame = std::min(out.smallest_frame, out.last);
    out.world = std::move(next.world);
  }
  MacGameKernelAccess::face_forces().clear();
  long double sunk_mass = 0;
  (void)sunk_mass;
  check(std::abs(mass_of(out.world) - mass0) <= 1e-9L * mass0, std::string(name) + ": mass changed");
  return out;
}
double pressure_mean(const MacClosedWorld &w, std::size_t x0, std::size_t x1, std::size_t y0, std::size_t y1) {
  double sum = 0;
  std::size_t count = 0;
  const auto W = w.state().grid.width;
  for (std::size_t y = y0; y <= y1; ++y)
    for (std::size_t x = x0; x <= x1; ++x) {
      sum += w.cell_reading(x + y * W).mechanical_pressure_pa;
      ++count;
    }
  return sum / static_cast<double>(count);
}
// The velocity through the x face on the high side of cell (x, y).
double speed_x(const MacClosedWorld &w, std::size_t x, std::size_t y) {
  const auto &s = w.state();
  const auto W = s.grid.width;
  const double inertia = .5 * (s.cells[x + y * W].mixture.mass() + s.cells[x + 1 + y * W].mixture.mass());
  return inertia > 0 ? s.momentum_x[(x + 1) + y * (W + 1)] / inertia : 0;
}
const double air = 1.3; // kg of oxygen a cell: about one atmosphere at 300 K

// Two sealed rooms joined by a one-cell opening with a fan in it.
MacClosedWorld two_rooms(double left = air, double right = air) {
  const auto oxygen = setup().registry.index("oxygen");
  return world_of(
      15, 6,
      [](std::size_t x, std::size_t y) {
        return x == 0 || y == 0 || x == 14 || y == 5 || (x == 7 && y != 2);
      },
      [&](std::size_t x, std::size_t, MacMaterialCell &cell) {
        cell.mixture.masses[oxygen] = x < 7 ? left : x > 7 ? right : .5 * (left + right);
      });
}
constexpr std::size_t opening = 7 + 2 * 15;

void stall_between_rooms() {
  // A wall fan on High: 120 Pa, 6 m/s, blowing right through the opening.
  const std::vector<MacGameFaceForce> fan{{opening, 1, 0, 120, 6}};
  auto run = drive(two_rooms(), fan, 600, "stall");
  const double rise = pressure_mean(run.world, 8, 13, 2, 2) - pressure_mean(run.world, 1, 6, 2, 2);
  const double through = speed_x(run.world, 7, 2);
  check(std::abs(rise - 120) < 2, "stall: the pressure difference did not settle at the stall pressure (" + std::to_string(rise) + " Pa)");
  check(std::abs(through) < .02, "stall: gas still flows through the stalled fan (" + std::to_string(through) + " m/s)");
  check(std::abs(run.last) < 120 * .02 * frame, "stall: the stalled fan still does work (" + std::to_string(run.last) + " J a frame)");
  check(run.total > 0, "stall: building the pressure difference took no work");
  check(run.largest_frame <= .25 * 120 * 6 * frame * (1 + 1e-9),
        "stall: a frame's work exceeded a quarter of stall pressure times free speed");
  // The same face pushed the other way, which is how its other cell names
  // it: the other room rises by the same amount.
  const std::vector<MacGameFaceForce> back{{opening + 1, -1, 0, 120, 6}};
  auto reversed = drive(two_rooms(), back, 600, "stall reversed");
  const double fall = pressure_mean(reversed.world, 8, 13, 2, 2) - pressure_mean(reversed.world, 1, 6, 2, 2);
  check(std::abs(fall + 120) < 2, "stall reversed: " + std::to_string(fall) + " Pa");
  // Two opposed handles cancel on still fluid. (On moving fluid the one it
  // flows against pushes with its whole stall pressure and the other with
  // less, so together they only brake: their net work is never positive.)
  const std::vector<MacGameFaceForce> opposed{{opening, 1, 0, 120, 6}, {opening + 1, -1, 0, 120, 6}};
  auto still = drive(two_rooms(), opposed, 50, "opposed");
  auto plain = drive(two_rooms(), {}, 50, "no forces");
  const double opposed_rise = pressure_mean(still.world, 8, 13, 2, 2) - pressure_mean(still.world, 1, 6, 2, 2),
               plain_rise = pressure_mean(plain.world, 8, 13, 2, 2) - pressure_mean(plain.world, 1, 6, 2, 2);
  const double opposed_work = static_cast<double>(still.work[0] + still.work[1]);
  check(std::abs(opposed_rise - plain_rise) < 1e-3, "opposed: two equal opposed forces moved gas (" + std::to_string(opposed_rise) + " Pa)");
  check(opposed_work <= 0 && opposed_work > -1e-3, "opposed: net work " + std::to_string(opposed_work) + " J");
  // Two handles the same way add up.
  const std::vector<MacGameFaceForce> twice{{opening, 1, 0, 60, 6}, {opening, 1, 0, 60, 6}};
  auto doubled = drive(two_rooms(), twice, 600, "two handles");
  const double both = pressure_mean(doubled.world, 8, 13, 2, 2) - pressure_mean(doubled.world, 1, 6, 2, 2);
  check(std::abs(both - 120) < 2, "two handles: " + std::to_string(both) + " Pa");
  check(std::abs(static_cast<double>(doubled.work[0] - doubled.work[1])) <= 1e-9 * std::abs(static_cast<double>(doubled.work[0])),
        "two handles: equal forces did unequal work");
  std::cout << "PASS face forces, two rooms: 120 Pa fan settles at " << rise << " Pa with " << through
            << " m/s through it and " << run.last << " J in the last frame (" << static_cast<double>(run.total)
            << " J in all); reversed " << fall << " Pa; two 60 Pa handles " << both
            << " Pa; opposed handles net " << opposed_work << " J\n";
}

void duct_loop() {
  // A ring of duct one cell wide around a solid block, one fan in it.
  const auto oxygen = setup().registry.index("oxygen");
  auto ring = world_of(
      12, 8,
      [](std::size_t x, std::size_t y) {
        return x == 0 || y == 0 || x == 11 || y == 7 || (x >= 2 && x <= 9 && y >= 2 && y <= 5);
      },
      [&](std::size_t, std::size_t, MacMaterialCell &cell) { cell.mixture.masses[oxygen] = air; });
  const std::size_t cell = 5 + 1 * 12; // bottom run, blowing right
  const std::vector<MacGameFaceForce> fan{{cell, 1, 0, 60, 4}};
  auto run = drive(std::move(ring), fan, 400, "loop");
  const double through = speed_x(run.world, 5, 1);
  check(through > .05 && through < 4, "loop: the flow did not settle between rest and the free speed (" + std::to_string(through) + " m/s)");
  check(run.largest_frame <= .25 * 60 * 4 * frame * (1 + 1e-9), "loop: a frame's work exceeded a quarter of stall pressure times free speed");
  check(run.last > 0, "loop: a fan driving a circulation does no work");
  // The fan curve: the work of the last frame is the force at that speed
  // times the speed, within the change of speed over one frame.
  const double expected = 60 * (1 - through / 4) * through * frame;
  check(std::abs(run.last - expected) < .05 * expected, "loop: the last frame's work " + std::to_string(run.last) +
                                                              " J is not the fan curve's " + std::to_string(expected) + " J");
  std::cout << "PASS face forces, duct loop: 60 Pa / 4 m/s fan drives " << through << " m/s, " << run.last / frame
            << " W of at most " << .25 * 60 * 4 << " W\n";
}

void backflow() {
  // The duct ring with two fans against each other: a strong one in the
  // bottom run drives the gas backwards through a weak one in the top run.
  const auto oxygen = setup().registry.index("oxygen");
  auto ring = world_of(
      12, 8,
      [](std::size_t x, std::size_t y) {
        return x == 0 || y == 0 || x == 11 || y == 7 || (x >= 2 && x <= 9 && y >= 2 && y <= 5);
      },
      [&](std::size_t, std::size_t, MacMaterialCell &cell) { cell.mixture.masses[oxygen] = air; });
  // Both blow right; round the ring that is opposite ways.
  const std::vector<MacGameFaceForce> fans{{5 + 1 * 12, 1, 0, 600, 8}, {5 + 6 * 12, 1, 0, 60, 4}};
  auto run = drive(std::move(ring), fans, 300, "backflow");
  const double through = speed_x(run.world, 5, 6);
  check(through < -.05, "backflow: the strong fan did not drive gas backwards through the weak one (" + std::to_string(through) + " m/s)");
  MacGameKernelAccess::face_forces() = fans;
  const auto last = run.world.advanced(frame).report.game_face_force_work_j;
  MacGameKernelAccess::face_forces().clear();
  check(last[0] > 0 && last[1] < 0, "backflow: work on gas driven backwards through a fan must be negative");
  // Against backflow the force is the stall pressure and no more: the work
  // of a frame is stall pressure times area times the distance the gas moved.
  const double expected = 60 * through * frame;
  check(std::abs(last[1] - expected) < .02 * std::abs(expected),
        "backflow: the weak fan's work " + std::to_string(last[1]) + " J is not its stall pressure's " + std::to_string(expected) + " J");
  std::cout << "PASS face forces, backflow: " << through << " m/s backwards through a 60 Pa fan, which does " << last[1] / frame
            << " W (stall pressure times flow: " << expected / frame << " W)\n";
}

void liquid() {
  // A trough of water under air, a swivel fan's face under the surface.
  const auto &f = setup();
  const auto water = f.registry.index("water"), oxygen = f.registry.index("oxygen");
  const auto trough = [&] {
    return world_of(
        14, 6, [](std::size_t x, std::size_t y) { return x == 0 || y == 0 || x == 13 || y == 5; },
        [&](std::size_t, std::size_t y, MacMaterialCell &cell) {
          if (y <= 2) cell.mixture.masses[water] = y == 2 ? 500 : 997;
          if (y >= 2) cell.mixture.masses[oxygen] = y == 2 ? air * .5 : air;
        });
  };
  const std::size_t cell = 6 + 1 * 14;
  const std::vector<MacGameFaceForce> fan{{cell, 1, 0, 60, 6}};
  auto run = drive(trough(), fan, 100, "liquid");
  const double through = speed_x(run.world, 6, 1);
  check(run.total > 0, "liquid: the force did no work on the water");
  check(through > 1e-4, "liquid: the water at the driven face does not move (" + std::to_string(through) + " m/s)");
  // The water piles up downstream of the face.
  const auto &s = run.world.state();
  double left = 0, right = 0;
  for (std::size_t y = 1; y <= 4; ++y)
    for (std::size_t x = 1; x <= 12; ++x) (x <= 6 ? left : right) += s.cells[x + y * 14].mixture.masses[water];
  check(right > left, "liquid: the water did not move the way it was pushed");
  std::cout << "PASS face forces, liquid: water pushed at " << through << " m/s, " << right - left
            << " kg more downstream, " << static_cast<double>(run.total) << " J\n";
}

void undriven_faces() {
  // Against a solid cell, out of the grid, with a zero strength: no force.
  const std::size_t beside_wall = 6 + 3 * 15; // its right neighbour (7, 3) is wall
  const std::vector<MacGameFaceForce> none{
      {beside_wall, 1, 0, 120, 6}, {1 + 1 * 15, -1, 0, 120, 6}, {1 + 1 * 15, 0, -1, 120, 6},
      {opening, 1, 0, 0, 6},       {opening, 1, 0, 120, 0},     {7 + 3 * 15, 1, 0, 120, 6}};
  auto run = drive(two_rooms(), none, 20, "undriven");
  auto plain = drive(two_rooms(), {}, 20, "no forces");
  for (const auto work : run.work) check(work == 0, "undriven: a face that cannot be driven booked work");
  check(run.world.checkpoint() == plain.world.checkpoint(), "undriven: forces on faces that cannot be driven changed the world");
  // A face closed to flow between two fluid cells.
  auto closed = two_rooms().state();
  closed.grid.closed_x.assign(closed.grid.x_width() * closed.grid.height, 0);
  closed.grid.closed_y.assign(closed.grid.width * closed.grid.y_height(), 0);
  closed.grid.closed_x[closed.grid.x_face(8, 2)] = 1;
  const auto &f = setup();
  const auto barrier = [&] {
    return MacClosedWorld::game_colony(f.registry, f.eos, closed, std::vector<double>(closed.cells.size(), 300), settings());
  };
  const std::vector<MacGameFaceForce> at_barrier{{opening, 1, 0, 120, 6}};
  auto blocked = drive(barrier(), at_barrier, 20, "closed face");
  auto unblocked = drive(barrier(), {}, 20, "closed face without forces");
  check(blocked.work[0] == 0 && blocked.world.checkpoint() == unblocked.world.checkpoint(),
        "closed face: a force acted on a face closed to flow");
  // Invalid requests are refused.
  for (const auto &bad : std::vector<MacGameFaceForce>{
           {opening, 1, 1, 120, 6}, {opening, 0, 0, 120, 6}, {opening, 2, 0, 120, 6}, {opening, 1, 0, -1, 6},
           {opening, 1, 0, 120, std::nan("")}, {15 * 6, 1, 0, 120, 6}}) {
    MacGameKernelAccess::face_forces() = {bad};
    bool refused = false;
    try {
      (void)two_rooms().advanced(frame);
    } catch (const std::exception &) {
      refused = true;
    }
    MacGameKernelAccess::face_forces().clear();
    check(refused, "invalid: a malformed face force was accepted");
  }
  std::cout << "PASS face forces, undriven faces: solid neighbours, grid edges, zero strengths and a closed face take no force and no work; malformed requests are refused\n";
}

void thin_gas() {
  // 2 mg of oxygen a cell: the implicit force must not overshoot the free speed.
  const auto oxygen = setup().registry.index("oxygen");
  auto thin = world_of(
      12, 3, [](std::size_t x, std::size_t y) { return x == 0 || y == 0 || x == 11 || y == 2; },
      [&](std::size_t, std::size_t, MacMaterialCell &cell) { cell.mixture.masses[oxygen] = 2e-6; });
  const std::vector<MacGameFaceForce> fan{{5 + 1 * 12, 1, 0, 120, 6}};
  auto run = drive(std::move(thin), fan, 25, "thin gas");
  double fastest = 0;
  for (std::size_t x = 1; x < 10; ++x) fastest = std::max(fastest, std::abs(speed_x(run.world, x, 1)));
  check(fastest <= 6 * 1.001, "thin gas: the fan drove thin gas past its free speed (" + std::to_string(fastest) + " m/s)");
  std::cout << "PASS face forces, thin gas: a 120 Pa fan on 2 mg cells keeps every face at or below " << fastest << " m/s of its 6 m/s\n";
}

void nothing_is_saved() {
  const std::vector<MacGameFaceForce> fan{{opening, 1, 0, 120, 6}};
  const auto &f = setup();
  auto run = drive(two_rooms(), fan, 30, "saved");
  // The checkpoint is the world's alone: a cold copy continues exactly as
  // the live world does, with the forces and without them, on any worker count.
  const auto image = run.world.checkpoint();
  const auto cold = MacClosedWorld::from_checkpoint(f.registry, f.eos, image);
  check(cold.checkpoint() == image, "saved: the checkpoint does not round-trip");
  const auto live_off = run.world.advanced(frame).world.checkpoint();
  MacGameKernelAccess::face_forces() = fan;
  const auto live_on = run.world.advanced(frame).world.checkpoint();
  const auto saved = mac_kernel_default_options();
  auto twelve = saved;
  twelve.threads = 12;
  set_mac_kernel_default_options(twelve);
  const auto cold_on = cold.advanced(frame).world.checkpoint();
  MacGameKernelAccess::face_forces().clear();
  const auto cold_off = cold.advanced(frame).world.checkpoint();
  set_mac_kernel_default_options(saved);
  check(live_off == cold_off, "saved: a restored world without forces differs from the live one");
  check(live_on == cold_on, "saved: a restored world with the forces set again differs from the live one, or the worker count matters");
  check(live_on != live_off, "saved: the force had no effect");
  // With no force requested a world that was driven before computes what
  // one that never was computes: the solver keeps nothing between requests.
  auto fresh = two_rooms();
  auto used = two_rooms();
  MacGameKernelAccess::face_forces() = fan;
  (void)two_rooms().advanced(frame);
  MacGameKernelAccess::face_forces().clear();
  for (unsigned n = 0; n < 5; ++n) {
    fresh = fresh.advanced(frame).world;
    used = used.advanced(frame).world;
  }
  check(fresh.checkpoint() == used.checkpoint(), "saved: an earlier request changed a later advance without forces");
  std::cout << "PASS face forces, nothing saved: checkpoints hold no force, restored worlds continue exactly cold and on 12 workers\n";
}

void earlier_profile() {
  // The single-region tick of the profiles before 6 takes the same request.
  const std::vector<MacGameFaceForce> fan{{opening, 1, 0, 120, 6}};
  const auto oxygen = setup().registry.index("oxygen");
  auto cfg = settings(MacGameProfile::game_kernel_free_surface);
  cfg.game.phase_rule = MacPhaseRule::equilibrium;
  auto rooms = world_of(
      15, 6, [](std::size_t x, std::size_t y) { return x == 0 || y == 0 || x == 14 || y == 5 || (x == 7 && y != 2); },
      [&](std::size_t, std::size_t, MacMaterialCell &cell) { cell.mixture.masses[oxygen] = air; }, cfg);
  auto run = drive(std::move(rooms), fan, 400, "earlier profile");
  const double rise = pressure_mean(run.world, 8, 13, 2, 2) - pressure_mean(run.world, 1, 6, 2, 2);
  check(std::abs(rise - 120) < 12, "earlier profile: " + std::to_string(rise) + " Pa");
  std::cout << "PASS face forces, single-region tick: " << rise << " Pa\n";
}
} // namespace

int main() {
  try {
    auto options = mac_kernel_default_options();
    options.threads = 1;
    set_mac_kernel_default_options(options);
    stall_between_rooms();
    duct_loop();
    backflow();
    liquid();
    undriven_faces();
    thin_gas();
    nothing_is_saved();
    earlier_profile();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
