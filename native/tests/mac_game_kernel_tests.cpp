#include "mac_fixture.hpp"
#include "mac_game_liquid_control.hpp"
#include "mac_game_surface_control.hpp"
#include "../src/mac_game/account.hpp"
#include "mixed_atmospheres/mac_kernel.hpp"
#include <array>
#include <bit>
#include <limits>
#include <random>

namespace {
using namespace ma;
using namespace ma::experimental;
void require(bool value, const char *text) {
  if (!value)
    throw std::runtime_error(text);
}
long double energy(const MacClosedWorld &world) {
  const auto &s = world.state();
  long double total = 0;
  for (std::size_t i = 0; i < s.cells.size(); ++i)
    total += s.cells[i].mixture.energy +
             static_cast<long double>(s.cells[i].mixture.mass()) *
                 world.settings().gravity_m_per_s2 *
                 (static_cast<long double>(i / s.grid.width) + .5L) *
                 s.grid.spacing_m;
  const auto I = mac_face_inertias(world.registry(), s);
  for (std::size_t f = 0; f < s.momentum_x.size(); ++f)
    if (s.momentum_x[f] != 0)
      total += .5L * s.momentum_x[f] * s.momentum_x[f] / I.x[f];
  for (std::size_t f = 0; f < s.momentum_y.size(); ++f)
    if (s.momentum_y[f] != 0)
      total += .5L * s.momentum_y[f] * s.momentum_y[f] / I.y[f];
  return total;
}
// The account's zero-add shortcut must return what the full two-sum
// update returns, bit for bit, including signed zeros and non-finite parts.
void zero_add_matches_two_sum() {
  struct Reference {
    double hi = 0, lo = 0;
    void add(double x) {
      const double sum = hi + x, z = sum - hi,
                   error = (hi - (sum - z)) + (x - z);
      const double tail = lo + error, next = sum + tail;
      lo = tail - (next - sum);
      hi = next;
    }
  };
  const auto same = [](const auto &a, const auto &b) {
    return std::bit_cast<std::uint64_t>(a.hi) ==
               std::bit_cast<std::uint64_t>(b.hi) &&
           std::bit_cast<std::uint64_t>(a.lo) ==
               std::bit_cast<std::uint64_t>(b.lo);
  };
  const double inf = std::numeric_limits<double>::infinity();
  std::vector<double> parts{0.0,  -0.0, 1.0,     -1.0,   0x1p-1074, -0x1p-1074,
                            0x1p-53, 0x1p-54, 1e300, -1e300, inf,       -inf,
                            std::numeric_limits<double>::quiet_NaN(),
                            std::numeric_limits<double>::max()};
  std::mt19937_64 random(20261026);
  for (int i = 0; i < 200; ++i)
    parts.push_back(std::bit_cast<double>(random()));
  for (const double hi : parts)
    for (const double lo : parts)
      for (const double x : {0.0, -0.0}) {
        Reference reference{hi, lo};
        game_kernel::DoubleDouble account{hi, lo};
        reference.add(x);
        account.add(x);
        require(same(reference, account),
                "Account zero add differs from the two-sum update");
      }
  for (int run = 0; run < 1000; ++run) {
    Reference reference;
    game_kernel::DoubleDouble account;
    for (int k = 0; k < 64; ++k) {
      const double x =
          random() % 3 == 0 ? (random() & 1 ? 0.0 : -0.0)
                            : std::ldexp(static_cast<double>(random() % 2001) - 1000,
                                         static_cast<int>(random() % 120) - 60);
      reference.add(x);
      account.add(x);
      require(same(reference, account),
              "Account accumulation differs from the two-sum update");
    }
  }
}
void closed_energy(long double before, long double after) {
  require(std::abs(after - before) <=
              1e-12L * (std::abs(before) + std::abs(after)),
          "Game boundary energy account failed");
}
MacClosedSettings config() {
  MacClosedSettings s;
  s.numerical_kernel = MacNumericalKernel::game;
  s.game_profile = MacGameProfile::game_kernel;
  s.vacuum = MacVacuumRepresentation::void_phase;
  s.thermal_conduction = true;
  s.maximum_timestep_s = .2;
  s.gravity_m_per_s2 = 9.81;
  s.trace_sink_threshold_kg = 1e-11;
  return s;
}
} // namespace
int main() {
  try {
    zero_add_matches_two_sum();
    const auto &f = ma::testing::setup();
    auto state = f.state(8, 4, false, true);
    std::vector<double> temperatures(state.cells.size(), 300);
    auto cfg = config();
    auto world = MacClosedWorld::game_colony(f.registry, f.eos, state,
                                             temperatures, cfg);
    for (std::size_t i = 0; i < state.cells.size(); ++i) {
      const auto reading = world.cell_reading(i);
      require(std::abs(reading.temperature_k - 300) < 1e-9,
              "Game import did not retain declared temperature");
      require(reading.gas_volume_m3 + reading.liquid_volume_m3 > 0,
              "Game reading lost volume");
    }
    const auto initial = world.checkpoint();
    require(initial[8] == 20, "Game checkpoint version differs");
    auto restored = MacClosedWorld::from_checkpoint(f.registry, f.eos, initial);
    require(restored.checkpoint() == initial,
            "Game checkpoint cold round trip changed bytes");
    const auto options = mac_kernel_default_options();
    auto threads = options;
    threads.threads = 1;
    set_mac_kernel_default_options(threads);
    auto first = world.advanced(.2);
    require(first.report.energy_bound_j >=
                1e-12L * std::abs(energy(world)) &&
                std::abs(first.report.energy_residual_j) <=
                    first.report.energy_bound_j &&
                first.report.energy_residual_wide_j.project().value ==
                    first.report.energy_residual_j &&
                first.report.energy_bound_wide_j.project().value ==
                    first.report.energy_bound_j,
            "Game energy diagnostics must report joules and matching wide fields");
    std::cout << "PASS game-kernel energy diagnostics in joules with matching wide fields\n";
    require(first.report.trace_sink.constituents > 0 &&
                first.report.trace_sink.cells > 0,
            "Game flow trace receipt metadata was omitted");
    auto one = std::move(first.world);
    threads.threads = 12;
    set_mac_kernel_default_options(threads);
    auto many =
        restored.advanced(.2, 1024, MacExecutionPolicy::production).world;
    require(one.checkpoint() == many.checkpoint(),
            "Game workers changed checkpoint bytes");
    for (unsigned tick = 0; tick < 5; ++tick) {
      one = one.advanced(.2).world;
      many =
          MacClosedWorld::from_checkpoint(f.registry, f.eos, many.checkpoint())
              .advanced(.2)
              .world;
      require(one.checkpoint() == many.checkpoint(),
              "Game cold continuation changed physical bytes");
    }
    require(many.settings().numerical_kernel == MacNumericalKernel::game &&
                many.settings().game.liquid_stiffness_ratio == .04,
            "Game continuation changed profile");
    const auto before = energy(many);
    const MacCellHeat heat{0, 100, 100000};
    auto warmed = many.with_cell_heat(std::span(&heat, 1));
    closed_energy(before + 100, energy(warmed));
    const auto oxygen = f.registry.index("oxygen");
    const MacFluidTransfer take{0, oxygen, false, .001, 0, {}, true};
    auto extracted = warmed.with_fluid_transfers(
        std::span(&take, 1), MacFluidTransferTopology::occupied_mixture);
    const auto &p = extracted.parcels.front();
    closed_energy(energy(warmed),
                  energy(extracted.world) + p.internal_energy_j +
                      mac_energy_long_double(p.resolved_kinetic_energy) +
                      mac_energy_long_double(p.potential_energy));
    require(p.mass_kg > 0 && !p.passives_before.empty() &&
                !p.passives_after.empty(),
            "Game extraction lost actual passive observations");
    {
      // An injection never gives the cell more than its request, also when
      // the binary64 sum rounds up: the cell's gain is the reported parcel.
      const auto &base = extracted.world;
      const double held = base.state().cells[0].mixture.masses[oxygen];
      unsigned rounded_up = 0;
      for (unsigned n = 0; n < 64; ++n) {
        const double r = held * (.3 + n * 0x1p-45);
        const double sum = held + r, back = sum - held;
        if ((held - (sum - back)) + (r - back) < 0)
          ++rounded_up;
        const MacFluidTransfer give{0, oxygen, true, r, 300, {}, false};
        const auto injected = base.with_fluid_transfers(
            std::span(&give, 1), MacFluidTransferTopology::occupied_mixture);
        const auto &q = injected.parcels.front();
        const double now = injected.world.state().cells[0].mixture.masses[oxygen];
        require(q.mass_kg > 0 && q.mass_kg <= r && now - held == q.mass_kg,
                "Game injection gave the cell more than its request");
        closed_energy(energy(base) + q.internal_energy_j +
                          mac_energy_long_double(q.potential_energy),
                      energy(injected.world));
      }
      require(rounded_up > 0, "Game injection control never rounded up");
      std::cout << "PASS game injection never exceeds its request ("
                << rounded_up << "/64 sums rounded up)\n";
    }
    std::vector<std::uint8_t> x(extracted.world.state().momentum_x.size()),
        y(extracted.world.state().momentum_y.size());
    x[1] = 1;
    const auto closed = extracted.world.with_closed_faces(x, y);
    closed_energy(energy(extracted.world), energy(closed));
    require(closed.state().momentum_x[1] == 0,
            "Game closed face retained momentum");
    auto corrupted = initial;
    corrupted.back() ^= 1;
    bool refused = false;
    try {
      (void)MacClosedWorld::from_checkpoint(f.registry, f.eos, corrupted);
    } catch (const std::invalid_argument &) {
      refused = true;
    }
    require(refused, "Game checkpoint ignored corruption");
    set_mac_kernel_default_options(options);
    // An isolated fluid cell has no passive rows after transport. Export must
    // accept that sparse representation, including after a refused boundary.
    auto isolated_state = f.state(3, 3, false);
    for (std::size_t i = 0; i < isolated_state.cells.size(); ++i) {
      isolated_state.grid.walls[i] = i != 4;
      isolated_state.cells[i].mixture = MixtureInventory(f.registry.size());
      isolated_state.cells[i].passive_quantities.clear();
    }
    isolated_state.cells[4].mixture.masses[oxygen] = 1;
    auto isolated_cfg = config();
    isolated_cfg.maximum_timestep_s = .1;
    isolated_cfg.thermal_conduction = false;
    auto isolated = MacClosedWorld::game_colony(
        f.registry, f.eos, std::move(isolated_state),
        std::vector<double>(9, 300), isolated_cfg);
    const auto isolated_image = isolated.checkpoint();
    threads.threads = 1;
    set_mac_kernel_default_options(threads);
    const auto isolated_next = isolated.advanced(.2).world;
    MacMovingParcel illegal;
    illegal.cell_index = 4;
    illegal.species_index = f.registry.size();
    illegal.mass_kg = 1;
    const auto refused_parcel = isolated.with_moving_parcels(std::span(&illegal, 1));
    require(!refused_parcel.accepted() && refused_parcel.receipts.empty() &&
                refused_parcel.world.checkpoint() == isolated_image,
            "Isolated parcel refusal changed the source");
    threads.threads = 12;
    set_mac_kernel_default_options(threads);
    const auto isolated_cold = MacClosedWorld::from_checkpoint(
        f.registry, f.eos, refused_parcel.world.checkpoint()).advanced(.2).world;
    require(isolated_cold.checkpoint() == isolated_next.checkpoint() &&
                isolated_next.state().cells[4].mixture.masses[oxygen] == 1 &&
                isolated_next.state().cells[4].mixture.energy ==
                    isolated.state().cells[4].mixture.energy &&
                isolated_next.state().cells[4].passive_quantities.empty(),
            "Passive-free isolated export changed physics or continuation");
    set_mac_kernel_default_options(options);
    std::cout << "PASS passive-free isolated game export after refusal, workers1/12 and cold continuation\n";
    ma::testing::positive_game::run();
    ma::testing::surface_game::run();
    std::cout
        << "PASS diagnostic game kernel own-EOS import/readings, sparse "
           "checkpoint20, workers1/12 and cold continuation, heat/parcel/wall "
           "energy, full passive observations and corruption refusal\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
