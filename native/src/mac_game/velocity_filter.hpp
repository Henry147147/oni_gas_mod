#pragma once
#include "eos.hpp"
#include "region.hpp"
namespace ma::experimental::game_kernel {
struct VelocityFilterReport {
  double wall_x = 0, wall_y = 0, wall_ops_x = 0, wall_ops_y = 0, heat = 0,
         max_speed = 0;
  std::size_t exchanges = 0, negative_heat_faces = 0;
};
// Private numerical-viscosity candidate. Jacobi face exchanges preserve J;
// actual kinetic work becomes local U. Fixed neighbours absorb a recorded
// wall impulse, with their impact heat retained in the fluid. Never clips J.
inline VelocityFilterReport filter_velocity(
    const ma::experimental::mac_kernel::Grid &g, const Topology &topology,
    State &state, const std::vector<Eos> &eos, double speed, unsigned sweeps,
    ma::experimental::mac_kernel::Pool &pool, RegionAudit *regions = nullptr) {
  namespace k = ma::experimental::mac_kernel;
  if (!(speed > 0) || !std::isfinite(speed) || sweeps == 0)
    throw std::runtime_error("Invalid private velocity filter");
  VelocityFilterReport report;
  const auto cm = [&](k::Index i) { return i == k::none ? 0 : eos[i].mass; };
  for (unsigned axis = 0; axis < 2; ++axis) {
    const bool x = axis == 0;
    const auto &left = x ? g.xl : g.yl, &right = x ? g.xr : g.yr;
    auto &j = x ? state.jx : state.jy;
    const auto NF = j.size(), width = x ? g.XW : g.W, height = x ? g.H : g.YH,
               blocks = (NF + 127) / 128;
    std::vector<double> inertia(NF), velocity(NF), next(NF), heat(NF), wall(NF),
        wall_ops(NF), flux_x(NF), flux_y(NF), work_x(NF), work_y(NF);
    std::vector<std::uint8_t> free(NF);
    std::vector<std::size_t> owners(NF, g.N), counts(blocks);
    for (std::size_t f = 0; f < NF; ++f) {
      inertia[f] = .5 * (cm(left[f]) + cm(right[f]));
      free[f] = left[f] != k::none && right[f] != k::none && !g.closed(!x, f) &&
                inertia[f] > 0;
      if (free[f])
        owners[f] = topology.labels[left[f]];
      else if (j[f] != 0)
        throw std::runtime_error("Private filter fixed face has J");
    }
    for (unsigned sweep = 0; sweep < sweeps; ++sweep) {
      for (std::size_t f = 0; f < NF; ++f)
        velocity[f] = free[f] ? j[f] / inertia[f] : 0;
      std::fill(counts.begin(), counts.end(), 0);
      pool.run(blocks, [&](std::size_t block) {
        for (std::size_t f = block * 128; f < std::min(NF, (block + 1) * 128);
             ++f) {
          const auto edge = [&](std::size_t h, double &flux, double &work) {
            flux = work = 0;
            if (!free[f] || h == NF || !free[h] || owners[f] != owners[h] ||
                std::max(std::abs(velocity[f]), std::abs(velocity[h])) <= speed)
              return;
            const double weight =
                .25 * inertia[f] * inertia[h] / (inertia[f] + inertia[h]);
            flux = weight * (velocity[f] - velocity[h]);
            work = .5 * weight *
                   (velocity[f] * velocity[f] - velocity[h] * velocity[h]);
            if (flux != 0)
              ++counts[block];
          };
          edge(f % width + 1 < width ? f + 1 : NF, flux_x[f], work_x[f]);
          edge(f / width + 1 < height ? f + width : NF, flux_y[f], work_y[f]);
        }
      });
      pool.run(blocks, [&](std::size_t block) {
        for (std::size_t f = block * 128; f < std::min(NF, (block + 1) * 128);
             ++f) {
          next[f] = j[f];
          heat[f] = wall[f] = wall_ops[f] = 0;
          if (!free[f])
            continue;
          DoubleDouble change, transport, wall_sum, wall_scale;
          if (f % width) {
            change.add(flux_x[f - 1]);
            transport.add(work_x[f - 1]);
          }
          change.add(-flux_x[f]);
          transport.add(-work_x[f]);
          if (f / width) {
            change.add(flux_y[f - width]);
            transport.add(work_y[f - width]);
          }
          change.add(-flux_y[f]);
          transport.add(-work_y[f]);
          if (std::abs(velocity[f]) > speed) {
            const std::array<std::size_t, 4> near{
                f % width ? f - 1 : NF, f % width + 1 < width ? f + 1 : NF,
                f / width ? f - width : NF,
                f / width + 1 < height ? f + width : NF};
            for (const auto h : near)
              if (h != NF && !free[h]) {
                const double impulse = -.25 * j[f];
                change.add(impulse);
                wall_sum.add(impulse);
                wall_scale.add(std::abs(impulse));
                if (impulse != 0)
                  ++counts[block];
              }
          }
          next[f] = j[f] + change.value();
          wall[f] = wall_sum.value();
          wall_ops[f] = wall_scale.value();
          DoubleDouble energy;
          energy.add(.5 * j[f] * velocity[f]);
          energy.add(transport.hi);
          energy.add(transport.lo);
          energy.add(-.5 * next[f] * (next[f] / inertia[f]));
          heat[f] = energy.value();
        }
      });
      for (std::size_t block = 0; block < blocks; ++block)
        report.exchanges += counts[block];
      DoubleDouble wall_sum, wall_scale, heat_sum;
      for (std::size_t f = 0; f < NF; ++f) {
        wall_sum.add(wall[f]);
        wall_scale.add(wall_ops[f]);
        heat_sum.add(heat[f]);
        if (heat[f] < 0)
          ++report.negative_heat_faces;
        if (regions && wall_ops[f] != 0)
          regions->momentum_receipt(left[f], x, wall[f], wall_ops[f]);
      }
      (x ? report.wall_x : report.wall_y) += wall_sum.value();
      (x ? report.wall_ops_x : report.wall_ops_y) += wall_scale.value();
      report.heat += heat_sum.value();
      pool.run(g.blocks, [&](std::size_t block) {
        for (std::size_t i = g.block_row_begin(block) * g.W;
             i < g.block_row_end(block) * g.W; ++i)
          if (!g.wall[i]) {
            const auto share = [&](std::size_t f) {
              if (heat[f] == 0)
                return 0.;
              const double mass = cm(left[f]) + cm(right[f]);
              return mass > 0 ? heat[f] * (eos[i].mass / mass) : 0.;
            };
            state.energy[i] +=
                x ? share(g.left_face(i)) + share(g.right_face(i))
                  : share(g.bottom_face(i)) + share(g.top_face(i));
          }
      });
      j.swap(next);
    }
    for (std::size_t f = 0; f < NF; ++f)
      if (free[f])
        report.max_speed =
            std::max(report.max_speed, std::abs(j[f] / inertia[f]));
  }
  return report;
}
} // namespace ma::experimental::game_kernel
