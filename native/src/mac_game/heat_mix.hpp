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
  HeatResult advance(const State &state, const std::vector<Eos> &eos,
                     double dt) {
    namespace k = ma::experimental::mac_kernel;
    k::LinearSystem system;
    system.g = &g;
    system.active.assign(g.N, 0);
    system.diag.assign(g.N, 0);
    system.wx.assign(g.NX, 0);
    system.wy.assign(g.NY, 0);
    std::vector<double> conductivity(g.N), rhs(g.N), increment;
    for (std::size_t i = 0; i < g.N; ++i) {
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
            if (inertia > 0)
              eddy = mixing * .5 * (eos[l].capacity + eos[r].capacity) / g.V *
                     std::abs(j / inertia) * source.spacing_m;
          }
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
    const auto solved = component_cache.solve(context, system, rhs, increment,
                                              1e-9, 1e-12, maximum_iterations);
    if (!solved.converged)
      throw std::runtime_error("Private heat linear solve failed");
    HeatResult result;
    result.iterations = solved.iterations;
    result.energy = state.energy;
    std::vector<double> flux_x(g.NX), flux_y(g.NY);
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
    for (std::size_t i = 0; i < g.N; ++i)
      if (!g.wall[i]) {
        result.energy[i] += flux_x[g.left_face(i)] - flux_x[g.right_face(i)] +
                            flux_y[g.bottom_face(i)] - flux_y[g.top_face(i)];
        audit.add(state.energy[i]);
        audit.add(-result.energy[i]);
      }
    if (!audit.closed())
      throw std::runtime_error("Private shared heat flux ledger failed");
    result.error = audit.operands.value() ? std::abs(audit.residual.value()) /
                                                audit.operands.value()
                                          : 0;
    return result;
  }
};
} // namespace ma::experimental::game_kernel
