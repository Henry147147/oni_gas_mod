#pragma once
#include "../mac_kernel_detail.hpp"
#include "account.hpp"
#include "eos.hpp"
#include <memory>
namespace ma::experimental::game_kernel {
namespace kernel = ma::experimental::mac_kernel;
struct Pressure {
  std::vector<double> p, jx, jy, qx, qy, damping_x, damping_y, body_impulses_y;
  std::size_t iterations = 0, corrections = 0;
  double residual = 0;
  bool converged = false;
  double wall_impulse_x = 0, wall_impulse_y = 0, body_impulse_y = 0,
         body_work = 0;
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
  unsigned gravity_rule = 0;
  kernel::Context context;

public:
  PressureSolver(const ma::experimental::MacGrid &g, unsigned threads,
                 ma::PressurePreconditioner preconditioner =
                     ma::PressurePreconditioner::automatic,
                 double tolerance = 1e-8, unsigned gravity_source = 0,
                 kernel::Pool *executor = nullptr)
      : source(g), grid(source),
        owned_pool(executor
                       ? nullptr
                       : std::make_unique<kernel::Pool>(static_cast<unsigned>(
                             std::min<std::size_t>(threads, grid.blocks)))),
        pool(executor ? *executor : *owned_pool), relative_tolerance(tolerance),
        gravity_rule(gravity_source), context{workspace,
                                              source,
                                              model,
                                              grid,
                                              settings,
                                              options,
                                              kernel::Exec{grid, pool},
                                              {},
                                              {}} {
    if (threads == 0 || !std::isfinite(relative_tolerance) ||
        relative_tolerance <= 0 || gravity_rule > 1)
      throw std::runtime_error("Invalid pressure worker count/tolerance");
    options.threads = threads;
    settings.preconditioner = preconditioner;
  }
  Pressure advance(const std::vector<Eos> &states,
                   const std::vector<double> &old_x,
                   const std::vector<double> &old_y, double dt, double gravity,
                   bool positive_correction = false) {
    const auto N = grid.N;
    const auto &g = grid;
    if (states.size() != N || old_x.size() != g.NX || old_y.size() != g.NY ||
        !(dt > 0))
      throw std::runtime_error("Invalid pressure shapes");
    kernel::LinearSystem system;
    system.g = &g;
    system.diag.assign(N, 0);
    system.active.assign(N, 0);
    system.wx.assign(g.NX, 0);
    system.wy.assign(g.NY, 0);
    std::vector<double> compliance(N), rhs(N), increment_rhs(N), ix(g.NX),
        iy(g.NY), base_x(g.NX), base_y(g.NY), body_y_face(g.NY);
    for (std::size_t i = 0; i < N; ++i) {
      if (g.wall[i] || states[i].mass == 0 || states[i].fixed_pressure)
        continue;
      const auto &s = states[i];
      if (s.nR > 0)
        compliance[i] = s.gas_volume / ((1 + s.nR / s.capacity) * s.pressure);
      if (s.liquid_nominal_volume > 0)
        compliance[i] += s.liquid_compliance;
      // A partly filled, gas-free surface is at vacuum pressure.
      if (s.nR == 0 && s.liquid_nominal_volume < g.V)
        continue;
      if (!(compliance[i] > 0 && std::isfinite(compliance[i])))
        throw std::runtime_error("Invalid pressure compliance");
      system.active[i] = 1;
      system.diag[i] = compliance[i];
      rhs[i] = compliance[i] * s.pressure;
    }
    const auto mass = [&](kernel::Index i) {
      return i == kernel::none ? 0 : states[i].mass;
    };
    const auto faces = [&](bool x, const auto &left, const auto &right,
                           const auto &old, auto &inertia, auto &base,
                           auto &weights) {
      for (std::size_t f = 0; f < old.size(); ++f) {
        const auto l = left[f], r = right[f];
        inertia[f] = .5 * (mass(l) + mass(r));
        if (l == kernel::none || r == kernel::none || g.closed(!x, f) ||
            inertia[f] == 0)
          continue;
        double body = x ? 0 : -gravity * dt * inertia[f];
        if (!x && gravity_rule == 1) {
          const double predicted =
              old[f] + dt * g.A * (states[l].pressure - states[r].pressure) +
              body;
          const double donor = predicted > 0   ? states[l].mass
                               : predicted < 0 ? states[r].mass
                                               : inertia[f];
          body = -gravity * dt * donor;
        }
        if (!x)
          body_y_face[f] = body;
        base[f] = dt * g.A * (old[f] + body) / inertia[f];
        const double coefficient = dt * dt * g.A * g.A / inertia[f];
        rhs[l] -= base[f];
        rhs[r] += base[f];
        const double pl = system.active[l] ? states[l].pressure : 0,
                     pr = system.active[r] ? states[r].pressure : 0;
        const double predicted = base[f] + coefficient * (pl - pr);
        increment_rhs[l] -= predicted;
        increment_rhs[r] += predicted;
        if (system.active[l] && system.active[r])
          weights[f] = coefficient;
        else if (system.active[l])
          system.diag[l] += coefficient;
        else if (system.active[r])
          system.diag[r] += coefficient;
      }
    };
    faces(true, g.xl, g.xr, old_x, ix, base_x, system.wx);
    faces(false, g.yl, g.yr, old_y, iy, base_y, system.wy);
    DoubleDouble rhs_norm;
    for (std::size_t i = 0; i < N; ++i) {
      if (!system.active[i]) {
        rhs[i] = 0;
        increment_rhs[i] = 0;
      }
      rhs_norm.add(rhs[i] * rhs[i]);
    }
    Pressure result;
    const double tolerance =
        std::max(1e-14, relative_tolerance * std::sqrt(rhs_norm.value()));
    const auto solved = kernel::solve_linear(context, system, increment_rhs,
                                             result.p, 0, tolerance, 200);
    for (std::size_t i = 0; i < N; ++i)
      if (system.active[i])
        result.p[i] += states[i].pressure;
    result.iterations = solved.iterations;
    result.residual = solved.residual;
    result.converged = solved.converged;
    result.jx.assign(g.NX, 0);
    result.jy.assign(g.NY, 0);
    result.qx.assign(g.NX, 0);
    result.qy.assign(g.NY, 0);
    result.damping_x.assign(g.NX, 0);
    result.damping_y.assign(g.NY, 0);
    DoubleDouble wall_x, wall_y, body_y, body_work, wall_scale_x, wall_scale_y;
    const auto publish = [&](bool x, const auto &left, const auto &right,
                             const auto &old, const auto &inertia, auto &next,
                             auto &swept) {
      for (std::size_t f = 0; f < old.size(); ++f) {
        const auto l = left[f], r = right[f];
        if (l == kernel::none || r == kernel::none || g.closed(!x, f) ||
            inertia[f] == 0) {
          if (old[f] != 0)
            throw std::runtime_error(
                "Prototype fixed/empty face has incoming momentum");
          const double pl = l == kernel::none ? 0 : result.p[l],
                       pr = r == kernel::none ? 0 : result.p[r];
          (x ? wall_x : wall_y).add(dt * g.A * (pr - pl));
          (x ? wall_scale_x : wall_scale_y)
              .add(dt * g.A * (std::abs(pr) + std::abs(pl)));
          continue;
        }
        const double body = x ? 0 : body_y_face[f];
        body_y.add(body);
        next[f] = old[f] + dt * g.A * (result.p[l] - result.p[r]) + body;
        swept[f] = dt * g.A * next[f] / inertia[f];
        body_work.add(next[f] / inertia[f] * body);
        const double impulse = next[f] - old[f];
        (x ? result.damping_x : result.damping_y)[f] =
            .5 * impulse * (impulse / inertia[f]);
      }
    };
    publish(true, g.xl, g.xr, old_x, ix, result.jx, result.qx);
    publish(false, g.yl, g.yr, old_y, iy, result.jy, result.qy);
    result.wall_impulse_x = wall_x.value();
    result.wall_impulse_y = wall_y.value();
    result.body_impulses_y = std::move(body_y_face);
    result.body_impulse_y = body_y.value();
    result.body_work = body_work.value();
    result.wall_operands_x = wall_scale_x.value();
    result.wall_operands_y = wall_scale_y.value();
    if (positive_correction) {
      auto constrained = states;
      bool changed = false;
      for (std::size_t i = 0; i < N; ++i)
        if (result.p[i] < 0) {
          constrained[i].fixed_pressure = true;
          changed = true;
        }
      if (changed) {
        auto corrected = advance(constrained, old_x, old_y, dt, gravity, false);
        corrected.iterations += result.iterations;
        corrected.corrections = 1;
        corrected.converged = corrected.converged && result.converged;
        return corrected;
      }
    }
    return result;
  }
};
} // namespace ma::experimental::game_kernel
