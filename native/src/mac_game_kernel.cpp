// Opt-in game kernel. Its dense owner bridge is temporary; physical stages
// operate on sparse fields and never call the reference evolution solver.
#include "mac_game/tick.hpp"
#include "mac_game_bridge.hpp"
#include "mixed_atmospheres/mac_kernel.hpp"
#include <numeric>

namespace ma::experimental {
namespace {
namespace k = game_kernel;
void require(bool yes, const char *message) {
  if (!yes)
    throw std::invalid_argument(message);
}
std::vector<k::Species> table(const SpeciesRegistry &registry,
                              const FluidThermodynamics &eos, double ratio,
                              MacGameLiquidLaw law = MacGameLiquidLaw::linear) {
  std::vector<k::Species> result(registry.size());
  for (const auto &m : eos.reference().materials()) {
    const auto key = registry.index(m.id);
    auto &s = result[key];
    s.gas = m.phase == ReferencePhase::ideal_gas;
    s.positive_liquid = !s.gas && law == MacGameLiquidLaw::positive_segments;
    s.liquid_group = registry[key].liquid_phase_group;
    s.molar_mass = m.molar_mass_kg_per_mol;
    s.cv = m.cv_j_per_kg_k;
    s.offset = m.energy_offset_j_per_kg;
    s.entropy_offset = m.entropy_offset_j_per_kg_k;
    s.rho0 = m.zero_pressure_density_kg_per_m3;
    s.stiffness = m.reference_bulk_modulus_pa * ratio;
    s.minimum_temperature = m.minimum_temperature_k;
    s.maximum_temperature = m.maximum_temperature_k;
    s.conductivity = registry[key].conductivity;
  }
  return result;
}
k::State sparse(const MacTransportState &state) {
  k::State result;
  result.energy.reserve(state.cells.size());
  result.jx = state.momentum_x;
  result.jy = state.momentum_y;
  result.masses.offsets.push_back(0);
  result.passives.offsets.push_back(0);
  for (const auto &cell : state.cells) {
    require(cell.energy_remainder_j.is_zero(),
            "Game kernel requires binary64 primary energy");
    result.energy.push_back(cell.mixture.energy);
    for (std::size_t key = 0; key < cell.mixture.masses.size(); ++key)
      if (cell.mixture.masses[key] > 0) {
        result.masses.species.push_back(static_cast<std::uint32_t>(key));
        result.masses.mass.push_back(cell.mixture.masses[key]);
      }
    result.masses.offsets.push_back(result.masses.mass.size());
    for (const auto &q : cell.passive_quantities) {
      result.passives.species.push_back(q.identity);
      result.passives.mass.push_back(q.quantity);
    }
    result.passives.offsets.push_back(result.passives.mass.size());
  }
  require(state.full_momentum_x.empty() && state.full_momentum_y.empty(),
          "Game kernel requires binary64 canonical momentum");
  return result;
}
MacTransportState dense(const MacTransportState &source, const k::State &state,
                        std::size_t species,
                        const std::vector<k::Eos> &readings) {
  MacTransportState result;
  result.grid = source.grid;
  result.cells.resize(state.energy.size());
  result.momentum_x = state.jx;
  result.momentum_y = state.jy;
  result.occupied_volumes_m3.resize(state.energy.size());
  for (std::size_t i = 0; i < state.energy.size(); ++i) {
    auto &cell = result.cells[i];
    cell.mixture = MixtureInventory(species);
    cell.mixture.energy = state.energy[i];
    for (std::size_t a = state.masses.offsets[i];
         a < state.masses.offsets[i + 1]; ++a)
      cell.mixture.masses[state.masses.species[a]] = state.masses.mass[a];
    if (!state.passives.mass.empty())
      for (std::size_t a = state.passives.offsets[i];
           a < state.passives.offsets[i + 1]; ++a)
        cell.passive_quantities.push_back(
            {state.passives.species[a], state.passives.mass[a]});
    result.occupied_volumes_m3[i] = readings[i].mass == 0 ? 0
                                    : readings[i].nR > 0
                                        ? result.grid.volume_m3()
                                        : readings[i].liquid_volume;
  }
  return result;
}
void fill_trace(MacTraceSinkReport &out, const std::vector<double> &species,
                double internal, double kinetic, double potential, double x,
                double y,
                const std::vector<std::pair<std::uint64_t, double>> &passives) {
  out.mass_kg.resize(species.size());
  for (std::size_t key = 0; key < species.size(); ++key)
    out.mass_kg[key] = MacScaled::from_double(species[key]);
  out.internal_energy_j = MacEnergy::from_double(internal);
  out.kinetic_energy_j = MacEnergy::from_double(kinetic);
  out.potential_energy_j = MacEnergy::from_double(potential);
  out.momentum_x = MacScaled::from_double(x);
  out.momentum_y = MacScaled::from_double(y);
  for (const auto &[id, value] : passives)
    if (value != 0)
      out.passives.push_back({id, value});
}
void audit_transfers(const MacClosedWorld &world, const MacTransportState &next,
                     std::span<const MacFluidParcel> parcels) {
  const auto old =
      k::totals(world.state().grid, sparse(world.state()),
                world.registry().size(), world.settings().gravity_m_per_s2);
  const auto now = k::totals(next.grid, sparse(next), world.registry().size(),
                             world.settings().gravity_m_per_s2);
  std::vector<k::Balance> masses(world.registry().size());
  k::Balance energy, x, y;
  std::map<std::uint64_t, k::Balance> passives;
  for (std::size_t key = 0; key < masses.size(); ++key) {
    k::add(masses[key], old.species[key]);
    k::add(masses[key], now.species[key], -1);
  }
  k::add(energy, old.energy);
  k::add(energy, now.energy, -1);
  k::add(x, old.jx);
  k::add(x, now.jx, -1);
  k::add(y, old.jy);
  k::add(y, now.jy, -1);
  for (const auto &[id, q] : old.passives)
    k::add(passives[id], q);
  for (const auto &[id, q] : now.passives)
    k::add(passives[id], q, -1);
  for (const auto &parcel : parcels) {
    const double sign = parcel.injected ? 1 : -1;
    masses[parcel.species_index].add(sign * parcel.mass_kg);
    energy.add(sign * parcel.internal_energy_j);
    energy.add(sign * parcel.potential_energy.project().value);
    energy.add(sign * parcel.resolved_kinetic_energy.project().value);
    x.add(sign * parcel.momentum_x.project().value);
    y.add(sign * parcel.momentum_y.project().value);
    for (const auto &q : parcel.passives)
      passives[q.identity].add(sign * q.quantity);
  }
  energy.operands = {};
  energy.operands.add(old.energy_operands.value());
  energy.operands.add(now.energy_operands.value());
  x.operands.add(old.jx_operands.value());
  x.operands.add(now.jx_operands.value());
  y.operands.add(old.jy_operands.value());
  y.operands.add(now.jy_operands.value());
  require(energy.closed() && x.closed() && y.closed(),
          "Game transfer U+K+G/J audit failed");
  for (const auto &m : masses)
    require(m.closed(), "Game transfer species audit failed");
  for (const auto &[id, q] : passives) {
    (void)id;
    require(q.closed(), "Game transfer passive audit failed");
  }
}
bool same_grid(const MacGrid &a, const MacGrid &b) {
  return a.width == b.width && a.height == b.height &&
         a.spacing_m == b.spacing_m && a.depth_m == b.depth_m &&
         a.periodic_x == b.periodic_x && a.periodic_y == b.periodic_y &&
         a.walls == b.walls && a.closed_x == b.closed_x &&
         a.closed_y == b.closed_y;
}
bool same_settings(const MacGameSettings &a, const MacGameSettings &b) {
  if (a.liquid_law != b.liquid_law ||
      a.pressure_maximum_iterations != b.pressure_maximum_iterations ||
      a.heat_maximum_iterations != b.heat_maximum_iterations ||
      a.liquid_stiffness_ratio != b.liquid_stiffness_ratio ||
      a.flow_parts != b.flow_parts ||
      a.pressure_relative_tolerance != b.pressure_relative_tolerance ||
      a.gas_heat_mixing != b.gas_heat_mixing ||
      a.sharpen_liquid_interfaces != b.sharpen_liquid_interfaces ||
      a.interface_detail_fraction != b.interface_detail_fraction ||
      a.phase_relaxation_time_s != b.phase_relaxation_time_s ||
      a.saturation_pairs.size() != b.saturation_pairs.size())
    return false;
  for (std::size_t i = 0; i < a.saturation_pairs.size(); ++i) {
    const auto &x = a.saturation_pairs[i];
    const auto &y = b.saturation_pairs[i];
    if (x.liquid != y.liquid || x.vapour != y.vapour ||
        x.critical_temperature_k != y.critical_temperature_k)
      return false;
  }
  return true;
}
} // namespace
void MacGameSettings::validate() const {
  require((liquid_law == MacGameLiquidLaw::linear ||
               liquid_law == MacGameLiquidLaw::positive_segments) &&
              pressure_maximum_iterations > 0 && pressure_maximum_iterations <= 65536 &&
              heat_maximum_iterations > 0 && heat_maximum_iterations <= 65536 &&
              std::isfinite(liquid_stiffness_ratio) && liquid_stiffness_ratio > 0 &&
              liquid_stiffness_ratio <= 1 && flow_parts > 0 &&
              flow_parts <= 16 && std::isfinite(pressure_relative_tolerance) &&
              pressure_relative_tolerance > 0 &&
              pressure_relative_tolerance <= .01 &&
              std::isfinite(gas_heat_mixing) && gas_heat_mixing >= 0 &&
              gas_heat_mixing <= 64 &&
              std::isfinite(interface_detail_fraction) &&
              interface_detail_fraction >= 0 && interface_detail_fraction < 1 &&
              std::isfinite(phase_relaxation_time_s) &&
              phase_relaxation_time_s > 0 && phase_relaxation_time_s <= 1000 &&
              saturation_pairs.size() <= 65536,
          "Invalid saved game-kernel options");
  for (const auto &pair : saturation_pairs)
    require(pair.liquid != pair.vapour &&
                std::isfinite(pair.critical_temperature_k) &&
                pair.critical_temperature_k > 0,
            "Invalid saved game saturation pair");
}
ReferenceEquilibriumState
MacGameKernelAccess::reading(const MacClosedWorld &world,
                             const MacMaterialCell &cell) {
  struct Cache {
    std::shared_ptr<const FluidThermodynamics> eos;
    double ratio = 0;
    MacGameLiquidLaw law = MacGameLiquidLaw::linear;
    std::vector<k::Species> species;
    std::vector<std::size_t> profile_keys;
  };
  thread_local Cache cache;
  bool groups = cache.species.size() == world.registry_.size();
  if (groups)
    for (std::size_t key = 0; key < cache.species.size(); ++key)
      groups = groups && cache.species[key].liquid_group ==
                             world.registry_[key].liquid_phase_group;
  if (cache.eos != world.thermodynamics_ ||
      cache.ratio != world.settings_.game.liquid_stiffness_ratio ||
      cache.law != world.settings_.game.liquid_law || !groups) {
    cache.eos = world.thermodynamics_;
    cache.ratio = world.settings_.game.liquid_stiffness_ratio;
    cache.law = world.settings_.game.liquid_law;
    cache.species = table(world.registry_, *cache.eos, cache.ratio, cache.law);
    cache.profile_keys.clear();
    for (const auto &m : cache.eos->reference().materials())
      cache.profile_keys.push_back(world.registry_.index(m.id));
  }
  const auto &species = cache.species;
  const auto e = k::evaluate(species, cell.mixture.masses, cell.mixture.energy,
                             world.state().grid.volume_m3());
  ReferenceEquilibriumState out;
  out.temperature_k = e.temperature;
  out.mechanical_pressure_pa = e.pressure;
  out.gas_pressure_pa = e.nR > 0 ? e.pressure : 0;
  out.gas_volume_m3 = e.gas_volume;
  out.liquid_volume_m3 = e.liquid_volume;
  out.vacuum_volume_m3 =
      e.nR > 0 ? 0
               : std::max(0., world.state().grid.volume_m3() - e.liquid_volume);
  out.reference_energy_j = e.offset;
  out.thermal_energy_j = e.capacity * e.temperature;
  out.elastic_energy_j = e.elastic_energy;
  out.intrinsic_heat_capacity_j_per_k =
      out.equilibrium_heat_capacity_at_cell_volume_j_per_k = e.capacity;
  out.has_gas = e.nR > 0;
  out.has_vacuum = out.vacuum_volume_m3 > 0;
  out.partial_volumes_m3.resize(species.size());
  out.specific_internal_energies_j_per_kg.resize(species.size());
  std::size_t profile = 0;
  for (const auto key : cache.profile_keys) {
    const auto &s = species[key];
    const auto mass = cell.mixture.masses[key];
    out.partial_volumes_m3[profile] =
        mass == 0 ? 0
        : s.gas   ? mass * k::R / s.molar_mass * e.temperature / e.pressure
                  : mass / s.rho0 * k::liquid_volume_fraction(s,e.pressure);
    out.specific_internal_energies_j_per_kg[profile] =
        s.offset + s.cv * e.temperature +
        (s.gas ? 0 : k::liquid_specific_elastic(s,e.pressure));
    ++profile;
  }
  out.entropy_j_per_k = k::entropy(species, cell.mixture.masses, e);
  return out;
}
ReferenceEquilibriumState MacClosedWorld::cell_reading(std::size_t cell) const {
  if (settings_.numerical_kernel == MacNumericalKernel::game)
    return MacGameKernelAccess::reading(*this, state().cells.at(cell));
  return thermodynamics_->evaluate(state().cells.at(cell).mixture,
                                   mac_material_volume(state(), cell));
}
void MacGameKernelAccess::initialize(const SpeciesRegistry &registry,
                                     const FluidThermodynamics &eos,
                                     MacTransportState &state,
                                     std::span<const double> temperatures,
                                     const MacClosedSettings &settings) {
  require(temperatures.size() == state.cells.size(),
          "Game import temperature shape differs");
  const auto species =
      table(registry, eos, settings.game.liquid_stiffness_ratio, settings.game.liquid_law);
  state.occupied_volumes_m3.assign(state.cells.size(), 0);
  for (std::size_t i = 0; i < state.cells.size(); ++i) {
    auto &cell = state.cells[i];
    if (state.grid.walls[i] || cell.mixture.mass() == 0) {
      require(cell.mixture.mass() == 0, "Game wall contains material");
      cell.mixture.energy = 0;
      cell.energy_remainder_j = {};
      continue;
    }
    const double T = temperatures[i];
    require(std::isfinite(T) && T > 0, "Invalid game import temperature");
    double capacity = 0, offset = 0, nR = 0, volume = 0, compliance = 0;
    for (std::size_t key = 0; key < species.size(); ++key) {
      const double mass = cell.mixture.masses[key];
      if (mass == 0)
        continue;
      const auto &s = species[key];
      capacity += mass * s.cv;
      offset += mass * s.offset;
      if (s.gas)
        nR += mass * k::R / s.molar_mass;
      else {
        const double nominal = mass / s.rho0;
        volume += nominal;
        compliance += nominal / s.stiffness;
      }
    }
    double pressure = 0;
    if (nR > 0) {
      const double b = state.grid.volume_m3() - volume, c = nR * T;
      if (compliance == 0)
        pressure = c / state.grid.volume_m3();
      else {
        const double d =
            std::hypot(b, 2 * std::sqrt(compliance) * std::sqrt(c));
        pressure = b >= 0 ? 2 * c / (b + d) : (d - b) / (2 * compliance);
      }
    } else if (volume > state.grid.volume_m3())
      pressure = (volume - state.grid.volume_m3()) / compliance;
    double elastic=.5*compliance*pressure*pressure;
    bool extended=false;
    for(std::size_t key=0;key<species.size();++key)if(cell.mixture.masses[key]>0&&!species[key].gas&&species[key].positive_liquid)extended=extended||pressure>=.5*species[key].stiffness;
    if(extended){
      const auto each=[&](auto emit){for(std::size_t key=0;key<species.size();++key)if(cell.mixture.masses[key]>0&&!species[key].gas)emit(cell.mixture.masses[key]/species[key].rho0,species[key].stiffness);};
      const auto root=k::positive_liquid::pressure(state.grid.volume_m3(),0,0,nR*T,true,pressure,each);
      pressure=root.pressure;elastic=.5*root.coefficients.c*pressure*pressure+root.coefficients.d;
    }
    cell.mixture.energy=offset+capacity*T+elastic;
    cell.energy_remainder_j = {};
    const auto e = k::evaluate(species, cell.mixture.masses,
                               cell.mixture.energy, state.grid.volume_m3());
    state.occupied_volumes_m3[i] =
        e.nR > 0 ? state.grid.volume_m3() : e.liquid_volume;
  }
}
MacClosedWorld MacClosedWorld::game_colony(
    SpeciesRegistry registry, std::shared_ptr<const FluidThermodynamics> eos,
    MacTransportState state, std::span<const double> temperatures,
    MacClosedSettings settings) {
  require(settings.numerical_kernel == MacNumericalKernel::game &&
              eos != nullptr,
          "Game import requires its explicit saved kernel");
  MacGameKernelAccess::initialize(registry, *eos, state, temperatures,
                                  settings);
  return MacClosedWorld(std::move(registry), std::move(eos), std::move(state),
                        std::move(settings));
}
MacFaceInertias MacGameKernelAccess::inertias(const SpeciesRegistry &registry,
                                              const MacTransportState &state) {
  mac_kernel::Grid grid(state.grid);
  require(state.cells.size() == grid.N && state.momentum_x.size() == grid.NX &&
              state.momentum_y.size() == grid.NY,
          "Game primary shape differs from grid");
  std::vector<double> mass(grid.N);
  for (std::size_t i = 0; i < grid.N; ++i) {
    const auto &cell = state.cells[i];
    cell.mixture.validate(registry);
    validate_passive_quantities(cell.passive_quantities);
    mass[i] = cell.mixture.mass();
    if (grid.wall[i] || mass[i] == 0)
      require(mass[i] == 0 && cell.mixture.energy == 0 &&
                  cell.passive_quantities.empty(),
              "Game solid/vacuum contains fluid energy/passives");
  }
  const auto value = [&](auto i) {
    return i == mac_kernel::none ? 0 : mass[i];
  };
  MacFaceInertias out;
  out.x.resize(grid.NX);
  out.y.resize(grid.NY);
  for (std::size_t f = 0; f < grid.NX; ++f)
    out.x[f] = .5 * (value(grid.xl[f]) + value(grid.xr[f]));
  for (std::size_t f = 0; f < grid.NY; ++f)
    out.y[f] = .5 * (value(grid.yl[f]) + value(grid.yr[f]));
  return out;
}
void MacGameKernelAccess::validate(const MacClosedWorld &world,
                                   MacTransportState &state) {
  require(!state.grid.periodic_x && !state.grid.periodic_y,
          "Game kernel requires impermeable nonperiodic boundaries");
  const auto species = table(world.registry_, *world.thermodynamics_,
                             world.settings_.game.liquid_stiffness_ratio, world.settings_.game.liquid_law);
  for (const auto &pair : world.settings_.game.saturation_pairs)
    require(pair.liquid < species.size() && pair.vapour < species.size() &&
                !species[pair.liquid].gas && species[pair.vapour].gas,
            "Game saturation pair disagrees with registry");
  (void)sparse(state);
  state.occupied_volumes_m3.assign(state.cells.size(), 0);
  for (std::size_t i = 0; i < state.cells.size(); ++i)
    if (!state.grid.walls[i]) {
      const auto e =
          k::evaluate(species, state.cells[i].mixture.masses,
                      state.cells[i].mixture.energy, state.grid.volume_m3());
      state.occupied_volumes_m3[i] = e.mass == 0 ? 0
                                     : e.nR > 0  ? state.grid.volume_m3()
                                                 : e.liquid_volume;
      if (e.mass == 0)
        require(state.cells[i].passive_quantities.empty(),
                "Game vacuum contains passives");
    }
}
MacClosedAdvance MacGameKernelAccess::advance(const MacClosedWorld &source,
                                              double seconds,
                                              std::size_t budget) {
  require_mac_energy_environment();
  source.settings_.validate();
  require(std::isfinite(seconds) && seconds > 0 && seconds <= 10 &&
              budget > 0 && budget <= 65536,
          "Invalid game interval/budget");
  struct Cache {
    std::shared_ptr<const FluidThermodynamics> eos;
    MacGrid grid;
    MacGameSettings settings;
    unsigned threads = 0;
    bool conduction = true, closure = false, trace_heat = false;
    std::unique_ptr<k::Tick> tick;
  };
  thread_local Cache cache;
  const auto &cfg = source.settings_.game;
  const auto options = mac_kernel_default_options();
  const unsigned threads = std::min(12U, options.threads);
  const bool trace_heat =
      source.settings_.game_profile == MacGameProfile::game_kernel_trace_heat;
  const bool closure =
      trace_heat ||
      source.settings_.game_profile == MacGameProfile::game_kernel_free_surface;
  if (!cache.tick || cache.eos != source.thermodynamics_ ||
      cache.threads != threads || cache.closure != closure ||
      cache.trace_heat != trace_heat ||
      cache.conduction != source.settings_.thermal_conduction ||
      !same_grid(cache.grid, source.state().grid) ||
      !same_settings(cache.settings, cfg)) {
    const auto species = table(source.registry_, *source.thermodynamics_,
                               cfg.liquid_stiffness_ratio, cfg.liquid_law);
    std::vector<k::PhasePair> pairs;
    for (const auto &p : cfg.saturation_pairs)
      pairs.push_back({p.liquid, p.vapour, p.critical_temperature_k});
    cache.tick = std::make_unique<k::Tick>(
        source.state().grid, species, pairs, threads, cfg.flow_parts,
        cfg.gas_heat_mixing, cfg.sharpen_liquid_interfaces,
        cfg.interface_detail_fraction, cfg.pressure_relative_tolerance, 0,
        false, 0, 2, cfg.phase_relaxation_time_s,
        source.settings_.thermal_conduction, cfg.pressure_maximum_iterations,
        cfg.heat_maximum_iterations, closure, trace_heat);
    cache.eos = source.thermodynamics_;
    cache.grid = source.state().grid;
    cache.settings = cfg;
    cache.threads = threads;
    cache.conduction = source.settings_.thermal_conduction;
    cache.closure = closure;
    cache.trace_heat = trace_heat;
  }
  MacClosedAdvance result{source, {}};
  auto state = sparse(source.state());
  double elapsed = 0;
  MacEnergySum energy_residual, energy_bound;
  std::vector<k::Eos> last;
  result.report.game_converted_mass_kg.assign(source.registry_.size(), 0);
  while (elapsed < seconds) {
    require(result.report.attempts < budget,
            "Game interval exceeds its tick budget");
    const double dt =
        std::min(source.settings_.maximum_timestep_s, seconds - elapsed);
    auto next =
        cache.tick->advance(state, dt, source.settings_.gravity_m_per_s2,
                            source.settings_.trace_sink_threshold_kg);
    state = std::move(next.state);
    last = std::move(next.eos);
    MacTraceSinkReport sink;
    fill_trace(sink, next.sink_species, next.sink_internal, next.sink_kinetic,
               next.sink_potential, next.sink_px, next.sink_py,
               next.sink_passive_quantities);
    sink.constituents = next.trace_constituents;
    sink.cells = next.trace_cells;
    result.report.trace_sink.merge(sink);
    for (std::size_t key = 0; key < next.conversion_species.size(); ++key)
      result.report.game_converted_mass_kg[key] += next.conversion_species[key];
    result.report.evaporated_kg += next.evaporated;
    result.report.condensed_kg += next.condensed;
    result.report.entropy_change_j_per_k += next.entropy_change;
    result.report.wall_impulse_x += next.wall_px;
    result.report.wall_impulse_y += next.wall_py;
    result.report.body_impulse_y += next.body_py;
    energy_residual.add(next.energy_residual.hi);
    energy_residual.add(next.energy_residual.lo);
    energy_bound.add(1e-12 * next.energy_operands.value());
    ++result.report.attempts;
    result.report.substeps += cfg.flow_parts;
    ++result.report.phase_change_stages;
    elapsed += dt;
  }
  result.world = source.derived(
      dense(source.state(), state, source.registry_.size(), last));
  result.world.time_s_ = source.time_s_ + seconds;
  require(std::isfinite(result.world.time_s_) &&
              result.world.time_s_ > source.time_s_,
          "Game clock cannot advance");
  result.report.energy_residual_wide_j = energy_residual.value();
  result.report.energy_bound_wide_j = energy_bound.value();
  result.report.energy_residual_j =
      result.report.energy_residual_wide_j.project().value;
  result.report.energy_bound_j = result.report.energy_bound_wide_j.project().value;
  result.report.full_wall_impulse_x =
      MacScaled::from_double(result.report.wall_impulse_x);
  result.report.full_wall_impulse_y =
      MacScaled::from_double(result.report.wall_impulse_y);
  result.report.full_body_impulse_y =
      MacScaled::from_double(result.report.body_impulse_y);
  return result;
}
MacTraceSink MacGameKernelAccess::trace(const MacClosedWorld &world) {
  MacTraceSink out{world, {}};
  const double threshold = world.settings_.trace_sink_threshold_kg;
  if (threshold == 0)
    return out;
  bool found = false;
  for (const auto &c : world.state().cells)
    for (double m : c.mixture.masses)
      if (m > 0 && m < threshold)
        found = true;
  if (!found)
    return out;
  const auto species = table(world.registry_, *world.thermodynamics_,
                             world.settings_.game.liquid_stiffness_ratio, world.settings_.game.liquid_law);
  const auto state = sparse(world.state());
  auto sunk = k::trace_sink(world.state().grid, species, state, threshold,
                            world.settings_.gravity_m_per_s2);
  std::vector<k::Eos> readings(state.energy.size());
  for (std::size_t i = 0; i < readings.size(); ++i)
    if (!world.state().grid.walls[i]) {
      const auto a = sunk.state.masses.offsets[i],
                 n = sunk.state.masses.offsets[i + 1] - a;
      readings[i] = k::evaluate_selected(
          species, n, [&](auto p) { return sunk.state.masses.species[a + p]; },
          [&](auto p) { return sunk.state.masses.mass[a + p]; },
          sunk.state.energy[i], world.state().grid.volume_m3());
    }
  out.world = world.derived(
      dense(world.state(), sunk.state, world.registry_.size(), readings));
  fill_trace(out.report, sunk.mass, sunk.internal, sunk.kinetic, sunk.potential,
             sunk.jx, sunk.jy, sunk.passives);
  out.report.constituents = sunk.constituents;
  out.report.cells = sunk.cells;
  return out;
}
MacClosedWorld MacGameKernelAccess::heat(const MacClosedWorld &source,
                                         std::span<const MacCellHeat> edits) {
  require(!edits.empty() && edits.size() <= 4096, "Invalid game heat batch");
  auto next = source.state();
  for (const auto &edit : edits) {
    require(edit.cell_index < next.cells.size() &&
                !next.grid.walls[edit.cell_index] &&
                std::isfinite(edit.joules) &&
                std::isfinite(edit.maximum_temperature_k) &&
                edit.maximum_temperature_k > 0,
            "Invalid game heat edit");
    auto &cell = next.cells[edit.cell_index];
    const double old = cell.mixture.energy;
    cell.mixture.energy += edit.joules;
    require(edit.joules == 0 || cell.mixture.energy != old,
            "Game heat change is unrepresentable");
    const auto e = reading(source, cell);
    require(edit.joules <= 0 || e.temperature_k <= edit.maximum_temperature_k,
            "Game heat exceeds reserved temperature limit");
  }
  return source.derived(std::move(next));
}
namespace {
std::pair<std::size_t, std::size_t> endpoints(const MacGrid &g, bool x,
                                              std::size_t f) {
  constexpr auto none = std::numeric_limits<std::size_t>::max();
  const auto col = f % (x ? g.x_width() : g.width),
             row = f / (x ? g.x_width() : g.width);
  std::size_t l = none, r = none;
  if (x) {
    if (col > 0)
      l = col - 1 + row * g.width;
    if (col < g.width)
      r = col + row * g.width;
  } else {
    if (row > 0)
      l = col + (row - 1) * g.width;
    if (row < g.height)
      r = col + row * g.width;
  }
  if (l != none && g.walls[l])
    l = none;
  if (r != none && g.walls[r])
    r = none;
  return {l, r};
}
double cell_mass(const MacTransportState &s, std::size_t i) {
  return i == std::numeric_limits<std::size_t>::max()
             ? 0
             : s.cells[i].mixture.mass();
}
double inertia(const MacTransportState &s, bool x, std::size_t f) {
  const auto [l, r] = endpoints(s.grid, x, f);
  return .5 * (cell_mass(s, l) + cell_mass(s, r));
}
std::array<std::size_t, 2> cell_faces(const MacGrid &g, std::size_t i, bool x) {
  const auto col = i % g.width, row = i / g.width;
  return x ? std::array<std::size_t, 2>{g.x_face(col, row),
                                        g.x_face(col + 1, row)}
           : std::array<std::size_t, 2>{g.y_face(col, row),
                                        g.y_face(col, row + 1)};
}
void occupied(const MacClosedWorld &source, MacTransportState &state,
              std::span<const std::size_t> touched) {
  if (state.occupied_volumes_m3.empty()) {
    state.occupied_volumes_m3.resize(state.cells.size());
    for (std::size_t i = 0; i < state.cells.size(); ++i)
      if (!state.grid.walls[i] && state.cells[i].mixture.mass() > 0)
        state.occupied_volumes_m3[i] = state.grid.volume_m3();
  }
  for (const auto i : touched) {
    if (state.grid.walls[i] || state.cells[i].mixture.mass() == 0) {
      require(state.cells[i].mixture.energy == 0 &&
                  state.cells[i].passive_quantities.empty(),
              "Game empty cell retains U/passives");
      state.occupied_volumes_m3[i] = 0;
      continue;
    }
    const auto e = MacGameKernelAccess::reading(source, state.cells[i]);
    state.occupied_volumes_m3[i] =
        e.has_gas ? state.grid.volume_m3() : e.liquid_volume_m3;
  }
}
} // namespace
MacFluidTransferResult
MacGameKernelAccess::transfers(const MacClosedWorld &source,
                               std::span<const MacFluidTransfer> edits,
                               MacFluidTransferTopology topology) {
  require(!edits.empty() && edits.size() <= 4096,
          "Invalid game transfer batch");
  require(topology == MacFluidTransferTopology::single_phase ||
              topology == MacFluidTransferTopology::occupied_mixture,
          "Invalid game transfer topology");
  auto next = source.state();
  std::vector<MacFluidParcel> parcels;
  parcels.reserve(edits.size());
  std::vector<std::size_t> touched;
  const auto species = table(source.registry_, *source.thermodynamics_,
                             source.settings_.game.liquid_stiffness_ratio, source.settings_.game.liquid_law);
  for (const auto &edit : edits) {
    touched.clear();
    require(edit.cell_index < next.cells.size() &&
                !next.grid.walls[edit.cell_index] &&
                edit.species_index < species.size() &&
                std::isfinite(edit.mass_kg) && edit.mass_kg >= 0 &&
                std::isfinite(edit.temperature_k) &&
                (!edit.inject || edit.temperature_k > 0),
            "Invalid game transfer request");
    auto &cell = next.cells[edit.cell_index];
    const auto &material = species[edit.species_index];
    validate_passive_quantities(edit.passives);
    require(edit.inject || edit.passives.empty(),
            "Game extraction cannot inject passives");
    if (topology == MacFluidTransferTopology::single_phase)
      for (std::size_t key = 0; key < species.size(); ++key)
        require(
            cell.mixture.masses[key] == 0 ||
                (material.gas ? species[key].gas : key == edit.species_index),
            "Game point-transfer phase mismatch");
    const auto before = reading(source, cell);
    MacFluidParcel p{};
    p.cell_index = edit.cell_index;
    p.species_index = edit.species_index;
    p.injected = edit.inject;
    p.temperature_k = edit.inject ? edit.temperature_k : before.temperature_k;
    if (edit.observe_passives)
      p.passives_before = cell.passive_quantities;
    if (edit.mass_kg == 0 ||
        (!edit.inject && cell.mixture.masses[edit.species_index] == 0)) {
      if (edit.observe_passives)
        p.passives_after = cell.passive_quantities;
      parcels.push_back(std::move(p));
      continue;
    }
    const double old_species = cell.mixture.masses[edit.species_index],
                 old_mass = cell.mixture.mass();
    const double requested =
        edit.inject ? edit.mass_kg : std::min(edit.mass_kg, old_species);
    const double new_species =
        edit.inject ? old_species + requested : old_species - requested;
    require(std::isfinite(new_species) && new_species >= 0 &&
                new_species != old_species,
            "Game transfer mass is unrepresentable");
    p.mass_kg =
        edit.inject ? new_species - old_species : old_species - new_species;
    std::array<double, 4> old_inertia{}, old_j{}, old_k{};
    unsigned slot = 0;
    for (bool x : {true, false})
      for (const auto f : cell_faces(next.grid, edit.cell_index, x)) {
        const double I = inertia(next, x, f),
                     J = (x ? next.momentum_x : next.momentum_y)[f];
        require(I > 0 || J == 0, "Game transfer J has no inertia");
        old_inertia[slot] = I;
        old_j[slot] = J;
        old_k[slot] = J == 0 ? 0 : .5 * J * (J / I);
        ++slot;
      }
    cell.mixture.masses[edit.species_index] = new_species;
    const double old_u = cell.mixture.energy;
    const double specific =
        material.offset + material.cv * p.temperature_k +
        (material.gas ? 0
                      : k::liquid_specific_elastic(material,before.mechanical_pressure_pa));
    cell.mixture.energy =
        cell.mixture.mass() == 0
            ? 0
            : old_u + (edit.inject ? 1 : -1) * p.mass_kg * specific;
    p.internal_energy_j =
        edit.inject ? cell.mixture.energy - old_u : old_u - cell.mixture.energy;
    require(std::isfinite(cell.mixture.energy),
            "Game transfer energy is unrepresentable");
    if (edit.inject) {
      for (const auto &q : edit.passives) {
        auto at = std::lower_bound(
            cell.passive_quantities.begin(), cell.passive_quantities.end(),
            q.identity, [](const auto &a, auto id) { return a.identity < id; });
        const double held =
            at == cell.passive_quantities.end() || at->identity != q.identity
                ? 0
                : at->quantity;
        const double actual = held + q.quantity;
        require(std::isfinite(actual), "Game passive injection overflow");
        if (actual == held)
          continue;
        if (held == 0)
          cell.passive_quantities.insert(at, {q.identity, actual});
        else
          at->quantity = actual;
        p.passives.push_back({q.identity, actual - held});
      }
    } else if (cell.mixture.mass() == 0) {
      p.passives = std::move(cell.passive_quantities);
      cell.passive_quantities.clear();
    } else {
      for (auto &q : cell.passive_quantities) {
        const double kept = q.quantity * (1 - p.mass_kg / old_mass);
        const double moved = q.quantity - kept;
        if (moved > 0) {
          p.passives.push_back({q.identity, moved});
          q.quantity = kept;
        }
      }
      std::erase_if(cell.passive_quantities,
                    [](const auto &q) { return q.quantity == 0; });
    }
    k::DoubleDouble carried_k, carried_x, carried_y;
    slot = 0;
    touched.push_back(edit.cell_index);
    for (bool x : {true, false})
      for (const auto f : cell_faces(next.grid, edit.cell_index, x)) {
        auto &J = (x ? next.momentum_x : next.momentum_y)[f];
        const double I = inertia(next, x, f);
        double outgoing = 0;
        if (!edit.inject && old_inertia[slot] > 0) {
          J = I == 0 ? 0 : old_j[slot] * (I / old_inertia[slot]);
          outgoing = old_k[slot] * (.5 * p.mass_kg / old_inertia[slot]);
          carried_k.add(outgoing);
          (x ? carried_x : carried_y).add(old_j[slot] - J);
        }
        const double K = J == 0 ? 0 : .5 * J * (J / I);
        const double heat = old_k[slot] - outgoing - K;
        const auto [l, r] = endpoints(next.grid, x, f);
        const double sum = cell_mass(next, l) + cell_mass(next, r);
        if (heat != 0) {
          require(sum > 0, "Game transfer work has no carrier");
          for (const auto i : {l, r})
            if (i != std::numeric_limits<std::size_t>::max() &&
                next.cells[i].mixture.mass() > 0) {
              next.cells[i].mixture.energy +=
                  heat * (next.cells[i].mixture.mass() / sum);
              touched.push_back(i);
            }
        }
        ++slot;
      }
    p.resolved_kinetic_energy = MacEnergy::from_double(carried_k.value());
    p.momentum_x = MacScaled::from_double(carried_x.value());
    p.momentum_y = MacScaled::from_double(carried_y.value());
    p.potential_energy =
        MacEnergy::from_double(p.mass_kg)
            .multiplied(source.settings_.gravity_m_per_s2)
            .multiplied(
                (static_cast<double>(edit.cell_index / next.grid.width) + .5) *
                next.grid.spacing_m);
    if (edit.observe_passives)
      p.passives_after = cell.passive_quantities;
    occupied(source, next, touched);
    parcels.push_back(std::move(p));
  }
  audit_transfers(source, next, parcels);
  return {source.derived(std::move(next)), std::move(parcels), 0, {}};
}
MacClosedWorld MacGameKernelAccess::terrain(const MacClosedWorld &source,
                                            std::span<const std::size_t> cells,
                                            bool open) {
  if (cells.empty())
    return source;
  auto next = source.state();
  std::vector<std::size_t> touched;
  for (auto i : cells) {
    require(i < next.cells.size(), "Game terrain cell outside grid");
    if (open) {
      if (!next.grid.walls[i])
        continue;
      require(next.cells[i].mixture.mass() == 0 &&
                  next.cells[i].mixture.energy == 0,
              "Game opening contains wall material");
      next.grid.walls[i] = 0;
      touched.push_back(i);
      continue;
    }
    if (next.grid.walls[i])
      continue;
    require(next.cells[i].mixture.mass() == 0 &&
                next.cells[i].mixture.energy == 0 &&
                next.cells[i].passive_quantities.empty(),
            "Game closing cell must first be displaced");
    for (bool x : {true, false})
      for (auto f : cell_faces(next.grid, i, x)) {
        auto &J = (x ? next.momentum_x : next.momentum_y)[f];
        if (J == 0)
          continue;
        const double I = inertia(next, x, f);
        require(I > 0, "Game terrain J without inertia");
        const double heat = .5 * J * (J / I);
        const auto [l, r] = endpoints(next.grid, x, f);
        const auto other = l == i ? r : l;
        require(other != std::numeric_limits<std::size_t>::max() &&
                    next.cells[other].mixture.mass() > 0,
                "Game wall impact has no carrier");
        next.cells[other].mixture.energy += heat;
        J = 0;
        touched.push_back(other);
      }
    next.grid.walls[i] = 1;
    touched.push_back(i);
  }
  occupied(source, next, touched);
  return source.derived(std::move(next));
}
MacClosedWorld MacGameKernelAccess::faces(const MacClosedWorld &source,
                                          std::vector<std::uint8_t> x,
                                          std::vector<std::uint8_t> y) {
  const auto &current = source.state();
  const auto settled = [](const auto &mask, const auto &momentum) {
    for (std::size_t f = 0; f < mask.size(); ++f)
      if (mask[f] && momentum[f] != 0)
        return false;
    return true;
  };
  if (x == current.grid.closed_x && y == current.grid.closed_y &&
      settled(x, current.momentum_x) && settled(y, current.momentum_y)) {
    if (source.revision_ == std::numeric_limits<std::uint64_t>::max())
      throw std::overflow_error("MAC closed revision overflow");
    auto result = source;
    ++result.revision_;
    return result;
  }
  auto next = source.state();
  require((x.empty() || x.size() == next.momentum_x.size()) &&
              (y.empty() || y.size() == next.momentum_y.size()),
          "Game closed-face shape differs");
  std::vector<std::size_t> touched;
  for (bool axis : {true, false}) {
    const auto &mask = axis ? x : y;
    auto &j = axis ? next.momentum_x : next.momentum_y;
    for (std::size_t f = 0; f < mask.size(); ++f) {
      require(mask[f] <= 1, "Invalid game face mask");
      if (!mask[f] || j[f] == 0)
        continue;
      const double I = inertia(next, axis, f);
      require(I > 0, "Game fixed face J has no inertia");
      const double heat = .5 * j[f] * (j[f] / I);
      const auto [l, r] = endpoints(next.grid, axis, f);
      const double sum = cell_mass(next, l) + cell_mass(next, r);
      for (auto i : {l, r})
        if (i != std::numeric_limits<std::size_t>::max() &&
            next.cells[i].mixture.mass() > 0) {
          next.cells[i].mixture.energy +=
              heat * (next.cells[i].mixture.mass() / sum);
          touched.push_back(i);
        }
      j[f] = 0;
    }
  }
  next.grid.closed_x = std::move(x);
  next.grid.closed_y = std::move(y);
  occupied(source, next, touched);
  return source.derived(std::move(next));
}
} // namespace ma::experimental
