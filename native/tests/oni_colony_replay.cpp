// Owner continuation under identical inputs (docs/colony-owner.md#saves).
//
//   ma_oni_colony_replay SAVED.owner JOURNAL.txt DISEASES.bin REFERENCE_PREFIX FRAMES [--menu]
//   ma_oni_colony_replay --self-test EMPTY_DIRECTORY
//
// SAVED.owner is the owner image of an actual save, JOURNAL.txt the
// uninterrupted branch's committed inputs (MA_COLONY_INPUT_JOURNAL_DIR),
// DISEASES.bin the game's disease table from the same journal directory and
// REFERENCE_PREFIX-<frame>.owner the uninterrupted branch's held images
// (MA_COLONY_CONTINUATION_DUMP). The saved owner is restored in this fresh
// process (a cold load) and, with --menu, stopped and restored again in it
// (a menu reload); each restored owner then receives exactly the
// uninterrupted branch's inputs. Every frame's complete owner state (wrapper
// ledgers and counters, germ clocks and accounts, held precision inventory
// and the physical image; not the next frame's queued inputs) must equal the
// uninterrupted image byte for byte. The restored queue must be the prefix
// of the first resumed frame's inputs, callback-owned requests included.
#include "../src/oni_colony.hpp"
#include "../src/oni_colony_journal.hpp"
#include "../src/oni_sha256.hpp"
#include <chrono>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <span>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <optional>
#include <utility>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace journal_replay = ma::oni::colony::journal_replay;
using namespace journal_replay;
bool release_restored = false; // negative control: the previous save policy
std::size_t replay(const std::vector<std::uint8_t>& saved, const std::vector<std::pair<std::uint64_t, Frame>>& frames,
    const std::string& prefix, std::uint64_t count, bool menu, std::uint64_t& ticket) {
    const auto compared = journal_replay::replay(saved, frames, count, release_restored, ticket, [&](std::uint64_t number, const Frame&) {
        const auto reference = parse(read_file(prefix + "-" + std::to_string(number) + ".owner"));
        const auto actual = parse(ma::oni::colony::checkpoint());
        require(actual.completed == number && reference.completed == number, "Replayed frame number differs");
        if (actual.state != reference.state) {
            std::size_t at = 0; while (at < actual.state.size() && at < reference.state.size() && actual.state[at] == reference.state[at]) ++at;
            throw std::runtime_error((menu ? "menu" : "cold") + std::string(" replay differs from the uninterrupted owner at frame ") + std::to_string(number) +
                " (state byte " + std::to_string(at) + " of " + std::to_string(actual.state.size()) + "/" + std::to_string(reference.state.size()) + ")");
        }
    });
    require(compared == count, "The journal or references do not cover every requested frame");
    return compared;
}

