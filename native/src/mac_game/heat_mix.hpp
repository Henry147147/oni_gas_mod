#pragma once
#include "cached_ic.hpp"
#include "flow_local.hpp"
namespace ma::experimental::game_kernel {
struct HeatResult {
  std::vector<double> energy;
  std::size_t iterations = 0;
  double error = 0;
};
// Fluid-only component. Registered solid faces are still missing.
class HeatSolver {
  ma::experimental::MacGrid source;
  ma::experimental::mac_kernel::Grid g;
  ma::experimental::mac_kernel::Workspace workspace;
  ma::experimental::mac_kernel::Model model;
  ma::experimental::MacClosedSettings settings;
  ma::experimental::MacKernelOptions options;
  std::unique_ptr<ma::experimental::mac_kernel::Pool> owned_pool;
  ma::experimental::mac_kernel::Pool &pool;
  ma::experimental::mac_kernel::Context context;
  CachedComponents component_cache;
  std::vector<Species> species;
  double mixing = 0;
  unsigned maximum_iterations = 200;
  // Scratch kept for its capacity: every entry is written before it is read.
  ma::experimental::mac_kernel::LinearSystem system;
  std::vector<double> conductivity, rhs, increment, flux_x, flux_y,
      transfer_x, transfer_y;
  std::vector<std::uint8_t> coupled_x, coupled_y, nonpositive;
  // Runs job(block) over the grid's fixed row blocks on the pool.
  void blocks(const std::function<void(std::size_t)> &job) {
    pool.run(g.blocks, job);
  }

public:
  HeatSolver(const ma::experimental::MacGrid &grid, std::vector<Species> table,
             unsigned threads, double coefficient = 0,
             ma::experimental::mac_kernel::Pool *executor = nullptr,
             unsigned maximum = 200)
      : source(grid), g(source),
        owned_pool(executor
                       ? nullptr
                       : std::make_unique<ma::experimental::mac_kernel::Pool>(
                             static_cast<unsigned>(
                                 std::min<std::size_t>(threads, g.blocks)))),
        pool(executor ? *executor : *owned_pool),
        context{workspace,
                source,
                model,
                g,
                settings,
                options,
                ma::experimental::mac_kernel::Exec{g, pool},
                {},
                {}},
        species(std::move(table)), mixing(coefficient), maximum_iterations(maximum) {
    if (maximum == 0 || maximum > 65536)
      throw std::runtime_error("Invalid saved heat iteration cap");
    settings.preconditioner = ma::PressurePreconditioner::incomplete_cholesky;
    options.threads = threads;
  }
  // The eddy term's share that survives a stable stratification (profile
  // 12, `stratified` = gravity; MAC_GAME_KERNEL.md, "Gas heat mixing across
  // a stable layer"): at this bulk Richardson number half of it is left.
  static constexpr double critical_richardson = .25;
  // Profile 12 liquid convection (MAC_GAME_KERNEL.md, "Liquid convection"):
  // the liquids' density does not depend on temperature here, so the flow
  // never carries heat up from a heated floor or sideways from a warm
  // column. Between two cells holding liquid the heat solve therefore adds
  // the exchange such convection would make, with a nominal thermal
  // expansion: upwards where the lower cell is the warmer, at the
  // free-convection speed sqrt(g beta dT h), and sideways at the
  // lock-exchange speed, half of that. Warm liquid over cold stays.
  static constexpr double liquid_expansion_per_k = 5e-4, liquid_share = .05;
  HeatResult advance(const State &state, const std::vector<Eos> &eos,
                     double dt, double stratified = 0) {
    namespace k = ma::experimental::mac_kernel;
    // Cells, then faces, then each cell's right-hand side, in the grid's
    // fixed row blocks. A cell adds its faces' transfers in the order the
    // serial face loops did (x faces, then y faces, each in face order: its
    // left, right, bottom and top faces).
    system.g = &g;
    system.active.resize(g.N);
    system.diag.resize(g.N);
    system.wx.resize(g.NX);
    system.wy.resize(g.NY);
    conductivity.resize(g.N);
    rhs.resize(g.N);
    transfer_x.resize(g.NX);
    transfer_y.resize(g.NY);
    coupled_x.resize(g.NX);
    coupled_y.resize(g.NY);
    blocks([&](std::size_t block) {
      for (std::size_t i = g.block_row_begin(block) * g.W; i < g.block_row_end(block) * g.W; ++i) {
        system.active[i] = 0;
        system.diag[i] = 0;
        conductivity[i] = 0;
        if (g.wall[i] || eos[i].mass == 0)
          continue;
        system.active[i] = 1;
        system.diag[i] = eos[i].capacity;
        double occupied = 0;
        for (std::size_t a = state.masses.offsets[i];
             a < state.masses.offsets[i + 1]; ++a) {
          const auto &s = species[state.masses.species[a]];
          const double m = state.masses.mass[a];
          const double volume =
              s.gas ? m * R / s.molar_mass * eos[i].temperature / eos[i].pressure
                    : m / s.rho0 * liquid_volume_fraction(s,eos[i].pressure);
          conductivity[i] += s.conductivity * volume;
          occupied += volume;
        }
        if (occupied > 0)
          conductivity[i] /= occupied;
      }
    });
    const auto faces = [&](bool x, const auto &left, const auto &right,
                           auto &weights, auto &transfers, auto &coupled,
                           std::size_t first, std::size_t end) {
      for (std::size_t f = first; f < end; ++f) {
        weights[f] = 0;
        coupled[f] = 0;
        const auto l = left[f], r = right[f];
        if (l == k::none || r == k::none || g.closed(!x, f) ||
            !system.active[l] || !system.active[r])
          continue;
        const double kl = conductivity[l], kr = conductivity[r];
        if (kl == 0 || kr == 0)
          continue;
        double eddy = 0;
        if (mixing > 0) {
          if (eos[l].liquid_nominal_volume == 0 &&
              eos[r].liquid_nominal_volume == 0) {
            const double inertia = .5 * (eos[l].mass + eos[r].mass),
                         j = x ? state.jx[f] : state.jy[f];
            if (inertia > 0) {
              const double speed = std::abs(j / inertia);
              eddy = mixing * .5 * (eos[l].capacity + eos[r].capacity) / g.V *
                     speed * source.spacing_m;
              // Across a vertical face whose lower gas is the denser, mixing
              // lifts mass and real turbulence pays for that from its
              // motion: it is suppressed by the bulk Richardson number
              // Ri = g h (rho_low - rho_up) / (rho_mean u^2), as
              // 1 / (1 + Ri / Ri_c). Without this the term mixed a resting
              // hot layer into the cold one below it for nothing, the
              // uneven mixing drove flow, and the flow more mixing: a
              // column at rest, 350 K over 290 K, moved at 4 cm/s after two
              // minutes and was half mixed after ten.
              if (!x && stratified != 0 && speed > 0) {
                const double low = eos[stratified > 0 ? l : r].mass,
                             high = eos[stratified > 0 ? r : l].mass;
                if (low > high)
                  eddy *= speed * speed /
                          (speed * speed +
                           std::abs(stratified) * source.spacing_m * (low - high) /
                               (.5 * (low + high)) / critical_richardson);
              }
            }
          }
        }
        if (stratified != 0 && eos[l].liquid_nominal_volume > liquid_share * g.V &&
            eos[r].liquid_nominal_volume > liquid_share * g.V) {
          const double warmer_left = eos[l].temperature - eos[r].temperature;
          const double difference =
              x ? std::abs(warmer_left)
                : (stratified > 0 ? warmer_left : -warmer_left);
          if (difference > 0)
            eddy = std::min(eos[l].capacity, eos[r].capacity) / g.V * (x ? .5 : 1.) *
                   std::sqrt(std::abs(stratified) * liquid_expansion_per_k *
                             difference * source.spacing_m) *
                   source.spacing_m;
        }
        weights[f] =
            dt * g.A / source.spacing_m * (2 * kl * kr / (kl + kr) + eddy);
        transfers[f] = weights[f] * (eos[r].temperature - eos[l].temperature);
        coupled[f] = 1;
      }
    };
    blocks([&](std::size_t block) {
      const auto y0 = g.block_row_begin(block);
      faces(true, g.xl, g.xr, system.wx, transfer_x, coupled_x, y0 * g.XW,
            g.block_row_end(block) * g.XW);
      faces(false, g.yl, g.yr, system.wy, transfer_y, coupled_y, y0 * g.W,
            g.block_yrow_end(block) * g.W);
    });
    blocks([&](std::size_t block) {
      for (std::size_t i = g.block_row_begin(block) * g.W; i < g.block_row_end(block) * g.W; ++i) {
        const auto l = g.left_face(i), r = g.right_face(i), b = g.bottom_face(i),
                   t = g.top_face(i);
        double value = 0;
        if (coupled_x[l])
          value -= transfer_x[l];
        if (coupled_x[r])
          value += transfer_x[r];
        if (coupled_y[b])
          value -= transfer_y[b];
        if (coupled_y[t])
          value += transfer_y[t];
        rhs[i] = value;
      }
    });
    std::vector<std::size_t> iterations_used;
    const auto solved = component_cache.solve(context, system, rhs, increment,
                                              1e-9, 1e-12, maximum_iterations,
                                              &iterations_used);
    if (!solved.converged)
      throw std::runtime_error("Private heat linear solve failed");
    HeatResult result;
    result.iterations = solved.iterations;
    result.energy.resize(g.N);
    flux_x.resize(g.NX);
    flux_y.resize(g.NY);
    nonpositive.resize(g.blocks);
    for (;;) {
      // Face fluxes, then each cell's new energy, in row blocks; the audit
      // sums the cells in cell order.
      blocks([&](std::size_t block) {
        const auto fluxes = [&](const auto &l, const auto &r, const auto &weights,
                                auto &flux, std::size_t first, std::size_t end) {
          for (std::size_t f = first; f < end; ++f)
            flux[f] = weights[f] != 0
                          ? weights[f] * ((eos[l[f]].temperature + increment[l[f]]) -
                                          (eos[r[f]].temperature + increment[r[f]]))
                          : 0;
        };
        const auto y0 = g.block_row_begin(block);
        fluxes(g.xl, g.xr, system.wx, flux_x, y0 * g.XW, g.block_row_end(block) * g.XW);
        fluxes(g.yl, g.yr, system.wy, flux_y, y0 * g.W, g.block_yrow_end(block) * g.W);
      });
      blocks([&](std::size_t block) {
        bool any = false;
        for (std::size_t i = g.block_row_begin(block) * g.W; i < g.block_row_end(block) * g.W; ++i) {
          result.energy[i] = state.energy[i];
          if (g.wall[i])
            continue;
          result.energy[i] += flux_x[g.left_face(i)] - flux_x[g.right_face(i)] +
                              flux_y[g.bottom_face(i)] - flux_y[g.top_face(i)];
          if (system.active[i] && !(result.energy[i] - eos[i].offset > 0))
            any = true;
        }
        nonpositive[block] = any;
      });
      // The ledger's residual and operand sums are separate chains, each
      // over the cells in cell order; they run side by side.
      Balance audit;
      pool.run(2, [&](std::size_t chain) {
        auto &sum = chain == 0 ? audit.residual : audit.operands;
        for (std::size_t i = 0; i < g.N; ++i)
          if (!g.wall[i]) {
            sum.add(chain == 0 ? state.energy[i] : std::abs(state.energy[i]));
            sum.add(chain == 0 ? -result.energy[i] : std::abs(-result.energy[i]));
          }
      });
      if (!audit.closed())
        throw std::runtime_error("Private shared heat flux ledger failed");
      result.error = audit.operands.value() ? std::abs(audit.residual.value()) /
                                                  audit.operands.value()
                                            : 0;
      if (std::none_of(nonpositive.begin(), nonpositive.end(), [](std::uint8_t v) { return v != 0; }))
        return result;

      // A global relative residual can exceed a near-vacuum cell's entire
      // heat budget. Refine the same implicit equation with its local heat
      // residual, retaining one shared conservative flux on every face.
      // The original solve and all refinements share each component's saved
      // iteration cap. No energy floor, flux limiter or relaxed audit is used.
      std::vector<double> residual(g.N), correction;
      for (std::size_t i = 0; i < g.N; ++i)
        if (system.active[i])
          residual[i] = flux_x[g.left_face(i)] - flux_x[g.right_face(i)] +
                        flux_y[g.bottom_face(i)] - flux_y[g.top_face(i)] -
                        eos[i].capacity * increment[i];
      const auto refined = component_cache.solve(context, system, residual,
                                                  correction, 1e-9, 1e-12,
                                                  maximum_iterations,
                                                  &iterations_used);
      result.iterations += refined.iterations;
      bool changed = false;
      for (std::size_t i = 0; i < g.N; ++i) {
        const double next = increment[i] + correction[i];
        changed = changed || next != increment[i];
        increment[i] = next;
      }
      if (!changed)
        throw std::runtime_error("Private heat refinement made no progress");
    }
  }
};
} // namespace ma::experimental::game_kernel
