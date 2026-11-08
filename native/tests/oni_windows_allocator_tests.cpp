// MinGW Windows only (cmake/OniNativeBackend.cmake): the owner DLL's
// allocator must retire the heap of every thread that ends. The bounded
// regional tick starts a coordinator thread for each interval that runs a
// dominant region on inner workers, and a rebuilt tick replaces its worker
// pool; their allocations outlive those threads. If ended threads kept their
// heaps, commit would grow by every such thread's memory, every frame.
#include "../src/mac_game/regional_tick.hpp"
#include <mimalloc.h>
#include <windows.h>
#include <psapi.h>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
namespace k = ma::experimental::game_kernel;

double committed_mib() {
    PROCESS_MEMORY_COUNTERS counters{};
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof counters)) throw std::runtime_error("GetProcessMemoryInfo failed");
    return static_cast<double>(counters.PagefileUsage) / (1024.0 * 1024.0);
}
// Commit once the allocator has returned what it can: abandoned heaps that
// are free and pages awaiting the delayed purge. A heap its thread never
// retired cannot be collected.
double collected_mib() { mi_collect(true); return committed_mib(); }

int scale = 1; // optional second argument: repetitions multiplier

// Commit after `warmup` repetitions against commit after `repeat` more.
// Retained ended-thread heaps grow by each repetition's thread allocations.
template<class Step> void bounded(const char* name, int warmup, int repeat, double limit_mib, Step&& step) {
    repeat *= scale;
    for (int i = 0; i < warmup; ++i) step();
    const double raw = committed_mib(), before = collected_mib();
    for (int i = 0; i < repeat; ++i) step();
    const double raw_growth = committed_mib() - raw, growth = collected_mib() - before;
    std::printf("%s: commit %.1f MiB, growth %.1f MiB (%.1f before collection) over %d repetitions (limit %.0f)\n",
        name, before, growth, raw_growth, repeat, limit_mib);
    if (!(growth < limit_mib)) throw std::runtime_error(std::string(name) + ": ended threads kept their allocator heaps");
}

// A fresh thread allocates 8 MiB the joining thread frees afterwards.
void ended_thread_allocations() {
    bounded("ended thread allocations", 8, 96, 64, [] {
        std::vector<std::unique_ptr<std::vector<double>>> blocks(32);
        std::thread worker([&] { for (auto& block : blocks) block = std::make_unique<std::vector<double>>(32768, 1.0); });
        worker.join();
        blocks.clear();
    });
}

// The game kernel's bounded regional tick with a controlled dominant region
// (as in mac_game_regional_execution_control.hpp): every advance runs the
// residual region on a fresh coordinator thread. A rebuilt tick also ends its
// pool's workers.
struct RegionalSetup {
    ma::experimental::MacGrid grid;
    std::vector<k::Species> species = std::vector<k::Species>(1);
    ma::experimental::MacGameSettings cfg;
    k::State initial;
    RegionalSetup() {
        grid.width = 65; grid.height = 256; grid.walls.resize(grid.width * grid.height);
        for (std::size_t y = 0; y < grid.height; ++y) grid.walls[32 + y * grid.width] = 1;
        species[0].molar_mass = .032; species[0].cv = 700; species[0].conductivity = .02;
        cfg.flow_parts = 2; cfg.gas_heat_mixing = 8; cfg.pressure_maximum_iterations = 400; cfg.heat_maximum_iterations = 800;
        initial.energy.resize(grid.walls.size()); initial.jx.resize(grid.x_width() * grid.height); initial.jy.resize(grid.width * grid.y_height());
        for (std::size_t i = 0; i < grid.walls.size(); ++i) {
            initial.masses.offsets.push_back(initial.masses.mass.size()); initial.passives.offsets.push_back(initial.passives.mass.size());
            if (grid.walls[i]) continue;
            const double mass = 1 + .0002 * static_cast<double>(i % grid.width);
            initial.masses.species.push_back(0); initial.masses.mass.push_back(mass);
            initial.energy[i] = mass * species[0].cv * (300 + .005 * static_cast<double>(i / grid.width));
        }
        initial.masses.offsets.push_back(initial.masses.mass.size()); initial.passives.offsets.push_back(initial.passives.mass.size());
        initial.jx[grid.x_face(5, 100)] = .01; initial.jy[grid.y_face(8, 128)] = -.02;
    }
    std::unique_ptr<k::RegionalTick> tick(const std::shared_ptr<k::RegionalExecutionHistory>& history) const {
        auto made = std::make_unique<k::RegionalTick>(grid, species, std::vector<k::PhasePair>{}, 8, cfg, true, true, true, 1e-6, history);
        if (made->execution_region_keys().size() != 2) throw std::runtime_error("Allocator control lost its two regions");
        return made;
    }
    // Measured costs would make the dispatch depend on host timing: pin the
    // first region as dominant before every advance, without serial probes.
    static k::TickResult advance(k::RegionalTick& tick, k::RegionalExecutionHistory& history, const k::State& state) {
        const auto keys = tick.execution_region_keys();
        history.costs[keys[0]] = {100 / .02, 2}; history.costs[keys[1]] = {1 / .02, 2};
        history.selected_intervals = 0;
        auto result = tick.advance(state, .02, 0, 1e-11);
        if (history.selected != keys[0]) throw std::runtime_error("Allocator control did not use the parallel regional path");
        return result;
    }
};

void regional_coordinator_per_interval(const RegionalSetup& setup) {
    auto history = std::make_shared<k::RegionalExecutionHistory>();
    auto tick = setup.tick(history);
    auto state = setup.initial;
    bounded("regional coordinator per interval", 8, 96, 64, [&] { state = RegionalSetup::advance(*tick, *history, state).state; });
}

void rebuilt_regional_pools(const RegionalSetup& setup) {
    auto history = std::make_shared<k::RegionalExecutionHistory>();
    bounded("rebuilt regional worker pools", 4, 32, 64, [&] {
        auto tick = setup.tick(history);
        (void)RegionalSetup::advance(*tick, *history, setup.initial);
    });
}
}

// Optional arguments: one case (threads, coordinator, pools or all) and a
// repetitions multiplier.
int main(int argc, char** argv) {
    try {
        std::string only = argc > 1 ? argv[1] : "";
        if (only == "all") only.clear();
        if (argc > 2) scale = std::max(1, std::atoi(argv[2]));
        if (only.empty() || only == "threads") ended_thread_allocations();
        const RegionalSetup setup;
        if (only.empty() || only == "coordinator") regional_coordinator_per_interval(setup);
        if (only.empty() || only == "pools") rebuilt_regional_pools(setup);
        std::puts("PASS ended threads return their allocator heaps: fresh threads, regional coordinators and rebuilt worker pools");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL %s\n", error.what());
        return 1;
    }
}
