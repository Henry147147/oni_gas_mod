#pragma once
#include "cached_ic.hpp"
#include <limits>
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
  // lock-exchange speed, half of that. Warm liquid over cold stays, and
  // two different liquids (oil on water, water on magma) exchange nothing
  // this way: their interface is not a convecting body.
  static constexpr double liquid_expansion_per_k = 5e-4, liquid_share = .05;
  HeatResult advance(const State &state, const std::vector<Eos> &eos,
                     double dt, double stratified = 0) {
    namespace k = ma::experimental::mac_kernel;
    k::LinearSystem system;
    system.g = &g;
    system.active.assign(g.N, 0);
    system.diag.assign(g.N, 0);
    system.wx.assign(g.NX, 0);
    system.wy.assign(g.NY, 0);
    std::vector<double> conductivity(g.N), rhs(g.N), increment;
    // Each cell's main liquid (the species holding most of its liquid mass),
    // for the liquid convection: only cells of one liquid exchange that way.
    constexpr auto no_liquid = std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> main_liquid;
    if (stratified != 0)
      main_liquid.assign(g.N, no_liquid);
    for (std::size_t i = 0; i < g.N; ++i) {
      if (g.wall[i] || eos[i].mass == 0)
        continue;
      system.active[i] = 1;
      system.diag[i] = eos[i].capacity;
      double occupied = 0, most = 0;
      for (std::size_t a = state.masses.offsets[i];
           a < state.masses.offsets[i + 1]; ++a) {
        const auto &s = species[state.masses.species[a]];
        const double m = state.masses.mass[a];
        const double volume =
            s.gas ? m * R / s.molar_mass * eos[i].temperature / eos[i].pressure
                  : m / s.rho0 * liquid_volume_fraction(s,eos[i].pressure);
        conductivity[i] += s.conductivity * volume;
        occupied += volume;
        if (stratified != 0 && !s.gas && m > most) {
          most = m;
          main_liquid[i] = static_cast<std::uint32_t>(state.masses.species[a]);
        }
      }
      if (occupied > 0)
        conductivity[i] /= occupied;
    }
    const auto faces = [&](bool x, const auto &left, const auto &right,
                           auto &weights) {
      for (std::size_t f = 0; f < weights.size(); ++f) {
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
        if (stratified != 0 && main_liquid[l] != no_liquid && main_liquid[l] == main_liquid[r] &&
            eos[l].liquid_nominal_volume > liquid_share * g.V &&
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
        const double transfer =
            weights[f] * (eos[r].temperature - eos[l].temperature);
        rhs[l] += transfer;
        rhs[r] -= transfer;
      }
    };
    faces(true, g.xl, g.xr, system.wx);
    faces(false, g.yl, g.yr, system.wy);
    DoubleDouble norm;
    for (double value : rhs)
      norm.add(value * value);
    std::vector<std::size_t> iterations_used;
    const auto solved = component_cache.solve(context, system, rhs, increment,
                                              1e-9, 1e-12, maximum_iterations,
                                              &iterations_used);
    if (!solved.converged)
      throw std::runtime_error("Private heat linear solve failed");
    HeatResult result;
    result.iterations = solved.iterations;
    result.energy = state.energy;
    std::vector<double> flux_x(g.NX), flux_y(g.NY);
    for (;;) {
      const auto fluxes = [&](const auto &l, const auto &r, const auto &weights,
                              auto &flux) {
        for (std::size_t f = 0; f < weights.size(); ++f)
          if (weights[f] != 0)
            flux[f] = weights[f] * ((eos[l[f]].temperature + increment[l[f]]) -
                                    (eos[r[f]].temperature + increment[r[f]]));
      };
      fluxes(g.xl, g.xr, system.wx, flux_x);
      fluxes(g.yl, g.yr, system.wy, flux_y);
      Balance audit;
      bool nonpositive = false;
      for (std::size_t i = 0; i < g.N; ++i)
        if (!g.wall[i]) {
          result.energy[i] += flux_x[g.left_face(i)] - flux_x[g.right_face(i)] +
                              flux_y[g.bottom_face(i)] - flux_y[g.top_face(i)];
          audit.add(state.energy[i]);
          audit.add(-result.energy[i]);
          if (system.active[i] && !(result.energy[i] - eos[i].offset > 0))
            nonpositive = true;
        }
      if (!audit.closed())
        throw std::runtime_error("Private shared heat flux ledger failed");
      result.error = audit.operands.value() ? std::abs(audit.residual.value()) /
                                                  audit.operands.value()
                                            : 0;
      if (!nonpositive)
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
      result.energy = state.energy;
    }
  }
};
} // namespace ma::experimental::game_kernel