// --self-test DIR: a synthetic owner journals breaths, a consumer and
// unanswered injections, is saved with callback-owned requests in flight, and
// continues; the saved image must then replay byte for byte, cold and menu.
std::vector<std::string> self_test(const std::string& parent,bool activity=false) {
    using namespace ma::oni::colony;
    // A fresh private directory: journals and dumps never overwrite.
    const auto directory = (std::filesystem::path(parent) / ("replay-self-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))).string();
    require(std::filesystem::create_directory(directory), "Cannot create a fresh self-test directory");
#if defined(_WIN32)
    require(_putenv_s("MA_COLONY_INPUT_JOURNAL_DIR", directory.c_str()) == 0, "Cannot select the journal");
#else
    require(setenv("MA_COLONY_INPUT_JOURNAL_DIR", directory.c_str(), 1) == 0, "Cannot select the journal");
#endif
    const auto material = [](const char* id, std::int32_t hash, std::uint32_t index, double molar, double c) {
        MAOniColonyMaterial m{}; m.struct_size = sizeof(m); m.abi_version = MA_ONI_COLONY_ABI_VERSION; m.element_hash = hash; m.native_index = index;
        m.phase = 1; m.molar_mass_kg_per_mol = molar; m.specific_heat_j_per_kg_k = c; m.conductivity_w_per_m_k = .02;
        m.low_temperature_k = 50; m.high_temperature_k = 1e4; std::snprintf(m.id, sizeof(m.id), "%s", id); return m;
    };
    constexpr std::uint32_t oxygen = 3, carbon = 4, vacuum = 9, width = 6, height = 4;
    const std::vector<MAOniColonyMaterial> mats{material("Oxygen", 11, oxygen, .016, 1000), material("CarbonDioxide", 12, carbon, .044, 850)};
    MAOniColonyConfig cfg{}; cfg.struct_size = sizeof(cfg); cfg.abi_version = MA_ONI_COLONY_ABI_VERSION; cfg.vacuum_index = vacuum;
    cfg.grid_width = cfg.width = width; cfg.grid_height = cfg.height = height; cfg.maximum_timestep_s = .1;
    if(activity)cfg.flags=MA_ONI_COLONY_PHYSICS_CONDUCTION|MA_ONI_COLONY_GAME_KERNEL|MA_ONI_COLONY_GAME_SURFACE|MA_ONI_COLONY_GAME_ACTIVITY;
    std::vector<MAOniColonyCell> cells(width * height, MAOniColonyCell{0, MA_ONI_COLONY_SOLID, 0, 0, 0, 0});
    for (std::uint32_t y = 1; y + 1 < height; ++y) for (std::uint32_t x = 1; x + 1 < width; ++x)
        cells[x + y * width] = {x % 2 ? oxygen : carbon, MA_ONI_COLONY_FLUID, 1.1f + .1f * static_cast<float>(x), 290.f + 3.f * static_cast<float>(y), 3, 103};
    set_diseases({}, {}, 0); start(cfg, mats, cells, 1); enable_precise_receipts(true);
    const auto source_session=status().session;
    std::string disease_path;
    for(const auto& entry:std::filesystem::directory_iterator(directory))
        if(entry.path().filename().string().starts_with("diseases-") && entry.path().extension()==".bin")disease_path=entry.path().string();
    require(!disease_path.empty(),"Self-test disease journal was not written");
    const auto write = [&](const std::string& name, const std::vector<std::uint8_t>& bytes) {
        std::ofstream out(directory + "/" + name, std::ios::binary); out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        require(out.good(), "Cannot write " + name);
    };
    const auto enqueue=[&](std::uint64_t frame) {
        std::vector<MAOniColonyTransfer> inputs(4);
        inputs[0] = {width + 1, oxygen, MA_ONI_COLONY_EXTRACT, 1, static_cast<std::int32_t>(100 + frame), 0, .004 + 1e-4 * static_cast<double>(frame), 0, 0}; // a breath
        inputs[1] = {width + 4, 0, MA_ONI_COLONY_EXTRACT_GAS | MA_ONI_COLONY_CONSUMER, 1, 0x7e000001, 0, .003, 0, 0};                       // a consumer
        inputs[2] = {2 * width + 2, carbon, MA_ONI_COLONY_INJECT, 0, -1, 0, .002, 310, 0};                                                 // a native change
        inputs[3] = {2 * width + 3, oxygen, MA_ONI_COLONY_INJECT, 0, static_cast<std::int32_t>(200 + frame), 0, .001, 305, 0};            // an answered output
        queue_transfers(inputs);
        const MAOniColonyHeat heat{width + 2, 0, 7.5 * static_cast<double>(frame), 2000}; queue_heat(std::span(&heat, 1));
        if(activity) {
            if(frame==6) {
                // A player's options change after the save (docs/settings.md):
                // journaled, and applied by the replay before the same frame.
                auto options=default_options();options.maximum_timestep_s=static_cast<double>(.1f);options.gas_heat_mixing=4;
                options.evaporation_below_boiling=1;
                require(change_options(status().session,options),"Self-test options change was refused");
                set_source_energy(status().session,1); // and a source energy change
            }
            if(frame==6)set_activity_tiers(status().session,false);
            if(frame==7)set_activity_tiers(status().session,true);
            const std::vector<std::uint32_t> actors=frame==6?std::vector<std::uint32_t>{}:
                std::vector<std::uint32_t>{frame>=7?width+4:width+1};
            (void)queue_activity(status().session,static_cast<double>(.2f),actors,{});
        }
    };
    for (std::uint64_t frame = 1; frame <= 8; ++frame) {
        if(!activity || frame!=5)enqueue(frame);
        if (frame == 4) {
            if(activity)enqueue(5); // two immutable submissions survive cold/menu restore
            write("saved.owner", checkpoint());
        }
        advance(frame, activity?static_cast<double>(.2f):.2, 2); wait(); require(status().state == 1, "Self-test owner faulted");
        std::vector<MAOniColonyReceipt> receipts(64); const auto n = copy_receipts(receipts.data(), 64); if (n) ack_receipts(receipts[n - 1].sequence);
        write("reference-" + std::to_string(frame) + ".owner", checkpoint());
    }
    stop();
    return {directory + "/saved.owner", directory + "/inputs-"+std::to_string(source_session)+".txt",
        disease_path, directory + "/reference", "5", "--menu"};
}
int run(std::vector<std::string> argv) {
    try {
        const auto argc = argv.size() + 1;
        require(argc == 6 || (argc == 7 && argv[5] == "--menu"),
            "Usage: ma_oni_colony_replay SAVED.owner JOURNAL.txt DISEASES.bin REFERENCE_PREFIX FRAMES [--menu]");
        const auto saved = read_file(argv[0]);require(saved.size()>=64,"Truncated saved owner");
        const auto frames = journal(argv[1],std::string(saved.end()-64,saved.end()));const auto diseases = read_file(argv[2]);
        const auto count = std::stoull(argv[4]);
        require(diseases.size() >= 12, "Invalid disease table");
        std::uint32_t counts[3]; std::memcpy(counts, diseases.data(), 12);
        require(diseases.size() == 12 + counts[0] * sizeof(MAOniColonyDisease) + counts[1] * sizeof(MAOniColonyDiseaseGrowth), "Disease table size differs");
        std::vector<MAOniColonyDisease> table(counts[0]); std::vector<MAOniColonyDiseaseGrowth> growth(counts[1]);
        if (counts[0]) std::memcpy(table.data(), diseases.data() + 12, counts[0] * sizeof(MAOniColonyDisease));
        if (counts[1]) std::memcpy(growth.data(), diseases.data() + 12 + counts[0] * sizeof(MAOniColonyDisease), counts[1] * sizeof(MAOniColonyDiseaseGrowth));
        ma::oni::colony::set_diseases(table, growth, counts[2]);
        std::uint64_t ticket = 0;
        const auto cold = replay(saved, frames, argv[3], count, false, ticket);
        std::cout << "PASS cold replay: " << cold << " frames equal the uninterrupted owner (state SHA256 " << ma::oni::hash_memory(saved.data(), saved.size()).substr(0, 16) << " source)\n";
        if (argc == 7) {
            ma::oni::colony::stop();
            const auto menu = replay(saved, frames, argv[3], count, true, ticket);
            std::cout << "PASS menu replay: " << menu << " frames equal the uninterrupted owner after an in-process restore\n";
        }
        ma::oni::colony::stop();
        return 0;
    } catch (const std::exception& error) {
        ma::oni::colony::stop();
        std::cerr << "REPLAY FAIL: " << error.what() << '\n'; return 1;
    }
}
}

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.size() == 2 && args[0] == "--self-test") {
        const auto parent=args[1];
        for(const bool activity:{false,true}) {
            release_restored=false;
            try { args = self_test(parent,activity); } catch (const std::exception& error) { std::cerr << "REPLAY FAIL: " << error.what() << '\n'; return 1; }
            if (run(args) != 0) return 1;
            // Dropping saved callback-owned inputs must diverge immediately.
            release_restored = true; args.pop_back();
            std::cerr << "expected failure follows (released callback-owned requests):\n";
            if (run(args) == 0) { std::cerr << "REPLAY FAIL: releasing in-flight requests was not detected\n"; return 1; }
            std::cout << "PASS negative control: releasing the saved in-flight requests diverges at the first resumed frame\n";
            std::filesystem::remove_all(std::filesystem::path(args[0]).parent_path());
        }
        return 0;
    }
    return run(args);
}
