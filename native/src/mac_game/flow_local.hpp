#pragma once
#include "patch.hpp"
#include "pressure_be.hpp"
#include "region.hpp"
#include <chrono>
namespace ma::experimental::game_kernel {
struct FlowReport {
  State state;
  std::vector<Eos> eos;
  std::size_t subcycles = 0, pressure_iterations = 0, pressure_corrections = 0,
              transport_cells = 0, transport_faces = 0;
  double wall_x = 0, wall_y = 0, body_y = 0, wall_ops_x = 0, wall_ops_y = 0,
         mass_error = 0, energy_error = 0, momentum_error = 0;
  double eos_ms = 0, pressure_ms = 0, transport_ms = 0;
};
class Flow {
  ma::experimental::MacGrid source;
  ma::experimental::mac_kernel::Grid g;
  Topology topology;
  std::vector<Species> species;
  mutable ma::experimental::mac_kernel::Pool eos_pool;
  PressureSolver pressure;
  unsigned gravity_rule = 0;
  bool audit_mode = true;
  PatchTransport patch;
  static double milliseconds(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - start)
        .count();
  }
  std::vector<double> totals(const SparseMass &masses) const {
    std::vector<double> result(g.N);
    for (std::size_t i = 0; i < g.N; ++i)
      for (std::size_t a = masses.offsets[i]; a < masses.offsets[i + 1]; ++a)
        result[i] += masses.mass[a];
    return result;
  }
  void book_energy(Balance &balance, const State &s, double gravity,
                   double sign) const {
    const auto mass = totals(s.masses);
    for (std::size_t i = 0; i < g.N; ++i) {
      balance.add(sign * s.energy[i]);
      balance.add(sign * mass[i] * gravity * (static_cast<double>(i / g.W) + .5) * source.spacing_m);
    }
    const auto faces = [&](const auto &left, const auto &right, const auto &j) {
      for (std::size_t f = 0; f < j.size(); ++f)
        if (j[f] != 0) {
          const double inertia =
              .5 *
              ((left[f] == ma::experimental::mac_kernel::none ? 0
                                                              : mass[left[f]]) +
               (right[f] == ma::experimental::mac_kernel::none
                    ? 0
                    : mass[right[f]]));
          balance.add(sign * .5 * j[f] * (j[f] / inertia));
        }
    };
    faces(g.xl, g.xr, s.jx);
    faces(g.yl, g.yr, s.jy);
  }

public:
  Flow(const ma::experimental::MacGrid &grid, std::vector<Species> table,
       unsigned threads, bool auditing = true, double pressure_tolerance = 1e-8,
       unsigned gravity_source = 0, unsigned pressure_iterations = 200,
       bool surface_closure = false)
      : source(grid), g(source), topology(g), species(std::move(table)),
        eos_pool(
            static_cast<unsigned>(std::min<std::size_t>(threads, g.blocks))),
        pressure(grid, threads, ma::PressurePreconditioner::incomplete_cholesky,
                 pressure_tolerance, gravity_source, &eos_pool, pressure_iterations,
                 surface_closure),
        gravity_rule(gravity_source), audit_mode(auditing),
        patch(grid, &eos_pool, &topology) {}
  ma::experimental::mac_kernel::Pool &worker_pool() const { return eos_pool; }
  const auto &region_topology() const { return topology; }
  const auto &kernel_grid() const { return g; }
  std::vector<Eos> evaluate_state(const State &state) const {
    std::vector<Eos> result(g.N);
    std::vector<std::string> errors(g.blocks);
    eos_pool.run(g.blocks, [&](std::size_t block) {
      for (std::size_t i = g.block_row_begin(block) * g.W;
           i < g.block_row_end(block) * g.W; ++i) {
        if (g.wall[i])
          continue;
        const auto begin = state.masses.offsets[i],
                   count = state.masses.offsets[i + 1] - begin;
        try {
          result[i] = evaluate_selected(
              species, count,
              [&](std::size_t a) { return state.masses.species[begin + a]; },
              [&](std::size_t a) { return state.masses.mass[begin + a]; },
              state.energy[i], g.V);
        } catch (const std::exception &e) {
          errors[block] =
              "Private flow EOS cell " + std::to_string(i) + ": " + e.what();
          break;
        }
      }
    });
    for (const auto &error : errors)
      if (!error.empty())
        throw std::runtime_error(error);
    return result;
  }
  FlowReport advance(const State &initial, double dt, double gravity,
                     RegionAudit *regions = nullptr,
                     const LiquidBoundary *boundary = nullptr) {
    namespace k = ma::experimental::mac_kernel;
    FlowReport result;
    auto start = std::chrono::steady_clock::now();
    const auto eos = evaluate_state(initial);
    result.eos_ms = milliseconds(start);
    start = std::chrono::steady_clock::now();
    const auto projected =
        pressure.advance(eos, initial.jx, initial.jy, dt, gravity, true);
    result.pressure_ms = milliseconds(start);
    result.pressure_iterations = projected.iterations;
    result.pressure_corrections = projected.corrections;
    if (!projected.converged)
      throw std::runtime_error("Private flow pressure did not converge");
    if (std::any_of(projected.p.begin(), projected.p.end(),
                    [](double p) { return p < 0; }))
      throw std::runtime_error(
          "Private flow correction retains negative pressure");
    result.state = initial;
    result.state.jx = projected.jx;
    result.state.jy = projected.jy;
    // Same time-centred swept volumes as the pressure impulse. No volume
    // limiter here.
    double max_out = 0;
    for (std::size_t i = 0; i < g.N; ++i)
      if (!g.wall[i]) {
        const auto l = g.left_face(i), r = g.right_face(i),
                   b = g.bottom_face(i), t = g.top_face(i);
        const double divergence = projected.qx[r] - projected.qx[l] +
                                  projected.qy[t] - projected.qy[b];
        result.state.energy[i] -= projected.p[i] * divergence;
        const double out =
            std::max(0., -projected.qx[l]) + std::max(0., projected.qx[r]) +
            std::max(0., -projected.qy[b]) + std::max(0., projected.qy[t]);
        max_out = std::max(max_out, out / g.V);
      }
    // Book body-force work locally. Potential-energy changes are booked
    // from each shared vertical mass flux during remap below.
    std::vector<double> body_heat(g.NY);
    for (std::size_t f = 0; f < g.NY; ++f) {
      const auto l = g.yl[f], r = g.yr[f];
      if (l == k::none || r == k::none || g.closed(true, f))
        continue;
      const double I = .5 * (eos[l].mass + eos[r].mass);
      body_heat[f] = gravity_rule == 0 ? gravity * dt * projected.jy[f]
                     : I > 0
                         ? -projected.body_impulses_y[f] * (projected.jy[f] / I)
                         : 0;
    }
    const auto deposit_vertical = [&](std::vector<double> &energy,
                                      const std::vector<double> &face,
                                      const SparseMass &inventory) {
      const auto mass = totals(inventory);
      for (std::size_t i = 0; i < g.N; ++i)
        if (!g.wall[i]) {
          const auto share = [&](std::size_t f) {
            if (face[f] == 0)
              return 0.;
            const double sum = (g.yl[f] == k::none ? 0 : mass[g.yl[f]]) +
                               (g.yr[f] == k::none ? 0 : mass[g.yr[f]]);
            if (sum == 0)
              throw std::runtime_error("Private empty vertical face has work");
            return face[f] * (mass[i] / sum);
          };
          energy[i] += share(g.bottom_face(i)) + share(g.top_face(i));
        }
    };
    deposit_vertical(result.state.energy, body_heat, result.state.masses);
    // Backward-Euler pressure removes unresolved kinetic oscillations.
    // Its exact nonnegative work defect is returned as local heat.
    for (std::size_t i = 0; i < g.N; ++i)
      if (!g.wall[i]) {
        const auto share = [&](bool x, std::size_t f, double value) {
          if (value == 0)
            return 0.;
          const auto &l = x ? g.xl : g.yl, &r = x ? g.xr : g.yr;
          const double sum = (l[f] == k::none ? 0 : eos[l[f]].mass) +
                             (r[f] == k::none ? 0 : eos[r[f]].mass);
          return value * (eos[i].mass / sum);
        };
        result.state.energy[i] +=
            share(true, g.left_face(i), projected.damping_x[g.left_face(i)]) +
            share(true, g.right_face(i), projected.damping_x[g.right_face(i)]) +
            share(false, g.bottom_face(i),
                  projected.damping_y[g.bottom_face(i)]) +
            share(false, g.top_face(i), projected.damping_y[g.top_face(i)]);
      }
    start = std::chrono::steady_clock::now();
    auto remap =
        patch.advance(result.state.masses, result.state.energy, result.state.jx,
                      result.state.jy, projected.qx, projected.qy, gravity,
                      audit_mode, &initial.passives, regions, boundary);
    result.subcycles = remap.microticks;
    result.transport_cells = remap.cell_updates;
    result.transport_faces = remap.face_updates;
    result.mass_error = remap.mass_error;
    result.wall_x += remap.wall_x;
    result.wall_y += remap.wall_y;
    result.wall_ops_x = remap.wall_ops_x;
    result.wall_ops_y = remap.wall_ops_y;
    result.state = {std::move(remap.masses), std::move(remap.energy),
                    std::move(remap.jx), std::move(remap.jy),
                    std::move(remap.passives)};
    result.transport_ms = milliseconds(start);
    result.wall_x += projected.wall_impulse_x;
    result.wall_y += projected.wall_impulse_y;
    result.body_y = projected.body_impulse_y;
    result.wall_ops_x += projected.wall_operands_x;
    result.wall_ops_y += projected.wall_operands_y;
    if (regions) {
      const auto book = [&](bool x, const auto &left, const auto &right) {
        const auto &blocked = x ? projected.blocked_x : projected.blocked_y;
        const auto &old = x ? initial.jx : initial.jy;
        for (std::size_t f = 0; f < left.size(); ++f) {
          const auto l = left[f], r = right[f];
          if (l == k::none || r == k::none || g.closed(!x, f) || blocked[f]) {
            if (l != k::none)
              regions->momentum(l, x, -dt * g.A * projected.p[l]);
            if (r != k::none)
              regions->momentum(r, x, dt * g.A * projected.p[r]);
            if (blocked[f] && old[f] != 0)
              regions->momentum(l, x, -old[f]);
          } else if (!x)
            regions->momentum(l, false,
                              gravity_rule == 0
                                  ? -gravity * dt * .5 *
                                        (eos[l].mass + eos[r].mass)
                                  : projected.body_impulses_y[f]);
        }
      };
      book(true, g.xl, g.xr);
      book(false, g.yl, g.yr);
    }
    if (audit_mode) {
      Balance energy, mx, my;
      book_energy(energy, initial, gravity, 1);
      book_energy(energy, result.state, gravity, -1);
      for (double j : initial.jx)
        mx.add(j);
      for (double j : result.state.jx)
        mx.add(-j);
      mx.add(result.wall_x);
      mx.operands.add(projected.wall_operands_x);
      for (double j : initial.jy)
        my.add(j);
      for (double j : result.state.jy)
        my.add(-j);
      my.add(result.wall_y);
      my.add(result.body_y);
      my.operands.add(projected.wall_operands_y);
      if (!energy.closed() || !mx.closed() || !my.closed())
        throw std::runtime_error(
            "Private flow total U+K+G/momentum audit failed: E=" +
            std::to_string(energy.residual.value()) +
            " mx=" + std::to_string(mx.residual.value()) +
            " my=" + std::to_string(my.residual.value()));
      result.energy_error =
          energy.operands.value()
              ? std::abs(energy.residual.value()) / energy.operands.value()
              : 0;
      result.momentum_error =
          std::max(mx.operands.value()
                       ? std::abs(mx.residual.value()) / mx.operands.value()
                       : 0,
                   my.operands.value()
                       ? std::abs(my.residual.value()) / my.operands.value()
                       : 0);
    }
    start = std::chrono::steady_clock::now();
    result.eos = evaluate_state(result.state);
    result.eos_ms += milliseconds(start);
    return result;
  }
};
} // namespace ma::experimental::game_kernel
