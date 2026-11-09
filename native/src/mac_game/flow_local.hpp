#pragma once
#include "patch.hpp"
#include "pressure_be.hpp"
#include "region.hpp"
#include "reject_log.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <limits>
namespace ma::experimental::game_kernel {
struct FlowReport {
  State state;
  std::vector<Eos> eos;
  std::size_t subcycles = 0, pressure_iterations = 0, pressure_corrections = 0,
              transport_cells = 0, transport_faces = 0;
  // Profile 12: thin gas cells whose heating the interval limited before
  // the transport, and that exchanged heat after it.
  std::size_t thin_limited_cells = 0, thin_heat_cells = 0;
  double wall_x = 0, wall_y = 0, body_y = 0, wall_ops_x = 0, wall_ops_y = 0,
         mass_error = 0, energy_error = 0, momentum_error = 0;
  double eos_ms = 0, pressure_ms = 0, transport_ms = 0;
  // A pressure-only trial can request a shorter interval before any work,
  // transport or ledger entry. Its unchanged EOS storage returns to the caller.
  bool pressure_refinement_requested = false;
  double pressure_volume_change = 0;
  // The solved pressures of an interval given a boiling source (profile 12).
  std::vector<double> pressure;
  // Why the refinement was requested (diagnostic, reject_log.hpp); no_cell where not applicable.
  reject_log::Reason reject_reason = reject_log::Reason::swept_volume;
  std::size_t reject_cell = reject_log::no_cell;
};
class Flow {
  ma::experimental::MacGrid source;
  ma::experimental::mac_kernel::Grid g;
  Topology topology;
  std::vector<Species> species;
  mutable ma::experimental::mac_kernel::Pool eos_pool;
  PressureSolver pressure;
  unsigned gravity_rule = 0;
  bool audit_mode = true;
  bool bounded_pressure_volume = false;
  // Profile 8: the net swept-volume bound applies only where the cell or a
  // neighbour flowing into it holds at least this mass (kg); 0 bounds all cells.
  double bound_mass_floor = 0;
  // Profile 10: vertical faces remap a gas/liquid donor's phases in gravity
  // order (patch.hpp, layer_donor).
  bool layered_remap = false;
  // Profile 12: the thin-gas drag's threshold and time constant, which also
  // set the thin-gas heat exchange (share_thin_heat); 0 disables both.
  double thin_mass = 0, thin_time = 0;
  // Profile 12: the room bound on liquid cells (advance, below) and the
  // volume-consistent remap (PatchTransport::advance).
  bool volume_consistent = false;
  PatchTransport patch;
  // Capacity of the previous pressure result, lent to the next solve.
  Pressure pressure_storage;
  // Scratch capacity; every entry is written before it is read.
  std::vector<double> body_heat;
  struct ThinCell {
    double mass = 0;
    std::size_t cell = 0;
  };
  std::vector<std::vector<ThinCell>> thin_found;
  std::vector<ThinCell> thin_order, thin_spare;
  std::vector<double> thin_capacity, thin_offset;
  std::vector<std::uint8_t> thin_marked;
  // Runs f(first row, end row, block) over the grid's fixed row blocks.
  template <class F> void rows(F &&f) const {
    eos_pool.run(g.blocks, [&](std::size_t block) {
      f(g.block_row_begin(block), g.block_row_end(block), block);
    });
  }
  static double milliseconds(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - start)
        .count();
  }
  std::vector<double> totals(const SparseMass &masses) const {
    std::vector<double> result(g.N);
    for (std::size_t i = 0; i < g.N; ++i)
      for (std::size_t a = masses.offsets[i]; a < masses.offsets[i + 1]; ++a)
        result[i] += masses.mass[a];
    return result;
  }
  void book_energy(Balance &balance, const State &s, double gravity,
                   double sign) const {
    const auto mass = totals(s.masses);
    for (std::size_t i = 0; i < g.N; ++i) {
      balance.add(sign * s.energy[i]);
      balance.add(sign * mass[i] * gravity * (static_cast<double>(i / g.W) + .5) * source.spacing_m);
    }
    const auto faces = [&](const auto &left, const auto &right, const auto &j) {
      for (std::size_t f = 0; f < j.size(); ++f)
        if (j[f] != 0) {
          const double inertia =
              .5 *
              ((left[f] == ma::experimental::mac_kernel::none ? 0
                                                              : mass[left[f]]) +
               (right[f] == ma::experimental::mac_kernel::none
                    ? 0
                    : mass[right[f]]));
          balance.add(sign * .5 * j[f] * (j[f] / inertia));
        }
    };
    faces(g.xl, g.xr, s.jx);
    faces(g.yl, g.yr, s.jy);
  }
  // Profile 12 thin-gas heating (user decision 2026-11-07, option C;
  // MAC_GAME_KERNEL.md). Thin gas holds almost no heat. A thin cell filling
  // faster than it drains takes pressure work and drag heat on almost no
  // mass, and the next cell fills from it, so a front heats itself step by
  // step: a hydrogen leak into an empty cave reached 1e9 K on a 1e-12 kg
  // trace within one frame. Two conservative steps in each flow interval keep
  // thin gas near the temperature of the gas it touches: limit_thin_work
  // before the transport and share_thin_heat after it. Both act only on
  // gas-only cells lighter than thin_mass, move energy only across open faces
  // (so within one region), visit cells lightest first (ties by index) so
  // heat passes up the mass gradient in the same pass, and do not depend on
  // the worker count. Masses and momenta are unchanged.
  //
  // Thin cells (mass in (0, thin_mass)) in cell order, as hold(i, mass)
  // reports them, then sorted by (mass, cell) into thin_order: a stable LSD
  // radix sort of the mass bits (positive doubles order as their bits).
  template <class Held> void order_thin(Held &&held) {
    thin_found.resize(g.blocks);
    rows([&](std::size_t y0, std::size_t y1, std::size_t block) {
      auto &found = thin_found[block];
      found.clear();
      for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
        double mass = 0;
        if (!g.wall[i] && held(i, mass) && mass > 0 && mass < thin_mass)
          found.push_back({mass, i});
      }
    });
    thin_order.clear();
    for (const auto &found : thin_found)
      thin_order.insert(thin_order.end(), found.begin(), found.end());
    thin_spare.resize(thin_order.size());
    for (unsigned shift = 0; shift < 64; shift += 8) {
      std::array<std::size_t, 257> start{};
      for (const auto &c : thin_order)
        ++start[((std::bit_cast<std::uint64_t>(c.mass) >> shift) & 255) + 1];
      if (std::find(start.begin() + 1, start.end(), thin_order.size()) != start.end())
        continue; // every key has this byte (or there are none)
      for (std::size_t b = 1; b < start.size(); ++b)
        start[b] += start[b - 1];
      for (const auto &c : thin_order)
        thin_spare[start[(std::bit_cast<std::uint64_t>(c.mass) >> shift) & 255]++] = c;
      thin_order.swap(thin_spare);
    }
  }
  static bool lighter(const ThinCell &a, const ThinCell &b) {
    return a.mass != b.mass ? a.mass < b.mass : a.cell < b.cell;
  }
  // Before the transport: the interval's pressure work, body work and
  // damping and drag heat may raise a thin cell at most to the hottest
  // reading among it and its occupied neighbours across open faces at the
  // start of the interval; the excess heats its heaviest such neighbour,
  // which must be heavier than it. A cell can only exceed that limit if its
  // own work heated it or a lighter cell passed it an excess, so the pass
  // starts from the cells the work heated and adds each thin receiver in
  // turn; the result is that of visiting every thin cell in order. Returns
  // the cells limited.
  std::size_t limit_thin_work(const std::vector<Eos> &eos, std::vector<double> &work) {
    namespace k = ma::experimental::mac_kernel;
    const auto thin = [&](std::size_t i) {
      return eos[i].mass > 0 && eos[i].mass < thin_mass &&
             eos[i].liquid_nominal_volume == 0 && eos[i].capacity > 0;
    };
    order_thin([&](std::size_t i, double &mass) {
      mass = eos[i].mass;
      return thin(i) && work[i] > eos[i].offset + eos[i].capacity * eos[i].temperature;
    });
    thin_marked.resize(g.N);
    for (const auto &c : thin_order)
      thin_marked[c.cell] = 1;
    // A min-heap on (mass, cell); thin_order already is one.
    const auto later = [](const ThinCell &a, const ThinCell &b) { return lighter(b, a); };
    std::size_t limited = 0;
    while (!thin_order.empty()) {
      std::pop_heap(thin_order.begin(), thin_order.end(), later);
      const auto i = thin_order.back().cell;
      thin_order.pop_back();
      thin_marked[i] = 0;
      double hottest = eos[i].temperature, heaviest = eos[i].mass;
      auto heavy = k::none;
      const auto look = [&](bool x, std::size_t f) {
        const auto l = (x ? g.xl : g.yl)[f], r = (x ? g.xr : g.yr)[f];
        if (l == k::none || r == k::none || g.closed(!x, f))
          return;
        const auto j = l == i ? r : l;
        if (!(eos[j].mass > 0))
          return;
        hottest = std::max(hottest, eos[j].temperature);
        if (eos[j].mass > heaviest) {
          heaviest = eos[j].mass;
          heavy = j;
        }
      };
      look(true, g.left_face(i));
      look(true, g.right_face(i));
      look(false, g.bottom_face(i));
      look(false, g.top_face(i));
      const double limit = eos[i].offset + eos[i].capacity * hottest;
      if (heavy == k::none || !(work[i] > limit))
        continue;
      const double excess = work[i] - limit;
      work[i] -= excess;
      work[heavy] += excess;
      ++limited;
      if (thin(heavy) && !thin_marked[heavy]) {
        thin_marked[heavy] = 1;
        thin_order.push_back({eos[heavy].mass, heavy});
        std::push_heap(thin_order.begin(), thin_order.end(), later);
      }
    }
    return limited;
  }
  // After the transport: each thin cell exchanges heat with each gas-only
  // neighbour across an open face at the drag's own rate, gamma =
  // (dt/thin_time)(thin_mass/m - 1) from the cell's mass m. Backward Euler,
  // so a pair's temperature difference relaxes by 1/(1 + gamma) as the drag
  // relaxes momentum; the cell and its neighbours are solved together (each
  // neighbour coupled only to it), and every new temperature lies between
  // the old ones. For a gas-only cell the EOS reads exactly
  // T = (U - offset) / capacity, with capacity and offset summed here in the
  // EOS's order. Returns the cells exchanged.
  std::size_t share_thin_heat(State &s, double dt) {
    namespace k = ma::experimental::mac_kernel;
    thin_capacity.resize(g.N);
    thin_offset.resize(g.N);
    order_thin([&](std::size_t i, double &mass) {
      double capacity = 0, offset = 0;
      bool gas = true;
      for (auto a = s.masses.offsets[i]; a < s.masses.offsets[i + 1]; ++a) {
        const double m = s.masses.mass[a];
        if (m == 0)
          continue;
        const auto &sp = species[s.masses.species[a]];
        gas = gas && sp.gas;
        mass += m;
        capacity += m * sp.cv;
        offset += m * sp.offset;
      }
      // Zero marks a cell that takes no part: empty, or holding liquid.
      thin_capacity[i] = gas && capacity > 0 ? capacity : 0;
      thin_offset[i] = offset;
      return thin_capacity[i] > 0;
    });
    std::size_t exchanged = 0;
    for (const auto &thin : thin_order) {
      const auto i = thin.cell;
      const double capacity = thin_capacity[i];
      const double t = (s.energy[i] - thin_offset[i]) / capacity;
      if (!std::isfinite(t))
        continue;
      const double gamma = dt / thin_time * (thin_mass / thin.mass - 1);
      struct Link {
        std::size_t cell;
        double weight, temperature;
      };
      std::array<Link, 4> links;
      std::size_t count = 0;
      double weight = capacity, sum = capacity * t;
      const auto link = [&](bool x, std::size_t f) {
        const auto l = (x ? g.xl : g.yl)[f], r = (x ? g.xr : g.yr)[f];
        if (l == k::none || r == k::none || g.closed(!x, f))
          return;
        const auto j = l == i ? r : l;
        const double other = thin_capacity[j];
        if (!(other > 0))
          return;
        const double tj = (s.energy[j] - thin_offset[j]) / other;
        // Pair conductance gamma C_i C_j/(C_i + C_j) (at most the drag's
        // rate times the heat capacity of 1 g of gas), seen from the cell
        // through the neighbour's own implicit response.
        const double conductance = gamma * (capacity * other / (capacity + other));
        const double w = conductance * other / (other + conductance);
        if (!std::isfinite(tj) || !(w > 0) || !std::isfinite(w))
          return;
        links[count++] = {j, w, tj};
        weight += w;
        sum += w * tj;
      };
      link(true, g.left_face(i));
      link(true, g.right_face(i));
      link(false, g.bottom_face(i));
      link(false, g.top_face(i));
      if (count == 0)
        continue;
      const double shared = sum / weight;
      for (std::size_t a = 0; a < count; ++a) {
        const double heat = links[a].weight * (shared - links[a].temperature);
        s.energy[links[a].cell] += heat;
        s.energy[i] -= heat;
      }
      ++exchanged;
    }
    return exchanged;
  }

