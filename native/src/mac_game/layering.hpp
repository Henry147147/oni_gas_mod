#pragma once
#include "flow_local.hpp"
#include "totals.hpp"
namespace ma::experimental::game_kernel {
struct LayerResult {
  State state;
  std::size_t faces = 0;
  double released = 0;
};
// Jacobi equal-volume buoyant exchanges. Miscible constituents share one phase.
// Donor-specific U and mass-carried passives use shared parcels. Face J is
// retained; its actual inertia/K change and gravity release go to local U.
inline LayerResult
layer(const ma::experimental::MacGrid &source,
      const std::vector<Species> &species, const State &initial,
      const std::vector<Eos> &eos, double dt, double gravity,
      const ma::experimental::mac_kernel::Grid *cached_grid = nullptr) {
  namespace k = ma::experimental::mac_kernel;
  k::Grid built;
  if (!cached_grid)
    built = k::Grid(source);
  const auto &g = cached_grid ? *cached_grid : built;
  LayerResult out;
  if (gravity == 0)
    return out;
  const double fraction = std::min(.25, -std::expm1(-dt / 2.));
  struct Pick {
    std::uint64_t phase = 0;
    double mass = 0, volume = 0;
    bool valid = false;
    double density() const { return mass / volume; }
  };
  std::vector<Pick> light(g.N), heavy(g.N);
  std::vector<double> old_mass(g.N), new_mass(g.N);
  const auto phase = [&](std::uint32_t key) {
    return species[key].liquid_group ? std::uint64_t(species[key].liquid_group)
                                     : (std::uint64_t(1) << 32) + key;
  };
  for (std::size_t i = 0; i < g.N; ++i) {
    std::map<std::uint64_t, Pick> phases;
    for (std::size_t a = initial.masses.offsets[i];
         a < initial.masses.offsets[i + 1]; ++a) {
      const auto key = initial.masses.species[a];
      const auto m = initial.masses.mass[a];
      old_mass[i] += m;
      if (species[key].gas)
        continue;
      auto &p = phases[phase(key)];
      p.phase = phase(key);
      p.mass += m;
      p.volume += m / species[key].rho0;
      p.valid = true;
    }
    for (const auto &[identity, p] : phases) {
      if (!light[i].valid || p.density() < light[i].density())
        light[i] = p;
      if (!heavy[i].valid || p.density() > heavy[i].density())
        heavy[i] = p;
    }
  }
  struct Parcel {
    std::size_t from, to;
    std::vector<std::pair<std::uint32_t, double>> species;
    double mass = 0, energy = 0;
  };
  std::vector<Parcel> parcels;
  std::vector<std::vector<std::size_t>> outgoing(g.N), incoming(g.N);
  for (std::size_t f = 0; f < g.NY; ++f) {
    const auto lower = gravity > 0 ? g.yl[f] : g.yr[f],
               upper = gravity > 0 ? g.yr[f] : g.yl[f];
    if (lower == k::none || upper == k::none || g.closed(true, f))
      continue;
    const auto &l = light[lower];
    const auto &h = heavy[upper];
    if (!l.valid || !h.valid || l.phase == h.phase ||
        !(l.density() < h.density()))
      continue;
    const double exchanged = fraction * std::min(l.volume, h.volume);
    const auto add_parcel = [&](std::size_t from, std::size_t to,
                                const Pick &chosen) {
      Parcel parcel{from, to, {}};
      const double share = exchanged / chosen.volume;
      const auto &state = eos[from];
      for (std::size_t a = initial.masses.offsets[from];
           a < initial.masses.offsets[from + 1]; ++a) {
        const auto key = initial.masses.species[a];
        const auto &s = species[key];
        if (s.gas || phase(key) != chosen.phase)
          continue;
        const double moved = initial.masses.mass[a] * share;
        parcel.species.emplace_back(key, moved);
        parcel.mass += moved;
        const double specific =
            s.offset + s.cv * state.temperature +
            liquid_specific_elastic(s,state.pressure);
        parcel.energy += moved * specific;
      }
      outgoing[from].push_back(parcels.size());
      incoming[to].push_back(parcels.size());
      parcels.push_back(std::move(parcel));
    };
    add_parcel(lower, upper, l);
    add_parcel(upper, lower, h);
    ++out.faces;
  }
  if (parcels.empty())
    return out;
  out.state = initial;
  out.state.masses = {};
  out.state.masses.offsets.push_back(0);
  const bool has_passives = !initial.passives.mass.empty();
  if (has_passives) {
    out.state.passives = {};
    out.state.passives.offsets.push_back(0);
  }
  for (std::size_t i = 0; i < g.N; ++i) {
    if (outgoing[i].empty() && incoming[i].empty()) {
      new_mass[i] = old_mass[i];
      out.state.masses.species.insert(
          out.state.masses.species.end(),
          initial.masses.species.begin() + initial.masses.offsets[i],
          initial.masses.species.begin() + initial.masses.offsets[i + 1]);
      out.state.masses.mass.insert(
          out.state.masses.mass.end(),
          initial.masses.mass.begin() + initial.masses.offsets[i],
          initial.masses.mass.begin() + initial.masses.offsets[i + 1]);
      out.state.masses.offsets.push_back(out.state.masses.mass.size());
      if (has_passives) {
        out.state.passives.species.insert(
            out.state.passives.species.end(),
            initial.passives.species.begin() + initial.passives.offsets[i],
            initial.passives.species.begin() + initial.passives.offsets[i + 1]);
        out.state.passives.mass.insert(
            out.state.passives.mass.end(),
            initial.passives.mass.begin() + initial.passives.offsets[i],
            initial.passives.mass.begin() + initial.passives.offsets[i + 1]);
        out.state.passives.offsets.push_back(out.state.passives.mass.size());
      }
      continue;
    }
    std::map<std::uint32_t, double> mass;
    std::map<std::uint64_t, double> passive;
    for (std::size_t a = initial.masses.offsets[i];
         a < initial.masses.offsets[i + 1]; ++a)
      mass[initial.masses.species[a]] = initial.masses.mass[a];
    if (has_passives)
      for (std::size_t a = initial.passives.offsets[i];
           a < initial.passives.offsets[i + 1]; ++a)
        passive[initial.passives.species[a]] = initial.passives.mass[a];
    const auto apply = [&](const Parcel &parcel, double sign) {
      for (const auto &[key, value] : parcel.species)
        mass[key] += sign * value;
      out.state.energy[i] += sign * parcel.energy;
      if (has_passives)
        for (std::size_t a = initial.passives.offsets[parcel.from];
             a < initial.passives.offsets[parcel.from + 1]; ++a)
          passive[initial.passives.species[a]] +=
              sign * initial.passives.mass[a] *
              (parcel.mass / old_mass[parcel.from]);
    };
    for (const auto index : outgoing[i])
      apply(parcels[index], -1);
    for (const auto index : incoming[i])
      apply(parcels[index], 1);
    for (const auto &[key, value] : mass) {
      if (value < 0)
        throw std::runtime_error("Private layering mass budget failed");
      if (value > 0) {
        out.state.masses.species.push_back(key);
        out.state.masses.mass.push_back(value);
        new_mass[i] += value;
      }
    }
    out.state.masses.offsets.push_back(out.state.masses.mass.size());
    if (has_passives) {
      for (const auto &[key, value] : passive) {
        if (value < 0)
          throw std::runtime_error("Private layering passive budget failed");
        if (value > 0) {
          out.state.passives.species.push_back(key);
          out.state.passives.mass.push_back(value);
        }
      }
      out.state.passives.offsets.push_back(out.state.passives.mass.size());
    }
  }
  DoubleDouble released;
  for (const auto &parcel : parcels) {
    const double drop = parcel.mass * gravity * source.spacing_m *
                        (double(parcel.from / g.W) - double(parcel.to / g.W));
    out.state.energy[parcel.from] += .5 * drop;
    out.state.energy[parcel.to] += .5 * drop;
    released.add(drop);
  }
  out.released = released.value();
  const auto faces = [&](const auto &left, const auto &right, const auto &j) {
    const auto m = [](const auto &values, k::Index i) {
      return i == k::none ? 0 : values[i];
    };
    for (std::size_t f = 0; f < j.size(); ++f)
      if (j[f] != 0) {
        const auto l = left[f], r = right[f];
        const double I0 = .5 * (m(old_mass, l) + m(old_mass, r)),
                     I1 = .5 * (m(new_mass, l) + m(new_mass, r));
        if (I0 == I1)
          continue;
        if (!(I1 > 0))
          throw std::runtime_error("Private layering removed face inertia");
        const double heat = .5 * j[f] * (j[f] / I0) - .5 * j[f] * (j[f] / I1),
                     sum = m(new_mass, l) + m(new_mass, r);
        if (l != k::none)
          out.state.energy[l] += heat * m(new_mass, l) / sum;
        if (r != k::none)
          out.state.energy[r] += heat * m(new_mass, r) / sum;
      }
  };
  faces(g.xl, g.xr, initial.jx);
  faces(g.yl, g.yr, initial.jy);
  return out;
}
} // namespace ma::experimental::game_kernel
