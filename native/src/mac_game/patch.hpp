#pragma once
#include "../mac_kernel_detail.hpp"
#include "account.hpp"
#include "region.hpp"
#include "sparse.hpp"
#include "liquid_boundary.hpp"
namespace ma::experimental::game_kernel {
struct PatchResult {
  SparseMass masses;
  SparsePassive passives;
  std::vector<double> energy, jx, jy;
  std::size_t microticks = 0, cell_updates = 0, face_updates = 0;
  double wall_x = 0, wall_y = 0, wall_ops_x = 0, wall_ops_y = 0, mass_error = 0;
};
// Private local-clock donor-cell remap. Dyadic face clocks are at least as fine
// as either adjacent cell's outgoing-volume clock. Each event has one shared
// mass/U flux and compatible dual J/K flux. Bulk faces run once, fast faces
// locally; no face sweep is clipped, and no candidate is retried.
class PatchTransport {
  ma::experimental::MacGrid source;
  ma::experimental::mac_kernel::Grid g;
  ma::experimental::mac_kernel::Pool *executor = nullptr;
  Topology own_topology;
  const Topology *topology;
  struct Face {
    bool x;
    std::size_t index;
    double q;
  };
  using Inventory = std::vector<std::pair<std::uint32_t, double>>;
  using PassiveInventory = std::vector<std::pair<std::uint64_t, double>>;
  struct CellUpdate {
    std::size_t i = 0;
    Inventory inventory;
    PassiveInventory passives;
    double energy = 0, mass = 0;
  };
  // Capacity only: all logical entries are overwritten before use. These
  // buffers carry no physics or warm-start state across calls.
  std::vector<Inventory> cells_storage;
  std::vector<PassiveInventory> passive_storage;
  std::vector<CellUpdate> updates_storage;
  // Runs job(block) over the grid's fixed row blocks, on the pool if any.
  void blocks(const std::function<void(std::size_t)> &job) {
    if (executor)
      executor->run(g.blocks, job);
    else
      for (std::size_t block = 0; block < g.blocks; ++block)
        job(block);
  }

public:
  PatchTransport(const ma::experimental::MacGrid &grid,
                 ma::experimental::mac_kernel::Pool *pool = nullptr,
                 const Topology *regions = nullptr)
      : source(grid), g(source), executor(pool),
        own_topology(regions ? Topology{} : Topology(g)),
        topology(regions ? regions : &own_topology) {}
  PatchResult
  advance(const SparseMass &initial, std::vector<double> energy,
          const std::vector<double> &jx, const std::vector<double> &jy,
          const std::vector<double> &qx, const std::vector<double> &qy,
          double gravity, bool auditing = true,
          const SparsePassive *initial_passives = nullptr,
          RegionAudit *regions = nullptr,
          const LiquidBoundary *boundary = nullptr) {
    namespace k = ma::experimental::mac_kernel;
    PatchResult result;
    result.energy = std::move(energy);
    result.jx = jx;
    result.jy = jy;
    const bool has_passives =
        initial_passives && !initial_passives->mass.empty();
    if (has_passives)
      initial_passives->validate(g.N);
    passive_storage.resize(has_passives ? g.N : 0);
    auto &passive_cells = passive_storage;
    cells_storage.resize(g.N);
    auto &cells = cells_storage;
    std::vector<double> mass(g.N), next_mass(g.N), fx(g.NX), fy(g.NY), dx(g.NX),
        dy(g.NY);
    std::vector<unsigned> level(g.N);
    if (g.px || g.py)
      throw std::runtime_error(
          "Private local clocks currently require a nonperiodic grid");
    // Inventories, masses and clock levels by cell, in the grid's fixed row
    // blocks.
    std::vector<unsigned> block_maximum(g.blocks);
    blocks([&](std::size_t block) {
      const auto y0 = g.block_row_begin(block), y1 = g.block_row_end(block);
      unsigned highest = 0;
      for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
        cells[i].clear();
        double total = 0;
        for (std::size_t a = initial.offsets[i]; a < initial.offsets[i + 1];
             ++a) {
          cells[i].emplace_back(initial.species[a], initial.mass[a]);
          total += initial.mass[a];
        }
        mass[i] = next_mass[i] = total;
        if (has_passives) {
          passive_cells[i].clear();
          for (std::size_t a = initial_passives->offsets[i];
               a < initial_passives->offsets[i + 1]; ++a)
            passive_cells[i].emplace_back(initial_passives->species[a],
                                          initial_passives->mass[a]);
          if (!passive_cells[i].empty() && total == 0)
            throw std::runtime_error("Private passives in an empty cell");
        }
        if (g.wall[i])
          continue;
        const double out = std::max(0., -qx[g.left_face(i)]) +
                           std::max(0., qx[g.right_face(i)]) +
                           std::max(0., -qy[g.bottom_face(i)]) +
                           std::max(0., qy[g.top_face(i)]);
        double cfl = out / g.V / .39;
        while (cfl > 1) {
          ++level[i];
          cfl *= .5;
        }
        highest = std::max(highest, level[i]);
      }
      block_maximum[block] = highest;
    });
    const unsigned maximum =
        *std::max_element(block_maximum.begin(), block_maximum.end());
    if (maximum > 16)
      throw std::runtime_error("Private local clocks exceed diagnostic budget; "
                               "front cap still required");
    std::vector<std::vector<Face>> groups(maximum + 1);
    const auto schedule = [&](bool x, const auto &left, const auto &right,
                              const auto &q) {
      for (std::size_t f = 0; f < q.size(); ++f)
        if (q[f] != 0) {
          if (left[f] == k::none || right[f] == k::none || g.closed(!x, f))
            throw std::runtime_error("Private local clock crosses fixed face");
          const auto lev = std::max(level[left[f]], level[right[f]]);
          groups[lev].push_back(
              {x, f, std::ldexp(q[f], -static_cast<int>(lev))});
        }
    };
    schedule(true, g.xl, g.xr, qx);
    schedule(false, g.yl, g.yr, qy);
    std::vector<std::uint32_t> cell_stamp(g.N), x_stamp(g.NX), y_stamp(g.NY);
    std::uint32_t stamp = 0;

