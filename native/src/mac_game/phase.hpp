#pragma once
#include "flow_local.hpp"
#include <optional>
namespace ma::experimental::game_kernel {
struct PhasePair {
  std::size_t liquid = 0, vapour = 0;
  double critical = 1e300;
};
struct PhaseResult {
  SparseMass masses;
  double evaporated = 0, condensed = 0, entropy_change = 0, mass_error = 0;
  std::size_t changed_cells = 0, trials = 0;
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
  PhaseResult advance(const State &state, double dt,
                      double relaxation_time = 1) {
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
        const auto reading = [&] {
          return evaluate_selected(
              species, keys.size(), [&](std::size_t a) { return keys[a]; },
              [&](std::size_t a) { return masses[keys[a]]; }, state.energy[i],
              grid.volume_m3());
        };
        const auto include = [&](std::size_t key) {
          const auto at = std::lower_bound(keys.begin(), keys.end(), key);
          if (at == keys.end() || *at != key)
            keys.insert(at, static_cast<std::uint32_t>(key));
        };
        auto before = reading();
        double initial_entropy = 0;
        auto &receipt = receipts[i];
        for (const auto &pair : pairs) {
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
              const auto eos = moved == 0 ? before : reading();
              const double pv = eos.gas_volume > 0
                                    ? masses[pair.vapour] * R /
                                          vapour.molar_mass * eos.temperature /
                                          eos.gas_volume
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
          if ((evaporation && ml == 0) ||
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
