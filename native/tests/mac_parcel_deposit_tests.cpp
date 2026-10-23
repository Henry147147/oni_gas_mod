#include "mac_fixture.hpp"
#include "mixed_atmospheres/mac_fields.hpp"
#include "mixed_atmospheres/mac_kernel.hpp"
#include <array>
#include <map>
using namespace ma;
using namespace ma::experimental;
namespace {
void check(bool ok, const char *text) {
  if (!ok)
    throw std::runtime_error(text);
}
struct Totals {
  MacEnergySum u, k, g, x, y;
  std::vector<MacEnergySum> mass;
  std::map<std::uint64_t, MacEnergySum> passives;
};
Totals totals(const MacClosedWorld &w) {
  const auto &s = w.state();
  const auto &g = s.grid;
  Totals out;
  out.mass.resize(w.registry().size());
  for (std::size_t i = 0; i < s.cells.size(); ++i) {
    const auto &c = s.cells[i];
    account_mac_cell_internal_energy(out.u, c);
    for (std::size_t a = 0; a < c.mixture.masses.size(); ++a)
      out.mass[a].add(c.mixture.masses[a]);
    out.g.add(
        MacEnergy::from_double(c.mixture.mass())
            .multiplied(w.settings().gravity_m_per_s2)
            .multiplied((static_cast<double>(i / g.width) + .5) * g.spacing_m));
    for (const auto &q : c.passive_quantities)
      out.passives[q.identity].add(q.quantity);
  }
  const auto half = [&](std::size_t i) {
    return g.walls[i] ? 0 : .5 * s.cells[i].mixture.mass();
  };
  for (bool x : {true, false}) {
    const auto values =
        mac_field_values(x ? s.momentum_x : s.momentum_y,
                         x ? s.full_momentum_x : s.full_momentum_y);
    for (std::size_t f = 0; f < values.size(); ++f) {
      const auto col = f % (x ? g.x_width() : g.width),
                 row = f / (x ? g.x_width() : g.width);
      double I = 0;
      if (x) {
        if (col > 0)
          I += half(col - 1 + row * g.width);
        if (col < g.width)
          I += half(col + row * g.width);
      } else {
        if (row > 0)
          I += half(col + (row - 1) * g.width);
        if (row < g.height)
          I += half(col + row * g.width);
      }
      if (!values[f].is_zero())
        out.k.add(MacEnergy::kinetic(I, values[f]));
      (x ? out.x : out.y).add(values[f]);
    }
  }
  return out;
}
void add(MacEnergyAccount &a, const MacEnergySum &s, double sign = 1) {
  for (std::size_t i = 0; i < s.size(); ++i)
    a.add(s.component(i).multiplied(sign));
}
void sum(MacEnergySum &a, const MacEnergySum &b, double sign = 1) {
  for (std::size_t i = 0; i < b.size(); ++i)
    a.add(b.component(i).multiplied(sign));
}
void exact_credits(const MacClosedWorld &before,
                   const MacParcelDepositResult &r) {
  const auto a = totals(before), b = totals(r.world);
  MacEnergySum U, K, G, X, Y;
  sum(U, a.u);
  sum(U, b.u, -1);
  sum(K, a.k);
  sum(K, b.k, -1);
  sum(G, a.g);
  sum(G, b.g, -1);
  sum(X, a.x);
  sum(X, b.x, -1);
  sum(Y, a.y);
  sum(Y, b.y, -1);
  std::vector<MacEnergySum> M(a.mass.size());
  std::map<std::uint64_t, MacEnergySum> Q;
  for (std::size_t k = 0; k < M.size(); ++k) {
    sum(M[k], a.mass[k]);
    sum(M[k], b.mass[k], -1);
  }
  for (const auto &[id, v] : a.passives)
    sum(Q[id], v);
  for (const auto &[id, v] : b.passives)
    sum(Q[id], v, -1);
  for (const auto &receipt : r.receipts) {
    sum(U, receipt.grid.internal_energy_j);
    sum(K, receipt.grid.kinetic_energy_j);
    sum(G, receipt.grid.potential_energy_j);
    sum(X, receipt.grid.momentum_x);
    sum(Y, receipt.grid.momentum_y);
    sum(M[receipt.consumed.species_index], receipt.grid.mass_kg);
    for (const auto &q : receipt.grid.passives)
      sum(Q[q.identity], q.quantity);
  }
  check(U.sign() == 0 && K.sign() == 0 && G.sign() == 0 && X.sign() == 0 &&
            Y.sign() == 0,
        "Authoritative grid mechanical credits omit represented components");
  for (const auto &m : M)
    check(m.sign() == 0,
          "Authoritative grid mass credit omits a minority tail");
  for (const auto &[id, q] : Q) {
    (void)id;
    check(q.sign() == 0,
          "Authoritative grid passive credit omits a minority tail");
  }
}
void audit(const MacClosedWorld &before, const MacParcelDepositResult &result) {
  check(result.accepted(), result.reason.c_str());
  exact_credits(before, result);
  const auto a = totals(before), b = totals(result.world);
  MacEnergyAccount E, X, Y;
  add(E, a.u);
  add(E, a.k);
  add(E, a.g);
  add(E, b.u, -1);
  add(E, b.k, -1);
  add(E, b.g, -1);
  add(X, a.x);
  add(X, b.x, -1);
  add(Y, a.y);
  add(Y, b.y, -1);
  std::vector<MacEnergyAccount> M(a.mass.size());
  std::map<std::uint64_t, MacEnergyAccount> Q;
  for (std::size_t k = 0; k < M.size(); ++k) {
    add(M[k], a.mass[k]);
    add(M[k], b.mass[k], -1);
  }
  for (const auto &[id, value] : a.passives)
    add(Q[id], value);
  for (const auto &[id, value] : b.passives)
    add(Q[id], value, -1);
  for (const auto &r : result.receipts) {
    E.add(r.consumed.internal_energy_j);
    E.add(r.consumed.kinetic_energy_j);
    E.add(r.consumed.potential_energy_j);
    X.add(r.consumed.momentum_x);
    X.add(r.wall_impulse_x);
    Y.add(r.consumed.momentum_y);
    Y.add(r.wall_impulse_y);
    M[r.consumed.species_index].add(r.consumed.mass_kg);
    for (const auto &q : r.consumed.passives)
      Q[q.identity].add(q.quantity);
  }
  check(E.within_bound() && X.within_bound() && Y.within_bound(),
        "Independent moving-parcel U+K+G/J audit failed");
  for (const auto &m : M)
    check(m.within_bound(), "Independent moving-parcel species audit failed");
  for (const auto &[id, q] : Q) {
    (void)id;
    check(q.within_bound(), "Independent moving-parcel passive audit failed");
  }
  check(result.world.time_s() == before.time_s() &&
            result.world.revision() == before.revision() + 1,
        "Moving parcel changed clock or revision contract");
}
MacClosedWorld world(MacNumericalKernel mode) {
  const bool game = mode == MacNumericalKernel::game;
  const auto &f = ma::testing::setup();
  auto s = f.state(6, 4, false);
  MacClosedSettings cfg;
  cfg.numerical_kernel = mode;
  cfg.vacuum = MacVacuumRepresentation::void_phase;
  cfg.gravity_m_per_s2 = 9.81;
  if (game) {
    cfg.numerical_kernel = MacNumericalKernel::game;
    cfg.game_profile = MacGameProfile::game_kernel;
    cfg.maximum_timestep_s = .2;
    std::vector<double> T(s.cells.size(), 300);
    return MacClosedWorld::game_colony(f.registry, f.eos, std::move(s), T, cfg);
  }
  return MacClosedWorld(f.registry, f.eos, std::move(s), cfg);
}
MacMovingParcel packet(const MacClosedWorld &w, std::size_t cell,
                       std::size_t species, double mass) {
  MacMovingParcel p;
  p.cell_index = cell;
  p.species_index = species;
  p.mass_kg = mass;
  p.internal_energy_j = MacEnergy::from_double(mass).multiplied(
      w.registry()[species].internal_energy(
          w.registry()[species].phase == Phase::gas ? 600 : 310));
  p.momentum_x = MacScaled::from_double(2 * mass);
  p.momentum_y = MacScaled::from_double(-3 * mass);
  p.kinetic_energy_j = MacEnergy::from_double(6.5 * mass);
  p.potential_energy_j = MacEnergy::from_double(mass)
                             .multiplied(w.settings().gravity_m_per_s2)
                             .multiplied(5.25);
  p.passives = {{17, 2}, {radiation_passive_identity, 1e-100}};
  return p;
}
void refused(const MacClosedWorld &w, std::span<const MacMovingParcel> p,
             MacParcelRefusal code) {
  auto r = w.with_moving_parcels(p);
  if (r.accepted() || r.refusal != code || !r.receipts.empty())
    throw std::runtime_error("Moving parcel refusal classification: " +
                             r.reason);
  check(r.world.checkpoint() == w.checkpoint(),
        "Refused moving parcel mutated physical bytes");
}
} // namespace
int main() {
  try {
    for (auto mode :
         {MacNumericalKernel::reference, MacNumericalKernel::production,
          MacNumericalKernel::game}) {
      const bool game = mode == MacNumericalKernel::game;
      const auto &f = ma::testing::setup();
      auto w = world(mode);
      const auto old = w.checkpoint();
      const auto oxygen = w.registry().index("oxygen"),
                 water = w.registry().index("water");
      std::array<MacMovingParcel, 2> mixed{packet(w, 8, oxygen, .125),
                                           packet(w, 8, water, .25)};
      auto r = w.with_moving_parcels(mixed);
      audit(w, r);
      check(w.checkpoint() == old && r.receipts.size() == 2,
            "Moving parcel source/mixture atomicity failed");
      for (const auto &q : r.receipts)
        check(q.consumed.mass_kg > 0 &&
                  q.consumed.mass_kg <= mixed[&q - r.receipts.data()].mass_kg &&
                  q.passives_after.back().identity ==
                      radiation_passive_identity,
              "Moving parcel debit/passive observations failed");
      check(mixed[0].internal_energy_j.mantissa() > 0 &&
                mixed[1].internal_energy_j.mantissa() < 0,
            "Formation-energy control lacks both signs");
      check(r.world.cell_reading(8).temperature_k > 0,
            "Moving parcel has no valid final EOS");
      auto cold = MacClosedWorld::from_checkpoint(
          w.registry(), ma::testing::setup().eos, r.world.checkpoint());
      check(cold.checkpoint() == r.world.checkpoint(),
            "Moving parcel checkpoint cold roundtrip failed");
      if (game) {
        auto opts = mac_kernel_default_options();
        auto one = opts;
        one.threads = 1;
        set_mac_kernel_default_options(one);
        auto a = r.world.advanced(.01).world;
        one.threads = 12;
        set_mac_kernel_default_options(one);
        auto b = cold.advanced(.01).world;
        check(a.checkpoint() == b.checkpoint(),
              "Moving parcel continuation depends on workers/cold cache");
        set_mac_kernel_default_options(opts);
      }
      auto wall = packet(w, 0, oxygen, .125);
      auto hit = w.with_moving_parcels(std::span(&wall, 1));
      audit(w, hit);
      const auto &h = hit.receipts.front();
      check(h.wall_impulse_x.mantissa() < 0 &&
                h.wall_impulse_y.mantissa() > 0 &&
                h.kinetic_mixing_heat_j.sign() != 0,
            "Blocked moving parcel did not book wall impulse/mixing heat");
      auto invalid = mixed;
      invalid[1].mass_kg = std::numeric_limits<double>::denorm_min();
      invalid[1].species_index = oxygen;
      refused(w, invalid, MacParcelRefusal::unrepresentable_primary);
      invalid = mixed;
      invalid[1].kinetic_energy_j = {};
      refused(w, invalid, MacParcelRefusal::invalid_request);
      invalid = mixed;
      invalid[1].internal_energy_j = MacEnergy::from_double(-1e100);
      refused(w, invalid, MacParcelRefusal::invalid_equilibrium);
      invalid = mixed;
      invalid[1].passives = {{17, 1}, {17, 2}};
      refused(w, invalid, MacParcelRefusal::invalid_request);
      if (game) {
        invalid = mixed;
        invalid[1].momentum_x = MacScaled::from_parts(.5, 5000);
        invalid[1].kinetic_energy_j =
            MacEnergy::kinetic(invalid[1].mass_kg, invalid[1].momentum_x);
        refused(w, invalid, MacParcelRefusal::unrepresentable_primary);
      }
      auto simultaneous = mixed;
      simultaneous[0].internal_energy_j = MacEnergy::from_double(-1e7);
      simultaneous[1].internal_energy_j = MacEnergy::from_double(1e7);
      auto combined = w.with_moving_parcels(simultaneous);
      audit(w, combined);
      auto repeated = r.world;
      for (unsigned n = 0; n < 5; ++n) {
        auto opposite = packet(repeated, 8, oxygen, .125);
        opposite.momentum_x = opposite.momentum_x.negated();
        opposite.momentum_y = opposite.momentum_y.negated();
        auto next = repeated.with_moving_parcels(std::span(&opposite, 1));
        audit(repeated, next);
        check(next.receipts.front().grid.momentum_x.sign() < 0 &&
                  next.receipts.front().kinetic_mixing_heat_j.sign() > 0,
              "Opposing parcel lost momentum or kinetic heat");
        repeated = std::move(next.world);
      }
      auto sparse = w.state();
      auto &minority = sparse.cells[8];
      minority.mixture.masses[oxygen] = 1e-100;
      minority.mixture =
          f.eos->initialize_at_temperature(minority.mixture.masses, 300, 1)
              .mixture;
      minority.passive_quantities = {{17, 1e-200},
                                     {radiation_passive_identity, 1e-240}};
      MacClosedWorld tiny =
          game
              ? MacClosedWorld::game_colony(
                    w.registry(), f.eos, sparse,
                    std::vector<double>(sparse.cells.size(), 300), w.settings())
              : MacClosedWorld(w.registry(), f.eos, sparse, w.settings());
      auto incoming = packet(tiny, 8, oxygen, .5);
      auto tail = tiny.with_moving_parcels(std::span(&incoming, 1));
      audit(tiny, tail);
      MacEnergySum leftover;
      leftover.add(incoming.mass_kg);
      sum(leftover, tail.receipts.front().grid.mass_kg, -1);
      check(
          leftover.sign() > 0 &&
              leftover.value().mantissa() ==
                  MacEnergy::from_double(1e-100).mantissa() &&
              leftover.value().exponent() ==
                  MacEnergy::from_double(1e-100).exponent(),
          "Old minority mass was not exposed as an exact retained difference");
      MacEnergySum germs;
      germs.add(2);
      for (const auto &q : tail.receipts.front().grid.passives)
        if (q.identity == 17)
          sum(germs, q.quantity, -1);
      check(germs.sign() > 0 &&
                germs.value().mantissa() ==
                    MacEnergy::from_double(1e-200).mantissa() &&
                germs.value().exponent() ==
                    MacEnergy::from_double(1e-200).exponent(),
            "Old minority passive was erased instead of measured");
      auto ranged = packet(w, 8, oxygen, .125);
      ranged.momentum_x = MacScaled::from_parts(
          .5, MacExponent::from_decimal("-18446744073709551616"));
      ranged.momentum_y = {};
      ranged.kinetic_energy_j =
          MacEnergy::kinetic(ranged.mass_kg, ranged.momentum_x).multiplied(2);
      if (game)
        refused(w, std::span(&ranged, 1),
                MacParcelRefusal::unrepresentable_primary);
      else {
        auto wide = w.with_moving_parcels(std::span(&ranged, 1));
        audit(w, wide);
        check(!wide.world.state().full_momentum_x.empty(),
              "Full-range moving momentum was flattened");
        auto reload = MacClosedWorld::from_checkpoint(w.registry(), f.eos,
                                                      wide.world.checkpoint());
        check(reload.checkpoint() == wide.world.checkpoint(),
              "Full-range parcel checkpoint lost canonical exponents");
      }
      std::cout << "PASS moving parcel "
                << (game                                     ? "game"
                    : mode == MacNumericalKernel::production ? "production"
                                                             : "reference")
                << " mixed U/K/G/J admission, wall impulse, full-ID passives, "
                   "atomic refusal and continuation\n";
    }
  } catch (const std::exception &e) {
    std::cerr << "FAIL " << e.what() << '\n';
    return 1;
  }
}
