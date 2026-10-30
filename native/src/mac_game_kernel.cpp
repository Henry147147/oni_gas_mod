// Opt-in game kernel. Its dense owner bridge is temporary; physical stages
// operate on sparse fields and never call the reference evolution solver.
#include "mac_hypot.hpp"
#include "mac_game/tick.hpp"
#include "mac_game/regional_tick.hpp"
#include "mac_game/activity.hpp"
#include "mac_game_bridge.hpp"
#include "mixed_atmospheres/mac_kernel.hpp"
#include <cmath>
#include <numeric>
#include <optional>
#include <unordered_map>
#include <unordered_set>

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
// Fixed chunks of cells for the conversions below, on the pool if any.
constexpr std::size_t conversion_chunk = 1024;
void chunks(mac_kernel::Pool *pool, std::size_t cells,
            const std::function<void(std::size_t, std::size_t, std::size_t)> &job) {
  const auto count = (cells + conversion_chunk - 1) / conversion_chunk;
  const auto run = [&](std::size_t chunk) {
    job(chunk, chunk * conversion_chunk,
        std::min(cells, (chunk + 1) * conversion_chunk));
  };
  if (pool)
    pool->run(count, run);
  else
    for (std::size_t chunk = 0; chunk < count; ++chunk)
      run(chunk);
}
// Dense cells to sparse fields: per-chunk counts, then each chunk fills its
// own slices.
k::State sparse(const MacTransportState &state, mac_kernel::Pool *pool = nullptr) {
  const auto N = state.cells.size();
  k::State result;
  result.energy.resize(N);
  result.jx = state.momentum_x;
  result.jy = state.momentum_y;
  const auto count = (N + conversion_chunk - 1) / conversion_chunk;
  std::vector<std::size_t> mass_start(count + 1), passive_start(count + 1);
  const auto masses_of = [](const MacMaterialCell &cell) -> const std::vector<double> * {
    // Shared zero storage holds no constituent.
    return cell.mixture.masses.implicit_zero()
               ? nullptr
               : &static_cast<const std::vector<double> &>(cell.mixture.masses);
  };
  chunks(pool, N, [&](std::size_t chunk, std::size_t first, std::size_t end) {
    std::size_t masses = 0, passives = 0;
    for (std::size_t i = first; i < end; ++i) {
      const auto &cell = state.cells[i];
      require(cell.energy_remainder_j.is_zero(),
              "Game kernel requires binary64 primary energy");
      if (const auto *values = masses_of(cell))
        for (const double mass : *values)
          masses += mass > 0;
      passives += cell.passive_quantities.size();
    }
    mass_start[chunk + 1] = masses;
    passive_start[chunk + 1] = passives;
  });
  require(state.full_momentum_x.empty() && state.full_momentum_y.empty(),
          "Game kernel requires binary64 canonical momentum");
  for (std::size_t chunk = 0; chunk < count; ++chunk) {
    mass_start[chunk + 1] += mass_start[chunk];
    passive_start[chunk + 1] += passive_start[chunk];
  }
  result.masses.offsets.resize(N + 1);
  result.masses.species.resize(mass_start.back());
  result.masses.mass.resize(mass_start.back());
  result.passives.offsets.resize(N + 1);
  result.passives.species.resize(passive_start.back());
  result.passives.mass.resize(passive_start.back());
  result.masses.offsets[0] = result.passives.offsets[0] = 0;
  chunks(pool, N, [&](std::size_t chunk, std::size_t first, std::size_t end) {
    auto mass_at = mass_start[chunk], passive_at = passive_start[chunk];
    for (std::size_t i = first; i < end; ++i) {
      const auto &cell = state.cells[i];
      result.energy[i] = cell.mixture.energy;
      if (const auto *values = masses_of(cell)) {
        std::uint32_t key = 0;
        for (const double mass : *values) {
          if (mass > 0) {
            result.masses.species[mass_at] = key;
            result.masses.mass[mass_at] = mass;
            ++mass_at;
          }
          ++key;
        }
      }
      result.masses.offsets[i + 1] = mass_at;
      for (const auto &q : cell.passive_quantities) {
        result.passives.species[passive_at] = q.identity;
        result.passives.mass[passive_at] = q.quantity;
        ++passive_at;
      }
      result.passives.offsets[i + 1] = passive_at;
    }
  });
  return result;
}
// Sparse fields back to dense cells. The advance builds them serially: on
// the pool, the cells' allocations in worker arenas made 12-thread frames
// about 3% slower.
MacTransportState dense(const MacTransportState &source, const k::State &state,
                        std::size_t species,
                        const std::vector<k::Eos> &readings,
                        mac_kernel::Pool *pool = nullptr,
                        std::span<const std::uint8_t> rebuilt = {}) {
  MacTransportState result;
  result.grid = source.grid;
  result.cells.resize(state.energy.size());
  result.momentum_x = state.jx;
  result.momentum_y = state.jy;
  result.occupied_volumes_m3.resize(state.energy.size());
  chunks(pool, state.energy.size(), [&](std::size_t, std::size_t first, std::size_t end) {
    for (std::size_t i = first; i < end; ++i) {
      auto &cell = result.cells[i];
      if (!rebuilt.empty() && !rebuilt[i]) {
        // Structured bindings also refuse a future unhandled cell field.
        const auto& [mixture, passives, remainder] = source.cells[i];
        const auto& [masses, energy] = mixture;
        cell.mixture.masses = masses.immutable_snapshot_copy();
        cell.mixture.energy = energy;
        cell.passive_quantities = passives;
        cell.energy_remainder_j = remainder;
        result.occupied_volumes_m3[i] = source.occupied_volumes_m3.empty()
            ? (!source.grid.walls[i] && cell.mixture.mass() > 0 ? source.grid.volume_m3() : 0)
            : source.occupied_volumes_m3[i];
        continue;
      }
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
  });
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
      a.phase_rule != b.phase_rule ||
      a.activity_tiers != b.activity_tiers || a.activity_hold_s != b.activity_hold_s ||
      a.activity_pause_s != b.activity_pause_s ||
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
              (phase_rule == MacPhaseRule::equilibrium ||
               phase_rule == MacPhaseRule::boil_only) &&
              std::isfinite(activity_hold_s) && activity_hold_s > 0 &&
              std::isfinite(activity_pause_s) && activity_pause_s >= activity_hold_s &&
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
            ma::rounded_hypot(b, 2 * std::sqrt(compliance) * std::sqrt(c));
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
  return advance_guarded(source, seconds, budget, {});
}
MacClosedAdvance MacGameKernelAccess::advance_guarded(
    const MacClosedWorld &source, double seconds, std::size_t budget,
    std::span<const std::uint8_t> liquid_blocked) {
  require_mac_energy_environment();
  source.settings_.validate();
  require(liquid_blocked.empty() ||
              liquid_blocked.size() == source.state().cells.size(),
          "Owner liquid boundary shape differs");
  require(std::all_of(liquid_blocked.begin(), liquid_blocked.end(),
                      [](std::uint8_t v) { return v <= 1; }),
          "Invalid owner liquid boundary mark");
  require(std::isfinite(seconds) && seconds > 0 && seconds <= 10 &&
              budget > 0 && budget <= 65536,
          "Invalid game interval/budget");
  struct Cache {
    std::shared_ptr<const FluidThermodynamics> eos;
    MacGrid grid;
    MacGameSettings settings;
    unsigned threads = 0;
    bool conduction = true, closure = false, trace_heat = false, bounded = false, sized = false, carried = false;
    std::unique_ptr<k::Tick> tick;
    std::unique_ptr<k::RegionalTick> regional;
    std::shared_ptr<k::RegionalExecutionHistory> regional_execution;
  };
  thread_local Cache cache;
  const auto &cfg = source.settings_.game;
  const auto options = mac_kernel_default_options();
  const unsigned threads = options.threads;
  // Profile 8 is profile 7 with momentum-carrying sharpening and layering
  // and a 1 mg floor on the net swept-volume bound.
  const bool activity = source.settings_.game_profile == MacGameProfile::game_kernel_activity_tiers;
  const bool carried = activity || source.settings_.game_profile == MacGameProfile::game_kernel_carried_momentum;
  const bool sized = carried || source.settings_.game_profile == MacGameProfile::game_kernel_sized_intervals;
  const bool bounded = sized || source.settings_.game_profile == MacGameProfile::game_kernel_bounded_pressure;
  const bool trace_heat = bounded ||
      source.settings_.game_profile == MacGameProfile::game_kernel_trace_heat;
  const bool closure =
      trace_heat ||
      source.settings_.game_profile == MacGameProfile::game_kernel_free_surface;
  if ((bounded ? !cache.regional : !cache.tick) || cache.eos != source.thermodynamics_ ||
      cache.threads != threads || cache.closure != closure ||
      cache.trace_heat != trace_heat || cache.bounded != bounded || cache.sized != sized || cache.carried != carried ||
      cache.conduction != source.settings_.thermal_conduction ||
      !same_grid(cache.grid, source.state().grid) ||
      !same_settings(cache.settings, cfg)) {
    const auto species = table(source.registry_, *source.thermodynamics_,
                               cfg.liquid_stiffness_ratio, cfg.liquid_law);
    std::vector<k::PhasePair> pairs;
    for (const auto &p : cfg.saturation_pairs)
      pairs.push_back({p.liquid, p.vapour, p.critical_temperature_k,
                       cfg.phase_rule == MacPhaseRule::boil_only});
    if(bounded) {
      if(!cache.regional_execution || cache.eos!=source.thermodynamics_ || cache.threads!=threads)
        cache.regional_execution=std::make_shared<k::RegionalExecutionHistory>();
      cache.regional=std::make_unique<k::RegionalTick>(source.state().grid,species,pairs,threads,cfg,source.settings_.thermal_conduction,sized,
                                                       carried,carried?1e-6:0.,cache.regional_execution);
      cache.tick.reset();
    } else {
    cache.tick = std::make_unique<k::Tick>(
        source.state().grid, species, pairs, threads, cfg.flow_parts,
        cfg.gas_heat_mixing, cfg.sharpen_liquid_interfaces,
        cfg.interface_detail_fraction, cfg.pressure_relative_tolerance, 0,
        false, 0, 2, cfg.phase_relaxation_time_s,
        source.settings_.thermal_conduction, cfg.pressure_maximum_iterations,
        cfg.heat_maximum_iterations, closure, trace_heat, bounded);
    cache.regional.reset();
    }
    cache.eos = source.thermodynamics_;
    cache.grid = source.state().grid;
    cache.settings = cfg;
    cache.threads = threads;
    cache.conduction = source.settings_.thermal_conduction;
    cache.closure = closure;
    cache.trace_heat = trace_heat;
    cache.bounded = bounded;
    cache.sized = sized;
    cache.carried = carried;
  }
  MacClosedAdvance result{source, {}};
  result.report.game_threads_used = bounded?cache.regional->threads_used():cache.tick->threads_used();
  auto& execution_pool=bounded?cache.regional->worker_pool():cache.tick->worker_pool();
  auto &stages = result.report.game_stages;
  const auto since = [](std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  };
  auto stamp = std::chrono::steady_clock::now();
  auto state = sparse(source.state(), &execution_pool);
  stages.sparse_ms = since(stamp);
  double elapsed = 0;
  MacEnergySum energy_residual, energy_bound;
  std::vector<k::Eos> last;
  k::Totals last_totals;
  std::vector<MacGameActivityCell> activity_state;
  std::vector<std::uint8_t> evolved;
  const k::ActivityRegions* activity_regions=nullptr;
  if(activity) {
    evolved.resize(source.state().cells.size());
    k::validate_activity(source.game_activity(),source.state().cells.size(),source.time_s_,source.state().grid.walls);
    k::validate_activity_interval(seconds,source.settings_.maximum_timestep_s,budget);
    activity_state.assign(source.game_activity().begin(),source.game_activity().end());
    activity_regions=&k::cached_activity_regions(source.state().grid);
  }
  result.report.game_converted_mass_kg.assign(source.registry_.size(), 0);
  std::size_t activity_intervals=0;
  while (elapsed < seconds) {
    if(activity && ++activity_intervals>budget)
      throw std::invalid_argument("Game activity interval exceeds its scheduling budget");
    require(result.report.attempts < budget,
            "Game interval exceeds its tick budget");
    const double dt =
        std::min(source.settings_.maximum_timestep_s, seconds - elapsed);
    const auto due=activity?k::activity_step(source.state().grid,*activity_regions,
        activity_state,cfg,source.time_s_+elapsed,dt):std::vector<double>{};
    if(activity)for(std::size_t i=0;i<due.size();++i)if(due[i]>0)evolved[i]=1;
    stamp = std::chrono::steady_clock::now();
    auto next = bounded?
        cache.regional->advance(state,dt,source.settings_.gravity_m_per_s2,
                               source.settings_.trace_sink_threshold_kg,liquid_blocked,budget-result.report.attempts,due):
        cache.tick->advance(state, dt, source.settings_.gravity_m_per_s2,
                            source.settings_.trace_sink_threshold_kg,
                            liquid_blocked, bounded ? budget-result.report.attempts : 65536);
    stages.tick_ms += since(stamp);
    stages.flow_ms += next.flow_ms;
    stages.eos_ms += next.eos_ms;
    stages.pressure_ms += next.pressure_ms;
    stages.transport_ms += next.transport_ms;
    stages.layering_ms += next.layering_ms;
    stages.interface_ms += next.interface_ms;
    stages.heat_ms += next.heat_ms;
    stages.phase_ms += next.phase_ms;
    stages.entropy_ms += next.entropy_ms;
    stages.audit_ms += next.trace_audit_ms;
    stages.pressure_iterations += next.pressure_iterations;
    stages.pressure_corrections += next.pressure_corrections;
    stages.pressure_attempts += next.pressure_attempts;
    stages.pressure_refinements += next.pressure_refinements;
    state = std::move(next.state);
    if(activity && !last.empty()) {
      for(std::size_t i=0;i<due.size();++i)if(due[i]>0)last[i]=next.eos[i];
    } else last = std::move(next.eos);
    last_totals = std::move(next.totals);
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
    result.report.attempts += bounded ? cache.regional->pressure_attempt_depth() : 1;
    result.report.rejected_attempts += bounded ? cache.regional->pressure_refinement_depth() : 0;
    result.report.substeps += bounded ? next.flow_substeps : cfg.flow_parts;
    if(!activity || std::any_of(due.begin(),due.end(),[](double value){return value>0;}))
      ++result.report.phase_change_stages;
    elapsed += dt;
  }
  stamp = std::chrono::steady_clock::now();
  result.world = source.derived(
      dense(source.state(), state, source.registry_.size(), last, nullptr, evolved));
  stages.dense_ms = since(stamp);
  result.world.game_totals_ =
      std::make_shared<const k::Totals>(std::move(last_totals));
  result.world.time_s_ = source.time_s_ + seconds;
  if(activity)result.world.game_activity_=
      std::make_shared<const std::vector<MacGameActivityCell>>(std::move(activity_state));
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
std::optional<MacGameAuditTotals> MacClosedWorld::game_audit_totals() const {
  if (!game_totals_)
    return std::nullopt;
  const auto value = [](const k::DoubleDouble &sum) {
    return static_cast<long double>(sum.hi) + sum.lo;
  };
  MacGameAuditTotals out;
  for (const auto &sum : game_totals_->species)
    out.species_kg.push_back(value(sum));
  out.energy_j = value(game_totals_->energy);
  return out;
}
MacTraceSink MacClosedWorld::with_trace_sink_in_cells(std::span<const std::size_t> cells) const {
  require(settings_.numerical_kernel == MacNumericalKernel::game,
          "Selected trace removal requires the game kernel");
  if (cells.empty()) return {*this, {}};
  std::vector<std::uint8_t> eligible(state().cells.size());
  std::size_t previous = 0;
  for (std::size_t k = 0; k < cells.size(); ++k) {
    const auto cell = cells[k];
    require(cell < eligible.size() && (k == 0 || cell > previous),
            "Selected trace cells must be sorted, unique and in range");
    eligible[cell] = 1; previous = cell;
  }
  return MacGameKernelAccess::trace(*this, eligible);
}
MacTraceSink MacGameKernelAccess::trace(const MacClosedWorld &world,
                                       std::span<const std::uint8_t> eligible) {
  require(eligible.empty() || eligible.size() == world.state().cells.size(),
          "Trace activity coverage differs");
  MacTraceSink out{world, {}};
  const double threshold = world.settings_.trace_sink_threshold_kg;
  if (threshold == 0)
    return out;
  bool found = false;
  for (std::size_t i = 0; i < world.state().cells.size(); ++i) {
    if (!eligible.empty() && !eligible[i]) continue;
    const auto &c = world.state().cells[i];
    if (!c.mixture.masses.implicit_zero())
      for (double m : c.mixture.masses)
        if (m > 0 && m < threshold)
          found = true;
  }
  if (!found)
    return out;
  const auto species = table(world.registry_, *world.thermodynamics_,
                             world.settings_.game.liquid_stiffness_ratio, world.settings_.game.liquid_law);
  const auto state = sparse(world.state());
  auto sunk = k::trace_sink(world.state().grid, species, state, threshold,
                            world.settings_.gravity_m_per_s2, nullptr, nullptr, nullptr, eligible);
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
      dense(world.state(), sunk.state, world.registry_.size(), readings, nullptr, eligible));
  fill_trace(out.report, sunk.mass, sunk.internal, sunk.kinetic, sunk.potential,
             sunk.jx, sunk.jy, sunk.passives);
  out.report.constituents = sunk.constituents;
  out.report.cells = sunk.cells;
  return out;
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
k::Eos transfer_reading(const MacClosedWorld &source,
                        const std::vector<k::Species> &species,
                        const MacMaterialCell &cell, double volume) {
  const auto e = k::evaluate(species, cell.mixture.masses, cell.mixture.energy,
                           volume);
  // The full reading also validates extended elastic branches of absent
  // liquids. Preserve those uncommon rejections without constructing its
  // unused arrays and entropy in the ordinary, low-strain transfer path.
  if (std::any_of(species.begin(), species.end(), [&](const auto &s) {
        return !s.gas && s.positive_liquid && e.pressure >= .5 * s.stiffness;
      }))
    (void)MacGameKernelAccess::reading(source, cell);
  return e;
}
void occupied(const MacClosedWorld &source, MacTransportState &state,
              std::span<const std::size_t> touched,
              const std::vector<k::Species> *species = nullptr) {
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
    if (species) {
      // Transfer batches already hold the same material table. The occupied
      // volume needs only the EOS, without presentation arrays or entropy.
      const auto &cell = state.cells[i];
      const auto e = transfer_reading(source, *species, cell,
                                      state.grid.volume_m3());
      state.occupied_volumes_m3[i] =
          e.nR > 0 ? state.grid.volume_m3() : e.liquid_volume;
    } else {
      const auto e = MacGameKernelAccess::reading(source, state.cells[i]);
      state.occupied_volumes_m3[i] =
          e.has_gas ? state.grid.volume_m3() : e.liquid_volume_m3;
    }
  }
}
// The tick's audit totals of a state (RegionAudit::capture), read from the
// dense cells: the same additions in the same fixed row blocks, merged in
// block order, without converting the state or building a kernel grid.
k::Totals dense_totals(const MacTransportState &state, std::size_t count,
                       double gravity) {
  const auto &grid = state.grid;
  constexpr auto none = std::numeric_limits<std::size_t>::max();
  std::vector<double> mass(state.cells.size());
  for (std::size_t i = 0; i < state.cells.size(); ++i) {
    const auto &cell = state.cells[i];
    if (!cell.mixture.masses.implicit_zero())
      for (const double m : static_cast<const std::vector<double> &>(cell.mixture.masses))
        if (m > 0)
          mass[i] += m;
  }
  const auto rows = mac_kernel::Grid::block_rows(grid.width);
  const auto merge = [](k::DoubleDouble &into, const k::DoubleDouble &part) {
    into.add(part.hi);
    into.add(part.lo);
  };
  k::Totals result;
  result.species.resize(count);
  for (std::size_t y0 = 0; y0 < grid.height; y0 += rows) {
    const auto y1 = std::min(grid.height, y0 + rows);
    k::Totals sum;
    sum.species.resize(count);
    for (std::size_t i = y0 * grid.width; i < y1 * grid.width; ++i) {
      const auto &cell = state.cells[i];
      if (!cell.mixture.masses.implicit_zero()) {
        const std::vector<double> &masses = cell.mixture.masses;
        for (std::size_t key = 0; key < masses.size(); ++key)
          if (masses[key] > 0)
            sum.species[key].add(masses[key]);
      }
      const double potential = mass[i] * gravity *
                               (static_cast<double>(i / grid.width) + .5) *
                               grid.spacing_m;
      sum.energy.add(cell.mixture.energy);
      sum.energy.add(potential);
      sum.energy_operands.add(std::abs(cell.mixture.energy));
      sum.energy_operands.add(std::abs(potential));
      for (const auto &q : cell.passive_quantities)
        sum.passives[q.identity].add(q.quantity);
    }
    // The block's x faces, then its y faces (the last block also takes the
    // top boundary row).
    const auto faces = [&](bool x, std::size_t first, std::size_t end) {
      const auto &j = x ? state.momentum_x : state.momentum_y;
      for (std::size_t f = first; f < end; ++f)
        if (j[f] != 0) {
          const auto [l, r] = endpoints(grid, x, f);
          const double inertia =
              .5 * ((l == none ? 0 : mass[l]) + (r == none ? 0 : mass[r]));
          if (!(inertia > 0))
            throw std::runtime_error("Private totals J without inertia");
          const double kinetic = .5 * j[f] * (j[f] / inertia);
          sum.energy.add(kinetic);
          sum.energy_operands.add(kinetic);
          (x ? sum.jx : sum.jy).add(j[f]);
          (x ? sum.jx_operands : sum.jy_operands).add(std::abs(j[f]));
        }
    };
    faces(true, y0 * grid.x_width(), y1 * grid.x_width());
    faces(false, y0 * grid.width,
          (y1 == grid.height ? grid.y_height() : y1) * grid.width);
    for (std::size_t key = 0; key < count; ++key)
      merge(result.species[key], sum.species[key]);
    for (auto [into, part] :
         {std::pair{&result.energy, &sum.energy}, std::pair{&result.jx, &sum.jx},
          std::pair{&result.jy, &sum.jy},
          std::pair{&result.energy_operands, &sum.energy_operands},
          std::pair{&result.jx_operands, &sum.jx_operands},
          std::pair{&result.jy_operands, &sum.jy_operands}})
      merge(*into, *part);
    for (const auto &[key, value] : sum.passives)
      merge(result.passives[key], value);
  }
  return result;
}
// The audit totals of a world: kept by the operation that produced it, or
// summed now.
std::shared_ptr<const k::Totals> totals_of(const MacClosedWorld &world,
                                           const k::Totals *kept) {
  if (kept)
    return {std::shared_ptr<const k::Totals>{}, kept};
  return std::make_shared<const k::Totals>(dense_totals(
      world.state(), world.registry().size(), world.settings().gravity_m_per_s2));
}
// A change of the audit sums.
struct TotalsChange {
  std::vector<k::DoubleDouble> species;
  k::DoubleDouble energy, energy_operands, jx, jy, jx_operands, jy_operands;
  std::map<std::uint64_t, k::DoubleDouble> passives;
};
// The original cells, face momenta and occupied volumes of an edit, each
// recorded before its first change: they audit the change and undo an edit
// made in place.
class EditJournal {
  std::vector<std::pair<std::size_t, MacMaterialCell>> cells_;
  std::unordered_map<std::size_t, std::size_t> cell_index_;
  std::map<std::pair<bool, std::size_t>, double> faces_;
  std::vector<std::pair<std::size_t, double>> volumes_;
  std::unordered_set<std::size_t> volume_seen_;
  std::optional<bool> volumes_empty_;

public:
  void cell(const MacTransportState &state, std::size_t i) {
    if (cell_index_.emplace(i, cells_.size()).second)
      cells_.emplace_back(i, state.cells[i]);
  }
  void face(const MacTransportState &state, bool x, std::size_t f) {
    faces_.try_emplace({x, f}, (x ? state.momentum_x : state.momentum_y)[f]);
  }
  void volumes(const MacTransportState &state,
               std::span<const std::size_t> touched) {
    if (!volumes_empty_)
      volumes_empty_ = state.occupied_volumes_m3.empty();
    if (*volumes_empty_)
      return;
    for (const auto i : touched)
      if (volume_seen_.insert(i).second)
        volumes_.emplace_back(i, state.occupied_volumes_m3[i]);
  }
  // A value before the edit: recorded, or never changed.
  const MacMaterialCell &before(const MacTransportState &state,
                                std::size_t i) const {
    const auto at = cell_index_.find(i);
    return at == cell_index_.end() ? state.cells[i] : cells_[at->second].second;
  }
  double before(const MacTransportState &state, bool x, std::size_t f) const {
    const auto at = faces_.find({x, f});
    return at == faces_.end() ? (x ? state.momentum_x : state.momentum_y)[f]
                              : at->second;
  }
  void undo(MacTransportState &state) const {
    for (const auto &[i, cell] : cells_)
      state.cells[i] = cell;
    for (const auto &[face, j] : faces_)
      (face.first ? state.momentum_x : state.momentum_y)[face.second] = j;
    if (volumes_empty_ && *volumes_empty_)
      state.occupied_volumes_m3.clear();
    else
      for (const auto &[i, v] : volumes_)
        state.occupied_volumes_m3[i] = v;
  }
};
// The change of the audit sums over the cells and faces an edit touched,
// from the journal's originals to `after`; every other cell and face is
// unchanged.
TotalsChange totals_change(const EditJournal &journal,
                           const MacTransportState &after, std::size_t count,
                           double gravity, std::vector<std::size_t> cells,
                           std::vector<std::pair<bool, std::size_t>> faces) {
  const auto &grid = after.grid;
  constexpr auto none = std::numeric_limits<std::size_t>::max();
  std::sort(cells.begin(), cells.end());
  cells.erase(std::unique(cells.begin(), cells.end()), cells.end());
  std::sort(faces.begin(), faces.end(), [](const auto &a, const auto &b) {
    return a.first != b.first ? a.first : a.second < b.second;
  });
  faces.erase(std::unique(faces.begin(), faces.end()), faces.end());
  TotalsChange change;
  change.species.resize(count);
  const auto mass_of = [](const MacMaterialCell &cell) {
    double total = 0;
    if (!cell.mixture.masses.implicit_zero())
      for (const double m : static_cast<const std::vector<double> &>(cell.mixture.masses))
        if (m > 0)
          total += m;
    return total;
  };
  const auto cell_of = [&](bool original, std::size_t i) -> const MacMaterialCell & {
    return original ? journal.before(after, i) : after.cells[i];
  };
  for (const auto i : cells)
    for (const auto &[original, sign] : {std::pair{true, -1.}, std::pair{false, 1.}}) {
      const auto &cell = cell_of(original, i);
      if (!cell.mixture.masses.implicit_zero()) {
        const std::vector<double> &masses = cell.mixture.masses;
        for (std::size_t key = 0; key < masses.size(); ++key)
          if (masses[key] > 0)
            change.species[key].add(sign * masses[key]);
      }
      const double potential = mass_of(cell) * gravity *
                               (static_cast<double>(i / grid.width) + .5) *
                               grid.spacing_m;
      change.energy.add(sign * cell.mixture.energy);
      change.energy.add(sign * potential);
      change.energy_operands.add(sign * std::abs(cell.mixture.energy));
      change.energy_operands.add(sign * std::abs(potential));
      for (const auto &q : cell.passive_quantities)
        change.passives[q.identity].add(sign * q.quantity);
    }
  for (const auto &[x, f] : faces)
    for (const auto &[original, sign] : {std::pair{true, -1.}, std::pair{false, 1.}}) {
      const double j = original ? journal.before(after, x, f)
                                : (x ? after.momentum_x : after.momentum_y)[f];
      if (j == 0)
        continue;
      const auto [l, r] = endpoints(grid, x, f);
      const double inertia = .5 * ((l == none ? 0 : mass_of(cell_of(original, l))) +
                                   (r == none ? 0 : mass_of(cell_of(original, r))));
      if (!(inertia > 0))
        throw std::runtime_error("Private totals J without inertia");
      const double kinetic = .5 * j * (j / inertia);
      change.energy.add(sign * kinetic);
      change.energy_operands.add(sign * kinetic);
      (x ? change.jx : change.jy).add(sign * j);
      (x ? change.jx_operands : change.jy_operands).add(sign * std::abs(j));
    }
  return change;
}
k::DoubleDouble plus(const k::DoubleDouble &a, const k::DoubleDouble &b) {
  auto sum = a;
  sum.add(b.hi);
  sum.add(b.lo);
  return sum;
}
// `before` plus the change: the audit totals of the edited state.
k::Totals changed_totals(const k::Totals &before, const TotalsChange &change) {
  k::Totals after;
  after.species.resize(before.species.size());
  for (std::size_t key = 0; key < after.species.size(); ++key)
    after.species[key] = plus(before.species[key], change.species[key]);
  after.energy = plus(before.energy, change.energy);
  after.energy_operands = plus(before.energy_operands, change.energy_operands);
  after.jx = plus(before.jx, change.jx);
  after.jy = plus(before.jy, change.jy);
  after.jx_operands = plus(before.jx_operands, change.jx_operands);
  after.jy_operands = plus(before.jy_operands, change.jy_operands);
  after.passives = before.passives;
  for (const auto &[id, value] : change.passives)
    after.passives[id] = plus(after.passives[id], value);
  return after;
}
// Every species, U+K+G, J and passive changes by exactly the booked parcels.
// The residual is the touched cells' and faces' change minus the parcels;
// the bound keeps its whole-world operands, before and after.
void audit_transfers(const k::Totals &old, const k::Totals &now,
                     const TotalsChange &change,
                     std::span<const MacFluidParcel> parcels) {
  const auto balance = [](const k::DoubleDouble &delta,
                          const k::DoubleDouble &a, const k::DoubleDouble &b) {
    k::Balance result;
    result.residual.add(-delta.hi);
    result.residual.add(-delta.lo);
    for (const double v : {a.hi, a.lo, b.hi, b.lo})
      result.operands.add(std::abs(v));
    return result;
  };
  std::vector<k::Balance> masses(old.species.size());
  for (std::size_t key = 0; key < masses.size(); ++key)
    masses[key] = balance(change.species[key], old.species[key], now.species[key]);
  k::Balance energy, x = balance(change.jx, old.jx, now.jx),
                     y = balance(change.jy, old.jy, now.jy);
  energy.residual.add(-change.energy.hi);
  energy.residual.add(-change.energy.lo);
  std::map<std::uint64_t, k::Balance> passives;
  for (const auto &[id, q] : now.passives) {
    const auto o = old.passives.find(id);
    const auto d = change.passives.find(id);
    passives[id] = balance(d == change.passives.end() ? k::DoubleDouble{} : d->second,
                           o == old.passives.end() ? k::DoubleDouble{} : o->second, q);
  }
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
} // namespace
namespace {
// Heat edits on `next`, the state of `source` or its copy, each cell recorded
// in the journal before it changes.
void apply_heat(const MacClosedWorld &source, MacTransportState &next,
                std::span<const MacCellHeat> edits, EditJournal &journal) {
  require(!edits.empty() && edits.size() <= 4096, "Invalid game heat batch");
  // Building the table pays off on batches; keep one-off/refused-edit
  // retries on the original cached reading path.
  const bool batch_reading = edits.size() >= 32;
  std::optional<std::vector<k::Species>> species;
  for (const auto &edit : edits) {
    require(edit.cell_index < next.cells.size() &&
                !next.grid.walls[edit.cell_index] &&
                std::isfinite(edit.joules) &&
                std::isfinite(edit.maximum_temperature_k) &&
                edit.maximum_temperature_k > 0,
            "Invalid game heat edit");
    journal.cell(next, edit.cell_index);
    auto &cell = next.cells[edit.cell_index];
    const double old = cell.mixture.energy;
    cell.mixture.energy += edit.joules;
    require(edit.joules == 0 || cell.mixture.energy != old,
            "Game heat change is unrepresentable");
    if (batch_reading && !species)
      species.emplace(table(source.registry(), source.thermodynamics(),
                            source.settings().game.liquid_stiffness_ratio,
                            source.settings().game.liquid_law));
    const double temperature = batch_reading
        ? transfer_reading(source, *species, cell, next.grid.volume_m3()).temperature
        : MacGameKernelAccess::reading(source, cell).temperature_k;
    require(edit.joules <= 0 || temperature <= edit.maximum_temperature_k,
            "Game heat exceeds reserved temperature limit");
  }
}
// A world's audit totals after a heat edit, when it had them. Only U changes.
std::shared_ptr<const k::Totals>
heated_totals(const k::Totals *kept, const EditJournal &journal,
              const MacTransportState &next, std::span<const MacCellHeat> edits,
              std::size_t count, double gravity) {
  if (!kept)
    return {};
  std::vector<std::size_t> cells;
  for (const auto &edit : edits)
    cells.push_back(edit.cell_index);
  return std::make_shared<const k::Totals>(changed_totals(
      *kept, totals_change(journal, next, count, gravity, std::move(cells), {})));
}
// The canonical void-phase volumes of a world's state (MacClosedWorld's
// derived constructor): none unless an occupied cell is partly filled.
void canonical_volumes(MacTransportState &state) {
  bool partial = false;
  for (std::size_t i = 0; i < state.occupied_volumes_m3.size() && !partial; ++i)
    partial = state.cells[i].mixture.mass() > 0 &&
              state.occupied_volumes_m3[i] < state.grid.volume_m3();
  if (!partial)
    state.occupied_volumes_m3.clear();
}
struct TransferWork {
  std::vector<MacFluidParcel> parcels;
  std::vector<std::size_t> cells;
  std::vector<std::pair<bool, std::size_t>> faces;
};
// Transfers on `next`, the state of `source` or its copy, each cell, face and
// occupied volume recorded in the journal before it changes.
TransferWork apply_transfers(const MacClosedWorld &source,
                             MacTransportState &next,
                             std::span<const MacFluidTransfer> edits,
                             MacFluidTransferTopology topology,
                             const std::vector<k::Species> &species,
                             EditJournal &journal) {
  require(!edits.empty() && edits.size() <= 4096,
          "Invalid game transfer batch");
  require(topology == MacFluidTransferTopology::single_phase ||
              topology == MacFluidTransferTopology::occupied_mixture,
          "Invalid game transfer topology");
  TransferWork work;
  work.parcels.reserve(edits.size());
  std::vector<std::size_t> touched;
  const auto reading = [&](const MacMaterialCell &cell) {
    return transfer_reading(source, species, cell, next.grid.volume_m3());
  };
  for (const auto &edit : edits) {
    touched.clear();
    require(edit.cell_index < next.cells.size() &&
                !next.grid.walls[edit.cell_index] &&
                edit.species_index < species.size() &&
                std::isfinite(edit.mass_kg) && edit.mass_kg >= 0 &&
                std::isfinite(edit.temperature_k) &&
                (!edit.inject || edit.temperature_k > 0),
            "Invalid game transfer request");
    journal.cell(next, edit.cell_index);
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
    const auto before = reading(cell);
    MacFluidParcel p{};
    p.cell_index = edit.cell_index;
    p.species_index = edit.species_index;
    p.injected = edit.inject;
    p.temperature_k = edit.inject ? edit.temperature_k : before.temperature;
    if (edit.observe_passives)
      p.passives_before = cell.passive_quantities;
    if (edit.mass_kg == 0 ||
        (!edit.inject && cell.mixture.masses[edit.species_index] == 0)) {
      if (edit.observe_passives)
        p.passives_after = cell.passive_quantities;
      work.parcels.push_back(std::move(p));
      continue;
    }
    const double old_species = cell.mixture.masses[edit.species_index],
                 old_mass = cell.mixture.mass();
    const double requested =
        edit.inject ? edit.mass_kg : std::min(edit.mass_kg, old_species);
    double new_species =
        edit.inject ? old_species + requested : old_species - requested;
    // An injection never gives the cell more than its source gave: when the
    // sum rounds up (its exact two-sum error is negative), the cell keeps the
    // representable value below it and the source keeps the sub-ulp rest (a
    // pipe port retains it exactly). An extraction's loss is always exact.
    if (edit.inject && std::isfinite(new_species)) {
      const double back = new_species - old_species;
      const double error =
          (old_species - (new_species - back)) + (requested - back);
      if (error < 0)
        new_species = std::nextafter(new_species, old_species);
    }
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
                      : k::liquid_specific_elastic(material,before.pressure));
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
        journal.face(next, x, f);
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
              journal.cell(next, i);
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
            .multiplied(source.settings().gravity_m_per_s2)
            .multiplied(
                (static_cast<double>(edit.cell_index / next.grid.width) + .5) *
                next.grid.spacing_m);
    if (edit.observe_passives)
      p.passives_after = cell.passive_quantities;
    journal.volumes(next, touched);
    occupied(source, next, touched, &species);
    work.cells.insert(work.cells.end(), touched.begin(), touched.end());
    for (bool x : {true, false})
      for (const auto f : cell_faces(next.grid, edit.cell_index, x))
        work.faces.emplace_back(x, f);
    work.parcels.push_back(std::move(p));
  }
  return work;
}
// The audited result world of a transfer edit.
std::shared_ptr<const k::Totals>
transferred_totals(const MacClosedWorld &source, const k::Totals &old_totals,
                   const EditJournal &journal, const MacTransportState &next,
                   TransferWork &work) {
  const auto change = totals_change(journal, next, source.registry().size(),
                                    source.settings().gravity_m_per_s2,
                                    std::move(work.cells), std::move(work.faces));
  auto now = std::make_shared<const k::Totals>(changed_totals(old_totals, change));
  audit_transfers(old_totals, *now, change, work.parcels);
  return now;
}
} // namespace
bool MacGameKernelAccess::detach_heat_state(MacClosedWorld &source) {
  if (source.settings_.numerical_kernel != MacNumericalKernel::game ||
      source.state_snapshot_.use_count() == 1)
    return false;
  // Complete the copy before assigning its pointer: failure leaves every
  // source field and the immutable Session/Published state untouched.
  // This must be mutable storage, matching replace_state(), since the
  // unique heat path subsequently edits it through the existing journal.
  auto detached = std::make_shared<MacTransportState>(*source.state_snapshot_);
  source.state_snapshot_ = std::move(detached);
  return true;
}
MacClosedWorld MacGameKernelAccess::heat(const MacClosedWorld &source,
                                         std::span<const MacCellHeat> edits) {
  auto next = source.state();
  EditJournal journal;
  apply_heat(source, next, edits, journal);
  auto now = heated_totals(source.game_totals_.get(), journal, next, edits,
                           source.registry_.size(),
                           source.settings_.gravity_m_per_s2);
  auto result = source.derived(std::move(next));
  result.game_totals_ = std::move(now);
  return result;
}
MacClosedWorld MacGameKernelAccess::heat(MacClosedWorld &&source,
                                         std::span<const MacCellHeat> edits) {
  if (source.state_snapshot_.use_count() != 1)
    return heat(std::as_const(source), edits);
  // No other world shares this state: edit it in place, undoing on failure.
  auto &next = const_cast<MacTransportState &>(*source.state_snapshot_);
  EditJournal journal;
  try {
    apply_heat(source, next, edits, journal);
    auto now = heated_totals(source.game_totals_.get(), journal, next, edits,
                             source.registry_.size(),
                             source.settings_.gravity_m_per_s2);
    MacClosedWorld result(MacClosedWorld::Derived{}, source,
                          source.state_snapshot_);
    canonical_volumes(next);
    source.state_snapshot_.reset(); // the result now owns the state
    result.game_totals_ = std::move(now);
    return result;
  } catch (...) {
    journal.undo(next);
    throw;
  }
}
MacFluidTransferResult
MacGameKernelAccess::transfers(const MacClosedWorld &source,
                               std::span<const MacFluidTransfer> edits,
                               MacFluidTransferTopology topology) {
  const auto species = table(source.registry_, *source.thermodynamics_,
                             source.settings_.game.liquid_stiffness_ratio, source.settings_.game.liquid_law);
  const auto old_totals = totals_of(source, source.game_totals_.get());
  auto next = source.state();
  EditJournal journal;
  auto work = apply_transfers(source, next, edits, topology, species, journal);
  auto now = transferred_totals(source, *old_totals, journal,
                                next, work);
  MacFluidTransferResult result{source.derived(std::move(next)),
                                std::move(work.parcels), 0, {}};
  result.world.game_totals_ = std::move(now);
  return result;
}
MacFluidTransferResult
MacGameKernelAccess::transfers(MacClosedWorld &&source,
                               std::span<const MacFluidTransfer> edits,
                               MacFluidTransferTopology topology) {
  if (source.state_snapshot_.use_count() != 1)
    return transfers(std::as_const(source), edits, topology);
  // No other world shares this state: edit it in place, undoing on failure.
  const auto species = table(source.registry_, *source.thermodynamics_,
                             source.settings_.game.liquid_stiffness_ratio, source.settings_.game.liquid_law);
  // The opening sum must precede mutation when a prior edit or load left
  // no cache; summing this shared state afterwards would book the delta twice.
  const auto old_totals = totals_of(source, source.game_totals_.get());
  auto &next = const_cast<MacTransportState &>(*source.state_snapshot_);
  EditJournal journal;
  try {
    auto work = apply_transfers(source, next, edits, topology, species, journal);
    auto now = transferred_totals(source, *old_totals, journal,
                                  next, work);
    MacFluidTransferResult result{
        MacClosedWorld(MacClosedWorld::Derived{}, source, source.state_snapshot_),
        std::move(work.parcels), 0, {}};
    canonical_volumes(next);
    source.state_snapshot_.reset(); // the result now owns the state
    result.world.game_totals_ = std::move(now);
    return result;
  } catch (...) {
    journal.undo(next);
    throw;
  }
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
