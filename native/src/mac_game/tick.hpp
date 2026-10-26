#pragma once
#include "heat_mix.hpp"
#include "layering.hpp"
#include "phase.hpp"
#include "surface.hpp"
#include "totals.hpp"
#include "trace.hpp"
#include "velocity_filter.hpp"
namespace ma::experimental::game_kernel {
struct TickResult {
  State state;
  std::vector<Eos> eos;
  // The accounts of `state`, as the closing audit summed them.
  Totals totals;
  double passive_error = 0, mass_error = 0, energy_error = 0,
         momentum_error = 0, sink_kg = 0, sink_j = 0, flow_ms = 0, heat_ms = 0,
         phase_ms = 0, trace_audit_ms = 0, interface_ms = 0, layering_ms = 0,
         pressure_ms = 0, transport_ms = 0, eos_ms = 0;
  std::size_t regions = 0, trace_constituents = 0, trace_cells = 0;
  std::vector<double> sink_species, conversion_species;
  std::vector<std::pair<std::uint64_t, double>> sink_passive_quantities;
  double sink_internal = 0, sink_kinetic = 0, sink_potential = 0, sink_px = 0,
         sink_py = 0, wall_px = 0, wall_py = 0, body_py = 0, evaporated = 0,
         condensed = 0;
  DoubleDouble energy_residual, energy_operands;
  double region_error = 0, entropy_change = 0, trace_entropy = 0,
         phase_entropy_change = 0, entropy_ms = 0, flow_entropy = 0,
         layer_entropy = 0, surface_entropy = 0, heat_entropy = 0,
         filter_ms = 0, filter_entropy = 0, filter_heat = 0,
         filter_max_speed = 0;
  std::size_t filter_exchanges = 0, filter_negative_heat = 0;
  std::size_t microevents = 0, interface_faces = 0, limited_faces = 0,
              interface_clusters = 0, largest_interface_cluster = 0;
};
// Private coupled prototype: region audits and optional liquid correction.
// Still missing sleep/front cap/checkpoint/owner integration. Heat and phase
// run once per tick; two fixed flow half-steps follow the F3 convergence
// control.
class Tick {
  ma::experimental::MacGrid grid;
  std::vector<Species> species;
  Flow flow;
  HeatSolver heat;
  PhaseSolver phase;
  unsigned parts;
  bool detailed_entropy = false;
  bool interface_enabled = false;
  double interface_detail = 0, filter_speed = 0;
  unsigned filter_sweeps = 2;
  double phase_time = 1;
  bool heat_enabled = true;
  // Profile 5: sink sub-threshold traces before conduction. A trace-only
  // cell has a negligible heat capacity but a full conduction weight, so the
  // heat solve's tolerance could otherwise drive its U below its offset.
  bool prune_before_heat = false;
  SurfaceScratch surface_scratch;
  static double ms(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - start)
        .count();
  }

public:
  Tick(const ma::experimental::MacGrid &source, std::vector<Species> table,
       std::vector<PhasePair> pairs, unsigned threads, unsigned flow_parts = 2,
       double heat_mixing = 0, bool interfaces = false, double detail = 0,
       double pressure_tolerance = 1e-8, unsigned gravity_source = 0,
       bool entropy_stages = false, double speed_limit = 0,
       unsigned viscosity_sweeps = 2, double phase_relaxation_time = 1,
       bool enable_heat = true, unsigned pressure_iterations = 200,
       unsigned heat_iterations = 200, bool surface_closure = false,
       bool trace_before_heat = false)
      : grid(source), species(table),
        flow(source, table, threads, false, pressure_tolerance, gravity_source, pressure_iterations,
             surface_closure),
        heat(source, table, threads, heat_mixing, &flow.worker_pool(), heat_iterations),
        phase(source, std::move(table), std::move(pairs), threads,
              &flow.worker_pool()),
        parts(flow_parts), detailed_entropy(entropy_stages),
        interface_enabled(interfaces), interface_detail(detail),
        filter_speed(speed_limit), filter_sweeps(viscosity_sweeps),
        phase_time(phase_relaxation_time), heat_enabled(enable_heat),
        prune_before_heat(trace_before_heat) {}
  std::vector<Eos> evaluate_state(const State &state) const {
    return flow.evaluate_state(state);
  }
  TickResult advance(const State &initial, double dt, double gravity,
                     double threshold,
                     std::span<const std::uint8_t> liquid_blocked = {}) {
    const LiquidBoundary boundary{liquid_blocked, species};
    // A preceding tick may have condensed liquid inside a marked cell. Keep
    // such a cell isolated before pressure AND the later layer/sharpen stages;
    // a transport-only guard cannot see a zero-sweep exchange face.
    if (!liquid_blocked.empty()) {
      const auto &g = flow.kernel_grid();
      std::vector<LiquidBoundaryFace> conflicts;
      for (std::size_t i = 0; i < liquid_blocked.size(); ++i) {
        if (!liquid_blocked[i])
          continue;
        bool wet = false;
        for (std::size_t a = initial.masses.offsets[i];
             a < initial.masses.offsets[i + 1]; ++a)
          wet = wet || (initial.masses.mass[a] > 0 &&
                        !species[initial.masses.species[a]].gas);
        if (!wet)
          continue;
        for (const auto &[x, face] :
             {std::pair{true, g.left_face(i)},
              std::pair{true, g.right_face(i)},
              std::pair{false, g.bottom_face(i)},
              std::pair{false, g.top_face(i)}}) {
          const auto l = (x ? g.xl : g.yl)[face];
          const auto r = (x ? g.xr : g.yr)[face];
          if (l == ma::experimental::mac_kernel::none ||
              r == ma::experimental::mac_kernel::none || g.closed(!x, face))
            continue;
          conflicts.push_back({x, face, i, l == i ? r : l});
        }
      }
      if (!conflicts.empty()) {
        std::sort(conflicts.begin(), conflicts.end(), [](const auto &a, const auto &b) {
          return a.x != b.x ? a.x < b.x : a.face < b.face;
        });
        conflicts.erase(std::unique(conflicts.begin(), conflicts.end(), [](const auto &a, const auto &b) {
          return a.x == b.x && a.face == b.face;
        }), conflicts.end());
        throw LiquidBoundaryConflict(std::move(conflicts));
      }
    }
    const auto start = std::chrono::steady_clock::now();
    const auto before =
        totals(grid, initial, species.size(), gravity, &flow.kernel_grid());
    RegionAudit regions(flow.kernel_grid(), flow.region_topology(),
                        species.size());
    regions.capture(initial, gravity, 1);
    auto entropy_start = std::chrono::steady_clock::now();
    auto readings = flow.evaluate_state(initial);
    const auto initial_entropy = total_entropy(
        flow.kernel_grid(), species, initial, readings, flow.worker_pool());
    TickResult out;
    out.entropy_ms = ms(entropy_start);
    out.regions = regions.count();
    out.state = initial;
    // Until the final reading, out.eos holds the readings of out.state.
    out.eos = std::move(readings);
    double wall_x = 0, wall_y = 0, body_y = 0, wall_ops_x = 0, wall_ops_y = 0,
           body_ops_y = 0;
    std::vector<DoubleDouble> sink_mass(species.size()),
        conversion(species.size());
    DoubleDouble sink_energy, sink_internal, sink_kinetic, sink_potential,
        sink_x, sink_y, sink_entropy;
    std::map<std::uint64_t, DoubleDouble> sink_passives;
    auto current_entropy = initial_entropy;
    const auto observe_entropy = [&](double &stage) {
      if (!detailed_entropy)
        return;
      const auto stamp = std::chrono::steady_clock::now();
      const auto next = total_entropy(flow.kernel_grid(), species, out.state,
                                      out.eos, flow.worker_pool());
      DoubleDouble delta;
      delta.add(next.hi);
      delta.add(next.lo);
      delta.add(-current_entropy.hi);
      delta.add(-current_entropy.lo);
      stage += delta.value();
      current_entropy = next;
      out.entropy_ms += ms(stamp);
    };
    const auto prune = [&](bool tracking = true) {
      // On an already valid state with no sub-threshold constituents,
      // the sink is exactly the identity, including passives and J.
      if (std::none_of(out.state.masses.mass.begin(),
                       out.state.masses.mass.end(),
                       [&](double mass) { return mass < threshold; }))
        return false;
      auto sunk = trace_sink(grid, species, out.state, threshold, gravity,
                             &regions, &flow.kernel_grid(), &flow.worker_pool());
      out.trace_constituents += sunk.constituents;
      out.trace_cells += sunk.cells;
      for (std::size_t key = 0; key < species.size(); ++key)
        sink_mass[key].add(sunk.mass[key]);
      for (const auto &[identity, value] : sunk.passives)
        sink_passives[identity].add(value);
      if (tracking)
        current_entropy.add(-sunk.entropy);
      sink_entropy.add(sunk.entropy);
      sink_internal.add(sunk.internal);
      sink_kinetic.add(sunk.kinetic);
      sink_potential.add(sunk.potential);
      sink_energy.add(sunk.internal);
      sink_energy.add(sunk.kinetic);
      sink_energy.add(sunk.potential);
      sink_x.add(sunk.jx);
      sink_y.add(sunk.jy);
      out.state = std::move(sunk.state);
      return true;
    };
    for (unsigned part = 0; part < parts; ++part) {
      if (prune())
        out.eos = flow.evaluate_state(out.state, std::move(out.eos));
      auto stage_start = std::chrono::steady_clock::now();
      auto advanced = flow.advance(out.state, dt / parts, gravity, &regions,
                                   liquid_blocked.empty() ? nullptr : &boundary,
                                   std::move(out.eos));
      out.flow_ms += ms(stage_start);
      out.pressure_ms += advanced.pressure_ms;
      out.transport_ms += advanced.transport_ms;
      out.eos_ms += advanced.eos_ms;
      out.microevents += advanced.subcycles;
      wall_x += advanced.wall_x;
      wall_y += advanced.wall_y;
      body_y += advanced.body_y;
      wall_ops_x += advanced.wall_ops_x;
      wall_ops_y += advanced.wall_ops_y;
      body_ops_y += std::abs(advanced.body_y);
      out.state = std::move(advanced.state);
      out.eos = std::move(advanced.eos);
      observe_entropy(out.flow_entropy);
      if (filter_speed > 0) {
        const auto filter_start = std::chrono::steady_clock::now();
        const auto filtered = filter_velocity(
            flow.kernel_grid(), flow.region_topology(), out.state, out.eos,
            filter_speed, filter_sweeps, flow.worker_pool(), &regions);
        wall_x += filtered.wall_x;
        wall_y += filtered.wall_y;
        wall_ops_x += filtered.wall_ops_x;
        wall_ops_y += filtered.wall_ops_y;
        out.filter_heat += filtered.heat;
        out.filter_max_speed =
            std::max(out.filter_max_speed, filtered.max_speed);
        out.filter_exchanges += filtered.exchanges;
        out.filter_negative_heat += filtered.negative_heat_faces;
        if (filtered.exchanges)
          out.eos = flow.evaluate_state(out.state, std::move(out.eos));
        out.filter_ms += ms(filter_start);
        observe_entropy(out.filter_entropy);
      }
    }
    auto stage_start = std::chrono::steady_clock::now();
    auto layered = layer(grid, species, out.state, out.eos, dt, gravity,
                         &flow.kernel_grid());
    if (layered.faces) {
      out.state = std::move(layered.state);
      out.eos = flow.evaluate_state(out.state, std::move(out.eos));
    }
    out.layering_ms = ms(stage_start);
    observe_entropy(out.layer_entropy);
    if (interface_enabled) {
      stage_start = std::chrono::steady_clock::now();
      auto sharpened =
          sharpen(grid, species, out.state, std::move(out.eos), dt, gravity,
                  interface_detail, &flow.worker_pool(), &flow.kernel_grid(),
                  &surface_scratch);
      if (sharpened.faces)
        out.state = std::move(sharpened.state);
      out.eos = std::move(sharpened.eos);
      out.interface_clusters = sharpened.clusters;
      out.largest_interface_cluster = sharpened.largest_cluster;
      out.interface_faces = sharpened.faces;
      out.limited_faces = sharpened.limited;
      out.interface_ms = ms(stage_start);
      observe_entropy(out.surface_entropy);
    }
    if (prune_before_heat && prune())
      out.eos = flow.evaluate_state(out.state, std::move(out.eos));
    stage_start = std::chrono::steady_clock::now();
    if (heat_enabled) {
      auto heated = heat.advance(out.state, out.eos, dt);
      out.heat_ms = ms(stage_start);
      out.state.energy = std::move(heated.energy);
    }
    stage_start = std::chrono::steady_clock::now();
    auto changed = phase.advance(out.state, dt, phase_time);
    out.evaporated = changed.evaporated;
    out.condensed = changed.condensed;
    out.phase_ms = ms(stage_start);
    for (std::size_t a = 0; a < out.state.masses.mass.size(); ++a)
      conversion[out.state.masses.species[a]].add(-out.state.masses.mass[a]);
    for (std::size_t a = 0; a < changed.masses.mass.size(); ++a)
      conversion[changed.masses.species[a]].add(changed.masses.mass[a]);
    for (std::size_t i = 0; i < initial.energy.size(); ++i) {
      for (std::size_t a = out.state.masses.offsets[i];
           a < out.state.masses.offsets[i + 1]; ++a)
        regions.mass(i, out.state.masses.species[a], -out.state.masses.mass[a]);
      for (std::size_t a = changed.masses.offsets[i];
           a < changed.masses.offsets[i + 1]; ++a)
        regions.mass(i, changed.masses.species[a], changed.masses.mass[a]);
    }
    out.phase_entropy_change = changed.entropy_change;
    out.state.masses = std::move(changed.masses);
    const auto pre_final_sink = sink_entropy;
    prune(false);
    out.eos = flow.evaluate_state(out.state, std::move(out.eos));
    entropy_start = std::chrono::steady_clock::now();
    const auto final_entropy = total_entropy(
        flow.kernel_grid(), species, out.state, out.eos, flow.worker_pool());
    DoubleDouble entropy_change;
    entropy_change.add(final_entropy.hi);
    entropy_change.add(final_entropy.lo);
    entropy_change.add(-initial_entropy.hi);
    entropy_change.add(-initial_entropy.lo);
    entropy_change.add(sink_entropy.hi);
    entropy_change.add(sink_entropy.lo);
    if (detailed_entropy) {
      DoubleDouble heat_entropy;
      heat_entropy.add(final_entropy.hi);
      heat_entropy.add(final_entropy.lo);
      heat_entropy.add(sink_entropy.hi);
      heat_entropy.add(sink_entropy.lo);
      heat_entropy.add(-pre_final_sink.hi);
      heat_entropy.add(-pre_final_sink.lo);
      heat_entropy.add(-current_entropy.hi);
      heat_entropy.add(-current_entropy.lo);
      heat_entropy.add(-changed.entropy_change);
      out.heat_entropy = heat_entropy.value();
    }
    out.entropy_change = entropy_change.value();
    out.trace_entropy = sink_entropy.value();
    out.entropy_ms += ms(entropy_start);
    regions.capture(out.state, gravity, -1);
    out.region_error = regions.finish();
    auto after =
        totals(grid, out.state, species.size(), gravity, &flow.kernel_grid());
    Balance energy, mx, my;
    add(energy, before.energy);
    add(energy, after.energy, -1);
    add(energy, sink_energy, -1);
    add(mx, before.jx);
    add(mx, after.jx, -1);
    mx.add(wall_x);
    add(mx, sink_x, -1);
    add(my, before.jy);
    add(my, after.jy, -1);
    my.add(wall_y);
    my.add(body_y);
    add(my, sink_y, -1);
    // The relative bound is on the full physical operands, not their
    // small signed net. Opposed wall loads and winds must not erase it.
    energy.operands = {};
    energy.operands.add(before.energy_operands.value());
    energy.operands.add(after.energy_operands.value());
    energy.operands.add(std::abs(sink_energy.value()));
    mx.operands = {};
    mx.operands.add(before.jx_operands.value());
    mx.operands.add(after.jx_operands.value());
    mx.operands.add(wall_ops_x);
    mx.operands.add(std::abs(sink_x.value()));
    my.operands = {};
    my.operands.add(before.jy_operands.value());
    my.operands.add(after.jy_operands.value());
    my.operands.add(wall_ops_y);
    my.operands.add(body_ops_y);
    my.operands.add(std::abs(sink_y.value()));
    for (std::size_t key = 0; key < species.size(); ++key) {
      Balance mass;
      add(mass, before.species[key]);
      add(mass, conversion[key]);
      add(mass, after.species[key], -1);
      add(mass, sink_mass[key], -1);
      if (!mass.closed())
        throw std::runtime_error(
            "Private coupled species/conversion/sink audit failed");
      if (mass.operands.value())
        out.mass_error =
            std::max(out.mass_error,
                     std::abs(mass.residual.value()) / mass.operands.value());
      out.sink_kg += sink_mass[key].value();
    }
    std::map<std::uint64_t, Balance> passive_balance;
    for (const auto &[identity, value] : before.passives)
      add(passive_balance[identity], value);
    for (const auto &[identity, value] : after.passives)
      add(passive_balance[identity], value, -1);
    for (const auto &[identity, value] : sink_passives)
      add(passive_balance[identity], value, -1);
    for (const auto &[identity, balance] : passive_balance) {
      if (!balance.closed())
        throw std::runtime_error("Private coupled passive/sink audit failed");
      if (balance.operands.value())
        out.passive_error =
            std::max(out.passive_error, std::abs(balance.residual.value()) /
                                            balance.operands.value());
    }
    if (!energy.closed() || !mx.closed() || !my.closed())
      throw std::runtime_error(
          "Private coupled energy/momentum/sink audit failed");
    out.energy_residual = energy.residual;
    out.energy_operands = energy.operands;
    out.energy_error =
        energy.operands.value()
            ? std::abs(energy.residual.value()) / energy.operands.value()
            : 0;
    out.momentum_error =
        std::max(mx.operands.value()
                     ? std::abs(mx.residual.value()) / mx.operands.value()
                     : 0,
                 my.operands.value()
                     ? std::abs(my.residual.value()) / my.operands.value()
                     : 0);
    out.sink_species.resize(species.size());
    out.conversion_species.resize(species.size());
    for (std::size_t k = 0; k < species.size(); ++k) {
      out.sink_species[k] = sink_mass[k].value();
      out.conversion_species[k] = conversion[k].value();
    }
    for (const auto &[key, value] : sink_passives)
      if (value.value() != 0)
        out.sink_passive_quantities.emplace_back(key, value.value());
    out.sink_internal = sink_internal.value();
    out.sink_kinetic = sink_kinetic.value();
    out.sink_potential = sink_potential.value();
    out.sink_px = sink_x.value();
    out.sink_py = sink_y.value();
    out.wall_px = wall_x;
    out.wall_py = wall_y;
    out.body_py = body_y;
    out.sink_j = sink_energy.value();
    out.totals = std::move(after);
    out.trace_audit_ms = ms(start) - out.flow_ms - out.heat_ms - out.phase_ms -
                         out.layering_ms - out.interface_ms - out.filter_ms;
    return out;
  }
};
} // namespace ma::experimental::game_kernel
