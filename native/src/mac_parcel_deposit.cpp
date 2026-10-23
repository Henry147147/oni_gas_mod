#include "mac_game_bridge.hpp"
#include "mixed_atmospheres/mac_closed_world.hpp"
#include "mixed_atmospheres/mac_fields.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>

namespace ma::experimental {
namespace {
struct Refusal {
  MacParcelRefusal code;
  const char *reason;
};
void need(bool ok, MacParcelRefusal code, const char *reason) {
  if (!ok)
    throw Refusal{code, reason};
}
constexpr auto none = std::numeric_limits<std::size_t>::max();
std::array<std::size_t, 2> ends(const MacGrid &g, bool x, std::size_t f) {
  const auto col = f % (x ? g.x_width() : g.width),
             row = f / (x ? g.x_width() : g.width);
  std::array<std::size_t, 2> out{none, none};
  if (x) {
    if (col || g.periodic_x)
      out[0] = (col + g.width - 1) % g.width + row * g.width;
    if (col < g.width)
      out[1] = col + row * g.width;
  } else {
    if (row || g.periodic_y)
      out[0] = col + ((row + g.height - 1) % g.height) * g.width;
    if (row < g.height)
      out[1] = col + row * g.width;
  }
  for (auto &i : out)
    if (i != none && g.walls[i])
      i = none;
  return out;
}
std::array<std::size_t, 2> faces(const MacGrid &g, std::size_t i, bool x) {
  const auto col = i % g.width, row = i / g.width;
  return x ? std::array<std::size_t, 2>{g.x_face(col, row),
                                        g.x_face((col + 1) % g.x_width(), row)}
           : std::array<std::size_t, 2>{
                 g.y_face(col, row), g.y_face(col, (row + 1) % g.y_height())};
}
bool blocked(const MacGrid &g, bool x, std::size_t f) {
  const auto e = ends(g, x, f);
  const auto &mask = x ? g.closed_x : g.closed_y;
  return e[0] == none || e[1] == none || (!mask.empty() && mask[f]);
}
double inertia(const MacTransportState &s, bool x, std::size_t f) {
  double value = 0;
  for (auto i : ends(s.grid, x, f))
    if (i != none) {
      const double mass = s.cells[i].mixture.mass(), half = .5 * mass;
      need(mass == 0 || half > 0, MacParcelRefusal::unrepresentable_primary,
           "Moving parcel half mass underflows");
      value += half;
    }
  need(std::isfinite(value), MacParcelRefusal::unrepresentable_primary,
       "Moving parcel inertia overflows");
  return value;
}
MacScaled momentum(const MacTransportState &s, bool x, std::size_t f) {
  const auto &wide = x ? s.full_momentum_x : s.full_momentum_y;
  return wide.empty()
             ? MacScaled::from_double((x ? s.momentum_x : s.momentum_y)[f])
             : wide[f];
}
double scalar(MacEnergy value) {
  const auto p = value.project();
  need(!p.rounded, MacParcelRefusal::unrepresentable_primary,
       "Moving parcel primary range is unsupported");
  return p.value;
}
void store_momentum(MacTransportState &s, bool x, std::size_t f,
                    MacScaled value, bool game) {
  auto &projected = x ? s.momentum_x : s.momentum_y;
  auto &full = x ? s.full_momentum_x : s.full_momentum_y;
  if (game) {
    need(full.empty(), MacParcelRefusal::unrepresentable_primary,
         "Game parcel requires binary64 canonical momentum");
    projected[f] = scalar(value);
    return;
  }
  const auto p = value.project();
  if (p.rounded && full.empty()) {
    full.reserve(projected.size());
    for (auto q : projected)
      full.push_back(MacScaled::from_double(q));
  }
  projected[f] = p.value;
  if (!full.empty())
    full[f] = value;
}
void add(MacEnergySum &target, const MacEnergySum &source, double sign = 1) {
  for (std::size_t i = 0; i < source.size(); ++i)
    target.add(source.component(i).multiplied(sign));
}
MacEnergy potential(const MacTransportState &s, std::size_t i, double gravity) {
  return MacEnergy::from_double(s.cells[i].mixture.mass())
      .multiplied(gravity)
      .multiplied((static_cast<double>(i / s.grid.width) + .5) *
                  s.grid.spacing_m);
}
void store_energy(MacMaterialCell &cell, const MacEnergySum &increment,
                  bool game, MacPrimaryRoundingCounts &rounding) {
  const auto before = cell;
  MacEnergySum total;
  account_mac_cell_internal_energy(total, before);
  add(total, increment);
  if (game) {
    need(before.energy_remainder_j.is_zero(),
         MacParcelRefusal::unrepresentable_primary,
         "Game parcel primary correction unsupported");
    cell.mixture.energy = scalar(total.value());
    cell.energy_remainder_j = {};
  } else
    (void)store_mac_cell_internal_energy(cell, total, &rounding);
  MacEnergyAccount audit;
  account_mac_cell_internal_energy(audit, before);
  for (std::size_t i = 0; i < increment.size(); ++i)
    audit.add(increment.component(i));
  account_mac_cell_internal_energy(audit, cell, -1);
  need(audit.within_bound(), MacParcelRefusal::unrepresentable_primary,
       "Moving parcel U write exceeds its local operand bound");
}
void occupied(const MacClosedWorld &world, MacTransportState &s,
              std::span<const std::size_t> touched) {
  const bool game =
      world.settings().numerical_kernel == MacNumericalKernel::game;
  const bool voids =
      world.settings().vacuum == MacVacuumRepresentation::void_phase;
  if (voids && s.occupied_volumes_m3.empty()) {
    s.occupied_volumes_m3.resize(s.cells.size());
    for (std::size_t i = 0; i < s.cells.size(); ++i)
      if (!s.grid.walls[i] && s.cells[i].mixture.mass() > 0)
        s.occupied_volumes_m3[i] = s.grid.volume_m3();
  }
  for (auto i : touched) {
    auto &cell = s.cells[i];
    if (cell.mixture.mass() == 0) {
      need(cell.mixture.energy == 0 && cell.energy_remainder_j.is_zero() &&
               cell.passive_quantities.empty(),
           MacParcelRefusal::invalid_equilibrium,
           "Empty parcel neighbor contains energy/passives");
      if (voids)
        s.occupied_volumes_m3[i] = 0;
      continue;
    }
    try {
      if (game) {
        const auto e = MacGameKernelAccess::reading(world, cell);
        s.occupied_volumes_m3[i] =
            e.has_gas ? s.grid.volume_m3() : e.liquid_volume_m3;
      } else {
        bool gas = false;
        double nominal = 0;
        for (const auto &m : world.thermodynamics().reference().materials()) {
          const auto key = world.registry().index(m.id);
          const double mass = cell.mixture.masses[key];
          if (m.phase == ReferencePhase::ideal_gas)
            gas = gas || mass > 0;
          else
            nominal += mass / m.zero_pressure_density_kg_per_m3;
        }
        const double volume = voids && !gas
                                  ? std::min(s.grid.volume_m3(), nominal)
                                  : s.grid.volume_m3();
        const auto e = world.thermodynamics().evaluate(cell.mixture, volume);
        need(!e.has_vacuum || voids, MacParcelRefusal::invalid_equilibrium,
             "Moving parcel creates an unsupported free surface");
        if (voids)
          s.occupied_volumes_m3[i] = volume;
      }
    } catch (const Refusal &) {
      throw;
    } catch (const std::exception &) {
      throw Refusal{MacParcelRefusal::invalid_equilibrium,
                    "Moving parcel final EOS is invalid"};
    }
  }
}
struct Touched {
  std::set<std::size_t> cells, x, y;
};
void account(MacEnergyAccount &energy, MacScaledAccount &px,
             MacScaledAccount &py, const MacTransportState &s,
             const Touched &touched, double gravity, double sign) {
  for (auto i : touched.cells) {
    account_mac_cell_internal_energy(energy, s.cells[i], sign);
    energy.add(potential(s, i, gravity).multiplied(sign));
  }
  for (bool x : {true, false})
    for (auto f : x ? touched.x : touched.y) {
      const auto j = momentum(s, x, f);
      const double I = inertia(s, x, f);
      need(I > 0 || j.is_zero(), MacParcelRefusal::unrepresentable_primary,
           "Moving parcel momentum has no carrier");
      if (!j.is_zero())
        energy.add(MacEnergy::kinetic(I, j).multiplied(sign));
      (x ? px : py).add(j.multiplied(sign));
    }
}
} // namespace
MacParcelDepositResult MacClosedWorld::with_moving_parcels(
    std::span<const MacMovingParcel> requests) const {
  require_mac_energy_environment();
  MacParcelDepositResult result{
      *this, {}, MacParcelRefusal::none, {}, {}, {}, {}, {}, {}, {}, {}};
  try {
    need(!requests.empty() && requests.size() <= 4096,
         MacParcelRefusal::invalid_request,
         "Moving parcel batch must have 1..4096 constituents");
    const bool game = settings_.numerical_kernel == MacNumericalKernel::game;
    auto next = state();
    Touched touched;
    MacEnergyAccount energy;
    MacScaledAccount px, py;
    for (const auto &request : requests) {
      need(request.cell_index < next.cells.size() &&
               !next.grid.walls[request.cell_index] &&
               request.species_index < registry_.size() &&
               registry_[request.species_index].phase != Phase::solid &&
               std::isfinite(request.mass_kg) && request.mass_kg > 0 &&
               request.kinetic_energy_j.mantissa() >= 0,
           MacParcelRefusal::invalid_request,
           "Invalid moving parcel material or mass");
      need(std::any_of(thermodynamics_->reference().materials().begin(),
                       thermodynamics_->reference().materials().end(),
                       [&](const auto &m) {
                         return m.id == registry_[request.species_index].id;
                       }),
           MacParcelRefusal::invalid_request,
           "Moving parcel material has no EOS profile");
      try {
        validate_passive_quantities(request.passives);
      } catch (const std::invalid_argument &) {
        throw Refusal{MacParcelRefusal::invalid_request,
                      "Invalid moving parcel passives"};
      }
      MacParcelDepositReceipt receipt;
      receipt.consumed = request;
      const auto cell_index = request.cell_index;
      auto &cell = next.cells[cell_index];
      const double old_mass = cell.mixture.masses[request.species_index];
      double mass = old_mass + request.mass_kg;
      need(std::isfinite(mass) && mass > old_mass,
           MacParcelRefusal::unrepresentable_primary,
           "Moving parcel constituent mass is unrepresentable");
      MacEnergySum excess;
      excess.add(mass);
      excess.add(-old_mass);
      excess.add(-request.mass_kg);
      if (excess.sign() > 0)
        mass = std::nextafter(mass, old_mass);
      const double actual = mass - old_mass;
      need(actual > 0 && actual <= request.mass_kg,
           MacParcelRefusal::unrepresentable_primary,
           "Moving parcel cannot make positive supported progress");
      // This API admits the whole nominal batch, not independent capacity
      // fractions. Only bounded primary-write remainders stay outside.
      need((request.mass_kg - actual) / request.mass_kg <= 0x1p-24,
           MacParcelRefusal::unrepresentable_primary,
           "Moving parcel requires a shared smaller batch before admission");
      MacScaledAccount mass_audit;
      mass_audit.add(old_mass);
      mass_audit.add(actual);
      mass_audit.add(-mass);
      need(mass_audit.within_bound(), MacParcelRefusal::conservation,
           "Moving parcel mass write exceeds its operand bound");
      receipt.grid.mass_kg.add(mass);
      receipt.grid.mass_kg.add(-old_mass);
      receipt.consumed.mass_kg = actual;
      for (auto *q :
           {&receipt.consumed.internal_energy_j,
            &receipt.consumed.kinetic_energy_j,
            &receipt.consumed.potential_energy_j, &receipt.consumed.momentum_x,
            &receipt.consumed.momentum_y})
        *q = q->shared(actual, request.mass_kg);
      const auto Kx = MacEnergy::kinetic(actual, receipt.consumed.momentum_x),
                 Ky = MacEnergy::kinetic(actual, receipt.consumed.momentum_y);
      MacEnergySum surplus;
      surplus.add(receipt.consumed.kinetic_energy_j);
      surplus.add(Kx.negated());
      surplus.add(Ky.negated());
      if (surplus.sign() < 0) {
        MacEnergyAccount tolerance;
        tolerance.add(receipt.consumed.kinetic_energy_j);
        tolerance.add(Kx.negated());
        tolerance.add(Ky.negated());
        need(tolerance.within_bound(), MacParcelRefusal::invalid_request,
             "Moving parcel K is smaller than its momentum requires");
      }
      std::array<double, 4> old_inertia{};
      std::array<MacScaled, 4> old_j{};
      std::array<MacEnergy, 4> old_k{};
      std::map<std::size_t, MacMaterialCell> before;
      before.emplace(cell_index, cell);
      std::size_t slot = 0;
      for (bool x : {true, false})
        for (auto f : faces(next.grid, cell_index, x)) {
          old_inertia[slot] = inertia(next, x, f);
          old_j[slot] = momentum(next, x, f);
          need(old_inertia[slot] > 0 || old_j[slot].is_zero(),
               MacParcelRefusal::unrepresentable_primary,
               "Moving parcel old momentum has no carrier");
          if (!old_j[slot].is_zero())
            old_k[slot] = MacEnergy::kinetic(old_inertia[slot], old_j[slot]);
          for (auto i : ends(next.grid, x, f))
            if (i != none)
              before.try_emplace(i, next.cells[i]);
          (x ? touched.x : touched.y).insert(f);
          ++slot;
        }
      const auto old_potential =
          potential(next, cell_index, settings_.gravity_m_per_s2);
      cell.mixture.masses[request.species_index] = mass;
      receipt.passives_before = cell.passive_quantities;
      receipt.consumed.passives.clear();
      const double fraction = actual / request.mass_kg;
      for (const auto &q : request.passives) {
        const double wanted = q.quantity * fraction;
        auto at = std::lower_bound(
            cell.passive_quantities.begin(), cell.passive_quantities.end(),
            q.identity, [](const auto &a, auto id) { return a.identity < id; });
        const double old =
            at == cell.passive_quantities.end() || at->identity != q.identity
                ? 0
                : at->quantity;
        double value = old + wanted;
        need(std::isfinite(value), MacParcelRefusal::unrepresentable_primary,
             "Moving parcel passive overflow");
        MacEnergySum over;
        over.add(value);
        over.add(-old);
        over.add(-wanted);
        if (over.sign() > 0)
          value = std::nextafter(value, old);
        if (value > old) {
          receipt.consumed.passives.push_back({q.identity, value - old});
          if (old == 0)
            cell.passive_quantities.insert(at, {q.identity, value});
          else
            at->quantity = value;
        }
      }
      for (const auto &q : request.passives) {
        const auto get = [&](const auto &values) {
          const auto at = std::lower_bound(
              values.begin(), values.end(), q.identity,
              [](const auto &a, auto id) { return a.identity < id; });
          return at == values.end() || at->identity != q.identity
                     ? 0
                     : at->quantity;
        };
        MacParcelPassiveCredit credit;
        credit.identity = q.identity;
        credit.quantity.add(get(cell.passive_quantities));
        credit.quantity.add(-get(receipt.passives_before));
        if (credit.quantity.sign() != 0)
          receipt.grid.passives.push_back(std::move(credit));
      }
      std::map<std::size_t, MacEnergySum> heat;
      heat[cell_index].add(receipt.consumed.internal_energy_j);
      MacEnergySum dG;
      dG.add(potential(next, cell_index, settings_.gravity_m_per_s2));
      dG.add(old_potential.negated());
      receipt.grid.potential_energy_j = dG;
      MacEnergySum gwork;
      gwork.add(receipt.consumed.potential_energy_j);
      add(gwork, dG, -1);
      receipt.potential_work_j = gwork;
      add(heat[cell_index], gwork);
      MacEnergySum dK, mix, dX, dY, wallX, wallY;
      slot = 0;
      for (bool x : {true, false})
        for (auto f : faces(next.grid, cell_index, x)) {
          const auto incoming =
              (x ? receipt.consumed.momentum_x : receipt.consumed.momentum_y)
                  .shifted(-1);
          MacScaledSum sum;
          sum.add(old_j[slot]);
          if (blocked(next.grid, x, f)) {
            need(old_j[slot].is_zero(), MacParcelRefusal::invalid_equilibrium,
                 "Closed parcel face carries old momentum");
            (x ? wallX : wallY).add(incoming.negated());
          } else
            sum.add(incoming);
          const auto value = mac_scaled_primary(sum);
          store_momentum(next, x, f, value, game);
          MacEnergySum change;
          change.add(value);
          change.add(old_j[slot].negated());
          add(x ? dX : dY, change);
          const double I = inertia(next, x, f);
          need(I > 0 || value.is_zero(),
               MacParcelRefusal::unrepresentable_primary,
               "Moving parcel new momentum has no carrier");
          const auto K =
              value.is_zero() ? MacEnergy{} : MacEnergy::kinetic(I, value);
          dK.add(K);
          dK.add(old_k[slot].negated());
          MacEnergySum local;
          local.add((x ? Kx : Ky).shifted(-1));
          for (std::size_t part = 0; part < surplus.size(); ++part)
            local.add(surplus.component(part).shifted(-2));
          local.add(old_k[slot]);
          local.add(K.negated());
          if (local.sign() != 0)
            for (auto i : ends(next.grid, x, f))
              if (i != none && next.cells[i].mixture.mass() > 0)
                heat[i].add(
                    local.value().shared(.5 * next.cells[i].mixture.mass(), I));
          ++slot;
        }
      mix.add(receipt.consumed.kinetic_energy_j);
      add(mix, dK, -1);
      receipt.grid.kinetic_energy_j = std::move(dK);
      receipt.grid.momentum_x = std::move(dX);
      receipt.grid.momentum_y = std::move(dY);
      receipt.wall_impulse_x = wallX.value();
      receipt.wall_impulse_y = wallY.value();
      receipt.kinetic_mixing_heat_j = std::move(mix);
      MacEnergySum dU;
      for (const auto &[i, old] : before) {
        touched.cells.insert(i);
        if (const auto at = heat.find(i); at != heat.end())
          store_energy(next.cells[i], at->second, game, result.primary_energy);
        account_mac_cell_internal_energy(dU, next.cells[i]);
        account_mac_cell_internal_energy(dU, old, -1);
      }
      receipt.grid.internal_energy_j = std::move(dU);
      receipt.passives_after = cell.passive_quantities;
      energy.add(receipt.consumed.internal_energy_j);
      energy.add(receipt.consumed.kinetic_energy_j);
      energy.add(receipt.consumed.potential_energy_j);
      px.add(receipt.consumed.momentum_x);
      px.add(receipt.wall_impulse_x);
      py.add(receipt.consumed.momentum_y);
      py.add(receipt.wall_impulse_y);
      result.receipts.push_back(std::move(receipt));
    }
    const std::vector<std::size_t> changed(touched.cells.begin(),
                                           touched.cells.end());
    occupied(*this, next, changed);
    account(energy, px, py, state(), touched, settings_.gravity_m_per_s2, 1);
    account(energy, px, py, next, touched, settings_.gravity_m_per_s2, -1);
    need(energy.within_bound() && px.within_bound() && py.within_bound(),
         MacParcelRefusal::conservation,
         "Moving parcel U+K+G/J account exceeds its operand bound");
    if (!next.full_momentum_x.empty()) {
      const auto full = next.full_momentum_x;
      store_mac_field(next.momentum_x, next.full_momentum_x, full);
    }
    if (!next.full_momentum_y.empty()) {
      const auto full = next.full_momentum_y;
      store_mac_field(next.momentum_y, next.full_momentum_y, full);
    }
    result.energy_residual_j = energy.residual();
    result.energy_bound_j = energy.bound();
    result.momentum_x_residual = px.residual();
    result.momentum_x_bound = px.bound();
    result.momentum_y_residual = py.residual();
    result.momentum_y_bound = py.bound();
    result.world = derived(std::move(next));
    return result;
  } catch (const Refusal &e) {
    result.refusal = e.code;
    result.reason = e.reason;
  } catch (const std::overflow_error &) {
    result.refusal = MacParcelRefusal::unrepresentable_primary;
    result.reason = "Moving parcel range overflow";
  } catch (const FluidFatalError &e) {
    if (e.code() != FluidFatalCode::numerical_precision)
      throw;
    result.refusal = MacParcelRefusal::unrepresentable_primary;
    result.reason = e.what();
  }
  result.receipts.clear();
  result.primary_energy = {};
  return result;
}
} // namespace ma::experimental
