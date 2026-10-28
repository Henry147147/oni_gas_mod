#include "../src/oni_colony.hpp"
#include "mixed_atmospheres/mac_kernel.hpp"
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
// The phase-rule selector of a core25/26/27 image, after the heat iteration cap.
std::uint32_t phase_selector(const std::vector<std::uint8_t> &image) {
  const std::string magic="MACCLS01";
  auto at=std::search(image.begin(),image.end(),magic.begin(),magic.end())-image.begin()+12;
  std::uint32_t model=0;std::memcpy(&model,image.data()+at,4);
  at+=4+model+8+8+4+4+8+8+8+8+1+8+8+4+8+8+1+8+8+4+4+4;
  std::uint32_t value=0;std::memcpy(&value,image.data()+at,4);return value;
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
    const auto physical = c::physical_core_count();
    require(physical >= 1 && physical <= 256 &&
                ma::experimental::mac_kernel_default_options().threads == physical,
            "Game owner did not default to physical cores");
    const auto unchanged = c::checkpoint();
    for (const unsigned requested : {1U, 13U, 30U, 256U, 0U}) {
      c::set_worker_count(requested);
      require(ma::experimental::mac_kernel_default_options().threads == (requested ? requested : physical) &&
                  c::checkpoint() == unchanged,
              "Live worker preference was capped or changed checkpoint bytes");
    }
    bool invalid_workers = false;
    try { c::set_worker_count(257); } catch (const std::invalid_argument&) { invalid_workers = true; }
    require(invalid_workers && ma::experimental::mac_kernel_default_options().threads == physical,
            "Invalid worker preference changed the previous choice");
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
    c::set_worker_count(30);
    c::advance(6, .2, 2);
    c::wait();
    const auto expected = c::checkpoint();
    c::stop();
    c::set_worker_count(1);
    selection.set("1");
    c::restore(config, materials, image, 1);
    require(ma::experimental::mac_kernel_default_options().threads == 1,
            "Restore ignored the current worker preference");
    c::advance(6, .2, 2);
    c::wait();
    require(c::checkpoint() == expected,
            "Game owner cold continuation differs");
    c::stop();
    c::set_worker_count(0);
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
    require(core_version(created)==27 && heat_mixing(created)==8 && phase_selector(created)==1,"Surface selector did not create boil-only core27 with heat mixing 8");
    c::advance(1,.2,2);c::wait();
    require(c::status().state==1,"Profile7 game owner faulted");
    const auto surface_image=c::checkpoint();
    c::advance(2,.2,2);c::wait();const auto surface_expected=c::checkpoint();c::stop();
    surface.set("0");
    c::restore(config,materials,surface_image,1);
    require(c::checkpoint()==surface_image,"Core27 idle restore changed saved profile");
    c::advance(2,.2,2);c::wait();
    require(c::checkpoint()==surface_expected,"Core27 owner cold continuation differs");
    c::stop();
    std::cout<<"PASS explicit core27 creation with saved heat mixing 8 and core27 restore without its selector\n";
    {
      auto flagged=config;flagged.flags|=MA_ONI_COLONY_GAME_SURFACE;
      c::start(flagged,materials,cells,1);
      const auto image=c::checkpoint();c::stop();
      require(core_version(image)==27 && heat_mixing(image)==8 && phase_selector(image)==1,"Profile flag did not create boil-only core27 with heat mixing 8");
      auto lone=flagged;lone.flags=MA_ONI_COLONY_PHYSICS_CONDUCTION|MA_ONI_COLONY_GAME_SURFACE;
      bool refused=false;
      try{c::start(lone,materials,cells,1);}catch(const std::exception&){refused=true;}
      c::stop();
      require(refused,"Profile flag without the game kernel was accepted");
    }
    std::cout<<"PASS default profile flag creates boil-only profile8 core27 without a selector; refused without the game kernel\n";
    {
      // Saved profile-7 options (docs/settings.md): the defaults reproduce the
      // colony created without the setter byte for byte; higher caps give the
      // same physical results; other values are saved and survive a restore.
      auto flagged=config;flagged.flags|=MA_ONI_COLONY_GAME_SURFACE;
      const auto same=[](const MAOniColonyOptions& a,const MAOniColonyOptions& b){return std::memcmp(&a,&b,sizeof a)==0;};
      const auto run=[&](const MAOniColonyOptions* options,std::uint64_t frames,std::vector<std::uint8_t>* created,
                         std::vector<MAOniColonyReading>* readings,std::vector<MAOniColonyConstituent>* parts,MAOniColonyOptions* saved){
        c::set_options(options);
        c::start(flagged,materials,cells,1);
        if(created)*created=c::checkpoint();
        if(saved)require(c::copy_options(*saved),"Profile7 options were not copied back");
        for(std::uint64_t frame=1;frame<=frames;++frame){c::advance(frame,.2,2);c::wait();}
        require(c::status().state==1 && c::status().completed_frames==frames,"Profile7 options colony faulted");
        if(readings){readings->clear();parts->clear();(void)c::readings(*readings,*parts);}
        const auto image=c::checkpoint();c::stop();return image;
      };
      const auto defaults=c::default_options();
      std::vector<std::uint8_t> plain_created,set_created,reset_created;
      std::vector<MAOniColonyReading> plain_readings,robust_readings;std::vector<MAOniColonyConstituent> plain_parts,robust_parts;
      MAOniColonyOptions copied{};
      c::set_options(nullptr);c::start(flagged,materials,cells,1);plain_created=c::checkpoint();c::stop();
      const auto plain=run(nullptr,3,&reset_created,&plain_readings,&plain_parts,&copied);
      require(same(copied,defaults),"Default options differ from the running colony's values");
      (void)run(&defaults,0,&set_created,nullptr,nullptr,nullptr);
      require(plain_created==reset_created && plain_created==set_created && core_version(set_created)==27 && heat_mixing(set_created)==8 &&
              phase_selector(set_created)==1 &&
              defaults.evaporation_below_boiling==0 && defaults.reserved==0 && defaults.abi_version==2 && defaults.struct_size==88,
              "Default options changed the new profile8 colony");
      {
        // Evaporation below boiling (the earlier equilibrium rule) writes
        // core27 with selector 0; without a liquid/vapour pair both rules
        // give the same physical results.
        auto realistic=defaults;realistic.evaporation_below_boiling=1;
        MAOniColonyOptions realistic_saved{};std::vector<std::uint8_t> realistic_created;
        std::vector<MAOniColonyReading> realistic_readings;std::vector<MAOniColonyConstituent> realistic_parts;
        const auto realistic_image=run(&realistic,3,&realistic_created,&realistic_readings,&realistic_parts,&realistic_saved);
        require(same(realistic_saved,realistic) && core_version(realistic_created)==27 && core_version(realistic_image)==27 &&
                phase_selector(realistic_created)==0 && realistic_created.size()==plain_created.size(),
                "Evaporation below boiling was not saved as core27 with the equilibrium selector");
        require(realistic_readings.size()==plain_readings.size() && realistic_parts.size()==plain_parts.size() &&
                std::memcmp(realistic_readings.data(),plain_readings.data(),plain_readings.size()*sizeof(MAOniColonyReading))==0 &&
                std::memcmp(realistic_parts.data(),plain_parts.data(),plain_parts.size()*sizeof(MAOniColonyConstituent))==0,
                "The phase rule changed a colony without liquid/vapour pairs");
        c::set_options(nullptr);
        c::restore(flagged,materials,realistic_created,1);
        MAOniColonyOptions restored{};require(c::copy_options(restored) && same(restored,realistic) && c::checkpoint()==realistic_created,
          "Restore did not keep evaporation below boiling");
        c::stop();
      }
      auto robust=defaults;robust.pressure_maximum_iterations=1600;robust.heat_maximum_iterations=3200;
      MAOniColonyOptions robust_saved{};
      const auto robust_image=run(&robust,3,nullptr,&robust_readings,&robust_parts,&robust_saved);
      require(same(robust_saved,robust) && robust_image!=plain,"Higher caps were not saved");
      require(robust_readings.size()==plain_readings.size() && robust_parts.size()==plain_parts.size() &&
              std::memcmp(robust_readings.data(),plain_readings.data(),plain_readings.size()*sizeof(MAOniColonyReading))==0 &&
              std::memcmp(robust_parts.data(),plain_parts.data(),plain_parts.size()*sizeof(MAOniColonyConstituent))==0,
              "Higher caps changed converged results");
      auto precise=defaults;precise.maximum_timestep_s=static_cast<double>(.1f);precise.pressure_relative_tolerance=1e-10;
      precise.pressure_maximum_iterations=1600;precise.heat_maximum_iterations=3200;precise.gas_heat_mixing=4;
      MAOniColonyOptions precise_saved{};std::vector<std::uint8_t> precise_created;
      (void)run(&precise,0,&precise_created,nullptr,nullptr,&precise_saved);
      require(same(precise_saved,precise) && core_version(precise_created)==27 && heat_mixing(precise_created)==4,"Precise options were not saved");
      c::set_options(nullptr);
      c::restore(flagged,materials,precise_created,1);
      MAOniColonyOptions restored{};require(c::copy_options(restored) && same(restored,precise),"Restore did not keep the saved options");
      require(c::checkpoint()==precise_created,"Options restore changed the saved image");
      c::advance(1,.2,2);c::wait();require(c::status().state==1,"Restored options colony faulted");
      bool busy=false;try{c::set_options(&robust);}catch(const std::invalid_argument&){busy=true;}
      c::stop();
      require(busy,"Options were accepted while an owner runs");
      for(int k=0;k<12;++k){
        auto bad=defaults;
        switch(k){case 0:bad.pressure_maximum_iterations=399;break;case 1:bad.heat_maximum_iterations=799;break;case 2:bad.flow_parts=0;break;
          case 3:bad.maximum_timestep_s=.2;break;case 4:bad.trace_sink_threshold_kg=1e-12;break;case 5:bad.liquid_stiffness_ratio=2;break;
          case 6:bad.pressure_relative_tolerance=std::nan("");break;case 7:bad.gas_heat_mixing=33;break;case 8:bad.sharpen_liquid_interfaces=2;break;
          case 9:bad.evaporation_below_boiling=2;break;case 10:bad.reserved=1;break;default:bad.struct_size=0;break;}
        bool refused=false;try{c::set_options(&bad);}catch(const std::invalid_argument&){refused=true;}
        require(refused,"Out-of-range colony options were accepted");
      }
      c::set_options(&robust);
      auto bad=defaults;bad.phase_relaxation_time_s=0;bool refused=false;try{c::set_options(&bad);}catch(const std::invalid_argument&){refused=true;}
      MAOniColonyOptions kept{};c::start(flagged,materials,cells,1);
      require(refused && c::copy_options(kept) && same(kept,robust),"A refused option replaced the previous options");
      c::stop();c::set_options(nullptr);
      auto old=config;c::set_options(&precise);c::start(old,materials,cells,1);
      const auto core20=c::checkpoint();MAOniColonyOptions legacy{};const bool copied20=c::copy_options(legacy);c::stop();c::set_options(nullptr);
      require(core_version(core20)==20 && copied20 && legacy.pressure_maximum_iterations==200 && legacy.evaporation_below_boiling==1,
              "Options leaked into an older game profile");
    }
    std::cout<<"PASS profile8 options: defaults byte-identical, higher caps identical results, saved/restored values, refusals\n";
    std::cout << "PASS diagnostic kernel3 owner flag5, ownEOS publication, "
                 "native heat, opened door, zero retry/phase skips, ledgers "
                 "and exact owner cold continuation\n";
  } catch (const std::exception &e) {
    ma::oni::colony::stop();
    std::cerr << e.what() << '\n';
    return 1;
  }
}
