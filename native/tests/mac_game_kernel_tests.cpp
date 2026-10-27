#include "mac_fixture.hpp"
#include "mac_game_liquid_control.hpp"
#include "mac_game_surface_control.hpp"
#include "../src/mac_game/account.hpp"
#include "../src/mac_game/entropy.hpp"
#include "../src/mac_game/heat_mix.hpp"
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
void trace_entropy_is_finite() {
  namespace k = ma::experimental::game_kernel;
  std::array<k::Species, 3> species{};
  species[0].gas = false;
  species[0].cv = 4180;
  species[0].rho0 = 1000;
  species[0].stiffness = 8e7;
  species[1].molar_mass = .016;
  species[1].cv = 1000;
  species[1].entropy_offset = 5000;
  species[2].molar_mass = .018;
  species[2].cv = 4180;
  species[2].entropy_offset = 8000;
  const std::array<double, 3> masses{.025, 1e-313, .002};
  const double energy =
      (masses[0] * 4180 + masses[1] * 1000 + masses[2] * 4180) * 258;
  const auto key = [](std::size_t a) { return a; };
  const auto mass = [&](std::size_t a) { return masses[a]; };
  const auto eos = k::evaluate_selected(species, 3, key, mass, energy, 1);
  const auto value = k::entropy_selected(species, 3, key, mass, eos);
  require(std::isfinite(value),
          "A subnormal transported trace overflowed its entropy diagnostic");
  // Independent wide evaluation of the original physical expression. Its
  // intermediate ratio exceeds binary64's range, but not long double here.
  long double reference = 0;
  for (std::size_t a = 0; a < masses.size(); ++a) {
    const auto &s = species[a];
    long double specific =
        s.cv * std::log(static_cast<long double>(eos.temperature) / 298.15L) +
        s.entropy_offset;
    if (s.gas) {
      const auto partial = static_cast<long double>(masses[a]) * k::R /
                           s.molar_mass * eos.temperature / eos.gas_volume;
      specific += k::R / s.molar_mass *
                  std::log(static_cast<long double>(k::R) * eos.temperature /
                           partial);
    }
    reference += masses[a] * specific;
  }
  if constexpr (std::numeric_limits<long double>::max_exponent > 1024)
    require(std::abs(value - reference) <
                1e-12L * std::max(1.L, std::abs(reference)),
            "Trace entropy differs from the wide physical reference");
  // The common path retains the original expression bit for bit.
  for (const double small : {1e-8, 1e-3, .1}) {
    auto ordinary = masses;
    ordinary[1] = small;
    const auto get = [&](std::size_t a) { return ordinary[a]; };
    const auto e = k::evaluate_selected(species, 3, key, get,
                                        energy + small * 1000 * 258, 1);
    double old = 0;
    const auto thermal = std::log(e.temperature / 298.15);
    for (std::size_t a = 0; a < ordinary.size(); ++a) {
      const auto &s = species[a];
      double specific = s.cv * thermal + s.entropy_offset;
      if (s.gas) {
        const auto partial =
            ordinary[a] * k::R / s.molar_mass * e.temperature / e.gas_volume;
        specific += k::R / s.molar_mass * std::log(k::R * e.temperature / partial);
      }
      old += ordinary[a] * specific;
    }
    require(std::bit_cast<std::uint64_t>(old) ==
                std::bit_cast<std::uint64_t>(
                    k::entropy_selected(species, 3, key, get, e)),
            "Ordinary entropy arithmetic changed");
  }
}
void near_vacuum_heat_remains_physical() {
  namespace k = ma::experimental::game_kernel;
  MacGrid grid;
  grid.width = 24;
  grid.height = 16;
  const auto count = grid.width * grid.height;
  grid.walls.assign(count, 0);
  std::vector<k::Species> species(1);
  species[0].cv = 480;
  species[0].offset = 85000;
  species[0].molar_mass = .044;
  species[0].conductivity = .03;
  k::State state;
  state.energy.resize(count);
  state.jx.resize(grid.x_width() * grid.height);
  state.jy.resize(grid.width * grid.y_height());
  std::vector<k::Eos> readings(count);
  std::mt19937_64 random(1);
  long double before = 0;
  for (std::size_t i = 0; i < count; ++i) {
    const double mass = random() % 3 == 0
                            ? 1.1e-11
                            : .5 + .125 * static_cast<double>(random() % 40);
    const double temperature = 100 + .5 * static_cast<double>(random() % 900);
    state.masses.offsets.push_back(i);
    state.masses.species.push_back(0);
    state.masses.mass.push_back(mass);
    state.energy[i] = mass * (85000 + 480 * temperature);
    readings[i] = k::evaluate(species, std::span<const double>(&mass, 1),
                              state.energy[i], 1);
    before += state.energy[i];
  }
  state.masses.offsets.push_back(count);
  for (std::size_t y = 0; y < grid.height; ++y)
    for (std::size_t x = 1; x < grid.width; ++x) {
      const auto right = x + y * grid.width, left = right - 1;
      state.jx[grid.x_face(x, y)] = .5 * (readings[left].mass + readings[right].mass) *
                                   (random() % 2 ? 17. : -17.);
    }
  for (std::size_t y = 1; y < grid.height; ++y)
    for (std::size_t x = 0; x < grid.width; ++x) {
      const auto top = x + y * grid.width, bottom = top - grid.width;
      state.jy[grid.y_face(x, y)] = .5 * (readings[bottom].mass + readings[top].mass) *
                                   (random() % 2 ? 17. : -17.);
    }
  const auto original_energy = state.energy;
  k::HeatSolver heat(grid, species, 1, 8, nullptr, 800);
  const auto result = heat.advance(state, readings, .2);
  long double after = 0;
  for (std::size_t i = 0; i < count; ++i) {
    const auto eos = k::evaluate(species,
        std::span<const double>(&state.masses.mass[i], 1), result.energy[i], 1);
    require(eos.temperature >= 100 - 1e-6 && eos.temperature <= 549.5 + 1e-6,
            "Near-vacuum heat left its initial temperature range");
    after += result.energy[i];
  }
  require(std::abs(after - before) <= 1e-14L * before && result.error <= 1e-12,
          "Near-vacuum heat lost conservative energy closure");
  require(result.iterations > 1 && result.iterations <= 800,
          "Connected heat solve exceeded its shared iteration cap");
  k::HeatSolver workers(grid, species, 12, 8, nullptr, 800);
  require(workers.advance(state, readings, .2).energy == result.energy,
          "Heat refinement depends on the worker count");
  // One connected component: spending its full cap again on each residual
  // solve would incorrectly let this smaller total budget succeed.
  k::HeatSolver limited(grid, species, 1, 8, nullptr,
                         static_cast<unsigned>(result.iterations - 1));
  bool refused = false;
  try { (void)limited.advance(state, readings, .2); }
  catch (const std::runtime_error &) { refused = true; }
  require(refused && state.energy == original_energy,
          "Heat refinement reset its iteration budget or mutated the source on refusal");
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
void full_worker_continuation() {
  const auto &f = ma::testing::setup();
  // 31 fixed row blocks: enough work to exercise 30 workers and the block
  // limit, with an odd final block. Temperature gradients drive real flow.
  constexpr std::size_t W = 64, H = 481, blocks = 31;
  auto state = f.state(W, H, false);
  std::vector<double> temperatures(W * H);
  for (std::size_t i = 0; i < temperatures.size(); ++i)
    temperatures[i] = 300 + .01 * static_cast<double>(i % 17);
  auto cfg = config();
  cfg.game_profile = MacGameProfile::game_kernel_trace_heat;
  cfg.game.liquid_law = MacGameLiquidLaw::positive_segments;
  cfg.game.gas_heat_mixing = 8;
  cfg.maximum_timestep_s = .02;
  const auto source = MacClosedWorld::game_colony(
      f.registry, f.eos, std::move(state), temperatures, cfg);
  const auto saved = mac_kernel_default_options();
  auto options = saved;
  options.threads = 1;
  set_mac_kernel_default_options(options);
  const auto first = source.advanced(.02);
  const auto second = first.world.advanced(.02);
  require(first.report.game_threads_used == 1 &&
              std::any_of(first.world.state().momentum_y.begin(),
                          first.world.state().momentum_y.end(),
                          [](double p) { return p != 0; }),
          "Single-worker game control did not advance");
  for (const unsigned requested : {12U, 13U, 22U, 30U, 256U}) {
    options.threads = requested;
    set_mac_kernel_default_options(options);
    const auto many = source.advanced(.02);
    require(many.report.game_threads_used == std::min<unsigned>(requested, blocks),
            "Game worker request was capped before the grid block limit");
    require(many.world.checkpoint() == first.world.checkpoint() &&
                many.report.energy_residual_j == first.report.energy_residual_j &&
                many.report.energy_bound_j == first.report.energy_bound_j,
            "Game workers changed physical bytes or energy audit");
    const auto cold = MacClosedWorld::from_checkpoint(
        f.registry, f.eos, many.world.checkpoint()).advanced(.02);
    require(cold.world.checkpoint() == second.world.checkpoint(),
            "Game workers changed cold continuation");
  }
  // A large request must still avoid creating idle workers on a small grid.
  const auto small = MacClosedWorld::game_colony(
      f.registry, f.eos, f.state(8, 4, false), std::vector<double>(32, 300), cfg);
  require(small.advanced(.02).report.game_threads_used == 1,
          "Small game grid ignored the block worker limit");
  set_mac_kernel_default_options(saved);
  std::cout << "PASS game workers1/12/13/22/30/256, actual pool size, block limit and exact cold continuation\n";
}
} // namespace
int main() {
  try {
    zero_add_matches_two_sum();
    trace_entropy_is_finite();
    near_vacuum_heat_remains_physical();
    full_worker_continuation();
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
    {
      // An expiring owner whose state no other world shares is edited in
      // place: the same world as the copying form, without copying, and a
      // refused batch leaves it unchanged and usable.
      const MacCellHeat h1{0, 50, 100000}, h2{1, -20, 100000};
      const std::array<MacFluidTransfer, 2> moves{
          MacFluidTransfer{0, oxygen, false, .0005, 0, {}, true},
          MacFluidTransfer{1, oxygen, true, .0007, 310, {}, false}};
      const auto heated = many.with_cell_heat(std::span(&h1, 1));
      const auto copied_heat = heated.with_cell_heat(std::span(&h2, 1));
      const auto copied = copied_heat.with_fluid_transfers(
          moves, MacFluidTransferTopology::occupied_mixture);
      auto owner = many.with_cell_heat(std::span(&h1, 1));
      const auto *held = &owner.state();
      owner = std::move(owner).with_cell_heat(std::span(&h2, 1));
      auto moved = std::move(owner).with_fluid_transfers(
          moves, MacFluidTransferTopology::occupied_mixture);
      require(&moved.world.state() == held,
              "Game expiring owner was copied instead of edited in place");
      require(moved.world.checkpoint() == copied.world.checkpoint() &&
                  moved.world.revision() == copied.world.revision() &&
                  moved.parcels.size() == copied.parcels.size(),
              "Game in-place edits differ from their copies");
      for (std::size_t n = 0; n < moved.parcels.size(); ++n)
        require(moved.parcels[n].mass_kg == copied.parcels[n].mass_kg &&
                    moved.parcels[n].internal_energy_j ==
                        copied.parcels[n].internal_energy_j,
                "Game in-place parcels differ from their copies");
      auto victim = std::move(moved.world);
      const auto image = victim.checkpoint();
      const auto revision = victim.revision();
      const std::array<MacFluidTransfer, 2> refused_moves{
          MacFluidTransfer{0, oxygen, false, .0005, 0, {}, false},
          MacFluidTransfer{1, oxygen, true, 1e-300, 300, {}, false}};
      const std::array<MacCellHeat, 2> refused_heat{MacCellHeat{0, 10, 100000},
                                                    MacCellHeat{1, 1e6, 1}};
      bool transfer_refused = false, heat_refused = false;
      try {
        (void)std::move(victim).with_fluid_transfers(
            refused_moves, MacFluidTransferTopology::occupied_mixture);
      } catch (const std::invalid_argument &) {
        transfer_refused = true;
      }
      try {
        (void)std::move(victim).with_cell_heat(refused_heat);
      } catch (const std::invalid_argument &) {
        heat_refused = true;
      }
      require(transfer_refused && heat_refused && victim.checkpoint() == image &&
                  victim.revision() == revision,
              "Game refused in-place edit changed its source");
      require(victim.advanced(.2).world.checkpoint() ==
                  copied.world.advanced(.2).world.checkpoint(),
              "Game refused in-place edit changed continuation");
      std::cout << "PASS game in-place heat/transfer edits equal their copies; "
                   "refused batches leave the owner unchanged\n";
    }
    {
      // Cold loads have no kept audit totals. In-place edits must take the
      // opening totals before changing the state, just like copying edits.
      for (const bool inject : {false, true}) {
        auto owner = MacClosedWorld::from_checkpoint(f.registry, f.eos,
                                                      many.checkpoint());
        require(!owner.game_audit_totals(), "Cold world already has audit totals");
        const MacFluidTransfer move{0, oxygen, inject, .0005, 300, {}, true};
        const auto copied = owner.with_fluid_transfers(
            std::span(&move, 1), MacFluidTransferTopology::occupied_mixture);
        const auto *held = &owner.state();
        auto edited = std::move(owner).with_fluid_transfers(
            std::span(&move, 1), MacFluidTransferTopology::occupied_mixture);
        const auto actual = edited.world.game_audit_totals();
        const auto expected = copied.world.game_audit_totals();
        require(&edited.world.state() == held && actual && expected &&
                    actual->species_kg == expected->species_kg &&
                    actual->energy_j == expected->energy_j &&
                    edited.world.checkpoint() == copied.world.checkpoint(),
                "Uncached in-place transfer double-counted its audit change");
        long double species_mass = 0;
        for (const auto &cell : edited.world.state().cells)
          species_mass += cell.mixture.masses[oxygen];
        require(std::abs(actual->species_kg[oxygen] - species_mass) < 1e-12L &&
                    std::abs(actual->energy_j - energy(edited.world)) <
                        1e-12L * std::abs(actual->energy_j),
                "Uncached transfer audit disagrees with the physical state");
      }
      std::cout << "PASS cold uncached in-place transfer totals equal copied and physical totals\n";
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