    struct DualUpdate {
      bool x;
      std::size_t f;
      double j, heat, wall, left_heat, right_heat, left_wall, right_wall,
          wall_operands, left_wall_operands, right_wall_operands;
      bool isolated;
    };
    DoubleDouble wall_x, wall_y, wall_ops_x, wall_ops_y;
    for (std::size_t tick = 0; tick < (std::size_t(1) << maximum); ++tick) {
      std::vector<Face> active;
      for (unsigned lev = 0; lev <= maximum; ++lev)
        if (tick % (std::size_t(1) << (maximum - lev)) == 0)
          active.insert(active.end(), groups[lev].begin(), groups[lev].end());
      if (active.empty())
        continue;
      if (boundary)
        for (const auto &face : active) {
          const auto &left = face.x ? g.xl : g.yl;
          const auto &right = face.x ? g.xr : g.yr;
          const auto donor = face.q > 0 ? left[face.index] : right[face.index];
          const auto target = face.q > 0 ? right[face.index] : left[face.index];
          if ((boundary->blocked[target] || boundary->blocked[donor]) &&
              std::any_of(cells[donor].begin(), cells[donor].end(),
                          [&](const auto &part) {
                            return part.second > 0 &&
                                   !boundary->species[part.first].gas;
                          }))
            throw LiquidBoundaryConflict(face.x, face.index, donor, target,
                                         tick);
        }
      ++stamp;
      ++result.microticks;
      std::vector<std::size_t> affected;
      const auto affect = [&](std::size_t i) {
        if (cell_stamp[i] != stamp) {
          cell_stamp[i] = stamp;
          affected.push_back(i);
        }
      };
      for (const auto &face : active) {
        const auto &l = face.x ? g.xl : g.yl, &r = face.x ? g.xr : g.yr;
        auto &flux = face.x ? fx : fy;
        auto &sweep = face.x ? dx : dy;
        const auto donor = face.q > 0 ? l[face.index] : r[face.index];
        flux[face.index] = face.q / g.V * mass[donor];
        sweep[face.index] = face.q;
        affect(l[face.index]);
        affect(r[face.index]);
      }
      std::sort(affected.begin(), affected.end());
      if (updates_storage.size() < affected.size())
        updates_storage.resize(affected.size());
      std::span<CellUpdate> updated(updates_storage.data(), affected.size());
      const auto fill_cell = [&](std::size_t cell_position) {
        const auto i = affected[cell_position];
        const auto l = g.left_face(i), r = g.right_face(i),
                   b = g.bottom_face(i), t = g.top_face(i);
        std::array<Contribution, 5> c{};
        c[0] = {i, 1 - (std::max(0., -dx[l]) + std::max(0., dx[r]) +
                        std::max(0., -dy[b]) + std::max(0., dy[t])) /
                           g.V};
        if (c[0].fraction < 0)
          throw std::runtime_error("Private local material CFL failed");
        if (dx[l] > 0)
          c[1] = {g.xl[l], dx[l] / g.V};
        if (dx[r] < 0)
          c[2] = {g.xr[r], -dx[r] / g.V};
        if (dy[b] > 0)
          c[3] = {g.yl[b], dy[b] / g.V};
        if (dy[t] < 0)
          c[4] = {g.yr[t], -dy[t] / g.V};
        auto &update = updated[cell_position];
        update.i = i;
        update.inventory.clear();
        update.passives.clear();
        update.energy = update.mass = 0;
        std::array<std::size_t, 5> pos{};
        for (;;) {
          auto key = std::numeric_limits<std::uint32_t>::max();
          for (unsigned a = 0; a < 5; ++a)
            if (c[a].fraction && pos[a] < cells[c[a].cell].size())
              key = std::min(key, cells[c[a].cell][pos[a]].first);
          if (key == std::numeric_limits<std::uint32_t>::max())
            break;
          double value = 0;
          for (unsigned a = 0; a < 5; ++a)
            if (c[a].fraction && pos[a] < cells[c[a].cell].size() &&
                cells[c[a].cell][pos[a]].first == key)
              value += c[a].fraction * cells[c[a].cell][pos[a]++].second;
          if (!(value > 0))
            throw std::runtime_error(
                "Private local material lost a constituent");
          update.inventory.emplace_back(key, value);
          update.mass += value;
        }
        if (has_passives) {
          std::array<std::size_t, 5> passive_pos{};
          for (;;) {
            std::uint64_t key = 0;
            bool found = false;
            for (unsigned a = 0; a < 5; ++a)
              if (c[a].fraction &&
                  passive_pos[a] < passive_cells[c[a].cell].size()) {
                const auto candidate =
                    passive_cells[c[a].cell][passive_pos[a]].first;
                if (!found || candidate < key) {
                  key = candidate;
                  found = true;
                }
              }
            if (!found)
              break;
            double value = 0;
            for (unsigned a = 0; a < 5; ++a)
              if (c[a].fraction &&
                  passive_pos[a] < passive_cells[c[a].cell].size() &&
                  passive_cells[c[a].cell][passive_pos[a]].first == key)
                value += c[a].fraction *
                         passive_cells[c[a].cell][passive_pos[a]++].second;
            if (!(value > 0))
              throw std::runtime_error("Private local passive lost positivity");
            update.passives.emplace_back(key, value);
          }
        }
        for (const auto &entry : c)
          if (entry.fraction)
            update.energy += entry.fraction * result.energy[entry.cell];
      };
      if (executor && affected.size() >= 1024) {
        const auto blocks = (affected.size() + 127) / 128;
        std::vector<std::exception_ptr> errors(blocks);
        executor->run(blocks, [&](std::size_t block) {
          try {
            for (std::size_t position = block * 128;
                 position < std::min(affected.size(), (block + 1) * 128);
                 ++position)
              fill_cell(position);
          } catch (...) {
            errors[block] = std::current_exception();
          }
        });
        for (const auto &error : errors)
          if (error)
            std::rethrow_exception(error);
      } else
        for (std::size_t position = 0; position < affected.size(); ++position)
          fill_cell(position);
      for (const auto &entry : updated)
        next_mass[entry.i] = entry.mass;
      std::vector<std::pair<bool, std::size_t>> dual_faces;
      dual_faces.reserve(affected.size() * 4);
      const auto select = [&](bool x, std::size_t f) {
        auto &seen = x ? x_stamp : y_stamp;
        if (seen[f] != stamp) {
          seen[f] = stamp;
          dual_faces.emplace_back(x, f);
        }
      };
      for (const auto i : affected) {
        select(true, g.left_face(i));
        select(true, g.right_face(i));
        select(false, g.bottom_face(i));
        select(false, g.top_face(i));
      }
      std::vector<DualUpdate> dual(dual_faces.size());
      const auto gather = [&](std::size_t position) {
        const auto [x, f] = dual_faces[position];
        const auto &left = x ? g.xl : g.yl, &right = x ? g.xr : g.yr;
        const auto &j = x ? result.jx : result.jy;
        const auto &fn = x ? fx : fy, &ft = x ? fy : fx;
        const std::size_t cw = x ? g.XW : g.W, ch = x ? g.H : g.YH,
                          NF = j.size();
        // Nonperiodic prototype; periodic paths remain on the component
        // reference.
        const auto normal_prev = [&](std::size_t a) {
          return x ? (a % cw ? a - 1 : NF) : (a / cw ? a - cw : NF);
        };
        const auto normal_next = [&](std::size_t a) {
          return x ? (a % cw + 1 < cw ? a + 1 : NF)
                   : (a / cw + 1 < ch ? a + cw : NF);
        };
        const auto transverse_prev = [&](std::size_t a) {
          return x ? (a / cw ? a - cw : NF) : (a % cw ? a - 1 : NF);
        };
        const auto transverse_next = [&](std::size_t a) {
          return x ? (a / cw + 1 < ch ? a + cw : NF)
                   : (a % cw + 1 < cw ? a + 1 : NF);
        };
        const auto cm = [](const auto &values, k::Index i) {
          return i == k::none ? 0 : values[i];
        };
        const auto inertia = [&](std::size_t a, const auto &values) {
          return .5 * (cm(values, left[a]) + cm(values, right[a]));
        };
        const auto velocity = [&](std::size_t a) {
          const double I = inertia(a, mass);
          if (I == 0 && j[a] != 0)
            throw std::runtime_error("Private local face J without inertia");
          return I > 0 ? j[a] / I : 0.;
        };
        const auto normal = [&](std::size_t upper) {
          const auto prev = normal_prev(upper);
          return prev != NF && left[upper] != k::none
                     ? .5 * (fn[prev] + fn[upper])
                     : 0.;
        };
        const auto transverse = [&](std::size_t upper) {
          if (transverse_prev(upper) == NF)
            return 0.;
          const auto cx = upper % cw, cy = upper / cw;
          double a = 0, b = 0;
          if (x) {
            if (cx > 0)
              a = ft[g.yface(cx - 1, cy)];
            if (cx < g.W)
              b = ft[g.yface(cx, cy)];
          } else {
            if (cy > 0)
              a = ft[g.xface(cx, cy - 1)];
            if (cy < g.H)
              b = ft[g.xface(cx, cy)];
          }
          return .5 * (a + b);
        };
        const bool isolated =
            left[f] != k::none && right[f] != k::none &&
            topology->labels[left[f]] != topology->labels[right[f]];
        if (isolated && j[f] != 0)
          throw std::runtime_error("Private isolated closed face has momentum");
        double outgoing = 0, jin = 0, kin = 0, left_heat = 0, right_heat = 0,
               left_wall = 0, right_wall = 0, wall_operands = 0,
               left_wall_operands = 0, right_wall_operands = 0;
        const auto edge = [&](double flux, std::size_t other, bool upper) {
          if (flux == 0)
            return;
          const double part = std::abs(flux);
          if ((upper && flux < 0) || (!upper && flux > 0))
            outgoing += part;
          else {
            const double v = velocity(other), incoming_j = part * v,
                         incoming_k = part * .5 * v * v;
            jin += incoming_j;
            kin += incoming_k;
            if (incoming_j != 0 || incoming_k != 0) {
              const auto owner =
                  left[other] != k::none ? left[other] : right[other];
              if (owner == k::none)
                throw std::runtime_error(
                    "Private wall impact without fluid donor");
              if (isolated) {
                if (topology->labels[owner] == topology->labels[left[f]]) {
                  left_heat += incoming_k;
                  left_wall -= incoming_j;
                  left_wall_operands += std::abs(incoming_j);
                } else if (topology->labels[owner] ==
                           topology->labels[right[f]]) {
                  right_heat += incoming_k;
                  right_wall -= incoming_j;
                  right_wall_operands += std::abs(incoming_j);
                } else
                  throw std::runtime_error(
                      "Private wall impact crossed a third region");
              }
              const bool fixed =
                  left[f] == k::none || right[f] == k::none || g.closed(!x, f);
              if (fixed)
                wall_operands += std::abs(incoming_j);
            }
          }
        };
        edge(normal(f), normal_prev(f), true);
        if (normal_next(f) != NF)
          edge(normal(normal_next(f)), normal_next(f), false);
        edge(transverse(f), transverse_prev(f), true);
        if (transverse_next(f) != NF)
          edge(transverse(transverse_next(f)), transverse_next(f), false);
        const double keep = inertia(f, mass) - outgoing, v = velocity(f);
        if (keep < 0)
          throw std::runtime_error("Private local dual CFL failed");
        const double js = keep * v + jin, ktr = keep * .5 * v * v + kin,
                     I1 = inertia(f, next_mass);
        if (js != 0 && I1 == 0)
          throw std::runtime_error("Private local dual J without new inertia");
        const bool fixed =
            left[f] == k::none || right[f] == k::none || g.closed(!x, f);
        const double next = fixed ? 0 : js;
        dual[position] = {x,
                          f,
                          next,
                          ktr - (next == 0 ? 0 : .5 * next * (next / I1)),
                          fixed ? -js : 0,
                          left_heat,
                          right_heat,
                          left_wall,
                          right_wall,
                          wall_operands,
                          left_wall_operands,
                          right_wall_operands,
                          isolated};
      };
      if (executor && dual_faces.size() >= 1024) {
        const auto blocks = (dual_faces.size() + 127) / 128;
        std::vector<std::exception_ptr> errors(blocks);
        executor->run(blocks, [&](std::size_t block) {
          try {
            for (std::size_t position = block * 128;
                 position < std::min(dual_faces.size(), (block + 1) * 128);
                 ++position)
              gather(position);
          } catch (...) {
            errors[block] = std::current_exception();
          }
        });
        for (const auto &error : errors)
          if (error)
            std::rethrow_exception(error);
      } else
        for (std::size_t position = 0; position < dual_faces.size(); ++position)
          gather(position);
      for (auto &entry : updated) {
        cells[entry.i].swap(entry.inventory);
        if (has_passives)
          passive_cells[entry.i].swap(entry.passives);
        result.energy[entry.i] = entry.energy;
        mass[entry.i] = entry.mass;
      }
      for (const auto &entry : dual) {
        const auto &left = entry.x ? g.xl : g.yl,
                   &right = entry.x ? g.xr : g.yr;
        auto &j = entry.x ? result.jx : result.jy;
        j[entry.f] = entry.j;
        (entry.x ? wall_x : wall_y).add(entry.wall);
        (entry.x ? wall_ops_x : wall_ops_y).add(std::abs(entry.wall));
        const auto l = left[entry.f], r = right[entry.f];
        if (regions && entry.wall_operands != 0) {
          if (entry.isolated) {
            regions->momentum_receipt(l, entry.x, entry.left_wall,
                                      entry.left_wall_operands);
            regions->momentum_receipt(r, entry.x, entry.right_wall,
                                      entry.right_wall_operands);
          } else
            regions->momentum_receipt(l != k::none ? l : r, entry.x, entry.wall,
                                      entry.wall_operands);
        }
        const double ml = l == k::none ? 0 : mass[l],
                     mr = r == k::none ? 0 : mass[r], sum = ml + mr;
        if (entry.isolated) {
          // A sealed face is two independent walls. Return impact
          // heat to its donor region rather than sharing it across
          // the closed door according to unrelated opposite mass.
          if (entry.left_heat != 0)
            result.energy[l] += entry.left_heat;
          if (entry.right_heat != 0)
            result.energy[r] += entry.right_heat;
        } else if (entry.heat != 0) {
          if (sum == 0)
            throw std::runtime_error("Private local heat without material");
          if (l != k::none)
            result.energy[l] += entry.heat * ml / sum;
          if (r != k::none)
            result.energy[r] += entry.heat * mr / sum;
        }
      }
      for (const auto &face : active) {
        auto &flux = face.x ? fx : fy;
        auto &sweep = face.x ? dx : dy;
        if (!face.x && flux[face.index] != 0) {
          const auto l = g.yl[face.index], r = g.yr[face.index];
          const double work = -gravity * source.spacing_m * flux[face.index],
                       sum = mass[l] + mass[r];
          result.energy[l] += work * mass[l] / sum;
          result.energy[r] += work * mass[r] / sum;
        }
        flux[face.index] = 0;
        sweep[face.index] = 0;
      }
      result.cell_updates += affected.size();
      result.face_updates += dual.size();
    }
    // Inventories back to sparse form, from per-block counts.
    const auto pack = [&](const auto &rows, auto &into) {
      std::vector<std::size_t> start(g.blocks + 1);
      blocks([&](std::size_t block) {
        std::size_t count = 0;
        for (std::size_t i = g.block_row_begin(block) * g.W;
             i < g.block_row_end(block) * g.W; ++i)
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
        for (std::size_t i = g.block_row_begin(block) * g.W;
             i < g.block_row_end(block) * g.W; ++i) {
          for (const auto &[key, value] : rows[i]) {
            into.species[at] = key;
            into.mass[at] = value;
            ++at;
          }
          into.offsets[i + 1] = at;
        }
      });
    };
    pack(cells, result.masses);
    if (has_passives) {
      pack(passive_cells, result.passives);
      result.passives.validate(g.N);
    }
    result.wall_x = wall_x.value();
    result.wall_y = wall_y.value();
    result.wall_ops_x = wall_ops_x.value();
    result.wall_ops_y = wall_ops_y.value();
    if (auditing) {
      const auto K = initial.species.empty()
                         ? 0
                         : 1 + *std::max_element(initial.species.begin(),
                                                 initial.species.end());
      std::vector<Balance> balance(K);
      for (std::size_t a = 0; a < initial.mass.size(); ++a)
        balance[initial.species[a]].add(initial.mass[a]);
      for (std::size_t a = 0; a < result.masses.mass.size(); ++a)
        balance[result.masses.species[a]].add(-result.masses.mass[a]);
      for (const auto &b : balance) {
        if (!b.closed())
          throw std::runtime_error("Private local species audit failed");
        if (b.operands.value())
          result.mass_error =
              std::max(result.mass_error,
                       std::abs(b.residual.value()) / b.operands.value());
      }
    }
    return result;
  }
};
} // namespace ma::experimental::game_kernel
