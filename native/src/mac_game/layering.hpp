#pragma once
#include "flow_local.hpp"
#include <span>
#include <sstream>
#include "totals.hpp"
namespace ma::experimental::game_kernel {
struct LayerResult {
  State state;
  std::size_t faces = 0;
  // The faces among them that exchanged liquid, or gas through a one-cell
  // opening: all but the convective adjustment's. A step longer than a
  // frame repeats layering only for these (Tick).
  std::size_t capped_faces = 0;
  double released = 0;
  // Profile 8: momentum the parcels carried into closed faces.
  double wall_x = 0, wall_y = 0, wall_ops_x = 0, wall_ops_y = 0;
};
namespace layering_detail {
struct Pick {
  std::uint64_t phase = 0;
  double mass = 0, volume = 0;
  bool valid = false;
  double density() const { return mass / volume; }
};
using Constituent = std::pair<std::uint32_t, double>;
// A parcel's constituents are [begin, end) of its block's `constituents`
// while the faces are scanned, then `data` points at them.
struct Parcel {
  std::size_t from, to, begin = 0, end = 0;
  double mass = 0, energy = 0;
  const Constituent *data = nullptr;
  std::span<const Constituent> species() const { return {data, end - begin}; }
};
// A cell that sends or receives a parcel: its new inventory (slices of its
// block's `kept` lists), its energy after the parcels and potential release,
// and its mass.
struct Changed {
  std::size_t cell = 0, mass_begin = 0, mass_end = 0, passive_begin = 0,
              passive_end = 0;
  double energy = 0, mass = 0;
};
} // namespace layering_detail
// Capacity that layer() reuses across calls; every entry it reads is
// written first in the same call. `spare` lends the buffers of a state the
// caller no longer needs to the next result (Tick hands back the state each
// result replaces), so a call allocates nothing once warm.
struct LayerScratch {
  std::vector<layering_detail::Pick> light, heavy;
  std::vector<double> old_mass, new_mass, gas_mass;
  struct Block {
    // Parcels of the block's faces, by pass: liquid layering, convective
    // adjustment, liquid resting on gas, gas through a one-cell opening;
    // their constituents; and each pass's first parcel in the joined list.
    std::array<std::vector<layering_detail::Parcel>, 4> parcels;
    std::vector<layering_detail::Constituent> constituents;
    std::array<std::size_t, 4> first{};
    std::size_t faces = 0, capped = 0, masses = 0, passives = 0;
    std::vector<layering_detail::Pick> phases;
    // The block's exchanging cells and their new inventories.
    std::vector<layering_detail::Changed> changed;
    std::vector<std::uint32_t> kept_species;
    std::vector<double> kept_mass, kept_passive_mass;
    std::vector<std::uint64_t> kept_passive_species;
    bool removed_inertia = false;
  };
  std::vector<Block> blocks;
  std::vector<layering_detail::Parcel> parcels;
  // Per pass and face, the call that last gave the face parcels (`call`
  // counts calls) and the first of its two parcels in its block's list.
  std::array<std::vector<std::uint32_t>, 4> face_call, face_parcel;
  // Faces a call's carried momentum touched (stamped with `call`).
  std::vector<std::uint32_t> touched_x, touched_y;
  std::uint32_t call = 0;
  State spare;
};
// Jacobi equal-volume buoyant exchanges. Miscible constituents share one phase.
// Donor-specific U and mass-carried passives use shared parcels. Profiles 2-7
// retain face J; profile 8 carries the parcels' donor face velocities. Actual
// face-K change and gravity release go to local U.
//
// Cell and face scans run in the grid's fixed row blocks; each block's
// parcels are kept by pass and joined in block order, which is the order of
// the serial face loops (all liquid layering faces in face order, then the
// convective adjustment's, then liquid on gas, then one-cell openings).
// Cells that exchange are few: they are updated one by one in cell order,
// and the result's other cells are copied in row blocks.
inline LayerResult
layer(const ma::experimental::MacGrid &source,
      const std::vector<Species> &species, const State &initial,
      const std::vector<Eos> &eos, double dt, double gravity,
      const ma::experimental::mac_kernel::Grid *cached_grid = nullptr,
      ma::experimental::mac_kernel::Pool *executor = nullptr,
      bool carry_momentum = false, RegionAudit *audit = nullptr,
      bool convective = false, bool sinking = false,
      bool count_only = false, LayerScratch *reuse = nullptr) {
  namespace k = ma::experimental::mac_kernel;
  using layering_detail::Parcel;
  using layering_detail::Pick;
  k::Grid built;
  if (!cached_grid)
    built = k::Grid(source);
  const auto &g = cached_grid ? *cached_grid : built;
  LayerResult out;
  if (gravity == 0)
    return out;
  LayerScratch local_scratch;
  auto &scratch = reuse ? *reuse : local_scratch;
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
  const double fraction = std::min(.25, -std::expm1(-dt / 2.));
  auto &light = scratch.light, &heavy = scratch.heavy;
  auto &old_mass = scratch.old_mass, &new_mass = scratch.new_mass,
       &gas_mass = scratch.gas_mass;
  light.resize(g.N);
  heavy.resize(g.N);
  old_mass.resize(g.N);
  new_mass.resize(g.N);
  if (convective && !count_only)
    gas_mass.resize(g.N);
  scratch.blocks.resize(g.blocks);
  // Call stamps: arrays sized anew start from zero.
  if (scratch.face_call[0].size() != g.NY || scratch.face_call[3].size() != g.NX ||
      scratch.touched_x.size() != g.NX || scratch.touched_y.size() != g.NY ||
      scratch.call == std::numeric_limits<std::uint32_t>::max()) {
    for (std::size_t pass = 0; pass < 4; ++pass) {
      scratch.face_call[pass].assign(pass < 3 ? g.NY : g.NX, 0);
      scratch.face_parcel[pass].assign(pass < 3 ? g.NY : g.NX, 0);
    }
    scratch.touched_x.assign(g.NX, 0);
    scratch.touched_y.assign(g.NY, 0);
    scratch.call = 0;
  }
  const auto call = ++scratch.call;
  const auto phase = [&](std::uint32_t key) {
    return species[key].liquid_group ? std::uint64_t(species[key].liquid_group)
                                     : (std::uint64_t(1) << 32) + key;
  };
  // Keep map key/accumulation order while reusing one small phase buffer.
  blocks([&](std::size_t block) {
  auto &phases = scratch.blocks[block].phases;
  phases.reserve(species.size());
  for (auto [i, end] = block_cells(block); i < end; ++i) {
    light[i] = heavy[i] = Pick{};
    old_mass[i] = 0;
    phases.clear();
    for (std::size_t a = initial.masses.offsets[i];
         a < initial.masses.offsets[i + 1]; ++a) {
      const auto key = initial.masses.species[a];
      const auto m = initial.masses.mass[a];
      old_mass[i] += m;
      if (species[key].gas)
        continue;
      const auto identity = phase(key);
      auto at = std::lower_bound(phases.begin(), phases.end(), identity,
          [](const Pick &p, std::uint64_t id) { return p.phase < id; });
      if (at == phases.end() || at->phase != identity)
        at = phases.insert(at, Pick{identity});
      auto &p = *at;
      p.phase = phase(key);
      p.mass += m;
      p.volume += m / species[key].rho0;
      p.valid = true;
    }
    for (const auto &p : phases) {
      if (!light[i].valid || p.density() < light[i].density())
        light[i] = p;
      if (!heavy[i].valid || p.density() > heavy[i].density())
        heavy[i] = p;
    }
    if (convective && !count_only) {
      gas_mass[i] = 0;
      for (std::size_t a = initial.masses.offsets[i];
           a < initial.masses.offsets[i + 1]; ++a)
        if (species[initial.masses.species[a]].gas)
          gas_mass[i] += initial.masses.mass[a];
    }
  }
  });
  using Block = LayerScratch::Block;
  // A face's two parcels in `pass` start at the block's next parcel.
  const auto mark = [&](std::size_t pass, std::size_t f, Block &b) {
    scratch.face_call[pass][f] = call;
    scratch.face_parcel[pass][f] = static_cast<std::uint32_t>(b.parcels[pass].size());
  };
  // Liquid layering across vertical face f.
  const auto liquid_face = [&](std::size_t f, Block &counts) {
    auto &parcels = counts.parcels[0];
    const auto lower = gravity > 0 ? g.yl[f] : g.yr[f],
               upper = gravity > 0 ? g.yr[f] : g.yl[f];
    if (lower == k::none || upper == k::none || g.closed(true, f))
      return;
    const auto &l = light[lower];
    const auto &top = heavy[upper];
    if (!l.valid || !top.valid || l.phase == top.phase ||
        !(l.density() < top.density()))
      return;
    const double exchanged = fraction * std::min(l.volume, top.volume);
    mark(0, f, counts);
    const auto add_parcel = [&](std::size_t from, std::size_t to,
                                const Pick &chosen) {
      Parcel parcel{from, to, counts.constituents.size()};
      const double share = exchanged / chosen.volume;
      const auto &state = eos[from];
      for (std::size_t a = initial.masses.offsets[from];
           a < initial.masses.offsets[from + 1]; ++a) {
        const auto key = initial.masses.species[a];
        const auto &s = species[key];
        if (s.gas || phase(key) != chosen.phase)
          continue;
        const double moved = initial.masses.mass[a] * share;
        counts.constituents.emplace_back(key, moved);
        parcel.mass += moved;
        const double specific =
            s.offset + s.cv * state.temperature +
            liquid_specific_elastic(s,state.pressure);
        parcel.energy += moved * specific;
      }
      parcel.end = counts.constituents.size();
      parcels.push_back(parcel);
    };
    add_parcel(lower, upper, l);
    add_parcel(upper, lower, top);
    ++counts.faces;
    ++counts.capped;
  };
  // Profile 11 sub-grid convective adjustment. Where the gas of a cell is
  // less dense than the gas of the cell above it, the stratification is
  // buoyantly unstable and real gas overturns within seconds; the
  // discrete solver can hold such a layer indefinitely when the columns
  // are identical. The two cells swap equal volumes of their gas (any
  // liquid stays), each share carrying its own EOS energy and passives;
  // the released potential energy is heat (below), so species, U+K+G and
  // passives are exact. The share is set by the free-convection speed
  // w = sqrt(g h (rho_up - rho_low) / rho_mean) over one cell height,
  // capped like layering. A stable or neutral face exchanges nothing.
  // The adjustment stands in for buoyant overturning that the resolved
  // flow does not produce, so it applies only to quiescent cells: where
  // the resolved speed on any face of either cell exceeds w, the resolved
  // flow already carries the mixing and the face exchanges nothing. This
  // also bounds the face kinetic energy the carried parcel momentum can
  // book to a cell (at most about w^2 per kilogram), which a cell's
  // thermal energy always covers; without it a parcel carried at a
  // plume's speed into a near-vacuum cell overdrew that cell's U.
  const double h = source.spacing_m;
  const auto face_speed = [&](bool x, std::size_t face) {
    const double j = (x ? initial.jx : initial.jy)[face];
    if (j == 0)
      return 0.;
    const auto l = (x ? g.xl : g.yl)[face], r = (x ? g.xr : g.yr)[face];
    const double inertia = .5 * ((l == k::none ? 0 : old_mass[l]) +
                                 (r == k::none ? 0 : old_mass[r]));
    return inertia > 0 ? std::abs(j / inertia)
                       : std::numeric_limits<double>::infinity();
  };
  const auto quiescent = [&](std::size_t a, std::size_t b, double speed) {
    bool still = true;
    for (const auto cell : {a, b})
      still = still && face_speed(true, g.left_face(cell)) <= speed &&
              face_speed(true, g.right_face(cell)) <= speed &&
              face_speed(false, g.bottom_face(cell)) <= speed &&
              face_speed(false, g.top_face(cell)) <= speed;
    return still;
  };
  const auto convective_face = [&](std::size_t f, Block &counts) {
    auto &parcels = counts.parcels[1];
    const auto gas_density = [&](std::size_t i) {
      return !g.wall[i] && gas_mass[i] > 0 && eos[i].gas_volume > 0
                 ? gas_mass[i] / eos[i].gas_volume
                 : 0.;
    };
    const auto lower = gravity > 0 ? g.yl[f] : g.yr[f],
               upper = gravity > 0 ? g.yr[f] : g.yl[f];
    if (lower == k::none || upper == k::none || g.closed(true, f))
      return;
    const double low = gas_density(lower), high = gas_density(upper);
    if (!(low > 0) || !(low < high))
      return;
    const double speed =
        std::sqrt(std::abs(gravity) * h * (high - low) / (.5 * (high + low)));
    if (!quiescent(lower, upper, speed))
      return;
    const double share = std::min(.25, -std::expm1(-dt * speed / h));
    const double volume =
        share * std::min(eos[lower].gas_volume, eos[upper].gas_volume);
    if (!(volume > 0))
      return;
    mark(1, f, counts);
    const auto add_gas = [&](std::size_t from, std::size_t to) {
      Parcel parcel{from, to, counts.constituents.size()};
      const double taken = volume / eos[from].gas_volume;
      for (std::size_t a = initial.masses.offsets[from];
           a < initial.masses.offsets[from + 1]; ++a) {
        const auto key = initial.masses.species[a];
        const auto &s = species[key];
        if (!s.gas)
          continue;
        const double moved = initial.masses.mass[a] * taken;
        counts.constituents.emplace_back(key, moved);
        parcel.mass += moved;
        parcel.energy += moved * (s.offset + s.cv * eos[from].temperature);
      }
      parcel.end = counts.constituents.size();
      parcels.push_back(parcel);
    };
    add_gas(lower, upper);
    add_gas(upper, lower);
    ++counts.faces;
  };
  // Profile 12 sub-grid exchange of liquid resting on gas. A face carries
  // one velocity, so where a cell's liquid lies on the gas of the cell
  // below it and that gas has no other way out (a one-tile opening into a
  // closed space), the resolved flow only compresses the gas until it
  // bears the liquid, and the liquid stands on it for good. Real liquid
  // falls through such an opening while the gas rises past it. The two
  // cells swap equal volumes of the upper cell's liquid and the lower
  // cell's gas, each share carrying its own EOS energy and passives; the
  // released potential energy is heat (below), so species, U+K+G and
  // passives are exact. The share follows the free-fall speed over one
  // cell, w = sqrt(g h (rho_liquid - rho_gas) / rho_mean), capped like
  // layering; as for the convective adjustment, a face whose cells
  // already move faster than w exchanges nothing.
  const auto sinking_face = [&](std::size_t f, Block &counts) {
    auto &parcels = counts.parcels[2];
    const auto lower = gravity > 0 ? g.yl[f] : g.yr[f],
               upper = gravity > 0 ? g.yr[f] : g.yl[f];
    // A thousandth of a cell of liquid at least: mist and droplets fall
    // through their own cell's gas as they always did, and a colony's air
    // holds them in thousands of cells.
    if (lower == k::none || upper == k::none || g.closed(true, f) ||
        g.wall[lower] || g.wall[upper] || !(eos[lower].gas_volume > 0) ||
        !(eos[upper].liquid_volume > 1e-3 * g.V))
      return;
    double gas = 0, liquid = 0;
    for (std::size_t a = initial.masses.offsets[lower];
         a < initial.masses.offsets[lower + 1]; ++a)
      if (species[initial.masses.species[a]].gas)
        gas += initial.masses.mass[a];
    for (std::size_t a = initial.masses.offsets[upper];
         a < initial.masses.offsets[upper + 1]; ++a)
      if (!species[initial.masses.species[a]].gas)
        liquid += initial.masses.mass[a];
    if (!(gas > 0) || !(liquid > 0))
      return;
    const double rare = gas / eos[lower].gas_volume,
                 dense = liquid / eos[upper].liquid_volume;
    if (!(rare < dense))
      return;
    const double speed = std::sqrt(std::abs(gravity) * h * (dense - rare) /
                                   (.5 * (dense + rare)));
    if (!quiescent(lower, upper, speed))
      return;
    const double share = std::min(.25, -std::expm1(-dt * speed / h));
    const double volume =
        share * std::min(eos[lower].gas_volume, eos[upper].liquid_volume);
    if (!(volume > 0))
      return;
    mark(2, f, counts);
    Parcel rising{lower, upper, counts.constituents.size()};
    for (std::size_t a = initial.masses.offsets[lower];
         a < initial.masses.offsets[lower + 1]; ++a) {
      const auto key = initial.masses.species[a];
      const auto &s = species[key];
      if (!s.gas)
        continue;
      const double moved =
          initial.masses.mass[a] * (volume / eos[lower].gas_volume);
      counts.constituents.emplace_back(key, moved);
      rising.mass += moved;
      rising.energy += moved * (s.offset + s.cv * eos[lower].temperature);
    }
    rising.end = counts.constituents.size();
    Parcel falling{upper, lower, counts.constituents.size()};
    for (std::size_t a = initial.masses.offsets[upper];
         a < initial.masses.offsets[upper + 1]; ++a) {
      const auto key = initial.masses.species[a];
      const auto &s = species[key];
      if (s.gas)
        continue;
      const double moved =
          initial.masses.mass[a] * (volume / eos[upper].liquid_volume);
      counts.constituents.emplace_back(key, moved);
      falling.mass += moved;
      falling.energy +=
          moved * (s.offset + s.cv * eos[upper].temperature +
                   liquid_specific_elastic(s, eos[upper].pressure));
    }
    falling.end = counts.constituents.size();
    parcels.push_back(rising);
    parcels.push_back(falling);
    ++counts.faces;
    ++counts.capped;
  };
  // Profile 12 sub-grid exchange of gas through an opening one cell high.
  // Where two rooms of different gas density meet (a heavy gas beside a
  // light one, the same gas hot and cold), the dense gas really runs out
  // along the floor of the opening while the light gas returns above it.
  // Two cells or more of opening resolve that; one cell has one velocity
  // on its face, so nothing passed at all: carbon dioxide stayed in its
  // room beside an oxygen room at the same pressure, and two rooms joined
  // by a one-cell hole kept an 80 K split. Across an open horizontal face
  // one of whose cells is closed above and below (the cell in the wall,
  // any cell of a corridor one cell high), the two cells swap equal
  // volumes of their gas, each share carrying its own EOS energy and
  // passives. The share is that of a lock exchange filling half the
  // opening each way at u = sqrt(g h |rho_l - rho_r| / rho_mean) / 2,
  // capped like layering, and, as for the convective adjustment, a face
  // whose cells already move faster than u exchanges nothing. Gases of
  // equal density do not exchange: only diffusion would mix them.
  const auto slot_face = [&](std::size_t f, Block &counts) {
    auto &parcels = counts.parcels[3];
    const auto l = g.xl[f], r = g.xr[f];
    if (l == k::none || r == k::none || !(g.slot[l] || g.slot[r]) ||
        g.closed(false, f) || !(eos[l].gas_volume > 0) ||
        !(eos[r].gas_volume > 0))
      return;
    const auto gas_density = [&](std::size_t i) {
      double mass = 0;
      for (std::size_t a = initial.masses.offsets[i];
           a < initial.masses.offsets[i + 1]; ++a)
        if (species[initial.masses.species[a]].gas)
          mass += initial.masses.mass[a];
      return mass / eos[i].gas_volume;
    };
    const double left = gas_density(l), right = gas_density(r);
    // Densities within a thousandth of each other (0.3 K, or the unevenness
    // any moving room has) exchange nothing: the lock exchange they would
    // drive creeps at 5 cm/s, and nearly every such opening in a colony
    // would otherwise count as exchanging.
    if (!(left > 0) || !(right > 0) ||
        !(std::abs(left - right) > 1e-3 * .5 * (left + right)))
      return;
    const double speed =
        .5 * std::sqrt(std::abs(gravity) * h * std::abs(left - right) /
                       (.5 * (left + right)));
    if (!quiescent(l, r, speed))
      return;
    const double share = std::min(.25, -std::expm1(-dt * speed / (2 * h)));
    const double volume =
        share * std::min(eos[l].gas_volume, eos[r].gas_volume);
    if (!(volume > 0))
      return;
    mark(3, f, counts);
    for (const auto &[from, to] : {std::pair{l, r}, std::pair{r, l}}) {
      Parcel parcel{from, to, counts.constituents.size()};
      const double taken = volume / eos[from].gas_volume;
      for (std::size_t a = initial.masses.offsets[from];
           a < initial.masses.offsets[from + 1]; ++a) {
        const auto key = initial.masses.species[a];
        const auto &s = species[key];
        if (!s.gas)
          continue;
        const double moved = initial.masses.mass[a] * taken;
        counts.constituents.emplace_back(key, moved);
        parcel.mass += moved;
        parcel.energy += moved * (s.offset + s.cv * eos[from].temperature);
      }
      parcel.end = counts.constituents.size();
      parcels.push_back(parcel);
    }
    ++counts.faces;
    ++counts.capped;
  };
  // Every face reads only the opening state and the cell pass above.
  blocks([&](std::size_t block) {
    auto &b = scratch.blocks[block];
    for (auto &list : b.parcels)
      list.clear();
    b.constituents.clear();
    b.faces = b.capped = 0;
    const auto y0 = g.block_row_begin(block);
    for (std::size_t f = y0 * g.W; f < g.block_yrow_end(block) * g.W; ++f) {
      liquid_face(f, b);
      if (convective && !count_only)
        convective_face(f, b);
      if (sinking)
        sinking_face(f, b);
    }
    if (sinking)
      for (std::size_t f = y0 * g.XW; f < g.block_row_end(block) * g.XW; ++f)
        slot_face(f, b);
  });
  auto &parcels = scratch.parcels;
  parcels.clear();
  for (std::size_t pass = 0; pass < 4; ++pass)
    for (auto &b : scratch.blocks) {
      b.first[pass] = parcels.size();
      for (auto parcel : b.parcels[pass]) {
        parcel.data = b.constituents.data() + parcel.begin;
        parcels.push_back(parcel);
      }
    }
  for (const auto &b : scratch.blocks) {
    out.faces += b.faces;
    out.capped_faces += b.capped;
  }
  // `count_only`: the faces that would exchange, and nothing else (no state,
  // no audit receipt); the convective adjustment's faces, which are not
  // capped, are not looked for.
  if (count_only || parcels.empty())
    return out;
  const bool has_passives = !initial.passives.mass.empty();
  // A cell's parcels come from its own faces: liquid layering, the
  // convective adjustment and liquid on gas across its bottom and top faces,
  // one-cell openings across its left and right faces. Each such face has
  // two consecutive parcels in the joined list. Outgoing and incoming
  // parcels are each listed in parcel order.
  struct CellParcels {
    std::array<std::size_t, 16> outgoing, incoming;
    std::size_t out_count = 0, in_count = 0;
  };
  const auto block_of = [&](bool x, std::size_t f) {
    const auto row = f / (x ? g.XW : g.W);
    return std::min(g.blocks - 1, row / g.rows_per_block);
  };
  const auto cell_parcels = [&](std::size_t i, CellParcels &list) {
    list.out_count = list.in_count = 0;
    const auto take = [&](std::size_t pass, bool x, std::size_t f) {
      if (scratch.face_call[pass][f] != call)
        return;
      const auto first = scratch.blocks[block_of(x, f)].first[pass] +
                         scratch.face_parcel[pass][f];
      for (const auto n : {first, first + 1}) {
        if (parcels[n].from == i)
          list.outgoing[list.out_count++] = n;
        else
          list.incoming[list.in_count++] = n;
      }
    };
    const auto bottom = g.bottom_face(i), top = g.top_face(i);
    for (std::size_t pass = 0; pass < 3; ++pass) {
      take(pass, false, bottom);
      take(pass, false, top);
    }
    take(3, true, g.left_face(i));
    take(3, true, g.right_face(i));
    std::sort(list.outgoing.begin(), list.outgoing.begin() + static_cast<std::ptrdiff_t>(list.out_count));
    std::sort(list.incoming.begin(), list.incoming.begin() + static_cast<std::ptrdiff_t>(list.in_count));
    return list.out_count + list.in_count != 0;
  };
  // Each exchanging cell's new inventory and energy, by row block in cell
  // order (the first failing cell of the lowest block throws, as the serial
  // pass would): its outgoing and then incoming parcels, then the potential
  // energy each of its parcels releases, in parcel order.
  const auto drop = [&](const Parcel &parcel) {
    return parcel.mass * gravity * source.spacing_m *
           (double(parcel.from / g.W) - double(parcel.to / g.W));
  };
  blocks([&](std::size_t block) {
    auto &b = scratch.blocks[block];
    b.changed.clear();
    b.kept_species.clear();
    b.kept_mass.clear();
    b.kept_passive_species.clear();
    b.kept_passive_mass.clear();
    const auto [first, last] = block_cells(block);
    b.masses = initial.masses.offsets[last] - initial.masses.offsets[first];
    b.passives = has_passives ? initial.passives.offsets[last] - initial.passives.offsets[first] : 0;
    std::map<std::uint32_t, double> mass;
    std::map<std::uint64_t, double> passive;
    CellParcels list;
    for (auto i = first; i < last; ++i) {
      if (g.wall[i] || !cell_parcels(i, list))
        continue;
      mass.clear();
      passive.clear();
      for (std::size_t a = initial.masses.offsets[i];
           a < initial.masses.offsets[i + 1]; ++a)
        mass[initial.masses.species[a]] = initial.masses.mass[a];
      if (has_passives)
        for (std::size_t a = initial.passives.offsets[i];
             a < initial.passives.offsets[i + 1]; ++a)
          passive[initial.passives.species[a]] = initial.passives.mass[a];
      double energy = initial.energy[i];
      const auto apply = [&](const Parcel &parcel, double sign) {
        for (const auto &[key, value] : parcel.species())
          mass[key] += sign * value;
        energy += sign * parcel.energy;
        if (has_passives &&
            initial.passives.offsets[parcel.from] !=
                initial.passives.offsets[parcel.from + 1]) {
          // A parcel holds one phase. A plain passive moves with the mass
          // share; a carried one (profile 12) with its carrier's share, or
          // with the phase the donor holds when it lacks the carrier.
          double gas = 0, liquid = 0;
          for (std::size_t a = initial.masses.offsets[parcel.from];
               a < initial.masses.offsets[parcel.from + 1]; ++a)
            (species[initial.masses.species[a]].gas ? gas : liquid) +=
                initial.masses.mass[a];
          const bool gas_parcel = !parcel.species().empty() &&
                                  species[parcel.species().front().first].gas;
          for (std::size_t a = initial.passives.offsets[parcel.from];
               a < initial.passives.offsets[parcel.from + 1]; ++a) {
            const auto identity = initial.passives.species[a];
            double share = parcel.mass / old_mass[parcel.from];
            if (identity & ma::passive_carrier_mask) {
              const bool with_gas = identity & ma::passive_gas_carrier
                                        ? gas > 0
                                        : !(liquid > 0);
              share = with_gas != gas_parcel ? 0
                      : parcel.mass / (gas_parcel ? gas : liquid);
            }
            passive[identity] += sign * initial.passives.mass[a] * share;
          }
        }
      };
      for (std::size_t n = 0; n < list.out_count; ++n)
        apply(parcels[list.outgoing[n]], -1);
      for (std::size_t n = 0; n < list.in_count; ++n)
        apply(parcels[list.incoming[n]], 1);
      layering_detail::Changed entry;
      entry.cell = i;
      entry.mass_begin = b.kept_mass.size();
      double total = 0;
      for (const auto &[key, value] : mass) {
        if (value < 0)
          throw std::runtime_error("Private layering mass budget failed");
        if (value > 0) {
          b.kept_species.push_back(key);
          b.kept_mass.push_back(value);
          total += value;
        }
      }
      entry.mass_end = b.kept_mass.size();
      entry.mass = total;
      entry.passive_begin = b.kept_passive_mass.size();
      if (has_passives)
        for (const auto &[key, value] : passive) {
          if (value < 0)
            throw std::runtime_error("Private layering passive budget failed");
          if (value > 0) {
            b.kept_passive_species.push_back(key);
            b.kept_passive_mass.push_back(value);
          }
        }
      entry.passive_end = b.kept_passive_mass.size();
      // The potential energy each parcel releases, half to each end.
      for (std::size_t o = 0, n = 0; o < list.out_count || n < list.in_count;) {
        const bool outgoing = n == list.in_count ||
                              (o < list.out_count && list.outgoing[o] < list.incoming[n]);
        energy += .5 * drop(parcels[outgoing ? list.outgoing[o++] : list.incoming[n++]]);
      }
      entry.energy = energy;
      b.masses = b.masses - (initial.masses.offsets[i + 1] - initial.masses.offsets[i]) +
                 (entry.mass_end - entry.mass_begin);
      if (has_passives)
        b.passives = b.passives -
                     (initial.passives.offsets[i + 1] - initial.passives.offsets[i]) +
                     (entry.passive_end - entry.passive_begin);
      b.changed.push_back(entry);
    }
  });
  // The result: the exchanging cells' new inventories and energies, every
  // other cell and face as it was, built in row blocks into the spare
  // state's buffers.
  auto &result = out.state;
  result = std::move(scratch.spare);
  scratch.spare = {};
  result.energy.resize(g.N);
  // Face momenta are copied by row block below when they cover the grid.
  const bool face_rows = initial.jx.size() == g.NX && initial.jy.size() == g.NY;
  if (face_rows) {
    result.jx.resize(g.NX);
    result.jy.resize(g.NY);
  } else {
    result.jx = initial.jx;
    result.jy = initial.jy;
  }
  result.masses.offsets.resize(g.N + 1);
  if (has_passives)
    result.passives.offsets.resize(g.N + 1);
  else
    result.passives = initial.passives;
  std::size_t mass_total = 0, passive_total = 0;
  for (auto &b : scratch.blocks) {
    const auto masses = b.masses, passives = b.passives;
    b.masses = mass_total;
    b.passives = passive_total;
    mass_total += masses;
    passive_total += passives;
  }
  result.masses.species.resize(mass_total);
  result.masses.mass.resize(mass_total);
  if (has_passives) {
    result.passives.species.resize(passive_total);
    result.passives.mass.resize(passive_total);
  }
  blocks([&](std::size_t block) {
    const auto &b = scratch.blocks[block];
    const auto [first, last] = block_cells(block);
    std::size_t c = 0;
    auto mass_at = b.masses, passive_at = b.passives;
    for (auto i = first; i < last; ++i) {
      if (c < b.changed.size() && b.changed[c].cell == i) {
        const auto &entry = b.changed[c++];
        for (auto a = entry.mass_begin; a < entry.mass_end; ++a, ++mass_at) {
          result.masses.species[mass_at] = b.kept_species[a];
          result.masses.mass[mass_at] = b.kept_mass[a];
        }
        if (has_passives)
          for (auto a = entry.passive_begin; a < entry.passive_end; ++a, ++passive_at) {
            result.passives.species[passive_at] = b.kept_passive_species[a];
            result.passives.mass[passive_at] = b.kept_passive_mass[a];
          }
        result.energy[i] = entry.energy;
        new_mass[i] = entry.mass;
      } else {
        for (auto a = initial.masses.offsets[i]; a < initial.masses.offsets[i + 1]; ++a, ++mass_at) {
          result.masses.species[mass_at] = initial.masses.species[a];
          result.masses.mass[mass_at] = initial.masses.mass[a];
        }
        if (has_passives)
          for (auto a = initial.passives.offsets[i]; a < initial.passives.offsets[i + 1];
               ++a, ++passive_at) {
            result.passives.species[passive_at] = initial.passives.species[a];
            result.passives.mass[passive_at] = initial.passives.mass[a];
          }
        result.energy[i] = initial.energy[i];
        new_mass[i] = old_mass[i];
      }
      result.masses.offsets[i + 1] = mass_at;
      if (has_passives)
        result.passives.offsets[i + 1] = passive_at;
    }
    if (!face_rows)
      return;
    const auto y0 = g.block_row_begin(block);
    std::copy(initial.jx.begin() + static_cast<std::ptrdiff_t>(y0 * g.XW),
              initial.jx.begin() + static_cast<std::ptrdiff_t>(g.block_row_end(block) * g.XW),
              result.jx.begin() + static_cast<std::ptrdiff_t>(y0 * g.XW));
    std::copy(initial.jy.begin() + static_cast<std::ptrdiff_t>(y0 * g.W),
              initial.jy.begin() + static_cast<std::ptrdiff_t>(g.block_yrow_end(block) * g.W),
              result.jy.begin() + static_cast<std::ptrdiff_t>(y0 * g.W));
  });
  result.masses.offsets[0] = 0;
  if (has_passives)
    result.passives.offsets[0] = 0;
  // The released potential energy's total, in parcel order (each cell took
  // its share above).
  DoubleDouble released;
  for (const auto &parcel : parcels)
    released.add(drop(parcel));
  out.released = released.value();
  const auto m = [](const std::vector<double> &values, k::Index i) {
    return i == k::none ? 0 : values[i];
  };
  // A cell this stage leaves without positive thermal energy is named with
  // what moved through it (the fault would otherwise surface a step later in
  // the EOS with only the final state). Checked by row block in cell order.
  const auto audit_touched = [&]() {
    if (!convective && !sinking)
      return;
    blocks([&](std::size_t block) {
      for (const auto &entry : scratch.blocks[block].changed) {
        const auto i = entry.cell;
        double offset = 0, mass_sum = 0;
        for (auto a = out.state.masses.offsets[i]; a < out.state.masses.offsets[i + 1]; ++a) {
          offset += out.state.masses.mass[a] * species[out.state.masses.species[a]].offset;
          mass_sum += out.state.masses.mass[a];
        }
        if (mass_sum == 0 || out.state.energy[i] - offset > 0)
          continue;
        double was_offset = 0;
        for (auto a = initial.masses.offsets[i]; a < initial.masses.offsets[i + 1]; ++a)
          was_offset += initial.masses.mass[a] * species[initial.masses.species[a]].offset;
        std::ostringstream text;
        text.precision(4);
        text << "Layering cell " << i << " no thermal U " << initial.energy[i] << ">" << out.state.energy[i]
             << " off " << was_offset << ">" << offset << " T " << eos[i].temperature << " m " << old_mass[i]
             << ">" << new_mass[i] << ";";
        CellParcels list;
        cell_parcels(i, list);
        for (std::size_t n = 0; n < list.out_count; ++n) {
          const auto &p = parcels[list.outgoing[n]];
          text << " out>" << p.to << " " << p.mass << "kg " << p.energy << "J;";
        }
        for (std::size_t n = 0; n < list.in_count; ++n) {
          const auto &p = parcels[list.incoming[n]];
          text << " in<" << p.from << " " << p.mass << "kg " << p.energy << "J T" << eos[p.from].temperature << ";";
        }
        throw std::runtime_error(text.str());
      }
    });
  };
  if (carry_momentum) {
    // Profile 8: each parcel keeps the velocities of the faces it leaves.
    // Its half on each face of its source cell moves to the face on the same
    // side of its destination (velocities from the initial momentum and
    // masses); momentum reaching a closed face is a wall impulse. Each
    // face's kinetic energy change, in face order, is local heat.
    auto &jx = out.state.jx;
    auto &jy = out.state.jy;
    const auto fluid = [&](bool x, std::size_t f) {
      const auto l = (x ? g.xl : g.yl)[f], r = (x ? g.xr : g.yr)[f];
      return l != k::none && r != k::none && !g.wall[l] && !g.wall[r] &&
             !g.closed(!x, f);
    };
    const auto velocity = [&](bool x, std::size_t f) {
      const double j = (x ? initial.jx : initial.jy)[f];
      if (j == 0)
        return 0.;
      const double I0 = .5 * (m(old_mass, (x ? g.xl : g.yl)[f]) +
                              m(old_mass, (x ? g.xr : g.yr)[f]));
      if (!(I0 > 0))
        throw std::runtime_error("Private layering J without inertia");
      return j / I0;
    };
    const auto side = [&](std::size_t cell, unsigned which) {
      return which == 0   ? std::pair{true, g.left_face(cell)}
             : which == 1 ? std::pair{true, g.right_face(cell)}
             : which == 2 ? std::pair{false, g.bottom_face(cell)}
                          : std::pair{false, g.top_face(cell)};
    };
    DoubleDouble wall_x, wall_y, ops_x, ops_y;
    const auto touch = [&](std::pair<bool, k::Index> face) {
      (face.first ? scratch.touched_x : scratch.touched_y)[face.second] = call;
    };
    for (const auto &parcel : parcels)
      for (unsigned which = 0; which < 4; ++which) {
        const auto from = side(parcel.from, which),
                   to = side(parcel.to, which);
        touch(from);
        touch(to);
        const double amount = .5 * parcel.mass * velocity(from.first, from.second);
        if (amount == 0)
          continue;
        (from.first ? jx : jy)[from.second] -= amount;
        if (fluid(to.first, to.second))
          (to.first ? jx : jy)[to.second] += amount;
        else {
          (to.first ? wall_x : wall_y).add(-amount);
          (to.first ? ops_x : ops_y).add(std::abs(amount));
          if (audit)
            audit->momentum_receipt(parcel.to, to.first, -amount, std::abs(amount));
        }
      }
    // The kinetic energy change of each touched face (x faces in face
    // order, then y faces): first whether any face lost its inertia, then
    // each cell's shares in that face order (its left, right, bottom and
    // top faces), as the serial loop added them.
    const auto face_heat = [&](bool x, std::size_t f, double &heat, double &sum) {
      const double j0 = (x ? initial.jx : initial.jy)[f], j1 = (x ? jx : jy)[f];
      if (j0 == 0 && j1 == 0)
        return false;
      const auto l = (x ? g.xl : g.yl)[f], r = (x ? g.xr : g.yr)[f];
      const double I0 = .5 * (m(old_mass, l) + m(old_mass, r)),
                   I1 = .5 * (m(new_mass, l) + m(new_mass, r));
      if ((j0 != 0 && !(I0 > 0)) || !(I1 > 0))
        throw std::runtime_error("Private layering removed face inertia");
      heat = (j0 == 0 ? 0. : .5 * j0 * (j0 / I0)) -
             (j1 == 0 ? 0. : .5 * j1 * (j1 / I1));
      sum = m(new_mass, l) + m(new_mass, r);
      return true;
    };
    blocks([&](std::size_t block) {
      auto &b = scratch.blocks[block];
      b.removed_inertia = false;
      double heat = 0, sum = 0;
      const auto check = [&](bool x, std::size_t first, std::size_t end) {
        const auto &touched = x ? scratch.touched_x : scratch.touched_y;
        for (auto f = first; f < end && !b.removed_inertia; ++f)
          if (touched[f] == call) {
            try {
              face_heat(x, f, heat, sum);
            } catch (const std::runtime_error &) {
              b.removed_inertia = true;
            }
          }
      };
      const auto y0 = g.block_row_begin(block);
      check(true, y0 * g.XW, g.block_row_end(block) * g.XW);
      check(false, y0 * g.W, g.block_yrow_end(block) * g.W);
    });
    for (const auto &b : scratch.blocks)
      if (b.removed_inertia)
        throw std::runtime_error("Private layering removed face inertia");
    blocks([&](std::size_t block) {
      double heat = 0, sum = 0;
      for (auto [i, end] = block_cells(block); i < end; ++i) {
        if (g.wall[i])
          continue;
        for (const auto &[x, f] : {std::pair{true, g.left_face(i)}, std::pair{true, g.right_face(i)},
                                   std::pair{false, g.bottom_face(i)}, std::pair{false, g.top_face(i)}})
          if ((x ? scratch.touched_x : scratch.touched_y)[f] == call && face_heat(x, f, heat, sum))
            out.state.energy[i] += heat * new_mass[i] / sum;
      }
    });
    out.wall_x = wall_x.value();
    out.wall_y = wall_y.value();
    out.wall_ops_x = ops_x.value();
    out.wall_ops_y = ops_y.value();
    audit_touched();
    return out;
  }
  // Face J keeps its value; where an endpoint's mass changed, the K change
  // goes to U. Such faces are found in row blocks, then booked in face order.
  const auto inertias = [&](bool x, std::size_t f) {
    const auto l = (x ? g.xl : g.yl)[f], r = (x ? g.xr : g.yr)[f];
    return std::pair{.5 * (m(old_mass, l) + m(old_mass, r)),
                     .5 * (m(new_mass, l) + m(new_mass, r))};
  };
  std::vector<std::vector<std::size_t>> moved_x(g.blocks), moved_y(g.blocks);
  blocks([&](std::size_t block) {
    const auto y0 = g.block_row_begin(block);
    for (std::size_t f = y0 * g.XW; f < g.block_row_end(block) * g.XW; ++f)
      if (initial.jx[f] != 0 && inertias(true, f).first != inertias(true, f).second)
        moved_x[block].push_back(f);
    for (std::size_t f = y0 * g.W; f < g.block_yrow_end(block) * g.W; ++f)
      if (initial.jy[f] != 0 && inertias(false, f).first != inertias(false, f).second)
        moved_y[block].push_back(f);
  });
  const auto faces = [&](bool x, const auto &lists) {
    const auto &left = x ? g.xl : g.yl, &right = x ? g.xr : g.yr;
    const auto &j = x ? initial.jx : initial.jy;
    for (const auto &list : lists)
      for (const auto f : list) {
        const auto l = left[f], r = right[f];
        const auto [I0, I1] = inertias(x, f);
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
  faces(true, moved_x);
  faces(false, moved_y);
  audit_touched();
  return out;
}
} // namespace ma::experimental::game_kernel
