#pragma once
#include "../mac_kernel_detail.hpp"
#include "account.hpp"
#include "eos.hpp"
#include <memory>
namespace ma::experimental::game_kernel {
namespace kernel = ma::experimental::mac_kernel;
struct Pressure {
  std::vector<double> p, jx, jy, qx, qy, damping_x, damping_y, body_impulses_y;
  // Faces closed for this interval: one side empty and the predicted
  // momentum would carry material out of that empty cell.
  std::vector<std::uint8_t> blocked_x, blocked_y;
  std::size_t iterations = 0, corrections = 0;
  double residual = 0;
  bool converged = false;
  double wall_impulse_x = 0, wall_impulse_y = 0, body_impulse_y = 0;
  double wall_operands_x = 0, wall_operands_y = 0;
};
// Component prototype only: transport, positivity and receipts follow.
// Vacuum is a zero-pressure boundary until material arrives there.
class PressureSolver {
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
  kernel::Context context;
  // Capacity reused by every solve. Each pass writes every entry that a
  // later pass reads, so nothing carries over between calls.
  struct Scratch {
    kernel::LinearSystem system;
    std::vector<double> rhs, increment_rhs, inertia_x, inertia_y, base_x,
        base_y, predicted_x, predicted_y, coefficient_x, coefficient_y;
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
                 bool surface_closure = false)
      : source(g), grid(source),
        owned_pool(executor
                       ? nullptr
                       : std::make_unique<kernel::Pool>(static_cast<unsigned>(
                             std::min<std::size_t>(threads, grid.blocks)))),
        pool(executor ? *executor : *owned_pool), relative_tolerance(tolerance),
        gravity_rule(gravity_source), maximum_iterations(maximum),
        close_empty_inflow(surface_closure), context{workspace,
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
                   Pressure storage = {}) {
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
    w.open_x.resize(g.NX);
    w.open_y.resize(g.NY);
    Pressure result = std::move(storage);
    result.iterations = result.corrections = 0;
    result.residual = 0;
    result.converged = false;
    for (auto *v : {&result.jx, &result.qx, &result.damping_x})
      v->resize(g.NX);
    for (auto *v : {&result.jy, &result.qy, &result.damping_y,
                    &result.body_impulses_y})
      v->resize(g.NY);
    result.blocked_x.resize(g.NX);
    result.blocked_y.resize(g.NY);
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
        double compliance = 0;
        if (s.nR > 0)
          compliance = s.gas_volume / ((1 + s.nR / s.capacity) * s.pressure);
        if (s.liquid_nominal_volume > 0)
          compliance += s.liquid_compliance;
        // A partly filled, gas-free surface is at vacuum pressure.
        if (s.nR == 0 && s.liquid_nominal_volume < g.V)
          continue;
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
      if (close_empty_inflow && fluid) {
        const double ml = mass(l), mr = mass(r);
        if ((ml == 0) != (mr == 0)) {
          const double I = .5 * (ml + mr);
          const double pl = system.active[l] ? states[l].pressure : 0,
                       pr = system.active[r] ? states[r].pressure : 0;
          const double predicted = old + (x ? 0 : -gravity * dt * I) +
                                   dt * g.A * (pl - pr);
          if ((ml == 0 && predicted > 0) || (mr == 0 && predicted < 0))
            blocked = 1;
        }
      }
      if (!fluid || inertia == 0 || blocked)
        return;
      double body = x ? 0 : -gravity * dt * inertia;
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
      const double base = dt * g.A * (old + body) / inertia;
      const double coefficient = dt * dt * g.A * g.A / inertia;
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
    const auto solved = kernel::solve_linear(context, system, w.increment_rhs,
                                             result.p, 0, tolerance, maximum_iterations);
    context.exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) {
      for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i)
        if (system.active[i])
          result.p[i] += states[i].pressure;
    });
    result.iterations = solved.iterations;
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
      next = old + dt * g.A * (result.p[l] - result.p[r]) + body;
      swept = dt * g.A * next / inertia;
      const double impulse = next - old;
      damping = .5 * impulse * (impulse / inertia);
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
        const auto iterations = result.iterations;
        const bool converged = result.converged;
        auto corrected = advance(states, old_x, old_y, dt, gravity, false,
                                 &constrained, std::move(result));
        corrected.iterations += iterations;
        corrected.corrections = 1;
        corrected.converged = corrected.converged && converged;
        return corrected;
      }
    }
    return result;
  }
};
} // namespace ma::experimental::game_kernel
