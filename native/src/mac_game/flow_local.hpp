#pragma once
#include "patch.hpp"
#include "pressure_be.hpp"
#include "region.hpp"
#include <bit>
#include <chrono>
#include <limits>
namespace ma::experimental::game_kernel {
struct FlowReport {
  State state;
  std::vector<Eos> eos;
  std::size_t subcycles = 0, pressure_iterations = 0, pressure_corrections = 0,
              transport_cells = 0, transport_faces = 0;
  double wall_x = 0, wall_y = 0, body_y = 0, wall_ops_x = 0, wall_ops_y = 0,
         mass_error = 0, energy_error = 0, momentum_error = 0;
  double eos_ms = 0, pressure_ms = 0, transport_ms = 0;
  // A pressure-only trial can request a shorter interval before any work,
  // transport or ledger entry. Its unchanged EOS storage returns to the caller.
  bool pressure_refinement_requested = false;
  double pressure_volume_change = 0;
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
  bool bounded_pressure_volume = false;
  PatchTransport patch;
  // Capacity of the previous pressure result, lent to the next solve.
  Pressure pressure_storage;
  // Scratch capacity; every entry is written before it is read.
  std::vector<double> cell_mass, body_heat;
  // Runs f(first row, end row, block) over the grid's fixed row blocks.
  template <class F> void rows(F &&f) const {
    eos_pool.run(g.blocks, [&](std::size_t block) {
      f(g.block_row_begin(block), g.block_row_end(block), block);
    });
  }
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
       bool surface_closure = false, bool bound_pressure_volume = false)
      : source(grid), g(source), topology(g), species(std::move(table)),
        eos_pool(
            static_cast<unsigned>(std::min<std::size_t>(threads, g.blocks))),
        pressure(grid, threads, ma::PressurePreconditioner::incomplete_cholesky,
                 pressure_tolerance, gravity_source, &eos_pool, pressure_iterations,
                 surface_closure),
        gravity_rule(gravity_source), audit_mode(auditing),
        bounded_pressure_volume(bound_pressure_volume),
        patch(grid, &eos_pool, &topology) {}
  ma::experimental::mac_kernel::Pool &worker_pool() const { return eos_pool; }
  const auto &region_topology() const { return topology; }
  const auto &kernel_grid() const { return g; }
  // `storage` lends the capacity of readings the caller no longer needs.
  // Every entry is assigned, walls the empty reading, so the result never
  // depends on its old contents.
  std::vector<Eos> evaluate_state(const State &state,
                                  std::vector<Eos> storage = {}) const {
    auto result = std::move(storage);
    result.resize(g.N);
    std::vector<std::string> errors(g.blocks);
    eos_pool.run(g.blocks, [&](std::size_t block) {
      for (std::size_t i = g.block_row_begin(block) * g.W;
           i < g.block_row_end(block) * g.W; ++i) {
        if (g.wall[i]) {
          result[i] = Eos{};
          continue;
        }
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
  // Readings of `after`, given `readings` of the cells `masses` and
  // `energy` describe. A cell whose inventory and energy are bitwise
  // unchanged keeps its reading, which evaluate_selected would reproduce
  // exactly (it reads nothing else); every other cell is evaluated as
  // evaluate_state does, so the result and any error are the same.
  std::vector<Eos> evaluate_changed(const SparseMass &masses,
                                    const std::vector<double> &energy,
                                    const State &after,
                                    std::vector<Eos> readings) const {
    if (readings.size() != g.N || masses.offsets.size() != g.N + 1 ||
        energy.size() != g.N)
      return evaluate_state(after, std::move(readings));
    const auto same = [](double a, double b) {
      return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
    };
    std::vector<std::string> errors(g.blocks);
    eos_pool.run(g.blocks, [&](std::size_t block) {
      for (std::size_t i = g.block_row_begin(block) * g.W;
           i < g.block_row_end(block) * g.W; ++i) {
        if (g.wall[i])
          continue;
        const auto begin = after.masses.offsets[i],
                   count = after.masses.offsets[i + 1] - begin;
        const auto old = masses.offsets[i];
        bool unchanged = same(energy[i], after.energy[i]) &&
                         masses.offsets[i + 1] - old == count;
        for (std::size_t a = 0; unchanged && a < count; ++a)
          unchanged = masses.species[old + a] == after.masses.species[begin + a] &&
                      same(masses.mass[old + a], after.masses.mass[begin + a]);
        if (unchanged)
          continue;
        try {
          readings[i] = evaluate_selected(
              species, count,
              [&](std::size_t a) { return after.masses.species[begin + a]; },
              [&](std::size_t a) { return after.masses.mass[begin + a]; },
              after.energy[i], g.V);
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
    return readings;
  }
  // `readings` are those of `initial` when the caller already holds them.
  FlowReport advance(const State &initial, double dt, double gravity,
                     RegionAudit *regions = nullptr,
                     const LiquidBoundary *boundary = nullptr,
                     std::vector<Eos> readings = {}) {
    namespace k = ma::experimental::mac_kernel;
    FlowReport result;
    auto start = std::chrono::steady_clock::now();
    auto eos = readings.empty() ? evaluate_state(initial) : std::move(readings);
    result.eos_ms = milliseconds(start);
    start = std::chrono::steady_clock::now();
    Pressure projected;
    try {
      projected = pressure.advance(eos, initial.jx, initial.jy, dt, gravity,
                                    true, nullptr, std::move(pressure_storage));
    } catch (const k::ComponentPressureSolveFailure &) {
      if (!bounded_pressure_volume) throw;
      result.pressure_ms = milliseconds(start);
      result.pressure_refinement_requested = true;
      result.eos = std::move(eos);
      return result;
    }
    result.pressure_ms = milliseconds(start);
    result.pressure_iterations = projected.iterations;
    result.pressure_corrections = projected.corrections;
    const auto request_refinement = [&] {
      result.pressure_refinement_requested = true;
      result.eos = std::move(eos);
      pressure_storage = std::move(projected);
    };
    if (!projected.converged) {
      if (!bounded_pressure_volume)
        throw std::runtime_error("Private flow pressure did not converge");
      request_refinement();
      return result;
    }
    const bool negative_pressure = std::any_of(projected.p.begin(), projected.p.end(),
                                              [](double p) { return p < 0; });
    if (negative_pressure && !bounded_pressure_volume)
      throw std::runtime_error(
          "Private flow correction retains negative pressure");
    if (bounded_pressure_volume) {
      if (negative_pressure || std::any_of(projected.p.begin(), projected.p.end(),
                                          [](double p) { return !std::isfinite(p); })) {
        request_refinement();
        return result;
      }
      std::vector<double> maxima(g.blocks);
      rows([&](std::size_t y0, std::size_t y1, std::size_t block) {
        double largest = 0;
        for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
          if (g.wall[i]) continue;
          const double divergence = projected.qx[g.right_face(i)] - projected.qx[g.left_face(i)] +
                                    projected.qy[g.top_face(i)] - projected.qy[g.bottom_face(i)];
          if (!std::isfinite(divergence)) {
            largest = std::numeric_limits<double>::infinity();
            break;
          }
          largest = std::max(largest, std::abs(divergence));
        }
        maxima[block] = largest;
      });
      const double largest = *std::max_element(maxima.begin(), maxima.end());
      result.pressure_volume_change = largest / g.V;
      // The pressure solve freezes its compliance and face velocities until
      // the remap finishes. Re-solve before a cell's net swept volume exceeds
      // the existing 0.4-cell transport scale. Net volume
      // leaves uniform fast through-flow unrestricted, and also detects a
      // front entering a cell containing a tiny nonzero gas inventory.
      // The one-pass nonnegative pressure correction can also need a finer
      // interval; neither condition may reach work or transport unchanged.
      if (largest > .4 * g.V) {
        request_refinement();
        return result;
      }
    }
    // Post-pressure U, cell by cell in the grid's fixed row blocks: the
    // pressure work of the time-centred swept volumes (no volume limiter
    // here), then body-force work from the vertical faces, then the damping
    // defect. Each cell adds its terms in the original order.
    const auto &mass = cell_mass;
    cell_mass.resize(g.N);
    body_heat.resize(g.NY);
    rows([&](std::size_t y0, std::size_t y1, std::size_t) {
      for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
        double total = 0;
        for (std::size_t a = initial.masses.offsets[i];
             a < initial.masses.offsets[i + 1]; ++a)
          total += initial.masses.mass[a];
        cell_mass[i] = total;
      }
    });
    // Body-force work is booked locally. Potential-energy changes are
    // booked from each shared vertical mass flux during remap below.
    rows([&](std::size_t y0, std::size_t, std::size_t block) {
      for (std::size_t f = y0 * g.W; f < g.block_yrow_end(block) * g.W; ++f) {
        const auto l = g.yl[f], r = g.yr[f];
        if (l == k::none || r == k::none || g.closed(true, f)) {
          body_heat[f] = 0;
          continue;
        }
        const double I = .5 * (eos[l].mass + eos[r].mass);
        body_heat[f] = gravity_rule == 0 ? gravity * dt * projected.jy[f]
                       : I > 0
                           ? -projected.body_impulses_y[f] * (projected.jy[f] / I)
                           : 0;
      }
    });
    std::vector<double> work = initial.energy;
    rows([&](std::size_t y0, std::size_t y1, std::size_t) {
      for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
        if (g.wall[i])
          continue;
        const auto l = g.left_face(i), r = g.right_face(i),
                   b = g.bottom_face(i), t = g.top_face(i);
        const double divergence = projected.qx[r] - projected.qx[l] +
                                  projected.qy[t] - projected.qy[b];
        double u = work[i];
        u -= projected.p[i] * divergence;
        const auto vertical = [&](std::size_t f) {
          if (body_heat[f] == 0)
            return 0.;
          const double sum = (g.yl[f] == k::none ? 0 : mass[g.yl[f]]) +
                             (g.yr[f] == k::none ? 0 : mass[g.yr[f]]);
          if (sum == 0)
            throw std::runtime_error("Private empty vertical face has work");
          return body_heat[f] * (mass[i] / sum);
        };
        u += vertical(b) + vertical(t);
        // Backward-Euler pressure removes unresolved kinetic oscillations.
        // Its exact nonnegative work defect is returned as local heat.
        const auto damping = [&](bool x, std::size_t f, double value) {
          if (value == 0)
            return 0.;
          const auto &left = x ? g.xl : g.yl, &right = x ? g.xr : g.yr;
          const double sum = (left[f] == k::none ? 0 : eos[left[f]].mass) +
                             (right[f] == k::none ? 0 : eos[right[f]].mass);
          return value * (eos[i].mass / sum);
        };
        u += damping(true, l, projected.damping_x[l]) +
             damping(true, r, projected.damping_x[r]) +
             damping(false, b, projected.damping_y[b]) +
             damping(false, t, projected.damping_y[t]);
        work[i] = u;
      }
    });
    start = std::chrono::steady_clock::now();
    auto remap =
        patch.advance(initial.masses, std::move(work), projected.jx,
                      projected.jy, projected.qx, projected.qy, gravity,
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
    pressure_storage = std::move(projected);
    start = std::chrono::steady_clock::now();
    result.eos = evaluate_changed(initial.masses, initial.energy, result.state,
                                  std::move(eos));
    result.eos_ms += milliseconds(start);
    return result;
  }
};
} // namespace ma::experimental::game_kernel
