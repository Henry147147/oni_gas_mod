#include "../src/oni_colony.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool yes, const char *text) {
  if (!yes)
    throw std::runtime_error(text);
}
struct Selection {
  const char *name;
  std::string previous;
  bool present = false;
  explicit Selection(const char *variable) : name(variable) {
    if (const char *value = std::getenv(name)) {
      previous = value;
      present = true;
    }
    set("0");
  }
  void set(const char *value) const {
#ifdef _WIN32
    _putenv_s(name, value ? value : "");
#else
    if (value) setenv(name, value, 1);
    else unsetenv(name);
#endif
  }
  ~Selection() { set(present ? previous.c_str() : nullptr); }
};
unsigned core_version(const std::vector<std::uint8_t> &image) {
  const std::string magic="MACCLS01";
  const auto at=std::search(image.begin(),image.end(),magic.begin(),magic.end());
  require(at!=image.end() && image.end()-at>=12,"Owner image omits physical core");
  return unsigned(at[8]) | unsigned(at[9])<<8 | unsigned(at[10])<<16 | unsigned(at[11])<<24;
}
// The saved gas heat mixing of a game-kernel physical image (MAC_CHECKPOINT.md).
double heat_mixing(const std::vector<std::uint8_t> &image) {
  const std::string magic="MACCLS01";
  auto at=std::search(image.begin(),image.end(),magic.begin(),magic.end())-image.begin()+12;
  std::uint32_t model=0;std::memcpy(&model,image.data()+at,4);
  at+=4+model+8+8+4+4+8+8+8+8+1+8+8+4+8;
  double value=0;std::memcpy(&value,image.data()+at,8);return value;
}
MAOniColonyMaterial oxygen() {
  MAOniColonyMaterial m{};
  m.struct_size = sizeof(m);
  m.abi_version = MA_ONI_COLONY_ABI_VERSION;
  m.element_hash = 11;
  m.native_index = 1;
  m.phase = 1;
  m.molar_mass_kg_per_mol = .032;
  m.specific_heat_j_per_kg_k = 1000;
  m.conductivity_w_per_m_k = .03;
  m.density_kg_per_m3 = 1;
  std::strcpy(m.id, "Oxygen");
  return m;
}
} // namespace
int main() {
  try {
    Selection selection("MA_COLONY_KERNEL3_POSITIVE"), surface("MA_COLONY_KERNEL3_SURFACE");
    namespace c = ma::oni::colony;
    constexpr std::size_t W = 8, H = 6;
    MAOniColonyConfig config{};
    config.struct_size = sizeof(config);
    config.abi_version = MA_ONI_COLONY_ABI_VERSION;
    config.flags = MA_ONI_COLONY_PHYSICS_CONDUCTION | MA_ONI_COLONY_GAME_KERNEL;
    config.vacuum_index = 999;
    config.grid_width = config.width = W;
    config.grid_height = config.height = H;
    config.gravity_m_per_s2 = 9.81;
    config.maximum_timestep_s = .1;
    auto hydrogen = oxygen();
    hydrogen.native_index = 2;
    hydrogen.element_hash = 22;
    hydrogen.molar_mass_kg_per_mol = .002;
    hydrogen.specific_heat_j_per_kg_k = 14000;
    std::strcpy(hydrogen.id, "Hydrogen");
    const std::vector<MAOniColonyMaterial> materials{oxygen(), hydrogen};
    std::vector<MAOniColonyCell> cells(W * H);
    for (std::size_t y = 0; y < H; ++y)
      for (std::size_t x = 0; x < W; ++x) {
        auto &cell = cells[x + y * W];
        if (x == 0 || x + 1 == W || y == 0 || y + 1 == H || x == 4)
          cell = {0, MA_ONI_COLONY_SOLID, 0, 0, 0, 0};
        else
          cell = {1, MA_ONI_COLONY_FLUID, x < 4 ? 1.2f : .3f, 300, 1, 100};
      }
    c::start(config, materials, cells, 1);
    require(c::status().state == 1, "Game owner did not start");
    const std::vector<MAOniColonyTerrain> opened{
        {4 + 2 * W, MA_ONI_COLONY_OPEN}, {4 + 3 * W, MA_ONI_COLONY_OPEN}};
    c::queue_terrain(opened);
    MAOniColonyTransfer tiny{};
    tiny.public_cell = 1 + W;
    tiny.native_index = 2;
    tiny.kind = MA_ONI_COLONY_INJECT;
    tiny.callback = -1;
    tiny.mass_kg = 1e-13;
    tiny.temperature_k = 300;
    c::queue_transfers(std::span(&tiny, 1));
    const MAOniColonyHeat heat{1 + W, 0, 10, 100000};
    c::queue_heat(std::span(&heat, 1));
    for (std::uint64_t frame = 1; frame <= 5; ++frame) {
      c::advance(frame, .2, 2);
      c::wait();
    }
    auto status = c::status();
    require(status.state == 1 && status.completed_frames == 5,
            "Diagnostic game owner faulted");
    require(status.trace_sink_kg > 0,
            "Game owner ignored the flow trace receipt");
    require(status.rejected_attempts == 0 && status.skipped_relaxations == 0,
            "Game owner retried or skipped phase");
    require(status.maximum_relative_mass_error < 1e-12 &&
                status.maximum_relative_energy_error < 1e-12,
            "Game owner ledgers do not close");
    std::vector<MAOniColonyReading> readings;
    std::vector<MAOniColonyConstituent> parts;
    (void)c::readings(readings, parts);
    require(!readings.empty() && !parts.empty(),
            "Game owner did not publish ownEOS readings");
    const auto image = c::checkpoint();
    require(core_version(image)==20,"Original kernel selector changed profile");
    c::advance(6, .2, 2);
    c::wait();
    const auto expected = c::checkpoint();
    c::stop();
    selection.set("1");
    c::restore(config, materials, image, 1);
    c::advance(6, .2, 2);
    c::wait();
    require(c::checkpoint() == expected,
            "Game owner cold continuation differs");
    c::stop();
    c::start(config,materials,cells,1);
    require(core_version(c::checkpoint())==21,"Positive selector did not create core21");
    c::advance(1,.2,2);c::wait();
    require(c::status().state==1,"Positive game owner faulted");
    const auto positive=c::checkpoint();
    c::advance(2,.2,2);c::wait();const auto positive_expected=c::checkpoint();c::stop();
    selection.set("0");
    c::restore(config,materials,positive,1);
    require(c::checkpoint()==positive,"Core21 idle restore changed saved profile");
    c::advance(2,.2,2);c::wait();
    require(c::checkpoint()==positive_expected,"Core21 owner cold continuation differs");
    c::stop();
    std::cout<<"PASS explicit core21 creation, core20 restore under new selector and core21 restore without it\n";
    surface.set("1");
    c::start(config,materials,cells,1);
    const auto created=c::checkpoint();
    require(core_version(created)==23 && heat_mixing(created)==8,"Surface selector did not create core23 with heat mixing 8");
    c::advance(1,.2,2);c::wait();
    require(c::status().state==1,"Profile5 game owner faulted");
    const auto surface_image=c::checkpoint();
    c::advance(2,.2,2);c::wait();const auto surface_expected=c::checkpoint();c::stop();
    surface.set("0");
    c::restore(config,materials,surface_image,1);
    require(c::checkpoint()==surface_image,"Core23 idle restore changed saved profile");
    c::advance(2,.2,2);c::wait();
    require(c::checkpoint()==surface_expected,"Core23 owner cold continuation differs");
    c::stop();
    std::cout<<"PASS explicit core23 creation with saved heat mixing 8 and core23 restore without its selector\n";
    {
      auto flagged=config;flagged.flags|=MA_ONI_COLONY_GAME_SURFACE;
      c::start(flagged,materials,cells,1);
      const auto image=c::checkpoint();c::stop();
      require(core_version(image)==23 && heat_mixing(image)==8,"Profile5 flag did not create core23 with heat mixing 8");
      auto lone=flagged;lone.flags=MA_ONI_COLONY_PHYSICS_CONDUCTION|MA_ONI_COLONY_GAME_SURFACE;
      bool refused=false;
      try{c::start(lone,materials,cells,1);}catch(const std::exception&){refused=true;}
      c::stop();
      require(refused,"Profile5 flag without the game kernel was accepted");
    }
    std::cout<<"PASS default profile5 flag creates core23 without a selector; refused without the game kernel\n";
    std::cout << "PASS diagnostic kernel3 owner flag5, ownEOS publication, "
                 "native heat, opened door, zero retry/phase skips, ledgers "
                 "and exact owner cold continuation\n";
  } catch (const std::exception &e) {
    ma::oni::colony::stop();
    std::cerr << e.what() << '\n';
    return 1;
  }
}
