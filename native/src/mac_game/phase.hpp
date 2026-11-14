#pragma once
#include "flow_local.hpp"
#include <optional>
#include <span>
namespace ma::experimental::game_kernel {
// boil_only (saved profile6 rule, MAC_PHASE_CHANGE.md): compare saturation
// with the cell's total gas pressure instead of the vapour's partial
// pressure. The total is the sum of Dalton partials (nR T / V_gas), so a cell
// whose only gas is the vapour gives exactly the equilibrium rule's value.
struct PhasePair {
  std::size_t liquid = 0, vapour = 0;
  double critical = 1e300;
  bool boil_only = false;
};
struct PhaseResult {
  SparseMass masses;
  double evaporated = 0, condensed = 0, entropy_change = 0, mass_error = 0;
  std::size_t changed_cells = 0, trials = 0;
};
// Profile 12 boiling with displacement (MAC_GAME_KERNEL.md, "Boiling in
// liquid-full cells"): a pair liquid boiling in a cell with too little room
// for its vapour converts during the flow intervals of a tick, its vapour
// volume pushing the liquid out within the pressure solve. One entry per
// cell and pair, ascending by cell then pair.
struct BoilPlan {
  std::size_t cell = 0, pair = 0;
  // Liquid to convert over the whole tick (kg) and the volume its vapour
  // adds at the cell's pressure (m3/kg).
  double mass = 0, volume_per_kg = 0;
};
struct BoilResult {
  std::vector<std::size_t> cells; // changed, ascending
  // Each changed cell's constituents before the conversion, for the region
  // audit (RegionAudit::convert over these cells alone).
  std::vector<std::vector<std::pair<std::uint32_t, double>>> before;
  double evaporated = 0, entropy_change = 0;
};
inline double saturation_pressure(const Species &liquid, const Species &vapour,
                                  double temperature, double pressure) {
  constexpr double Tref = 298.15;
  const double r = R / vapour.molar_mass;
  const double gl =
      liquid.offset + liquid.cv * temperature +
      liquid_specific_elastic(liquid,pressure) +
      pressure / liquid.rho0 * liquid_volume_fraction(liquid,pressure) -
      temperature *
          (liquid.cv * std::log(temperature / Tref) + liquid.entropy_offset);
  const double gv = vapour.offset + vapour.cv * temperature + r * temperature -
                    temperature * (vapour.cv * std::log(temperature / Tref) +
                                   vapour.entropy_offset);
  return R * temperature * std::exp(-(gv - gl) / (r * temperature));
}
inline double entropy(const std::vector<Species> &species,
                      const std::vector<double> &masses, const Eos &eos) {
  if (eos.mass == 0)
    return 0;
  double value = 0;
  for (std::size_t k = 0; k < species.size(); ++k)
    if (masses[k] > 0) {
      const auto &s = species[k];
      double specific =
          s.cv * std::log(eos.temperature / 298.15) + s.entropy_offset;
      if (s.gas) {
        const double partial =
            masses[k] * R / s.molar_mass * eos.temperature / eos.gas_volume;
        specific += R / s.molar_mass * std::log(R * eos.temperature / partial);
      }
      value += masses[k] * specific;
    }
  return value;
}
class PhaseSolver {
  std::vector<Species> species;
  std::vector<PhasePair> pairs;
  ma::experimental::MacGrid grid;
  std::unique_ptr<ma::experimental::mac_kernel::Pool> owned_pool;
  ma::experimental::mac_kernel::Pool &pool;
  // plan_boiling's lookups, made on first use: the pairs of each liquid
  // species, and each pair's boiling temperature at one atmosphere (a liquid
  // at or above that pressure does not boil below it).
  std::vector<std::vector<std::size_t>> pairs_of_liquid;
  std::vector<double> boiling_at_atmosphere;

public:
  PhaseSolver(const ma::experimental::MacGrid &source,
              std::vector<Species> table, std::vector<PhasePair> phase_pairs,
              unsigned threads,
              ma::experimental::mac_kernel::Pool *executor = nullptr)
      : species(std::move(table)), pairs(std::move(phase_pairs)), grid(source),
        owned_pool(
            executor
                ? nullptr
                : std::make_unique<ma::experimental::mac_kernel::Pool>(std::min(
                      threads,
                      static_cast<unsigned>(
                          (source.width * source.height + 127) / 128)))),
        pool(executor ? *executor : *owned_pool) {}
  // The temperature at which `pair`'s liquid boils at `pressure`
  // (saturation_pressure(T, pressure) == pressure), below `temperature`;
  // 0 if it does not boil there.
  double boiling_temperature(const PhasePair &pair, double temperature,
                             double pressure) const {
    const auto &liquid = species[pair.liquid];
    const auto &vapour = species[pair.vapour];
    const auto above = [&](double t) {
      return saturation_pressure(liquid, vapour, t, pressure) > pressure;
    };
    if (!above(temperature))
      return 0;
    double low = .5 * temperature, high = temperature;
    for (unsigned k = 0; k < 8 && above(low); ++k)
      low *= .5;
    if (above(low))
      return 0;
    for (unsigned iter = 0; iter < 60; ++iter) {
      const double mid = .5 * (low + high);
      if (mid == low || mid == high)
        break;
      (above(mid) ? high : low) = mid;
    }
    return high;
  }
  // Profile 12: liquids that boil at their cell's pressure in a cell
  // without room for the vapour. The isobaric amount is the heat above the
  // boiling point at the cell's pressure over the latent heat (as in a
  // flash), relaxed over the tick like the rest of the phase change, and
  // limited so the vapour adds at most the 0.4-cell transport scale per
  // flow part. Only a liquid body boils this way: a cell its liquid fills
  // at least half (as the game shows a cell's liquid) whose pressure is at
  // least the weight of its contents (`weight_per_kg` times its mass), so
  // that a column or atmosphere holds it. Elsewhere, and in a cell whose gas
  // and void already hold the vapour, the relaxation at the tick's end boils
  // in place up to the saturation pressure and the flow carries the vapour
  // away: liquid in a mostly gas cell evaporates into that gas, and in a
  // cell whose own thin vapour or void sets its pressure the vapour volume
  // at that near-vacuum pressure would push out far more than boils.
  // The vapour a plan may add in one flow part, as a share of the cell: the
  // 0.4-cell transport scale until 2026-11-13, when a pool a few kelvin
  // above its boiling point threw 800 kg of each surface cell's water into
  // the air every tick and the falling water hammered its floor to a
  // megapascal. A liquid that is most of its cell's liquid (a pool boiling)
  // now adds 0.03 cell a flow part, so that such a pool boils its superheat
  // off over tens of seconds and stays in place. A liquid that is the
  // lesser part of its cell's liquid (liquid carbon dioxide let into warm
  // water, which holds the heat to flash all of it) keeps the 0.4 cell and
  // is gone within seconds.
  static constexpr double planned_vapour_share = .03;
  // `surface` and `gravity` (profile 12, 2026-11-13): the kernel grid, for
  // a cell's neighbour against gravity across an open face. A cell its
  // liquid fills has its surface on that face when the neighbour holds gas,
  // and boils there as an interface cell does, without the nucleation
  // superheat: a pool whose top row was exactly full could not boil at all
  // until it was 5 K above its boiling point, and then flashed that whole
  // superheat at once.
  std::vector<BoilPlan> plan_boiling(const State &state,
                                     const std::vector<Eos> &eos, double dt,
                                     double relaxation_time,
                                     unsigned flow_parts,
                                     double weight_per_kg,
                                     const ma::experimental::mac_kernel::Grid *surface = nullptr,
                                     double gravity = 0) {
    if (pairs_of_liquid.size() != species.size()) {
      pairs_of_liquid.assign(species.size(), {});
      boiling_at_atmosphere.assign(pairs.size(), 0);
      for (std::size_t p = 0; p < pairs.size(); ++p) {
        pairs_of_liquid[pairs[p].liquid].push_back(p);
        const double top = std::min(pairs[p].critical, 1e4);
        const double boiling = boiling_temperature(pairs[p], top, 101325);
        boiling_at_atmosphere[p] = boiling > 0 ? boiling : top;
      }
    }
    const double fraction = -std::expm1(-dt / relaxation_time);
    const double V = grid.volume_m3();
    const auto N = state.masses.cells();
    const auto blocks = (N + 127) / 128;
    std::vector<std::vector<BoilPlan>> found(blocks);
    pool.run(blocks, [&](std::size_t block) {
      for (std::size_t i = block * 128; i < std::min(N, (block + 1) * 128); ++i) {
        if (grid.walls[i] || state.masses.offsets[i] == state.masses.offsets[i + 1])
          continue;
        const auto &e = eos[i];
        if (!(e.pressure > 0) || !(e.liquid_volume >= .5 * V) ||
            e.pressure < e.mass * weight_per_kg)
          continue;
        bool interface = e.gas_volume > 1e-6 * V;
        if (!interface && surface && gravity != 0) {
          const auto f = gravity > 0 ? surface->top_face(i) : surface->bottom_face(i);
          const auto up = gravity > 0 ? surface->yr[f] : surface->yl[f];
          interface = up != ma::experimental::mac_kernel::none && !surface->wall[up] &&
                      !surface->closed(true, f) && eos[up].gas_volume > 1e-3 * V;
        }
        double liquid_mass = 0;
        for (auto a = state.masses.offsets[i]; a < state.masses.offsets[i + 1]; ++a)
          if (!species[state.masses.species[a]].gas)
            liquid_mass += state.masses.mass[a];
        for (auto a = state.masses.offsets[i]; a < state.masses.offsets[i + 1]; ++a) {
          const auto key = state.masses.species[a];
          const double ml = state.masses.mass[a];
          if (species[key].gas || !(ml > 0))
            continue;
          for (const auto p : pairs_of_liquid[key]) {
            const auto &pair = pairs[p];
            if (e.temperature >= pair.critical ||
                (e.pressure >= 101325 && e.temperature <= boiling_at_atmosphere[p]))
              continue;
            const auto &liquid = species[pair.liquid];
            const auto &vapour = species[pair.vapour];
            // Boiling at the cell's pressure; a gas-free liquid also needs the
            // nucleation superheat, as in advance.
            if (!(saturation_pressure(liquid, vapour, e.temperature, e.pressure) > e.pressure) ||
                (!interface && !(saturation_pressure(liquid, vapour, e.temperature - 5,
                                                     e.pressure) > e.pressure)))
              continue;
            const double boiling = boiling_temperature(pair, e.temperature, e.pressure);
            if (!(boiling > 0) || !(boiling < e.temperature))
              continue;
            const double r = R / vapour.molar_mass;
            const double latent =
                vapour.offset + (vapour.cv + r) * e.temperature -
                (liquid.offset + liquid.cv * e.temperature +
                 e.pressure / liquid.rho0 * liquid_volume_fraction(liquid, e.pressure));
            const double volume_per_kg =
                r * e.temperature / e.pressure -
                liquid_volume_fraction(liquid, e.pressure) / liquid.rho0;
            if (!(latent > 0) || !(volume_per_kg > 0) || !std::isfinite(volume_per_kg))
              continue;
            const double flash = std::min(
                ml, (e.capacity + e.nR) * (e.temperature - boiling) / latent);
            double mass = fraction * flash;
            if (!(mass > 0) || mass * volume_per_kg <= std::max(0., V - e.liquid_volume))
              continue;
            const double share =
                !surface || ml < .5 * liquid_mass ? .4 : planned_vapour_share;
            mass = std::min(mass, share * V * flow_parts / volume_per_kg);
            found[block].push_back({i, p, mass, volume_per_kg});
          }
        }
      }
    });
    std::vector<BoilPlan> plans;
    for (auto &part : found) {
      std::sort(part.begin(), part.end(), [](const BoilPlan &a, const BoilPlan &b) {
        return a.cell != b.cell ? a.cell < b.cell : a.pair < b.pair;
      });
      plans.insert(plans.end(), part.begin(), part.end());
    }
    return plans;
  }
  // Converts `share` of each plan's mass in `state` (after a flow
  // interval), at fixed U, while the pair liquid lasts, no more than the
  // heat above its boiling point at the cell's pressure boils, and the
  // cell's gas pressure stays at or below saturation. The cell's pressure
  // is the interval's solved one (`solved`), or its reading if higher: the
  // interval has just pushed the cell's own material out, so its reading
  // alone would not show a sealed space filling up. Boiling into a sealed
  // space thereby stops once that space reaches the saturation pressure.
  // A plan keeps pushing its share out for the whole tick: a cell whose
  // conversion falls short refills from its neighbours in the next
  // interval, and converts again once its pressure falls. Updates `eos`
  // for changed cells.
  BoilResult boil_planned(State &state, std::vector<Eos> &eos,
                          std::span<const BoilPlan> plans, double share,
                          std::span<const double> solved) const {
    BoilResult result;
    if (plans.empty())
      return result;
    const double V = grid.volume_m3();
    std::vector<std::pair<std::size_t, std::vector<std::pair<std::uint32_t, double>>>> changed;
    std::vector<double> masses(species.size());
    std::vector<std::uint32_t> keys;
    for (std::size_t at = 0; at < plans.size();) {
      const auto i = plans[at].cell;
      std::fill(masses.begin(), masses.end(), 0);
      keys.assign(state.masses.species.begin() + state.masses.offsets[i],
                  state.masses.species.begin() + state.masses.offsets[i + 1]);
      for (auto a = state.masses.offsets[i]; a < state.masses.offsets[i + 1]; ++a)
        masses[state.masses.species[a]] = state.masses.mass[a];
      const auto include = [&](std::size_t key) {
        const auto pos = std::lower_bound(keys.begin(), keys.end(), key);
        if (pos == keys.end() || *pos != key)
          keys.insert(pos, static_cast<std::uint32_t>(key));
      };
      const auto read = [&](Eos &out) {
        return try_evaluate_selected(
            out, species, keys.size(), [&](std::size_t a) { return keys[a]; },
            [&](std::size_t a) { return masses[keys[a]]; }, state.energy[i], V);
      };
      Eos before;
      if (read(before)) {
        while (at < plans.size() && plans[at].cell == i)
          ++at;
        continue;
      }
      const double initial_entropy = entropy(species, masses, before);
      const double around = std::max(before.pressure, solved.size() > i ? solved[i] : 0.);
      bool any = false;
      for (; at < plans.size() && plans[at].cell == i; ++at) {
        const auto &pair = pairs[plans[at].pair];
        const double ml = masses[pair.liquid], mv = masses[pair.vapour];
        const auto &liquid = species[pair.liquid];
        const auto &vapour = species[pair.vapour];
        const double boiling = boiling_temperature(pair, before.temperature, around);
        const double latent =
            vapour.offset + (vapour.cv + R / vapour.molar_mass) * before.temperature -
            (liquid.offset + liquid.cv * before.temperature +
             around / liquid.rho0 * liquid_volume_fraction(liquid, around));
        if (!(boiling > 0) || !(boiling < before.temperature) || !(latent > 0))
          continue;
        const double planned = plans[at].mass * share;
        const double target = std::min(
            {ml, planned,
             (before.capacity + before.nR) * (before.temperature - boiling) / latent});
        if (!(target > 0))
          continue;
        include(pair.liquid);
        include(pair.vapour);
        // Gas pressure minus saturation after moving `moved` (the
        // relaxation's departure); nullopt where the EOS refuses it.
        const auto departure = [&](double moved) -> std::optional<double> {
          masses[pair.liquid] = ml - moved;
          masses[pair.vapour] = mv + moved;
          Eos trial;
          if (read(trial))
            return std::nullopt;
          const double pv =
              trial.gas_volume > 0
                  ? (pair.boil_only ? trial.nR : masses[pair.vapour] * R / vapour.molar_mass) *
                        trial.temperature / trial.gas_volume
                  : trial.pressure;
          const double d = pv - saturation_pressure(liquid, vapour, trial.temperature, trial.pressure);
          if (!std::isfinite(d))
            return std::nullopt;
          return d;
        };
        double near = 0, far = target;
        const auto end = departure(far);
        if (end && *end <= 0)
          near = far;
        else
          for (unsigned iter = 0; iter < 32; ++iter) {
            const double mid = .5 * (near + far);
            if (mid == near || mid == far)
              break;
            const auto d = departure(mid);
            if (d && *d < 0)
              near = mid;
            else
              far = mid;
          }
        masses[pair.liquid] = ml - near;
        masses[pair.vapour] = mv + near;
        if (near > 0) {
          any = true;
          result.evaporated += near;
        }
      }
      if (!any)
        continue;
      Eos after;
      if (read(after))
        throw std::runtime_error("Private boiling left an unreadable cell");
      result.entropy_change += entropy(species, masses, after) - initial_entropy;
      auto &entry = changed.emplace_back(i, std::vector<std::pair<std::uint32_t, double>>{});
      for (const auto key : keys)
        if (masses[key] > 0)
          entry.second.emplace_back(key, masses[key]);
      eos[i] = after;
    }
    if (changed.empty())
      return result;
    result.before.reserve(changed.size());
    bool same_keys = true;
    for (const auto &[i, list] : changed) {
      auto &before = result.before.emplace_back();
      const auto first = state.masses.offsets[i], last = state.masses.offsets[i + 1];
      before.reserve(last - first);
      for (auto a = first; a < last; ++a)
        before.emplace_back(state.masses.species[a], state.masses.mass[a]);
      same_keys = same_keys && list.size() == last - first;
      for (std::size_t n = 0; same_keys && n < list.size(); ++n)
        same_keys = list[n].first == state.masses.species[first + n];
    }
    // A boil that keeps every changed cell's species (the usual case: the
    // cell already holds its vapour) writes the new masses in place; a plan
    // then costs its own cells, not a copy of the grid's inventories each
    // interval.
    if (same_keys) {
      for (const auto &[i, list] : changed) {
        const auto first = state.masses.offsets[i];
        for (std::size_t n = 0; n < list.size(); ++n)
          state.masses.mass[first + n] = list[n].second;
        result.cells.push_back(i);
      }
      return result;
    }
    // Rebuild the sparse inventories with the changed cells' new lists.
    SparseMass next;
    next.offsets.reserve(state.masses.offsets.size());
    next.offsets.push_back(0);
    std::size_t c = 0;
    for (std::size_t i = 0; i + 1 < state.masses.offsets.size(); ++i) {
      if (c < changed.size() && changed[c].first == i) {
        for (const auto &[key, value] : changed[c].second) {
          next.species.push_back(key);
          next.mass.push_back(value);
        }
        result.cells.push_back(i);
        ++c;
      } else
        for (auto a = state.masses.offsets[i]; a < state.masses.offsets[i + 1]; ++a) {
          next.species.push_back(state.masses.species[a]);
          next.mass.push_back(state.masses.mass[a]);
        }
      next.offsets.push_back(next.mass.size());
    }
    state.masses = std::move(next);
    return result;
  }
  PhaseResult advance(const State &state, double dt,
                      double relaxation_time = 1,
                      std::span<const BoilPlan> planned = {}) {
    const auto N = state.masses.cells();
    std::vector<std::vector<std::pair<std::uint32_t, double>>> cells(N);
    struct Receipt {
      double evaporated = 0, condensed = 0, entropy = 0, mass_error = 0;
      std::size_t trials = 0;
      bool changed = false;
    };
    std::vector<Receipt> receipts(N);
    const double fraction = -std::expm1(-dt / relaxation_time);
    const auto blocks = (N + 127) / 128;
    pool.run(blocks, [&](std::size_t block) {
      std::vector<double> masses(species.size());
      // The cell's constituents and the species of every pair it has
      // entered, in ascending order. Every other mass stays zero, which the
      // EOS skips, so reading these keys repeats the full-table reading.
      std::vector<std::uint32_t> keys;
      for (std::size_t i = block * 128; i < std::min(N, (block + 1) * 128);
           ++i) {
        if (grid.walls[i] ||
            state.masses.offsets[i] == state.masses.offsets[i + 1])
          continue;
        std::fill(masses.begin(), masses.end(), 0);
        keys.assign(state.masses.species.begin() + state.masses.offsets[i],
                    state.masses.species.begin() + state.masses.offsets[i + 1]);
        for (std::size_t a = state.masses.offsets[i];
             a < state.masses.offsets[i + 1]; ++a)
          masses[state.masses.species[a]] = state.masses.mass[a];
        const auto try_reading = [&](Eos &out) {
          return try_evaluate_selected(
              out, species, keys.size(), [&](std::size_t a) { return keys[a]; },
              [&](std::size_t a) { return masses[keys[a]]; }, state.energy[i],
              grid.volume_m3());
        };
        const auto reading = [&] {
          Eos out;
          if (const auto error = try_reading(out))
            throw std::runtime_error(error);
          return out;
        };
        const auto include = [&](std::size_t key) {
          const auto at = std::lower_bound(keys.begin(), keys.end(), key);
          if (at == keys.end() || *at != key)
            keys.insert(at, static_cast<std::uint32_t>(key));
        };
        auto before = reading();
        double initial_entropy = 0;
        auto &receipt = receipts[i];
        // Pairs this tick's flow intervals boiled (boil_planned) do not
        // evaporate again here.
        const auto plans_from = std::lower_bound(
            planned.begin(), planned.end(), i,
            [](const BoilPlan &plan, std::size_t cell) { return plan.cell < cell; });
        for (std::size_t index = 0; index < pairs.size(); ++index) {
          const auto &pair = pairs[index];
          const double ml = masses[pair.liquid], mv = masses[pair.vapour];
          if ((ml == 0 && mv == 0) ||
              (ml == 0 && before.temperature >= pair.critical))
            continue;
          include(pair.liquid);
          include(pair.vapour);
          const auto &liquid = species[pair.liquid];
          const auto &vapour = species[pair.vapour];
          const auto departure = [&](double moved) -> std::optional<double> {
            masses[pair.liquid] = ml - moved;
            masses[pair.vapour] = mv + moved;
            ++receipt.trials;
            try {
              Eos trial;
              if (moved != 0 && try_reading(trial))
                return std::nullopt;
              const auto &eos = moved == 0 ? before : trial;
              const double pv =
                  eos.gas_volume > 0
                      ? (pair.boil_only ? eos.nR : masses[pair.vapour] * R /
                                                       vapour.molar_mass) *
                            eos.temperature / eos.gas_volume
                      : eos.pressure;
              const double ps = saturation_pressure(
                  liquid, vapour, eos.temperature, eos.pressure);
              if (!std::isfinite(pv - ps))
                return std::nullopt;
              return pv - ps;
            } catch (const std::runtime_error &) {
              return std::nullopt;
            }
          };
          const auto start = departure(0);
          if (!start || *start == 0)
            continue;
          const bool evaporation = *start < 0;
          bool boiled = false;
          for (auto plan = plans_from; plan != planned.end() && plan->cell == i; ++plan)
            boiled = boiled || plan->pair == index;
          if ((evaporation && (ml == 0 || boiled)) ||
              (!evaporation &&
               (mv == 0 || before.temperature >= pair.critical)))
            continue;
          const double saturation = saturation_pressure(
              liquid, vapour, before.temperature, before.pressure);
          if (std::abs(*start) <= 1e-3 * saturation)
            continue;
          const bool interface = before.gas_volume > 1e-6 * grid.volume_m3();
          if (evaporation && !interface &&
              !(saturation_pressure(liquid, vapour, before.temperature - 5,
                                    before.pressure) > before.pressure))
            continue;
          double near = 0, far = evaporation ? ml : -mv;
          const auto end = departure(far);
          if (end && (evaporation ? *end <= 0 : *end >= 0))
            near = far;
          else
            for (unsigned iter = 0; iter < 32; ++iter) {
              const double mid = .5 * (near + far);
              if (mid == near || mid == far)
                break;
              const auto d = departure(mid);
              if (d && (evaporation ? *d < 0 : *d > 0))
                near = mid;
              else
                far = mid;
            }
          const double moved = fraction * near;
          if (moved != 0 && !receipt.changed) {
            masses[pair.liquid] = ml;
            masses[pair.vapour] = mv;
            initial_entropy = entropy(species, masses, before);
          }
          masses[pair.liquid] = ml - moved;
          masses[pair.vapour] = mv + moved;
          Balance mass;
          mass.add(ml);
          mass.add(mv);
          mass.add(-masses[pair.liquid]);
          mass.add(-masses[pair.vapour]);
          if (!mass.closed())
            throw std::runtime_error("Private phase pair mass ledger failed");
          if (mass.operands.value())
            receipt.mass_error =
                std::max(receipt.mass_error, std::abs(mass.residual.value()) /
                                                 mass.operands.value());
          if (moved != 0) {
            receipt.changed = true;
            (evaporation ? receipt.evaporated : receipt.condensed) +=
                std::abs(moved);
          }
          before = reading();
        }
        if (receipt.changed)
          receipt.entropy = entropy(species, masses, before) - initial_entropy;
        for (std::size_t key = 0; key < species.size(); ++key)
          if (masses[key] > 0)
            cells[i].emplace_back(key, masses[key]);
      }
    });
    PhaseResult result;
    result.masses.offsets.push_back(0);
    DoubleDouble evaporated, condensed, entropy_total;
    for (std::size_t i = 0; i < N; ++i) {
      for (const auto &[key, mass] : cells[i]) {
        result.masses.species.push_back(key);
        result.masses.mass.push_back(mass);
      }
      result.masses.offsets.push_back(result.masses.mass.size());
      const auto &receipt = receipts[i];
      evaporated.add(receipt.evaporated);
      condensed.add(receipt.condensed);
      entropy_total.add(receipt.entropy);
      result.trials += receipt.trials;
      result.changed_cells += receipt.changed;
      result.mass_error = std::max(result.mass_error, receipt.mass_error);
    }
    result.evaporated = evaporated.value();
    result.condensed = condensed.value();
    result.entropy_change = entropy_total.value();
    return result;
  }
};
} // namespace ma::experimental::game_kernel
