#pragma once
#include "flow_local.hpp"
#include "height_flux.hpp"
#include <optional>
namespace ma::experimental::game_kernel {
struct SurfaceResult {
  State state;
  std::vector<Eos> eos;
  std::size_t faces = 0, limited = 0, clusters = 0, largest_cluster = 0;
};
// Conservative anti-diffusive interface correction around the admitted remap.
// Equal phase/remaining-volume parcels leave J unchanged; actual G and face-K
// changes are local U work. A bounded local flux limiter admits each correction
// before application. No timestep or whole-world attempt is retried.
inline SurfaceResult
sharpen(const ma::experimental::MacGrid &source,
        const std::vector<Species> &species, const State &initial,
        const std::vector<Eos> &initial_eos, double dt, double gravity,
        double detail_fraction = 0,
        ma::experimental::mac_kernel::Pool *executor = nullptr,
        const ma::experimental::mac_kernel::Grid *cached_grid = nullptr) {
  namespace k = ma::experimental::mac_kernel;
  SurfaceResult out;
  bool any = false;
  for (const auto key : initial.masses.species)
    any = any || !species[key].gas;
  if (!any)
    return out;
  k::Grid built;
  if (!cached_grid)
    built = k::Grid(source);
  const auto &g = cached_grid ? *cached_grid : built;
  using Inventory = std::vector<std::pair<std::uint32_t, double>>;
  using Passives = std::vector<std::pair<std::uint64_t, double>>;
  std::vector<Inventory> cells(g.N);
  std::vector<Passives> passives(g.N);
  std::vector<double> mass(g.N), energy = initial.energy;
  std::vector<Eos> eos = initial_eos;
  std::vector<Geometry> geom(g.N);
  for (std::size_t i = 0; i < g.N; ++i) {
    for (std::size_t a = initial.masses.offsets[i];
         a < initial.masses.offsets[i + 1]; ++a) {
      cells[i].emplace_back(initial.masses.species[a], initial.masses.mass[a]);
      mass[i] += initial.masses.mass[a];
    }
    if (!initial.passives.mass.empty())
      for (std::size_t a = initial.passives.offsets[i];
           a < initial.passives.offsets[i + 1]; ++a)
        passives[i].emplace_back(initial.passives.species[a],
                                 initial.passives.mass[a]);
    if (!g.wall[i])
      geom[i] = geometry(species, cells[i], energy[i], g.V, gravity);
  }
  const auto normal_geometry =
      geom; // Freeze height normals for the complete correction pass.
  const auto phase_volume = [&](std::size_t i, std::uint64_t group) {
    for (const auto &p : geom[i].phases)
      if (p.identity == group)
        return p.volume;
    return 0.;
  };
  const auto height_weight = [&](std::size_t i, std::uint64_t group,
                                 bool x_axis) {
    const auto x = static_cast<long>(i % g.W), y = static_cast<long>(i / g.W);
    const auto normal_volume = [&](std::size_t cell) {
      for (const auto &phase : normal_geometry[cell].phases)
        if (phase.identity == group)
          return phase.volume;
      return 0.;
    };
    const double center = normal_volume(i) / g.V;
    const auto alpha = [&](long cx, long cy) {
      cx = std::clamp(cx, 0L, static_cast<long>(g.W) - 1);
      cy = std::clamp(cy, 0L, static_cast<long>(g.H) - 1);
      const auto j = static_cast<std::size_t>(cx + cy * static_cast<long>(g.W));
      if (!g.wall[j])
        return normal_volume(j) / g.V;
      const auto along_y =
                     static_cast<std::size_t>(x + cy * static_cast<long>(g.W)),
                 along_x =
                     static_cast<std::size_t>(cx + y * static_cast<long>(g.W));
      if (!g.wall[along_y])
        return normal_volume(along_y) / g.V;
      if (!g.wall[along_x])
        return normal_volume(along_x) / g.V;
      return center;
    };
    double gx = 0, gy = 0;
    for (long a = -1; a <= 1; ++a) {
      gx += .5 * (alpha(x + 1, y + a) - alpha(x - 1, y + a));
      gy += .5 * (alpha(x + a, y + 1) - alpha(x + a, y - 1));
    }
    const double scale = std::abs(gx) + std::abs(gy);
    return scale > 0 ? (x_axis ? std::abs(gx) : std::abs(gy)) / scale : .5;
  };
  const double fraction = std::min(.45, 4 * dt);
  struct Counts {
    std::size_t faces = 0, limited = 0;
  };
  const auto process = [&](bool x_axis, std::size_t face, Counts &counts) {
    const auto &left = x_axis ? g.xl : g.yl, &right = x_axis ? g.xr : g.yr;
    const auto l = left[face], r = right[face];
    if (l == k::none || r == k::none || g.closed(!x_axis, face) ||
        !geom[l].liquid || !geom[r].liquid)
      return;
    std::uint64_t chosen = gas_group;
    std::size_t donor = 0, target = 0;
    double volume = 0;
    std::vector<std::uint64_t> groups;
    groups.reserve(geom[l].phases.size() + geom[r].phases.size());
    for (const auto &phase : geom[l].phases)
      if (phase.identity != gas_group)
        groups.push_back(phase.identity);
    for (const auto &phase : geom[r].phases)
      if (phase.identity != gas_group)
        groups.push_back(phase.identity);
    std::sort(groups.begin(), groups.end());
    groups.erase(std::unique(groups.begin(), groups.end()), groups.end());
    for (const auto group : groups) {
      const double vl = phase_volume(l, group), vr = phase_volume(r, group);
      if (vl == vr)
        continue;
      const auto from = vl < vr ? l : r, to = vl < vr ? r : l;
      const double low = std::min(vl, vr), high = std::max(vl, vr),
                   room = g.V - high;
      if (!(low > detail_fraction * g.V && room > detail_fraction * g.V))
        continue;
      const double gradient = std::min(1., (high - low) / (.1 * g.V));
      const double weight = .5 * (height_weight(l, group, x_axis) +
                                  height_weight(r, group, x_axis));
      const double requested =
          fraction * std::min(low, room) * gradient * weight;
      if (requested > volume) {
        volume = requested;
        chosen = group;
        donor = from;
        target = to;
      }
    }
    if (!(volume > 0) || chosen == gas_group)
      return;
    const double from_volume = phase_volume(donor, chosen),
                 remaining = g.V - phase_volume(target, chosen);
    std::map<std::uint32_t, double> delta;
    double forward_energy = 0, back_energy = 0, forward_mass = 0, back_mass = 0;
    for (const auto &[key, value] : cells[donor])
      if (phase_group(species, key) == chosen) {
        const auto &s = species[key];
        const double moved = value * volume / from_volume;
        delta[key] -= moved;
        forward_mass += moved;
        forward_energy +=
            moved * (s.offset + s.cv * eos[donor].temperature +
                     (s.gas ? 0
                            : liquid_specific_elastic(s,eos[donor].pressure)));
      }
    for (const auto &[key, value] : cells[target])
      if (phase_group(species, key) != chosen) {
        const auto &s = species[key];
        const double moved = value * volume / remaining;
        delta[key] += moved;
        back_mass += moved;
        back_energy +=
            moved * (s.offset + s.cv * eos[target].temperature +
                     (s.gas ? 0
                            : liquid_specific_elastic(s,eos[target].pressure)));
      }
    // Anti-diffusion may not reinforce an inverted full-liquid layer.
    // Buoyant exchange remains responsible for its density ordering.
    if (!x_axis && geom[donor].alpha > .99 && geom[target].alpha > .99 &&
        gravity * (double(target / g.W) - double(donor / g.W)) *
                (forward_mass - back_mass) >
            0)
      return;
    std::vector<std::pair<bool, std::size_t>> faces;
    faces.reserve(8);
    for (const auto i : {donor, target})
      for (const auto entry :
           {std::pair{true, g.left_face(i)}, std::pair{true, g.right_face(i)},
            std::pair{false, g.bottom_face(i)},
            std::pair{false, g.top_face(i)}})
        faces.push_back(entry);
    std::sort(faces.begin(), faces.end());
    faces.erase(std::unique(faces.begin(), faces.end()), faces.end());
    std::vector<std::size_t> touched{donor, target};
    touched.reserve(10);
    for (const auto [x, f] : faces)
      if ((x ? initial.jx : initial.jy)[f] != 0)
        for (const auto i : {(x ? g.xl : g.yl)[f], (x ? g.xr : g.yr)[f]})
          if (i != k::none && !g.wall[i])
            touched.push_back(i);
    std::sort(touched.begin(), touched.end());
    touched.erase(std::unique(touched.begin(), touched.end()), touched.end());
    struct Local {
      std::size_t i = 0;
      Inventory inventory;
      double energy = 0, mass = 0;
      Eos eos;
    };
    const auto trial = [&](double theta) -> std::optional<std::vector<Local>> {
      std::vector<Local> local;
      local.reserve(touched.size());
      for (const auto i : touched) {
        Local row;
        row.i = i;
        row.energy = energy[i];
        row.mass = mass[i];
        if (i == donor || i == target) {
          std::map<std::uint32_t, double> inventory;
          for (const auto &[key, value] : cells[i])
            inventory[key] = value;
          for (const auto &[key, value] : delta)
            inventory[key] += (i == donor ? 1 : -1) * theta * value;
          row.mass = 0;
          for (const auto &[key, value] : inventory) {
            if (value < 0 || !std::isfinite(value))
              return std::nullopt;
            if (value > 0) {
              row.inventory.emplace_back(key, value);
              row.mass += value;
            }
          }
          row.energy +=
              (i == donor ? 1 : -1) * theta * (back_energy - forward_energy);
        }
        local.push_back(std::move(row));
      }
      const auto find = [&](std::size_t i) -> Local & {
        for (auto &row : local)
          if (row.i == i)
            return row;
        throw std::runtime_error("Private surface local footprint incomplete");
      };
      DoubleDouble potential;
      for (const auto i : {donor, target}) {
        const double h = (static_cast<double>(i / g.W) + .5) * source.spacing_m;
        potential.add(mass[i] * gravity * h);
        potential.add(-find(i).mass * gravity * h);
      }
      find(donor).energy += .5 * potential.value();
      find(target).energy += .5 * potential.value();
      const auto m = [&](k::Index i, bool updated) {
        if (i == k::none || g.wall[i])
          return 0.;
        return updated ? find(i).mass : mass[i];
      };
      for (const auto [x, f] : faces) {
        const auto a = (x ? g.xl : g.yl)[f], b = (x ? g.xr : g.yr)[f];
        const double j = (x ? initial.jx : initial.jy)[f];
        if (j == 0)
          continue;
        const double I0 = .5 * (m(a, false) + m(b, false)),
                     I1 = .5 * (m(a, true) + m(b, true));
        if (!(I0 > 0 && I1 > 0))
          return std::nullopt;
        const double heat = .5 * j * (j / I0) - .5 * j * (j / I1),
                     sum = m(a, true) + m(b, true);
        if (a != k::none && !g.wall[a])
          find(a).energy += heat * m(a, true) / sum;
        if (b != k::none && !g.wall[b])
          find(b).energy += heat * m(b, true) / sum;
      }
      try {
        for (auto &row : local) {
          const auto &inventory =
              row.i == donor || row.i == target ? row.inventory : cells[row.i];
          row.eos = evaluate_selected(
              species, inventory.size(),
              [&](std::size_t a) { return inventory[a].first; },
              [&](std::size_t a) { return inventory[a].second; }, row.energy,
              g.V);
        }
      } catch (const std::runtime_error &) {
        return std::nullopt;
      }
      return local;
    };
    double accepted = 1;
    auto candidate = trial(1);
    if (!candidate) {
      ++counts.limited;
      double low = 0, high = 1;
      for (unsigned iteration = 0; iteration < 12; ++iteration) {
        const double middle = .5 * (low + high);
        auto value = trial(middle);
        if (value) {
          low = middle;
          candidate = std::move(value);
        } else
          high = middle;
      }
      accepted = low;
    }
    if (!candidate || accepted == 0)
      return;
    std::map<std::uint64_t, double> passive_delta;
    for (const auto &[identity, quantity] : passives[donor])
      passive_delta[identity] -=
          quantity * (accepted * forward_mass / mass[donor]);
    for (const auto &[identity, quantity] : passives[target])
      passive_delta[identity] +=
          quantity * (accepted * back_mass / mass[target]);
    for (const auto i : {donor, target}) {
      std::map<std::uint64_t, double> inventory;
      for (const auto &[identity, quantity] : passives[i])
        inventory[identity] = quantity;
      for (const auto &[identity, quantity] : passive_delta)
        inventory[identity] += (i == donor ? 1 : -1) * quantity;
      passives[i].clear();
      for (const auto &[identity, quantity] : inventory) {
        if (quantity < 0)
          throw std::runtime_error("Private surface passive budget failed");
        if (quantity > 0)
          passives[i].emplace_back(identity, quantity);
      }
    }
    for (auto &row : *candidate) {
      if (row.i == donor || row.i == target)
        cells[row.i] = std::move(row.inventory);
      mass[row.i] = row.mass;
      energy[row.i] = row.energy;
      eos[row.i] = row.eos;
      geom[row.i] =
          geometry(species, cells[row.i], energy[row.i], g.V, gravity);
    }
    ++counts.faces;
  };
  // Disjoint correction footprints commute. Preserve the original x/y
  // face order inside each connected footprint cluster, and run only
  // independent clusters concurrently.
  struct Action {
    bool x;
    std::size_t face;
  };
  std::vector<Action> actions;
  std::vector<std::size_t> parent(g.N);
  for (std::size_t i = 0; i < g.N; ++i)
    parent[i] = i;
  const auto root = [&](std::size_t i) {
    while (parent[i] != i) {
      parent[i] = parent[parent[i]];
      i = parent[i];
    }
    return i;
  };
  const auto unite = [&](std::size_t a, std::size_t b) {
    a = root(a);
    b = root(b);
    if (a != b)
      parent[std::max(a, b)] = std::min(a, b);
  };
  const auto register_face = [&](bool x, std::size_t f) {
    const auto l = (x ? g.xl : g.yl)[f], r = (x ? g.xr : g.yr)[f];
    if (l == k::none || r == k::none || g.closed(!x, f) || !geom[l].liquid ||
        !geom[r].liquid)
      return;
    actions.push_back({x, f});
    unite(l, r);
    for (const auto i : {l, r})
      for (const auto edge :
           {std::pair{true, g.left_face(i)}, std::pair{true, g.right_face(i)},
            std::pair{false, g.bottom_face(i)},
            std::pair{false, g.top_face(i)}})
        if ((edge.first ? initial.jx : initial.jy)[edge.second] != 0)
          for (const auto cell : {(edge.first ? g.xl : g.yl)[edge.second],
                                  (edge.first ? g.xr : g.yr)[edge.second]})
            if (cell != k::none && !g.wall[cell])
              unite(l, cell);
  };
  for (std::size_t f = 0; f < g.NX; ++f)
    register_face(true, f);
  for (std::size_t f = 0; f < g.NY; ++f)
    register_face(false, f);
  std::map<std::size_t, std::vector<Action>> grouped;
  for (const auto action : actions)
    grouped[root((action.x ? g.xl : g.yl)[action.face])].push_back(action);
  std::vector<std::vector<Action>> clusters;
  for (auto &[identity, list] : grouped) {
    out.largest_cluster = std::max(out.largest_cluster, list.size());
    clusters.push_back(std::move(list));
  }
  out.clusters = clusters.size();
  std::vector<Counts> counts(clusters.size());
  std::vector<std::exception_ptr> errors(clusters.size());
  const auto execute = [&](std::size_t index) {
    try {
      for (const auto action : clusters[index])
        process(action.x, action.face, counts[index]);
    } catch (...) {
      errors[index] = std::current_exception();
    }
  };
  if (executor)
    executor->run(clusters.size(), execute);
  else
    for (std::size_t index = 0; index < clusters.size(); ++index)
      execute(index);
  for (std::size_t index = 0; index < clusters.size(); ++index) {
    if (errors[index])
      std::rethrow_exception(errors[index]);
    out.faces += counts[index].faces;
    out.limited += counts[index].limited;
  }
  if (out.faces == 0)
    return out;
  out.state = initial;
  out.state.energy = std::move(energy);
  out.state.masses = {};
  out.state.masses.offsets.push_back(0);
  out.state.passives = {};
  if (!initial.passives.mass.empty())
    out.state.passives.offsets.push_back(0);
  for (std::size_t i = 0; i < g.N; ++i) {
    for (const auto &[key, value] : cells[i]) {
      out.state.masses.species.push_back(key);
      out.state.masses.mass.push_back(value);
    }
    out.state.masses.offsets.push_back(out.state.masses.mass.size());
    if (!initial.passives.mass.empty()) {
      for (const auto &[identity, value] : passives[i]) {
        out.state.passives.species.push_back(identity);
        out.state.passives.mass.push_back(value);
      }
      out.state.passives.offsets.push_back(out.state.passives.mass.size());
    }
  }
  out.eos = std::move(eos);
  return out;
}
} // namespace ma::experimental::game_kernel
