#pragma once
#include "flow_local.hpp"
#include "height_flux.hpp"
#include <optional>
namespace ma::experimental::game_kernel {
struct SurfaceResult {
  State state;
  std::vector<Eos> eos;
  std::size_t faces = 0, limited = 0, clusters = 0, largest_cluster = 0;
  // Profile 8: momentum the moved material carried into closed faces
  // (impulse on the fluid) and its operand scale.
  double wall_x = 0, wall_y = 0, wall_ops_x = 0, wall_ops_y = 0;
};
// Conservative anti-diffusive interface correction around the admitted remap.
// Equal phase/remaining-volume parcels retain face J on profiles 2-7 and
// carry their donor face velocities on profile 8; actual G and face-K changes
// are local U work. A bounded local flux limiter admits each correction
// before application. No timestep or whole-world attempt is retried.
// `eos` holds the readings of `initial`; the result returns them, updated
// for every corrected cell.
// Capacity that sharpen reuses across calls; every entry is rewritten
// before it is read.
struct SurfaceScratch {
  std::vector<std::vector<std::pair<std::uint32_t, double>>> cells;
  std::vector<std::vector<std::pair<std::uint64_t, double>>> passives;
  std::vector<Geometry> geom;
  std::vector<double> mass;
  // Frozen phase volumes of the height normals: (identity, volume) slices.
  std::vector<std::size_t> normal_start;
  std::vector<std::pair<std::uint64_t, double>> normal_phases;
  std::vector<std::size_t> parent;
};
inline SurfaceResult
sharpen(const ma::experimental::MacGrid &source,
        const std::vector<Species> &species, const State &initial,
        std::vector<Eos> eos, double dt, double gravity,
        double detail_fraction = 0,
        ma::experimental::mac_kernel::Pool *executor = nullptr,
        const ma::experimental::mac_kernel::Grid *cached_grid = nullptr,
        SurfaceScratch *reuse = nullptr, bool carry_momentum = false,
        RegionAudit *audit = nullptr) {
  namespace k = ma::experimental::mac_kernel;
  SurfaceResult out;
  bool any = false;
  for (const auto key : initial.masses.species)
    any = any || !species[key].gas;
  if (!any) {
    out.eos = std::move(eos);
    return out;
  }
  k::Grid built;
  if (!cached_grid)
    built = k::Grid(source);
  const auto &g = cached_grid ? *cached_grid : built;
  using Inventory = std::vector<std::pair<std::uint32_t, double>>;
  SurfaceScratch local_scratch;
  auto &scratch = reuse ? *reuse : local_scratch;
  // Fixed row blocks, on the pool when there is one.
  const auto blocks = [&](const std::function<void(std::size_t)> &job) {
    if (executor)
      executor->run(g.blocks, job);
    else
      for (std::size_t block = 0; block < g.blocks; ++block)
        job(block);
  };
  const auto block_cells = [&](std::size_t block) {
    return std::pair{g.block_row_begin(block) * g.W,
                     g.block_row_end(block) * g.W};
  };
  const bool has_passives = !initial.passives.mass.empty();
  auto &cells = scratch.cells;
  auto &passives = scratch.passives;
  auto &geom = scratch.geom;
  auto &mass = scratch.mass;
  cells.resize(g.N);
  passives.resize(g.N);
  geom.resize(g.N);
  mass.resize(g.N);
  std::vector<double> energy = initial.energy;
  // Profile 8 moves face momentum with the material; otherwise J is read
  // from the initial state and never changes here.
  std::vector<double> carried_x, carried_y;
  if (carry_momentum) {
    carried_x = initial.jx;
    carried_y = initial.jy;
  }
  const auto &JX = carry_momentum ? carried_x : initial.jx;
  const auto &JY = carry_momentum ? carried_y : initial.jy;
  const auto fluid_face = [&](bool x, std::size_t f) {
    const auto l = (x ? g.xl : g.yl)[f], r = (x ? g.xr : g.yr)[f];
    return l != k::none && r != k::none && !g.wall[l] && !g.wall[r] &&
           !g.closed(!x, f);
  };
  blocks([&](std::size_t block) {
    for (auto [i, end] = block_cells(block); i < end; ++i) {
      cells[i].clear();
      double total = 0;
      for (std::size_t a = initial.masses.offsets[i];
           a < initial.masses.offsets[i + 1]; ++a) {
        cells[i].emplace_back(initial.masses.species[a], initial.masses.mass[a]);
        total += initial.masses.mass[a];
      }
      mass[i] = total;
      passives[i].clear();
      if (has_passives)
        for (std::size_t a = initial.passives.offsets[i];
             a < initial.passives.offsets[i + 1]; ++a)
          passives[i].emplace_back(initial.passives.species[a],
                                   initial.passives.mass[a]);
      if (!g.wall[i])
        geometry_into(geom[i], species, cells[i], energy[i], g.V, gravity,
                      &eos[i]);
      else {
        geom[i].liquid = false;
        geom[i].alpha = 0;
        geom[i].phases.clear();
      }
    }
  });
  // Freeze height normals for the complete correction pass.
  auto &normal_start = scratch.normal_start;
  auto &normal_phases = scratch.normal_phases;
  normal_start.resize(g.N + 1);
  std::vector<std::size_t> block_start(g.blocks + 1);
  blocks([&](std::size_t block) {
    std::size_t count = 0;
    for (auto [i, end] = block_cells(block); i < end; ++i)
      count += geom[i].phases.size();
    block_start[block + 1] = count;
  });
  for (std::size_t block = 0; block < g.blocks; ++block)
    block_start[block + 1] += block_start[block];
  normal_phases.resize(block_start.back());
  blocks([&](std::size_t block) {
    auto at = block_start[block];
    for (auto [i, end] = block_cells(block); i < end; ++i) {
      normal_start[i] = at;
      for (const auto &phase : geom[i].phases)
        normal_phases[at++] = {phase.identity, phase.volume};
    }
  });
  normal_start[g.N] = normal_phases.size();
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
      for (auto a = normal_start[cell]; a < normal_start[cell + 1]; ++a)
        if (normal_phases[a].first == group)
          return normal_phases[a].second;
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
  struct Receipt {
    std::size_t cell;
    bool x;
    double value;
  };
  struct Counts {
    std::size_t faces = 0, limited = 0;
    std::vector<Receipt> receipts;
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
      for (const auto &entry :
           {std::pair{true, g.left_face(i)}, std::pair{true, g.right_face(i)},
            std::pair{false, g.bottom_face(i)},
            std::pair{false, g.top_face(i)}})
        faces.push_back(entry);
    std::sort(faces.begin(), faces.end());
    faces.erase(std::unique(faces.begin(), faces.end()), faces.end());
    std::vector<std::size_t> touched{donor, target};
    touched.reserve(10);
    for (const auto &[x, f] : faces)
      if (carry_momentum || (x ? JX : JY)[f] != 0)
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
    // Profile 8: the momentum the trial leaves on `faces`, and what moved
    // material carried into closed faces.
    struct Trial {
      std::vector<Local> rows;
      std::vector<double> momentum;
      std::vector<Receipt> walls;
    };
    const auto trial = [&](double theta) -> std::optional<Trial> {
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
      Trial result;
      if (!carry_momentum)
        for (const auto &[x, f] : faces) {
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
      else {
        // Each parcel keeps the velocities of the faces it leaves: the
        // forward parcel's half on each donor face moves to the target's
        // face on the same side, the backward parcel's likewise. Momentum
        // reaching a closed face is a wall impulse; every face's kinetic
        // energy change is local heat.
        auto &next = result.momentum;
        next.resize(faces.size());
        for (std::size_t n = 0; n < faces.size(); ++n)
          next[n] = (faces[n].first ? JX : JY)[faces[n].second];
        const auto position = [&](std::pair<bool, std::size_t> which_face) {
          return static_cast<std::size_t>(
              std::lower_bound(faces.begin(), faces.end(), which_face) - faces.begin());
        };
        const auto side = [&](std::size_t cell, unsigned which) {
          return which == 0   ? std::pair{true, g.left_face(cell)}
                 : which == 1 ? std::pair{true, g.right_face(cell)}
                 : which == 2 ? std::pair{false, g.bottom_face(cell)}
                              : std::pair{false, g.top_face(cell)};
        };
        bool valid = true;
        const auto velocity = [&](std::pair<bool, std::size_t> which_face) {
          const double j = (which_face.first ? JX : JY)[which_face.second];
          if (j == 0)
            return 0.;
          const auto a = (which_face.first ? g.xl : g.yl)[which_face.second],
                     b = (which_face.first ? g.xr : g.yr)[which_face.second];
          const double I0 = .5 * (m(a, false) + m(b, false));
          if (!(I0 > 0))
            valid = false;
          return I0 > 0 ? j / I0 : 0.;
        };
        const auto carry = [&](std::pair<bool, std::size_t> from,
                               std::pair<bool, std::size_t> to,
                               std::size_t receiver, double amount) {
          if (amount == 0)
            return;
          next[position(from)] -= amount;
          if (fluid_face(to.first, to.second))
            next[position(to)] += amount;
          else
            result.walls.push_back({receiver, to.first, -amount});
        };
        const double moved_forward = theta * forward_mass,
                     moved_back = theta * back_mass;
        for (unsigned which = 0; which < 4; ++which) {
          const auto d = side(donor, which), t = side(target, which);
          const double forward = .5 * moved_forward * velocity(d),
                       backward = .5 * moved_back * velocity(t);
          carry(d, t, target, forward);
          carry(t, d, donor, backward);
        }
        if (!valid)
          return std::nullopt;
        for (std::size_t n = 0; n < faces.size(); ++n) {
          const auto [x, f] = faces[n];
          const double j0 = (x ? JX : JY)[f], j1 = next[n];
          if (j0 == 0 && j1 == 0)
            continue;
          const auto a = (x ? g.xl : g.yl)[f], b = (x ? g.xr : g.yr)[f];
          const double I0 = .5 * (m(a, false) + m(b, false)),
                       I1 = .5 * (m(a, true) + m(b, true));
          if ((j0 != 0 && !(I0 > 0)) || !(I1 > 0))
            return std::nullopt;
          const double heat = (j0 == 0 ? 0. : .5 * j0 * (j0 / I0)) -
                              (j1 == 0 ? 0. : .5 * j1 * (j1 / I1)),
                       sum = m(a, true) + m(b, true);
          if (a != k::none && !g.wall[a])
            find(a).energy += heat * m(a, true) / sum;
          if (b != k::none && !g.wall[b])
            find(b).energy += heat * m(b, true) / sum;
        }
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
      result.rows = std::move(local);
      return result;
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
    if (carry_momentum) {
      for (std::size_t n = 0; n < faces.size(); ++n)
        (faces[n].first ? carried_x : carried_y)[faces[n].second] =
            candidate->momentum[n];
      counts.receipts.insert(counts.receipts.end(), candidate->walls.begin(),
                             candidate->walls.end());
    }
    for (auto &row : candidate->rows) {
      if (row.i == donor || row.i == target)
        cells[row.i] = std::move(row.inventory);
      mass[row.i] = row.mass;
      energy[row.i] = row.energy;
      eos[row.i] = row.eos;
      geometry_into(geom[row.i], species, cells[row.i], energy[row.i], g.V,
                    gravity, &row.eos);
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
  auto &parent = scratch.parent;
  parent.resize(g.N);
  for (std::size_t i = 0; i < g.N; ++i)
    parent[i] = i;
  const auto root = [&](std::size_t i) {
    while (parent[i] != i) {
      parent[i] = parent[parent[i]];
      i = parent[i];
    }
    return i;
  };
  // A cluster's root is its lowest cell, whatever the union order.
  const auto unite = [&](std::size_t a, std::size_t b) {
    a = root(a);
    b = root(b);
    if (a != b)
      parent[std::max(a, b)] = std::min(a, b);
  };
  const auto liquid_face = [&](bool x, std::size_t f) {
    const auto l = (x ? g.xl : g.yl)[f], r = (x ? g.xr : g.yr)[f];
    return l != k::none && r != k::none && !g.closed(!x, f) &&
           geom[l].liquid && geom[r].liquid;
  };
  const auto register_face = [&](bool x, std::size_t f) {
    const auto l = (x ? g.xl : g.yl)[f], r = (x ? g.xr : g.yr)[f];
    actions.push_back({x, f});
    unite(l, r);
    for (const auto i : {l, r})
      for (const auto &edge :
           {std::pair{true, g.left_face(i)}, std::pair{true, g.right_face(i)},
            std::pair{false, g.bottom_face(i)},
            std::pair{false, g.top_face(i)}})
        if (carry_momentum || (edge.first ? initial.jx : initial.jy)[edge.second] != 0)
          for (const auto cell : {(edge.first ? g.xl : g.yl)[edge.second],
                                  (edge.first ? g.xr : g.yr)[edge.second]})
            if (cell != k::none && !g.wall[cell])
              unite(l, cell);
  };
  // Liquid/liquid faces are found in row blocks, then registered in face
  // order: x faces, then y faces.
  std::vector<std::vector<std::size_t>> liquid_x(g.blocks), liquid_y(g.blocks);
  blocks([&](std::size_t block) {
    const auto y0 = g.block_row_begin(block);
    for (std::size_t f = y0 * g.XW; f < g.block_row_end(block) * g.XW; ++f)
      if (liquid_face(true, f))
        liquid_x[block].push_back(f);
    for (std::size_t f = y0 * g.W; f < g.block_yrow_end(block) * g.W; ++f)
      if (liquid_face(false, f))
        liquid_y[block].push_back(f);
  });
  for (const auto &list : liquid_x)
    for (const auto f : list)
      register_face(true, f);
  for (const auto &list : liquid_y)
    for (const auto f : list)
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
  if (out.faces == 0) {
    out.eos = std::move(eos);
    return out;
  }
  // Wall impulses of carried momentum, booked in cluster order.
  DoubleDouble wall_x, wall_y, ops_x, ops_y;
  for (const auto &cluster : counts)
    for (const auto &receipt : cluster.receipts) {
      (receipt.x ? wall_x : wall_y).add(receipt.value);
      (receipt.x ? ops_x : ops_y).add(std::abs(receipt.value));
      if (audit)
        audit->momentum_receipt(receipt.cell, receipt.x, receipt.value,
                                std::abs(receipt.value));
    }
  out.wall_x = wall_x.value();
  out.wall_y = wall_y.value();
  out.wall_ops_x = ops_x.value();
  out.wall_ops_y = ops_y.value();
  out.state.energy = std::move(energy);
  out.state.jx = carry_momentum ? std::move(carried_x) : initial.jx;
  out.state.jy = carry_momentum ? std::move(carried_y) : initial.jy;
  // Corrected inventories back to sparse form, from per-block counts.
  const auto pack = [&](const auto &rows, auto &into) {
    std::vector<std::size_t> start(g.blocks + 1);
    blocks([&](std::size_t block) {
      std::size_t count = 0;
      for (auto [i, end] = block_cells(block); i < end; ++i)
        count += rows[i].size();
      start[block + 1] = count;
    });
    for (std::size_t block = 0; block < g.blocks; ++block)
      start[block + 1] += start[block];
    into.offsets.resize(g.N + 1);
    into.species.resize(start.back());
    into.mass.resize(start.back());
    into.offsets[0] = 0;
    blocks([&](std::size_t block) {
      auto at = start[block];
      for (auto [i, end] = block_cells(block); i < end; ++i) {
        for (const auto &[key, value] : rows[i]) {
          into.species[at] = key;
          into.mass[at] = value;
          ++at;
        }
        into.offsets[i + 1] = at;
      }
    });
  };
  pack(cells, out.state.masses);
  if (has_passives)
    pack(passives, out.state.passives);
  out.eos = std::move(eos);
  return out;
}
} // namespace ma::experimental::game_kernel
