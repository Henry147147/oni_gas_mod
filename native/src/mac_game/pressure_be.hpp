#pragma once
#include "../mac_kernel_detail.hpp"
#include "account.hpp"
#include "eos.hpp"
#include <algorithm>
#include <memory>
namespace ma::experimental::game_kernel {
inline constexpr double liquid_drag_time = 1; // seconds; profile 12, see PressureSolver
inline constexpr double gas_drag_time = 20;   // seconds; profile 12, see PressureSolver
// Entrained liquid at a gas vent, in m³ of liquid per m³ of face volume.
inline constexpr double vent_mist_fraction = 1e-6;
// Profile 12 liquid surfaces under thin gas (PressureSolver::advance): a
// partly filled liquid cell whose gas pressure is below this share of the
// weight its top face carries cannot hold that face up, and material above
// it of at most held_surface_trace_kg (the net swept-volume bound's 1 mg
// floor) is a trace the face is closed against.
inline constexpr double held_surface_gas_share = .1;
inline constexpr double held_surface_trace_kg = 1e-6;

namespace kernel = ma::experimental::mac_kernel;
// A driven face (docs/face-forces.md): an external force on the fluid of
// one face of this grid, along its axis (sign +1) or against it (-1),
//   F = A * stall_pa * clamp(1 - u / free_speed, 0, 1),
// with u the fluid's velocity through the face in the force's direction. A
// fan's curve: its full stall pressure against still or opposing fluid,
// nothing once the fluid moves at the free speed. Never saved.
struct FaceForce {
  std::size_t face = 0;
  bool x = true;
  double sign = 1, stall_pa = 0, free_speed = 0;
};
struct Pressure {
  std::vector<double> p, jx, jy, qx, qy, damping_x, damping_y, body_impulses_y;
  // Profile 12: momentum each open face gave to the thin-gas drag this
  // interval (an external load). Empty without the drag.
  std::vector<double> drag_x, drag_y;
  // Faces closed for this interval: one side empty and the predicted
  // momentum would carry material out of that empty cell.
  std::vector<std::uint8_t> blocked_x, blocked_y;
  // Profile 12: vertical faces above a liquid surface its gas cannot hold
  // up, whose body impulse is the weight of the material above rather than
  // of the face's inertia (see advance). Their body work is booked from
  // body_impulses_y. Empty without the profile.
  std::vector<std::uint8_t> held_y;
  // Face forces (PressureSolver::set_forces), one entry a force in the
  // given order: the impulse it gave the fluid along its face's axis this
  // interval (an external load) and the work that impulse did (an external
  // energy input). Empty without forces.
  std::vector<double> force_impulse, force_work;
  // geometric: solves (of this result and its correction) the geometric PCG made.
  std::size_t iterations = 0, corrections = 0, geometric = 0;
  double residual = 0;
  bool converged = false;
  double wall_impulse_x = 0, wall_impulse_y = 0, body_impulse_y = 0;
  double wall_operands_x = 0, wall_operands_y = 0;
};
// Profile 12 (MAC_GAME_KERNEL.md, "Room in liquid cells"): a liquid cell
// whose void and compressible gas the interval would use up. It enters the
// solve with `compliance` (its liquid's plus what remains of its gas) and
// `offset`, the volume its room takes before that compliance applies,
// expressed against its pressure at the start: compliance * dp = inflow +
// offset. compliance <= 0 marks an ordinary cell.
struct Stiffened {
  double compliance = 0, offset = 0;
};
// Component prototype only: transport, positivity and receipts follow.
// Vacuum is a zero-pressure boundary until material arrives there.
class PressureSolver {
public:
  // Space venting (MAC_GAME_KERNEL.md, "Gas at cells space
  // empties"): cells the owner empties every frame (open sky, cells ONI
  // deletes the gas of), one flag a cell, and the speed gas may reach through
  // their gas or mist faces. Such a face takes the implicit drag that holds the speed its
  // start-of-interval pressures predict at that cap; momentum and heat are
  // booked as for the thin-gas drag.
  const std::vector<std::uint8_t> *sinks = nullptr;
  double sink_speed = 0;
  // Face forces (docs/face-forces.md): the faces `list` drives until the
  // next call; null or empty: none, and then no value of a solve differs
  // from a solver that has never had any. The list must outlive its use.
  // In the backward-Euler step a force is the same shape as the drags,
  //   J (1 + gamma) = J_old + body + pressure impulse,
  // with dt A stall_pa added to the body impulse and, inside the linear
  // range 0 <= u <= free_speed, dt A stall_pa / (I free_speed) to gamma:
  // the force taken implicitly in the new velocity, so that a strong fan on
  // thin gas cannot overshoot its free speed. Against backflow the force is
  // constant, at or above the free speed zero; the range is the one the
  // face's momentum falls in under the pressures at the start of the
  // interval (drive_face). Unlike a drag the term is not dissipation:
  // the whole effect is the one external force, whose impulse is a load like
  // a wall's and whose work, impulse * J_new / I, enters U+K+G from outside
  // (Flow::advance books both). A face closed to flow, blocked for the
  // interval or without fluid takes no force.
  void set_forces(const std::vector<FaceForce> *list) {
    for (const auto &entry : driven)
      (entry.x ? force_slot_x : force_slot_y)[entry.face] = 0;
    driven.clear();
    force_order.clear();
    forces = list && !list->empty() ? list : nullptr;
    if (!forces) {
      force_slot_x.clear();
      force_slot_y.clear();
      return;
    }
    force_slot_x.resize(grid.NX);
    force_slot_y.resize(grid.NY);
    force_order.resize(forces->size());
    for (std::size_t k = 0; k < forces->size(); ++k) {
      const auto &f = (*forces)[k];
      if (f.face >= (f.x ? grid.NX : grid.NY) || !(f.sign == 1 || f.sign == -1) ||
          !(f.stall_pa > 0) || !std::isfinite(f.stall_pa) ||
          !(f.free_speed > 0) || !std::isfinite(f.free_speed))
        throw std::runtime_error("Invalid face force");
      force_order[k] = k;
    }
    // Forces of one face are adjacent, in the given order.
    std::stable_sort(force_order.begin(), force_order.end(),
                     [&](std::size_t a, std::size_t b) {
                       const auto &l = (*forces)[a], &r = (*forces)[b];
                       return l.x != r.x ? l.x > r.x : l.face < r.face;
                     });
    for (std::size_t at = 0; at < force_order.size(); ++at) {
      const auto &f = (*forces)[force_order[at]];
      auto &slot = (f.x ? force_slot_x : force_slot_y)[f.face];
      if (!slot) {
        driven.push_back({f.face, f.x, at, 0, 0, 0});
        slot = static_cast<std::uint32_t>(driven.size());
      }
      ++driven[slot - 1].count;
    }
    force_part.assign(forces->size(), 0);
  }
private:
  const std::vector<FaceForce> *forces = nullptr;
  // One entry a driven face: its forces are force_order[first .. first +
  // count), and body and gamma their terms in the current solve.
  struct Driven {
    std::size_t face;
    bool x;
    std::size_t first, count;
    double body, gamma;
  };
  std::vector<Driven> driven;
  std::vector<std::size_t> force_order;
  // A face's entry in `driven`, plus one; 0: not driven. Empty without forces.
  std::vector<std::uint32_t> force_slot_x, force_slot_y;
  // Each force's range in the current solve: 0 none, 1 constant, 2 linear.
  std::vector<std::uint8_t> force_part;
  ma::experimental::MacGrid source;
  kernel::Grid grid;
  kernel::Workspace workspace;
  kernel::Model model;
  ma::experimental::MacClosedSettings settings;
  ma::experimental::MacKernelOptions options;
  std::unique_ptr<kernel::Pool> owned_pool;
  kernel::Pool &pool;
  double relative_tolerance = 1e-8;
  unsigned gravity_rule = 0, maximum_iterations = 200;
  bool close_empty_inflow = false;
  // Profile 12 liquid drag (2026-11-12; MAC_GAME_KERNEL.md, "Liquid drag"):
  // with the thin-gas drag, a face also feels an implicit drag at the rate
  // (1/liquid_drag_time) times its liquid share by volume (the mean of its
  // two cells' liquid volume fractions, at most 1). Liquid here had no
  // dissipation but the implicit step's own: a 35-cell pool with a
  // half-cell step sloshed at full height for minutes. One second makes
  // the pool's longest wave about critically damped: it is flat in ten to
  // fifteen seconds without oscillating, and liquid falls at up to
  // g * liquid_drag_time. The momentum taken is an external load and its
  // kinetic work is local heat, exactly as for the thin-gas drag.
  // Profile 12 gas drag (2026-11-13; MAC_GAME_KERNEL.md, "Gas drag"): the
  // rest of the face, its share of gas by volume, feels the same drag at
  // 1/gas_drag_time. Gas had no dissipation either: a room stirred once
  // kept moving at centimetres a second for half an hour, and the
  // first-order transport mixed resting layers into it all that time.
  // Twenty seconds is an eddy's turnover in a room: pressure differences
  // still equalise in a frame or two and a strong plume is unchanged, a
  // weak one (1 kW) rises at 0.19 m/s for 0.34, and left alone the gas is at
  // rest in about a minute.
  // Profile 12 thin-gas drag: a face whose inertia I (half its two cells'
  // mass) is below thin_mass feels an implicit drag on its momentum at the
  // rate (1/thin_time)(thin_mass/I - 1), zero at the threshold. 0 disables it.
  double thin_mass = 0, thin_time = 0;
  // Profile 12: a solve that is one connected system of at least this many
  // cells first tries the geometric PCG (solve_components_cached). 0: never.
  std::size_t geometric_cells = 0;
  kernel::Context context;
  // Component structure of the last solve, and of the last corrected solve
  // (cells held at zero pressure); see solve_components_cached. Solves with
  // stiffened cells (profile 12) keep their own two, so that their active
  // cells do not discard the structure the next interval's solve reuses.
  kernel::ComponentSolveCache component_cache[4];
  // Capacity reused by every solve. Each pass writes every entry that a
  // later pass reads, so nothing carries over between calls.
  struct Scratch {
    kernel::LinearSystem system;
    std::vector<double> rhs, increment_rhs, inertia_x, inertia_y, base_x,
        base_y, predicted_x, predicted_y, coefficient_x, coefficient_y,
        gamma_x, gamma_y;
    std::vector<std::uint8_t> open_x, open_y;
    // Wall and body impulse totals of one row block.
    struct Loads {
      DoubleDouble wall_x, wall_y, body_y, wall_scale_x, wall_scale_y;
    };
    std::vector<Loads> loads;
  } scratch;

public:
  PressureSolver(const ma::experimental::MacGrid &g, unsigned threads,
                 ma::PressurePreconditioner preconditioner =
                     ma::PressurePreconditioner::automatic,
                 double tolerance = 1e-8, unsigned gravity_source = 0,
                 kernel::Pool *executor = nullptr, unsigned maximum = 200,
                 bool surface_closure = false, double thin_gas_mass = 0,
                 double thin_gas_time = 0, std::size_t geometric_system_cells = 0)
      : source(g), grid(source),
        owned_pool(executor
                       ? nullptr
                       : std::make_unique<kernel::Pool>(static_cast<unsigned>(
                             std::min<std::size_t>(threads, grid.blocks)))),
        pool(executor ? *executor : *owned_pool), relative_tolerance(tolerance),
        gravity_rule(gravity_source), maximum_iterations(maximum),
        close_empty_inflow(surface_closure), thin_mass(thin_gas_mass),
        thin_time(thin_gas_time), geometric_cells(geometric_system_cells),
        context{workspace,
                                              source,
                                              model,
                                              grid,
                                              settings,
                                              options,
                                              kernel::Exec{grid, pool},
                                              {},
                                              {}} {
    // Cells gather their face terms in nonperiodic face order.
    if (grid.px || grid.py)
      throw std::runtime_error("Game pressure requires a nonperiodic grid");
    if (threads == 0 || !std::isfinite(relative_tolerance) ||
        relative_tolerance <= 0 || gravity_rule > 1 || maximum == 0 || maximum > 65536)
      throw std::runtime_error("Invalid pressure worker count/tolerance");
    if (!std::isfinite(thin_mass) || thin_mass < 0 || !std::isfinite(thin_time) ||
        thin_time < 0 || (thin_mass > 0) != (thin_time > 0))
      throw std::runtime_error("Invalid thin-gas drag");
    options.threads = threads;
    settings.preconditioner = preconditioner;
  }
  // Cells marked in `fixed` are held like readings with fixed_pressure.
  // `storage` lends the capacity of a result the caller no longer needs;
  // every entry is assigned. Cell and face passes run in the grid's fixed
  // row blocks, and each cell gathers its face terms in the original face
  // order, so no value depends on the worker count.
  Pressure advance(const std::vector<Eos> &states,
                   const std::vector<double> &old_x,
                   const std::vector<double> &old_y, double dt, double gravity,
                   bool positive_correction = false,
                   const std::vector<std::uint8_t> *fixed = nullptr,
                   Pressure storage = {},
                   const std::vector<double> *boiling = nullptr,
                   const std::vector<Stiffened> *stiffened = nullptr) {
    const auto N = grid.N;
    const auto &g = grid;
    if (states.size() != N || old_x.size() != g.NX || old_y.size() != g.NY ||
        !(dt > 0) || (fixed && fixed->size() != N))
      throw std::runtime_error("Invalid pressure shapes");
    auto &w = scratch;
    auto &system = w.system;
    system.g = &g;
    for (auto *v : {&system.diag, &w.rhs, &w.increment_rhs})
      v->resize(N);
    system.active.resize(N);
    for (auto *v : {&system.wx, &w.inertia_x, &w.base_x, &w.predicted_x,
                    &w.coefficient_x})
      v->resize(g.NX);
    for (auto *v : {&system.wy, &w.inertia_y, &w.base_y, &w.predicted_y,
                    &w.coefficient_y})
      v->resize(g.NY);
    const bool thin = thin_mass > 0;
    w.gamma_x.assign(thin ? g.NX : 0, 0.);
    w.gamma_y.assign(thin ? g.NY : 0, 0.);
    w.open_x.resize(g.NX);
    w.open_y.resize(g.NY);
    Pressure result = std::move(storage);
    result.iterations = result.corrections = result.geometric = 0;
    result.residual = 0;
    result.converged = false;
    for (auto *v : {&result.jx, &result.qx, &result.damping_x})
      v->resize(g.NX);
    for (auto *v : {&result.jy, &result.qy, &result.damping_y,
                    &result.body_impulses_y})
      v->resize(g.NY);
    result.blocked_x.resize(g.NX);
    result.blocked_y.resize(g.NY);
    result.held_y.assign(thin ? g.NY : 0, 0);
    result.drag_x.assign(thin ? g.NX : 0, 0.);
    result.drag_y.assign(thin ? g.NY : 0, 0.);
    const bool forced = forces != nullptr;
    result.force_impulse.assign(forced ? forces->size() : 0, 0.);
    result.force_work.assign(forced ? forces->size() : 0, 0.);
    for (auto &entry : driven)
      entry.body = entry.gamma = 0;
    if (forced)
      std::fill(force_part.begin(), force_part.end(), std::uint8_t{0});
    const auto x_faces = [&](std::size_t y0, std::size_t y1) {
      return std::pair{y0 * g.XW, y1 * g.XW};
    };
    const auto y_faces = [&](std::size_t y0, std::size_t block) {
      return std::pair{y0 * g.W, g.block_yrow_end(block) * g.W};
    };
    context.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) {
      for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
        system.active[i] = 0;
        system.diag[i] = 0;
        w.rhs[i] = 0;
        if (g.wall[i] || states[i].mass == 0 || states[i].fixed_pressure ||
            (fixed && (*fixed)[i]))
          continue;
        const auto &s = states[i];
        const bool stiff = stiffened && (*stiffened)[i].compliance > 0;
        double compliance = 0;
        if (stiff)
          compliance = (*stiffened)[i].compliance;
        else {
          if (s.nR > 0)
            compliance = s.gas_volume / ((1 + s.nR / s.capacity) * s.pressure);
          if (s.liquid_nominal_volume > 0)
            compliance += s.liquid_compliance;
          // A partly filled, gas-free surface is at vacuum pressure.
          if (s.nR == 0 && s.liquid_nominal_volume < g.V)
            continue;
        }
        if (!(compliance > 0 && std::isfinite(compliance)))
          throw std::runtime_error("Invalid pressure compliance");
        system.active[i] = 1;
        system.diag[i] = compliance;
        w.rhs[i] = compliance * s.pressure;
      }
    });
    const auto mass = [&](kernel::Index i) {
      return i == kernel::none ? 0 : states[i].mass;
    };
    // A driven face's momentum for the impulse `rest` of everything else and
    // the drag `drag`: the root J of
    //   J (1 + drag) = rest + sum_k s_k B_k clamp(1 - s_k J / (I v_k), 0, 1),
    // B_k = dt A stall_k, whose right side never rises with J, so there is
    // one. Each force's range is the one the root lies in, and the entry's
    // body and gamma are their sums. (A range chosen from the velocity before
    // the step instead would let a face of thin gas that drifts backwards
    // take the whole constant impulse: dt A stall on a milligram.)
    const auto drive_face = [&](Driven &entry, double inertia, double rest, double drag, double step) {
      const auto begin = entry.first, end = entry.first + entry.count;
      std::vector<double> cuts{0.};
      for (auto at = begin; at < end; ++at) {
        const auto &force = (*forces)[force_order[at]];
        cuts.push_back(force.sign * (inertia * force.free_speed));
      }
      std::sort(cuts.begin(), cuts.end());
      const auto excess = [&](double j) {
        double sum = j * (1 + drag) - rest;
        for (auto at = begin; at < end; ++at) {
          const auto &force = (*forces)[force_order[at]];
          const double share = 1 - force.sign * j / (inertia * force.free_speed);
          sum -= force.sign * (step * g.A * force.stall_pa) * std::clamp(share, 0., 1.);
        }
        return sum;
      };
      std::size_t cut = 0;
      while (cut < cuts.size() && excess(cuts[cut]) < 0)
        ++cut;
      const double probe = cut == 0 ? cuts.front() - std::max(1., std::abs(cuts.front()))
                           : cut == cuts.size() ? cuts.back() + std::max(1., std::abs(cuts.back()))
                                                : .5 * (cuts[cut - 1] + cuts[cut]);
      entry.body = entry.gamma = 0;
      for (auto at = begin; at < end; ++at) {
        const auto k = force_order[at];
        const auto &force = (*forces)[k];
        const double along = force.sign * probe, limit = inertia * force.free_speed;
        const double impulse = step * g.A * force.stall_pa;
        force_part[k] = along >= limit ? 0 : along >= 0 ? 2 : 1;
        if (force_part[k])
          entry.body += force.sign * impulse;
        if (force_part[k] == 2)
          entry.gamma += impulse / limit;
      }
      return (rest + entry.body) / (1 + (drag + entry.gamma));
    };
    // One face's terms. Nothing crosses a face out of an empty cell. Where
    // the momentum predicted from the current pressures would do so, the
    // face is a closed free surface for this interval: no body or pressure
    // impulse acts on material that is not there, and any momentum left on
    // it is returned to the boundary as local heat (booked below).
    const auto face = [&](bool x, std::size_t f) {
      const auto l = (x ? g.xl : g.yl)[f], r = (x ? g.xr : g.yr)[f];
      const double old = (x ? old_x : old_y)[f];
      auto &blocked = (x ? result.blocked_x : result.blocked_y)[f];
      auto &inertia = (x ? w.inertia_x : w.inertia_y)[f];
      auto &open = (x ? w.open_x : w.open_y)[f];
      auto &weight = (x ? system.wx : system.wy)[f];
      blocked = open = 0;
      weight = 0;
      if (!x)
        result.body_impulses_y[f] = 0;
      inertia = .5 * (mass(l) + mass(r));
      const bool fluid =
          l != kernel::none && r != kernel::none && !g.closed(!x, f);
      // Face forces: this face's entry, if it is driven.
      Driven *drive = nullptr;
      if (forced && fluid && inertia > 0)
        if (const auto slot = (x ? force_slot_x : force_slot_y)[f])
          drive = &driven[slot - 1];
      if (close_empty_inflow && fluid) {
        const double ml = mass(l), mr = mass(r);
        if ((ml == 0) != (mr == 0)) {
          const double I = .5 * (ml + mr);
          const double pl = system.active[l] ? states[l].pressure : 0,
                       pr = system.active[r] ? states[r].pressure : 0;
          double predicted = old + (x ? 0 : -gravity * dt * I) +
                             dt * g.A * (pl - pr);
          if (drive)
            predicted = drive_face(*drive, inertia, predicted, 0, dt);
          if ((ml == 0 && predicted > 0) || (mr == 0 && predicted < 0))
            blocked = 1;
        }
      }
      // Profile 12 (MAC_GAME_KERNEL.md, "A liquid surface under thin gas"):
      // the face above a partly filled liquid cell carries half that cell's
      // liquid as inertia, and gravity acts on all of it. Under a room's gas
      // the cell's own gas holds that weight, compressed until it carries it.
      // Where the cell's gas could not carry even a tenth of it (vacuum, a
      // few nanograms of vapour) the weight had nothing to oppose it but
      // drag: the face fell at a terminal 30-80 m/s, sweeping cells of
      // near-vacuum into the cell every interval while carrying no material
      // (the profile 4 closure above acts only when the cell above is exactly
      // empty). Downward across such a face only the material above can move:
      // when that material holds no more liquid than the vent drag's mist
      // and is lighter than the face's inertia, the face is closed for the
      // interval like an empty cell's if the material is a trace (at most
      // held_surface_trace_kg; momentum stranded on it becomes heat), and
      // otherwise takes the weight of that material only. Liquid falling onto
      // the surface, and a surface its gas can hold (a pool under air), keep
      // the face's full weight.
      bool held_face = false;
      if (!x && thin && close_empty_inflow && gravity_rule == 0 && gravity != 0 && fluid && !blocked && inertia > 0) {
        const auto below = gravity > 0 ? l : r, above = gravity > 0 ? r : l;
        const auto &held = states[below], &over = states[above];
        if (held.mass > 0 && held.liquid_nominal_volume > 0 && held.liquid_nominal_volume < g.V &&
            !held.fixed_pressure && !(fixed && (*fixed)[below]) &&
            !(stiffened && (*stiffened)[below].compliance > 0) &&
            !(over.liquid_nominal_volume > vent_mist_fraction * g.V) && over.mass < inertia &&
            (held.nR > 0 ? held.pressure : 0.) <
                held_surface_gas_share * std::abs(gravity) * (inertia - over.mass) / g.A) {
          const double pl = system.active[l] ? states[l].pressure : 0,
                       pr = system.active[r] ? states[r].pressure : 0;
          if ((old - gravity * dt * inertia + dt * g.A * (pl - pr)) * gravity < 0) {
            if (over.mass <= held_surface_trace_kg)
              blocked = 1;
            else
              held_face = true;
          }
        }
      }
      if (!fluid || inertia == 0 || blocked) {
        // No force acts on a face nothing crosses.
        if (drive) {
          for (std::size_t at = drive->first; at < drive->first + drive->count; ++at)
            force_part[force_order[at]] = 0;
          drive->body = drive->gamma = 0;
        }
        return;
      }
      double body = x ? 0 : -gravity * dt * inertia;
      if (held_face) {
        body = -gravity * dt * states[gravity > 0 ? r : l].mass;
        result.held_y[f] = 1;
      }
      if (!x && gravity_rule == 1) {
        const double predicted =
            old + dt * g.A * (states[l].pressure - states[r].pressure) + body;
        const double donor = predicted > 0   ? states[l].mass
                             : predicted < 0 ? states[r].mass
                                             : inertia;
        body = -gravity * dt * donor;
      }
      if (!x)
        result.body_impulses_y[f] = body;
      // Drag (thin gas, liquid, gas), implicit: J(1 + gamma) = old + body +
      // pressure impulse, so the face enters the system with inertia
      // I(1 + gamma).
      double effective = inertia;
      if (thin) {
        double gamma = inertia < thin_mass ? dt / thin_time * (thin_mass / inertia - 1) : 0;
        // Liquid drag: the face's share of liquid by volume, relaxed at
        // 1/liquid_drag_time.
        const double liquid = .5 * (states[l].liquid_nominal_volume + states[r].liquid_nominal_volume) / g.V;
        if (liquid > 0)
          gamma += dt / liquid_drag_time * std::min(1., liquid);
        // Gas drag: the rest of the face, relaxed at 1/gas_drag_time.
        if (liquid < 1)
          gamma += dt / gas_drag_time * (1 - std::max(0., liquid));
        if (sinks && sink_speed > 0 && liquid <= vent_mist_fraction && ((*sinks)[l] || (*sinks)[r]) &&
            (states[l].nR > 0 || states[r].nR > 0)) {
          const double pl = system.active[l] ? states[l].pressure : 0,
                       pr = system.active[r] ? states[r].pressure : 0;
          const double predicted = std::abs(old + body + dt * g.A * (pl - pr));
          gamma = std::max(gamma, predicted / (inertia * sink_speed) - 1);
        }
        if (gamma > 0) {
          (x ? w.gamma_x : w.gamma_y)[f] = gamma;
          effective = inertia * (1 + gamma);
        }
      }
      if (drive) {
        // The drags' gamma and the forces' share one implicit update. Each
        // force's range is that of the momentum the pressures at the start
        // of the interval predict.
        const double drag = thin ? (x ? w.gamma_x : w.gamma_y)[f] : 0;
        const double pl = system.active[l] ? states[l].pressure : 0,
                     pr = system.active[r] ? states[r].pressure : 0;
        drive_face(*drive, inertia, old + body + dt * g.A * (pl - pr), drag, dt);
        effective = inertia * (1 + (drag + drive->gamma));
        const double base = dt * g.A * (old + body + drive->body) / effective;
        const double coefficient = dt * dt * g.A * g.A / effective;
        (x ? w.base_x : w.base_y)[f] = base;
        (x ? w.predicted_x : w.predicted_y)[f] = base + coefficient * (pl - pr);
        (x ? w.coefficient_x : w.coefficient_y)[f] = coefficient;
        if (system.active[l] && system.active[r])
          weight = coefficient;
        open = 1;
        return;
      }
      const double base = dt * g.A * (old + body) / effective;
      const double coefficient = dt * dt * g.A * g.A / effective;
      const double pl = system.active[l] ? states[l].pressure : 0,
                   pr = system.active[r] ? states[r].pressure : 0;
      (x ? w.base_x : w.base_y)[f] = base;
      (x ? w.predicted_x : w.predicted_y)[f] = base + coefficient * (pl - pr);
      (x ? w.coefficient_x : w.coefficient_y)[f] = coefficient;
      if (system.active[l] && system.active[r])
        weight = coefficient;
      open = 1;
    };
    context.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t block) {
      for (auto [f, end] = x_faces(y0, y1); f < end; ++f)
        face(true, f);
      for (auto [f, end] = y_faces(y0, block); f < end; ++f)
        face(false, f);
    });
    // Each active cell adds its open faces' terms in the order of the face
    // loops they came from: left and right x faces, then bottom and top y
    // faces. A coupling to an inactive neighbour joins the diagonal.
    context.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) {
      for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
        if (!system.active[i]) {
          w.rhs[i] = 0;
          w.increment_rhs[i] = 0;
          continue;
        }
        double rhs = w.rhs[i], increment = 0, diagonal = system.diag[i];
        const auto gather = [&](bool x, std::size_t f, bool upper) {
          if (!(x ? w.open_x : w.open_y)[f])
            return;
          const auto other = upper ? (x ? g.xr : g.yr)[f] : (x ? g.xl : g.yl)[f];
          const double base = (x ? w.base_x : w.base_y)[f],
                       predicted = (x ? w.predicted_x : w.predicted_y)[f];
          if (upper) {
            rhs -= base;
            increment -= predicted;
          } else {
            rhs += base;
            increment += predicted;
          }
          if (!system.active[other])
            diagonal += (x ? w.coefficient_x : w.coefficient_y)[f];
        };
        gather(true, g.left_face(i), false);
        gather(true, g.right_face(i), true);
        gather(false, g.bottom_face(i), false);
        gather(false, g.top_face(i), true);
        // Profile 12: the volume the cell's boiling adds this interval
        // (BoilPlan) pushes material out at the solved pressure.
        if (boiling && (*boiling)[i] != 0) {
          rhs += (*boiling)[i];
          increment += (*boiling)[i];
        }
        if (stiffened && (*stiffened)[i].compliance > 0) {
          rhs += (*stiffened)[i].offset;
          increment += (*stiffened)[i].offset;
        }
        w.rhs[i] = rhs;
        w.increment_rhs[i] = increment;
        system.diag[i] = diagonal;
      }
    });
    DoubleDouble rhs_norm;
    for (std::size_t i = 0; i < N; ++i)
      rhs_norm.add(w.rhs[i] * w.rhs[i]);
    const double tolerance =
        std::max(1e-14, relative_tolerance * std::sqrt(rhs_norm.value()));
    // Components of at least 4096 cells solve across the pool; their dot
    // products sum fixed 1024-cell blocks (PressureParallelPlan).
    const auto solved = kernel::solve_components_cached(
        context, component_cache[(fixed ? 1 : 0) + (stiffened ? 2 : 0)], system, w.increment_rhs,
        result.p, 0, tolerance, maximum_iterations, 4096, geometric_cells);
    context.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) {
      for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i)
        if (system.active[i])
          result.p[i] += states[i].pressure;
    });
    result.iterations = solved.iterations;
    result.geometric = solved.geometric ? 1 : 0;
    result.residual = solved.residual;
    result.converged = solved.converged;
    // Wall and body impulse totals: double-double partials of the fixed row
    // blocks, combined in block order.
    using Loads = Scratch::Loads;
    w.loads.assign(g.blocks, Loads{});
    const auto publish = [&](bool x, std::size_t f, Loads &loads) {
      const auto l = (x ? g.xl : g.yl)[f], r = (x ? g.xr : g.yr)[f];
      const double old = (x ? old_x : old_y)[f];
      const double inertia = (x ? w.inertia_x : w.inertia_y)[f];
      auto &next = (x ? result.jx : result.jy)[f];
      auto &swept = (x ? result.qx : result.qy)[f];
      auto &damping = (x ? result.damping_x : result.damping_y)[f];
      if (!(x ? w.open_x : w.open_y)[f]) {
        const bool blocked = (x ? result.blocked_x : result.blocked_y)[f];
        if (old != 0 && !blocked)
          throw std::runtime_error(
              "Prototype fixed/empty face has incoming momentum");
        next = swept = damping = 0;
        const double pl = l == kernel::none ? 0 : result.p[l],
                     pr = r == kernel::none ? 0 : result.p[r];
        auto &wall = x ? loads.wall_x : loads.wall_y;
        auto &scale = x ? loads.wall_scale_x : loads.wall_scale_y;
        wall.add(dt * g.A * (pr - pl));
        scale.add(dt * g.A * (std::abs(pr) + std::abs(pl)));
        if (blocked && old != 0) {
          // The boundary takes the stranded momentum; its kinetic energy
          // becomes heat in the occupied neighbour.
          wall.add(-old);
          scale.add(std::abs(old));
          damping = .5 * old * (old / inertia);
        }
        return;
      }
      const double body = x ? 0 : result.body_impulses_y[f];
      loads.body_y.add(body);
      if (const auto slot = forced ? (x ? force_slot_x : force_slot_y)[f] : 0) {
        // A driven face. The drag's share of the implicit update is taken
        // and heats as on any face; the forces' share is their impulse.
        const auto &drive = driven[slot - 1];
        const double gamma = thin ? (x ? w.gamma_x : w.gamma_y)[f] : 0;
        next = (old + dt * g.A * (result.p[l] - result.p[r]) + body + drive.body) /
               (1 + (gamma + drive.gamma));
        auto &wall = x ? loads.wall_x : loads.wall_y;
        auto &scale = x ? loads.wall_scale_x : loads.wall_scale_y;
        double taken = 0;
        if (gamma > 0) {
          taken = gamma * next;
          wall.add(-taken);
          scale.add(std::abs(taken));
          (x ? result.drag_x : result.drag_y)[f] = taken;
        }
        const double velocity = next / inertia;
        for (std::size_t at = drive.first; at < drive.first + drive.count; ++at) {
          const auto k = force_order[at];
          if (!force_part[k])
            continue;
          const auto &force = (*forces)[k];
          const double pushed = force.sign * (dt * g.A * force.stall_pa);
          const double eased =
              force_part[k] == 2 ? dt * g.A * force.stall_pa / (inertia * force.free_speed) * next : 0;
          const double given = pushed - eased;
          wall.add(pushed);
          wall.add(-eased);
          scale.add(std::abs(pushed));
          scale.add(std::abs(eased));
          result.force_impulse[k] = given;
          result.force_work[k] = given * velocity;
        }
        swept = dt * g.A * next / inertia;
        const double impulse = next - old;
        damping = .5 * impulse * (impulse / inertia);
        if (gamma > 0)
          damping += taken * (next / inertia);
        return;
      }
      next = old + dt * g.A * (result.p[l] - result.p[r]) + body;
      const double gamma = thin ? (x ? w.gamma_x : w.gamma_y)[f] : 0;
      if (gamma > 0) {
        // The drag takes gamma J of momentum, an external load, and its
        // kinetic work gamma J^2 / I becomes local heat with the damping.
        const double before_drag = next;
        next /= 1 + gamma;
        // At subnormal J, rounding the retained and removed momenta
        // independently can give the wall one more unit than the face lost.
        const double taken = std::abs(next) < std::numeric_limits<double>::min()
                                 ? before_drag - next
                                 : gamma * next;
        auto &wall = x ? loads.wall_x : loads.wall_y;
        auto &scale = x ? loads.wall_scale_x : loads.wall_scale_y;
        wall.add(-taken);
        scale.add(std::abs(taken));
        (x ? result.drag_x : result.drag_y)[f] = taken;
      }
      swept = dt * g.A * next / inertia;
      const double impulse = next - old;
      damping = .5 * impulse * (impulse / inertia);
      if (gamma > 0)
        damping += (x ? result.drag_x : result.drag_y)[f] * (next / inertia);
    };
    context.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t block) {
      auto &loads = w.loads[block];
      for (auto [f, end] = x_faces(y0, y1); f < end; ++f)
        publish(true, f, loads);
      for (auto [f, end] = y_faces(y0, block); f < end; ++f)
        publish(false, f, loads);
    });
    Loads total;
    for (const auto &loads : w.loads)
      for (auto [sum, part] :
           {std::pair{&total.wall_x, &loads.wall_x},
            std::pair{&total.wall_y, &loads.wall_y},
            std::pair{&total.body_y, &loads.body_y},
            std::pair{&total.wall_scale_x, &loads.wall_scale_x},
            std::pair{&total.wall_scale_y, &loads.wall_scale_y}}) {
        sum->add(part->hi);
        sum->add(part->lo);
      }
    result.wall_impulse_x = total.wall_x.value();
    result.wall_impulse_y = total.wall_y.value();
    result.body_impulse_y = total.body_y.value();
    result.wall_operands_x = total.wall_scale_x.value();
    result.wall_operands_y = total.wall_scale_y.value();
    if (positive_correction) {
      auto constrained = fixed ? *fixed : std::vector<std::uint8_t>(N);
      bool changed = false;
      for (std::size_t i = 0; i < N; ++i)
        if (result.p[i] < 0) {
          constrained[i] = 1;
          changed = true;
        }
      if (changed) {
        const auto iterations = result.iterations, geometric = result.geometric;
        const bool converged = result.converged;
        auto corrected = advance(states, old_x, old_y, dt, gravity, false,
                                 &constrained, std::move(result), boiling,
                                 stiffened);
        corrected.iterations += iterations;
        corrected.geometric += geometric;
        corrected.corrections = 1;
        corrected.converged = corrected.converged && converged;
        return corrected;
      }
    }
    return result;
  }
};
} // namespace ma::experimental::game_kernel