public:
  Flow(const ma::experimental::MacGrid &grid, std::vector<Species> table,
       unsigned threads, bool auditing = true, double pressure_tolerance = 1e-8,
       unsigned gravity_source = 0, unsigned pressure_iterations = 200,
       bool surface_closure = false, bool bound_pressure_volume = false,
       double bound_mass = 0, bool layered = false, double thin_gas_mass = 0,
       double thin_gas_time = 0, bool bound_room = false)
      : source(grid), g(source), topology(g), species(std::move(table)),
        eos_pool(
            static_cast<unsigned>(std::min<std::size_t>(threads, g.blocks))),
        pressure(grid, threads, ma::PressurePreconditioner::incomplete_cholesky,
                 pressure_tolerance, gravity_source, &eos_pool, pressure_iterations,
                 surface_closure, thin_gas_mass, thin_gas_time),
        gravity_rule(gravity_source), audit_mode(auditing),
        bounded_pressure_volume(bound_pressure_volume),
        bound_mass_floor(bound_mass), layered_remap(layered),
        thin_mass(thin_gas_mass), thin_time(thin_gas_time),
        volume_consistent(bound_pressure_volume && bound_room),
        patch(grid, &eos_pool, &topology) {}
  ma::experimental::mac_kernel::Pool &worker_pool() const { return eos_pool; }
  const auto &region_topology() const { return topology; }
  const auto &kernel_grid() const { return g; }
  // `storage` lends the capacity of readings the caller no longer needs.
  // Every entry is assigned, walls the empty reading, so the result never
  // depends on its old contents.
  std::vector<Eos> evaluate_state(const State &state,
                                  std::vector<Eos> storage = {}) const {
    auto result = std::move(storage);
    result.resize(g.N);
    std::vector<std::string> errors(g.blocks);
    eos_pool.run(g.blocks, [&](std::size_t block) {
      for (std::size_t i = g.block_row_begin(block) * g.W;
           i < g.block_row_end(block) * g.W; ++i) {
        if (g.wall[i]) {
          result[i] = Eos{};
          continue;
        }
        const auto begin = state.masses.offsets[i],
                   count = state.masses.offsets[i + 1] - begin;
        try {
          result[i] = evaluate_selected(
              species, count,
              [&](std::size_t a) { return state.masses.species[begin + a]; },
              [&](std::size_t a) { return state.masses.mass[begin + a]; },
              state.energy[i], g.V);
        } catch (const std::exception &e) {
          errors[block] =
              "Private flow EOS cell " + std::to_string(i) + " (" + std::to_string(i % g.W) + "," + std::to_string(i / g.W) + " of " + std::to_string(g.W) + "x" + std::to_string(g.N / g.W) + "): " + e.what();
          break;
        }
      }
    });
    for (const auto &error : errors)
      if (!error.empty())
        throw std::runtime_error(error);
    return result;
  }
  // Whether cell i holds bitwise the inventory and energy `masses` and
  // `energy` describe (evaluate_selected reads nothing else).
  static bool unchanged_cell(const SparseMass &masses,
                             const std::vector<double> &energy,
                             const State &after, std::size_t i) {
    const auto same = [](double a, double b) {
      return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
    };
    const auto begin = after.masses.offsets[i],
               count = after.masses.offsets[i + 1] - begin;
    const auto old = masses.offsets[i];
    bool unchanged = same(energy[i], after.energy[i]) &&
                     masses.offsets[i + 1] - old == count;
    for (std::size_t a = 0; unchanged && a < count; ++a)
      unchanged = masses.species[old + a] == after.masses.species[begin + a] &&
                  same(masses.mass[old + a], after.masses.mass[begin + a]);
    return unchanged;
  }
  // Readings evaluate_selected made of `after` for some cells, per row
  // block in cell order.
  using KnownReadings = std::vector<std::vector<std::pair<std::size_t, Eos>>>;
  // Readings of `after`, given `readings` of the cells `masses` and
  // `energy` describe. A cell whose inventory and energy are bitwise
  // unchanged keeps its reading, which evaluate_selected would reproduce
  // exactly (it reads nothing else); every other cell is evaluated as
  // evaluate_state does, so the result and any error are the same.
  // `known` readings are used where given.
  std::vector<Eos> evaluate_changed(const SparseMass &masses,
                                    const std::vector<double> &energy,
                                    const State &after,
                                    std::vector<Eos> readings,
                                    const KnownReadings *known = nullptr) const {
    if (readings.size() != g.N || masses.offsets.size() != g.N + 1 ||
        energy.size() != g.N)
      return evaluate_state(after, std::move(readings));
    std::vector<std::string> errors(g.blocks);
    eos_pool.run(g.blocks, [&](std::size_t block) {
      std::size_t next = 0;
      for (std::size_t i = g.block_row_begin(block) * g.W;
           i < g.block_row_end(block) * g.W; ++i) {
        if (g.wall[i])
          continue;
        if (unchanged_cell(masses, energy, after, i))
          continue;
        if (known && next < (*known)[block].size() && (*known)[block][next].first == i) {
          readings[i] = (*known)[block][next++].second;
          continue;
        }
        const auto begin = after.masses.offsets[i],
                   count = after.masses.offsets[i + 1] - begin;
        try {
          readings[i] = evaluate_selected(
              species, count,
              [&](std::size_t a) { return after.masses.species[begin + a]; },
              [&](std::size_t a) { return after.masses.mass[begin + a]; },
              after.energy[i], g.V);
        } catch (const std::exception &e) {
          errors[block] =
              "Private flow EOS cell " + std::to_string(i) + " (" + std::to_string(i % g.W) + "," + std::to_string(i / g.W) + " of " + std::to_string(g.W) + "x" + std::to_string(g.N / g.W) + "): " + e.what();
          break;
        }
      }
    });
    for (const auto &error : errors)
      if (!error.empty())
        throw std::runtime_error(error);
    return readings;
  }
  // `readings` are those of `initial` when the caller already holds them.
  FlowReport advance(const State &initial, double dt, double gravity,
                     RegionAudit *regions = nullptr,
                     const LiquidBoundary *boundary = nullptr,
                     std::vector<Eos> readings = {},
                     const std::vector<double> *boiling = nullptr) {
    namespace k = ma::experimental::mac_kernel;
    FlowReport result;
    auto start = std::chrono::steady_clock::now();
    auto eos = readings.empty() ? evaluate_state(initial) : std::move(readings);
    result.eos_ms = milliseconds(start);
    start = std::chrono::steady_clock::now();
    Pressure projected;
    try {
      projected = pressure.advance(eos, initial.jx, initial.jy, dt, gravity,
                                    true, nullptr, std::move(pressure_storage),
                                    boiling);
    } catch (const k::ComponentPressureSolveFailure &) {
      if (!bounded_pressure_volume) throw;
      result.pressure_ms = milliseconds(start);
      result.pressure_refinement_requested = true;
      result.reject_reason = reject_log::Reason::pressure_solve_failure;
      result.eos = std::move(eos);
      return result;
    }
    result.pressure_ms = milliseconds(start);
    result.pressure_iterations = projected.iterations;
    result.pressure_corrections = projected.corrections;
    const auto request_refinement = [&] {
      result.pressure_refinement_requested = true;
      result.eos = std::move(eos);
      pressure_storage = std::move(projected);
    };
    if (!projected.converged) {
      if (!bounded_pressure_volume)
        throw std::runtime_error("Private flow pressure did not converge");
      result.reject_reason = reject_log::Reason::pressure_not_converged;
      request_refinement();
      return result;
    }
    // Profile 12: the most the transport may leave a liquid cell that had
    // room above its surroundings: twice the largest of its own start
    // pressure, the solved pressures of it and its open neighbours and the
    // weight of its own cell, plus one atmosphere.
    const auto tolerated = [&](std::size_t i, double weight) {
      double drive = std::max({projected.p[i], eos[i].pressure, weight});
      const auto around = [&](bool x, std::size_t f) {
        const auto l = (x ? g.xl : g.yl)[f], r = (x ? g.xr : g.yr)[f];
        if (l == k::none || r == k::none || g.closed(!x, f))
          return;
        drive = std::max(drive, projected.p[l == i ? r : l]);
      };
      around(true, g.left_face(i));
      around(true, g.right_face(i));
      around(false, g.bottom_face(i));
      around(false, g.top_face(i));
      return 2 * drive + 101325;
    };
    if (volume_consistent) {
      // Profile 12 room in liquid cells (MAC_GAME_KERNEL.md). The solve sees
      // a partly filled gas-free cell as a zero-pressure boundary and a gas
      // bubble by its tangent compliance at the start of the interval, so it
      // can send a liquid cell more net volume than its void or gas can take
      // (and the transport draws a cell's gas out of its top face first);
      // the liquid's stiffness then turned the excess into megapascals after
      // the transport. A cell's room is the void and gas its own sweeps
      // leave. The tangent model holds up to it; beyond it the cell is full
      // and only its liquid's compliance applies, from the pressure rise the
      // tangent model reaches there (zero for a void or a near-vacuum
      // bubble), so the two branches meet. A liquid cell whose solve uses up
      // its room re-enters the solve on the second branch, like the
      // nonnegative-pressure correction, and stays there for the interval:
      // the solve then stops its inflow within the interval. At most three
      // passes add cells; the check after the transport (below) refines an
      // interval whose cells still end far above their surroundings.
      // Allocated when a cell first stiffens (most intervals have none).
      std::vector<Stiffened> stiff;
      std::vector<std::vector<std::pair<std::size_t, Stiffened>>> found(g.blocks);
      // Adds the cells the latest solve overfills; true if any.
      const auto classify = [&] {
        rows([&](std::size_t y0, std::size_t y1, std::size_t block) {
          auto &out = found[block];
          out.clear();
          for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
            if (g.wall[i])
              continue;
            const double inflow = -(projected.qx[g.right_face(i)] - projected.qx[g.left_face(i)] +
                                    projected.qy[g.top_face(i)] - projected.qy[g.bottom_face(i)]);
            // Overfilling takes a net inflow above the trace scale below
            // (the room is never negative), so other cells skip their state.
            if (!(inflow > 1e-6 * g.V))
              continue;
            const auto &s = eos[i];
            if (!(s.liquid_nominal_volume > 0) || !(s.mass > 0) ||
                (!stiff.empty() && stiff[i].compliance > 0))
              continue;
            // What the cell's own sweeps leave of its void and gas.
            const double horizontal = std::max(0., -projected.qx[g.left_face(i)]) +
                                      std::max(0., projected.qx[g.right_face(i)]),
                         up = std::max(0., projected.qy[g.top_face(i)]),
                         down = std::max(0., -projected.qy[g.bottom_face(i)]);
            const double empty = std::max(0., g.V - s.gas_volume - s.liquid_volume);
            double gas = 0, kept_empty = 0;
            if (layered_remap && gravity > 0 && s.gas_volume > 0) {
              // Horizontal faces sweep in proportion; the top face draws gas
              // first and the bottom face liquid first.
              const double share = std::max(0., 1 - horizontal / g.V);
              gas = s.gas_volume * share - std::min(up, s.gas_volume * share);
              gas = std::max(0., gas - std::max(0., down - s.liquid_volume * share));
            } else {
              const double share = std::max(0., 1 - (horizontal + up + down) / g.V);
              gas = s.gas_volume * share;
              kept_empty = empty * share;
            }
            const double room = kept_empty + gas;
            const bool active = s.nR > 0 || !(s.liquid_nominal_volume < g.V);
            const double tangent =
                s.nR > 0 ? s.gas_volume / ((1 + s.nR / s.capacity) * s.pressure) : 0;
            const double kink = active ? room / (tangent + s.liquid_compliance) : 0;
            const bool full = active ? s.nR > 0 && projected.p[i] - s.pressure > kink
                                     : inflow > room;
            // An overfill below a millionth of the cell (the kernel's trace
            // scale for a phase's share of a cell) is left to the tangent
            // model: it costs no extra solve and no measurable pressure.
            if (!full || !(inflow - room > 1e-6 * g.V))
              continue;
            const Stiffened entry{s.liquid_compliance, -room + s.liquid_compliance * kink};
            if (entry.compliance > 0 && std::isfinite(entry.offset))
              out.emplace_back(i, entry);
          }
        });
        bool any = false;
        for (const auto &block : found)
          for (const auto &[i, entry] : block) {
            if (stiff.empty())
              stiff.assign(g.N, {});
            stiff[i] = entry;
            any = true;
          }
        return any;
      };
      bool added = classify();
      for (unsigned pass = 0; added && pass < 3; ++pass) {
        try {
          projected = pressure.advance(eos, initial.jx, initial.jy, dt, gravity,
                                        true, nullptr, std::move(projected),
                                        boiling, &stiff);
        } catch (const k::ComponentPressureSolveFailure &) {
          result.pressure_ms = milliseconds(start);
          result.pressure_refinement_requested = true;
          result.reject_reason = reject_log::Reason::pressure_solve_failure;
          result.eos = std::move(eos);
          return result;
        }
        result.pressure_iterations += projected.iterations;
        result.pressure_corrections += projected.corrections;
        if (!projected.converged) {
          result.reject_reason = reject_log::Reason::pressure_not_converged;
          request_refinement();
          return result;
        }
        added = classify();
      }
      result.pressure_ms = milliseconds(start);
    }
    const bool negative_pressure = std::any_of(projected.p.begin(), projected.p.end(),
                                              [](double p) { return p < 0; });
    if (negative_pressure && !bounded_pressure_volume)
      throw std::runtime_error(
          "Private flow correction retains negative pressure");
    if (bounded_pressure_volume) {
      if (negative_pressure || std::any_of(projected.p.begin(), projected.p.end(),
                                          [](double p) { return !std::isfinite(p); })) {
        result.reject_reason = reject_log::Reason::negative_or_nonfinite_pressure;
        if (reject_log::level())
          for (std::size_t i = 0; i < projected.p.size(); ++i)
            if (projected.p[i] < 0 || !std::isfinite(projected.p[i])) { result.reject_cell = i; break; }
        request_refinement();
        return result;
      }
      std::vector<double> maxima(g.blocks);
      rows([&](std::size_t y0, std::size_t y1, std::size_t block) {
        double largest = 0;
        for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
          if (g.wall[i]) continue;
          const double divergence = projected.qx[g.right_face(i)] - projected.qx[g.left_face(i)] +
                                    projected.qy[g.top_face(i)] - projected.qy[g.bottom_face(i)];
          if (!std::isfinite(divergence)) {
            largest = std::numeric_limits<double>::infinity();
            break;
          }
          if (bound_mass_floor > 0) {
            // Below the floor in the cell and every neighbour that flows
            // into it, no mass worth bounding is swept in.
            double held = eos[i].mass;
            const auto inflow = [&](bool x, std::size_t f, bool upper) {
              const double q = (x ? projected.qx : projected.qy)[f];
              const auto other = upper ? (x ? g.xr : g.yr)[f] : (x ? g.xl : g.yl)[f];
              if (other != k::none && (upper ? q < 0 : q > 0))
                held = std::max(held, eos[other].mass);
            };
            inflow(true, g.left_face(i), false);
            inflow(true, g.right_face(i), true);
            inflow(false, g.bottom_face(i), false);
            inflow(false, g.top_face(i), true);
            if (held < bound_mass_floor)
              continue;
          }
          largest = std::max(largest, std::abs(divergence));
        }
        maxima[block] = largest;
      });
      const double largest = *std::max_element(maxima.begin(), maxima.end());
      result.pressure_volume_change = largest / g.V;
      // The pressure solve freezes its compliance and face velocities until
      // the remap finishes. Re-solve before a cell's net swept volume exceeds
      // the existing 0.4-cell transport scale. Net volume
      // leaves uniform fast through-flow unrestricted, and also detects a
      // front entering a cell containing a tiny nonzero gas inventory.
      // The one-pass nonnegative pressure correction can also need a finer
      // interval; neither condition may reach work or transport unchanged.
      if (largest > .4 * g.V) {
        result.reject_reason = reject_log::Reason::swept_volume;
        if (reject_log::level()) {
          // Diagnostic rescan (same bound as above) for the offending cell.
          double worst = -1;
          for (std::size_t i = 0; i < g.N; ++i) {
            if (g.wall[i]) continue;
            const double divergence = projected.qx[g.right_face(i)] - projected.qx[g.left_face(i)] +
                                      projected.qy[g.top_face(i)] - projected.qy[g.bottom_face(i)];
            const double magnitude = std::isfinite(divergence) ? std::abs(divergence) : std::numeric_limits<double>::infinity();
            if (bound_mass_floor > 0) {
              double held = eos[i].mass;
              const auto inflow = [&](bool x, std::size_t f, bool upper) {
                const double q = (x ? projected.qx : projected.qy)[f];
                const auto other = upper ? (x ? g.xr : g.yr)[f] : (x ? g.xl : g.yl)[f];
                if (other != k::none && (upper ? q < 0 : q > 0)) held = std::max(held, eos[other].mass);
              };
              inflow(true, g.left_face(i), false);
              inflow(true, g.right_face(i), true);
              inflow(false, g.bottom_face(i), false);
              inflow(false, g.top_face(i), true);
              if (held < bound_mass_floor) continue;
            }
            if (magnitude > worst) { worst = magnitude; result.reject_cell = i; }
          }
        }
        request_refinement();
        return result;
      }
    }
    // Post-pressure U, cell by cell in the grid's fixed row blocks: the
    // pressure work of the time-centred swept volumes (no volume limiter
    // here), then body-force work from the vertical faces, then the damping
    // defect. Each cell adds its terms in the original order.
    body_heat.resize(g.NY);
    // Body-force work is booked locally. Potential-energy changes are
    // booked from each shared vertical mass flux during remap below.
    rows([&](std::size_t y0, std::size_t, std::size_t block) {
      for (std::size_t f = y0 * g.W; f < g.block_yrow_end(block) * g.W; ++f) {
        const auto l = g.yl[f], r = g.yr[f];
        if (l == k::none || r == k::none || g.closed(true, f)) {
          body_heat[f] = 0;
          continue;
        }
        const double I = .5 * (eos[l].mass + eos[r].mass);
        body_heat[f] = gravity_rule == 0 ? gravity * dt * projected.jy[f]
                       : I > 0
                           ? -projected.body_impulses_y[f] * (projected.jy[f] / I)
                           : 0;
      }
    });
    std::vector<double> work = initial.energy;
    rows([&](std::size_t y0, std::size_t y1, std::size_t) {
      for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
        if (g.wall[i])
          continue;
        const auto l = g.left_face(i), r = g.right_face(i),
                   b = g.bottom_face(i), t = g.top_face(i);
        const double divergence = projected.qx[r] - projected.qx[l] +
                                  projected.qy[t] - projected.qy[b];
        double u = work[i];
        u -= projected.p[i] * divergence;
        const auto vertical = [&](std::size_t f) {
          if (body_heat[f] == 0)
            return 0.;
          // EOS sums these same sparse constituents in the same order.
          // Reuse that exact mass instead of another full-grid pass.
          const double sum = (g.yl[f] == k::none ? 0 : eos[g.yl[f]].mass) +
                             (g.yr[f] == k::none ? 0 : eos[g.yr[f]].mass);
          if (sum == 0)
            throw std::runtime_error("Private empty vertical face has work");
          return body_heat[f] * (eos[i].mass / sum);
        };
        u += vertical(b) + vertical(t);
        // Backward-Euler pressure removes unresolved kinetic oscillations.
        // Its exact nonnegative work defect is returned as local heat.
        const auto damping = [&](bool x, std::size_t f, double value) {
          if (value == 0)
            return 0.;
          const auto &left = x ? g.xl : g.yl, &right = x ? g.xr : g.yr;
          const double sum = (left[f] == k::none ? 0 : eos[left[f]].mass) +
                             (right[f] == k::none ? 0 : eos[right[f]].mass);
          return value * (eos[i].mass / sum);
        };
        u += damping(true, l, projected.damping_x[l]) +
             damping(true, r, projected.damping_x[r]) +
             damping(false, b, projected.damping_y[b]) +
             damping(false, t, projected.damping_y[t]);
        work[i] = u;
      }
    });
    if (thin_mass > 0)
      result.thin_limited_cells = limit_thin_work(eos, work);
    start = std::chrono::steady_clock::now();
    auto remap =
        patch.advance(initial.masses, std::move(work), projected.jx,
                      projected.jy, projected.qx, projected.qy, gravity,
                      audit_mode, &initial.passives, regions, boundary,
                      layered_remap ? &species : nullptr,
                      volume_consistent, volume_consistent);
    result.subcycles = remap.microticks;
    result.transport_cells = remap.cell_updates;
    result.transport_faces = remap.face_updates;
    result.mass_error = remap.mass_error;
    result.wall_x += remap.wall_x;
    result.wall_y += remap.wall_y;
    result.wall_ops_x = remap.wall_ops_x;
    result.wall_ops_y = remap.wall_ops_y;
    result.state = {std::move(remap.masses), std::move(remap.energy),
                    std::move(remap.jx), std::move(remap.jy),
                    std::move(remap.passives)};
    if (thin_mass > 0)
      result.thin_heat_cells = share_thin_heat(result.state, dt);
    result.transport_ms = milliseconds(start);
    KnownReadings checked;
    if (volume_consistent) {
      // Profile 12: the transport must not leave a liquid cell that had room
      // (void or gas) far above the pressure the solve gave it and its
      // neighbours, its own pressure at the start or the weight of its cell:
      // at most twice that and one atmosphere. Above it the interval sent the
      // cell volume its room could not take after all (its sweeps took more
      // of its room than the solve was told, or the room filled after the
      // solve's last pass); a shorter interval re-solves with the compliance
      // the cell then has. A refused advance has booked nothing.
      // Only the cells that had room are read here; the readings of the
      // whole result follow below, as on other profiles.
      // A cell the transport left bitwise unchanged keeps its start
      // pressure, which the bound exceeds; the readings made here are those
      // the final evaluation below would make, so it reuses them.
      const bool comparable = initial.masses.offsets.size() == g.N + 1 &&
                              initial.energy.size() == g.N;
      std::vector<double> excess(g.blocks);
      checked.resize(g.blocks);
      rows([&](std::size_t y0, std::size_t y1, std::size_t block) {
        double worst = 0;
        auto &kept = checked[block];
        kept.clear();
        for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
          if (g.wall[i] || !(g.V - eos[i].liquid_volume > 1e-6 * g.V))
            continue;
          if (comparable && unchanged_cell(initial.masses, initial.energy, result.state, i))
            continue;
          const auto begin = result.state.masses.offsets[i],
                     count = result.state.masses.offsets[i + 1] - begin;
          bool liquid = false;
          for (std::size_t a = 0; a < count && !liquid; ++a)
            liquid = !species[result.state.masses.species[begin + a]].gas;
          if (!liquid)
            continue;
          Eos s;
          if (try_evaluate_selected(
                  s, species, count,
                  [&](std::size_t a) { return result.state.masses.species[begin + a]; },
                  [&](std::size_t a) { return result.state.masses.mass[begin + a]; },
                  result.state.energy[i], g.V)) {
            worst = std::numeric_limits<double>::infinity();
            continue;
          }
          if (s.liquid_nominal_volume > 0)
            worst = std::max(worst, s.pressure / tolerated(i, s.mass * std::abs(gravity) / g.A));
          kept.emplace_back(i, s);
        }
        excess[block] = worst;
      });
      const double worst = *std::max_element(excess.begin(), excess.end());
      if (worst > 1) {
        result.pressure_refinement_requested = true;
        result.reject_reason = reject_log::Reason::swept_volume;
        result.pressure_volume_change =
            std::max(result.pressure_volume_change, .4 * std::min(worst, 1024.));
        result.eos = std::move(eos);
        result.state = {};
        pressure_storage = std::move(projected);
        return result;
      }
      if (regions)
        for (const auto &receipt : remap.receipts)
          regions->momentum_receipt(receipt.cell, receipt.x, receipt.value, receipt.operands);
    }
    result.wall_x += projected.wall_impulse_x;
    result.wall_y += projected.wall_impulse_y;
    result.body_y = projected.body_impulse_y;
    result.wall_ops_x += projected.wall_operands_x;
    result.wall_ops_y += projected.wall_operands_y;
    if (regions) {
      const auto book = [&](bool x, const auto &left, const auto &right) {
        const auto &blocked = x ? projected.blocked_x : projected.blocked_y;
        const auto &old = x ? initial.jx : initial.jy;
        for (std::size_t f = 0; f < left.size(); ++f) {
          const auto l = left[f], r = right[f];
          if (l == k::none || r == k::none || g.closed(!x, f) || blocked[f]) {
            if (l != k::none)
              regions->momentum(l, x, -dt * g.A * projected.p[l]);
            if (r != k::none)
              regions->momentum(r, x, dt * g.A * projected.p[r]);
            if (blocked[f] && old[f] != 0)
              regions->momentum(l, x, -old[f]);
          } else {
            if (!x)
              regions->momentum(l, false,
                                gravity_rule == 0
                                    ? -gravity * dt * .5 *
                                          (eos[l].mass + eos[r].mass)
                                    : projected.body_impulses_y[f]);
            // Profile 12: the thin-gas drag's load on this open face.
            const auto &drag = x ? projected.drag_x : projected.drag_y;
            if (!drag.empty() && drag[f] != 0)
              regions->momentum(l, x, -drag[f]);
          }
        }
      };
      book(true, g.xl, g.xr);
      book(false, g.yl, g.yr);
    }
    if (audit_mode) {
      Balance energy, mx, my;
      book_energy(energy, initial, gravity, 1);
      book_energy(energy, result.state, gravity, -1);
      for (double j : initial.jx)
        mx.add(j);
      for (double j : result.state.jx)
        mx.add(-j);
      mx.add(result.wall_x);
      mx.operands.add(projected.wall_operands_x);
      for (double j : initial.jy)
        my.add(j);
      for (double j : result.state.jy)
        my.add(-j);
      my.add(result.wall_y);
      my.add(result.body_y);
      my.operands.add(projected.wall_operands_y);
      if (!energy.closed() || !mx.closed() || !my.closed())
        throw std::runtime_error(
            "Private flow total U+K+G/momentum audit failed: E=" +
            std::to_string(energy.residual.value()) +
            " mx=" + std::to_string(mx.residual.value()) +
            " my=" + std::to_string(my.residual.value()));
      result.energy_error =
          energy.operands.value()
              ? std::abs(energy.residual.value()) / energy.operands.value()
              : 0;
      result.momentum_error =
          std::max(mx.operands.value()
                       ? std::abs(mx.residual.value()) / mx.operands.value()
                       : 0,
                   my.operands.value()
                       ? std::abs(my.residual.value()) / my.operands.value()
                       : 0);
    }
    if (boiling)
      result.pressure = projected.p;
    pressure_storage = std::move(projected);
    start = std::chrono::steady_clock::now();
    result.eos = evaluate_changed(initial.masses, initial.energy, result.state,
                                  std::move(eos), volume_consistent ? &checked : nullptr);
    result.eos_ms += milliseconds(start);
    return result;
  }
};
} // namespace ma::experimental::game_kernel
