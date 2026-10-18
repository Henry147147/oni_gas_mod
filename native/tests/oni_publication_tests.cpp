#include "../src/oni_publication.hpp"
#include <atomic>
#include <iostream>
#include <thread>

namespace {
using namespace ma::oni::published_frames;
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
template<class E, class F> void rejects(F operation, const char* why) {
    bool failed = false; try { operation(); } catch (const E&) { failed = true; } check(failed, why);
}
Frame frame(std::uint64_t completed, double temperature) {
    Frame f; f.status.struct_size = sizeof(f.status); f.status.abi_version = MA_ONI_DOMAIN_ABI_VERSION;
    f.status.state = 1; f.status.cells = 6; f.status.session = 7; f.status.completed_frames = completed;
    f.status.time_seconds = .2 * static_cast<double>(completed); f.cells.resize(6);
    for (auto& c : f.cells) { c.temperature = temperature; c.masses[1] = 1; }
    f.status.native_ticket = 1000 + completed;
    f.solid_heat = {sizeof(f.solid_heat), 1, 1, 0, 7, completed, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    f.solid_endpoint.struct_size = sizeof(f.solid_endpoint); f.solid_endpoint.abi_version = 1;
    f.solid_endpoint.state = 1; f.solid_endpoint.session = 7; f.solid_endpoint.completed_frame = completed;
    f.solid_cell.struct_size = sizeof(f.solid_cell); f.solid_cell.abi_version = 1;
    f.solid_cell.state = 1; f.solid_cell.session = 7; f.solid_cell.completed_frame = completed;
    f.solid_cell.native_ticket = f.status.native_ticket;
    f.native_epoch = 100 + completed; return f;
}
void* identity(std::uintptr_t n) { return reinterpret_cast<void*>(n); }
}
int main() {
    try {
        static_assert(sizeof(MAOniDomainPublicationInfo) == 64);
        reset();
        auto mixed = frame(1, 300); mixed.solid_heat.completed_frame = 2;
        rejects<std::invalid_argument>([&] { record(identity(10), mixed); }, "Mixed-frame heat journal was admitted");
        mixed = frame(1, 300); mixed.solid_cell.native_ticket = 999;
        rejects<std::invalid_argument>([&] { record(identity(10), mixed); }, "Stale native-cell observation was admitted");
        rejects<std::logic_error>([] { static_cast<void>(acquire(identity(1))); }, "Unbound publication was admitted");
        record(identity(10), frame(1, 300)); bind(identity(10), identity(1));
        const auto first = acquire(identity(1)); const auto held = read(first.token); const auto* cells = held->cells.data();
        check(first.completed_frame == 1 && first.session == 7 && first.native_epoch == 101 && cells[0].temperature == 300,
            "Publication did not name its actual source");
        record(identity(10), frame(2, 301));
        const auto still_bound = acquire(identity(1));
        check(still_bound.completed_frame == 1, "A future native write changed an already bound source"); release(still_bound.token);
        bind(identity(10), identity(1)); const auto next = acquire(identity(1));
        check(next.completed_frame == 2 && read(next.token)->cells[0].temperature == 301 && cells[0].temperature == 300,
            "New binding changed the retained frame or failed to advance"); release(next.token);
        rejects<std::logic_error>([] { static_cast<void>(acquire(identity(2))); }, "Wrong native packet was admitted");
        record(identity(10), std::nullopt); bind(identity(10), identity(1));
        rejects<std::logic_error>([] { static_cast<void>(acquire(identity(1))); }, "Unowned native source retained old authority");
        // Cache eviction and owner replacement must not invalidate a lease.
        for (std::uintptr_t i = 20; i < 40; ++i) record(identity(i), frame(i, static_cast<double>(i)));
        check(statistics().cached_frames == 8 && cells[0].temperature == 300, "Publication retention is unbounded or mutated a lease");
        bind(identity(39), identity(1)); std::vector<std::uint64_t> tokens;
        for (int i = 0; i < 15; ++i) tokens.push_back(acquire(identity(1)).token);
        rejects<std::length_error>([] { static_cast<void>(acquire(identity(1))); }, "Lease capacity was not bounded");
        reset(); check(cells[0].temperature == 300 && read(first.token)->status.completed_frames == 1,
            "Owner reset destroyed a leased frame");
        rejects<std::logic_error>([] { static_cast<void>(acquire(identity(1))); }, "Reset retained an unacquired binding");
        for (auto token : tokens) release(token);
        release(first.token); rejects<std::invalid_argument>([&] { release(first.token); }, "Lease released twice");
        rejects<std::invalid_argument>([&] { static_cast<void>(read(first.token)); }, "Released lease remained readable");
        check(statistics().leases == 0, "Lease release leaked retention");
        // Two native callers can reuse the same packet address. Their source
        // bindings must stay local to the calling thread.
        record(identity(50), frame(50, 350)); record(identity(51), frame(51, 351));
        std::atomic<unsigned> ready{}, errors{};
        const auto worker = [&](std::uintptr_t key) {
            try {
                bind(identity(key), identity(1)); ++ready;
                while (ready.load() != 2) std::this_thread::yield();
                for (int i = 0; i < 1000; ++i) {
                    const auto info = acquire(identity(1)); const auto f = read(info.token);
                    if (info.completed_frame != key || f->cells[0].temperature != static_cast<double>(300 + key)) ++errors;
                    release(info.token);
                }
            } catch (...) { ++errors; }
        };
        std::thread a(worker, 50), b(worker, 51); a.join(); b.join();
        check(errors == 0 && statistics().leases == 0, "Concurrent native publication bindings mixed frames or leaked leases");
        std::atomic<unsigned> phase{};
        std::thread stale([&] {
            try {
                bind(identity(50), identity(1)); const auto old = acquire(identity(1)); phase = 1;
                while (phase.load() != 2) std::this_thread::yield();
                rejects<std::logic_error>([] { static_cast<void>(acquire(identity(1))); }, "Another thread's reset retained a stale binding");
                check(read(old.token)->status.completed_frames == 50, "Reset changed a foreign thread's lease"); release(old.token);
            } catch (...) { ++errors; phase = 1; }
        });
        while (phase.load() != 1) std::this_thread::yield();
        reset(); phase = 2; stale.join();
        check(errors == 0 && statistics().leases == 0, "Cross-thread reset or lease lifetime failed");
        std::cout << "PASS native publication identity, bounded leases, immutable views, reset and concurrent readers\n"; return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
