#include "mac_fixture.hpp"
#include "mixed_atmospheres/mac_kernel.hpp"
#include <array>

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
    std::cout
        << "PASS diagnostic game kernel own-EOS import/readings, sparse "
           "checkpoint20, workers1/12 and cold continuation, heat/parcel/wall "
           "energy, full passive observations and corruption refusal\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
