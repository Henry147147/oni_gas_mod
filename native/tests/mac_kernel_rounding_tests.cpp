// Below-range primal parcels: exact product-error records, deterministic
// accepted ledgers, rollback and ordinary checkpoint continuation.
#include "mac_kernel_fixture.hpp"
#include "../src/mac_kernel_detail.hpp"
#include <bit>
#include <filesystem>
#include <fstream>
#include <iomanip>

using namespace ma::testing;
namespace {
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
void wide(MacEnergy value) { std::cout << ' ' << value.mantissa() << ' ' << value.exponent(); }
void write(const MacClosedWorld& world, const std::filesystem::path& path) {
    const auto bytes = world.checkpoint(); std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    check(static_cast<bool>(file), "Cannot write transport-rounding checkpoint");
}
void products() {
    mac_kernel::TransferRounding all;
    std::size_t id = 0;
    const auto test = [&](double a, double b) {
        const double stored = a * b;
        mac_kernel::TransferRounding ledger; ledger.product(a, b, stored);
        check(ledger.within_bound(), "Subnormal product error exceeded its half-quantum bound");
        all.merge(ledger);
        std::cout << "TPRODUCT " << id++ << ' ' << a << ' ' << b << ' ' << stored << ' ' << ledger.count << ' ' << ledger.error.size();
        for (std::size_t j = 0; j < ledger.error.size(); ++j) wide(ledger.error.component(j));
        wide(ledger.bound()); std::cout << '\n';
    };
    const double tiny = std::numeric_limits<double>::denorm_min(), normal = std::numeric_limits<double>::min();
    for (const auto a : {0., tiny, 2 * tiny, 3 * tiny, 5 * tiny, normal, std::nextafter(normal, 0.), 2 * normal})
        for (const auto b : {0., .25, .5, .75, 1., std::nextafter(1., 0.), -std::nextafter(.5, 0.)}) test(a, b);
    std::uint64_t random = UINT64_C(0x6e554b32c789a7d1);
    const auto next = [&]() { random ^= random << 13; random ^= random >> 7; random ^= random << 17; return random; };
    for (std::size_t i = 0; i < 1024; ++i) {
        const double fraction = std::bit_cast<double>((next() & UINT64_C(0xfffffffffffff)) | UINT64_C(0x3fe0000000000000));
        const double a = std::ldexp(fraction, -1020 - static_cast<int>(next() % 55));
        const double b = std::ldexp(std::bit_cast<double>((next() & UINT64_C(0xfffffffffffff)) | UINT64_C(0x3fe0000000000000)), -static_cast<int>(next() % 180));
        test(i % 2 == 0 ? a : -a, b);
    }
    check(all.within_bound() && all.count > 900, "Merged product ledger lost its counts or bound");
    std::cout << "TMERGE " << id << ' ' << all.count << ' ' << all.error.size();
    for (std::size_t j = 0; j < all.error.size(); ++j) wide(all.error.component(j));
    wide(all.bound()); std::cout << '\n';
    mac_kernel::TransferRounding invalid; invalid.product(tiny, .75, 0);
    check(!invalid.within_bound(), "A wrong parcel projection passed the rounding audit");
    invalid = {}; invalid.error.add(MacEnergy::from_double(1).shifted(-1076));
    check(!invalid.within_bound(), "An uncounted error passed the rounding audit");
}
kernel::Scene tracer(std::size_t w, std::size_t h, double amount) {
    const auto& s = setup(); auto scene = kernel::wave(w, h);
    const auto helium = s.registry.index("helium"), centre = w / 2 + (h / 2) * w;
    for (std::size_t i = 0; i < scene.state.cells.size(); ++i) {
        auto masses = scene.state.cells[i].mixture.masses; masses[helium] = i == centre ? amount : 0;
        scene.state.cells[i].mixture = s.eos->initialize_at_temperature(masses, 300, 1).mixture;
        scene.state.cells[i].passive_quantities.clear();
        if (i == centre) scene.state.cells[i].passive_quantities = {{UINT64_C(0x80000000), amount}};
    }
    return scene;
}
void audit(const MacClosedAdvance& after, const MacKernelReport& report) {
    check(!report.reference_fallback && after.report.interval_reference_fallbacks == 0 &&
        after.report.interval_kernel_intervals == 1, "Trace transport left the production kernel");
    std::size_t total = 0;
    for (const auto& entry : report.audits) {
        check(entry.passed, "Trace transport failed its interval audit"); total += entry.rounded_transfers;
    }
    check(total == report.rounded_transfers && after.report.interval_rounded_updates == total + report.rounded_range_inputs + report.rounded_primary_writes + report.rounded_inventory_writes,
        "Accepted parcel ledger did not reach the owner report");
}
void balance(const MacClosedWorld& world, double amount) {
    const auto helium = setup().registry.index("helium"); MacEnergyAccount m, q; m.add(-amount); q.add(-amount);
    for (const auto& cell : world.state().cells) {
        m.add(cell.mixture.masses[helium]);
        for (const auto& passive : cell.passive_quantities) q.add(passive.quantity);
    }
    check(m.within_bound() && q.within_bound(), "Shared trace parcels lost their original inventory bound");
}
void threaded(const std::filesystem::path& output) {
    const auto& s = setup(); auto scene = tracer(193, 173, 1e-310);
    const MacClosedWorld source(s.registry, s.eos, scene.state, scene.settings); const auto held = source.checkpoint();
    MacKernelOptions options; options.threads = 1;
    const auto a = advance_mac_production(source, .002, 4096, options); const auto ra = last_mac_production_report(); audit(a, ra);
    options.threads = 12;
    const auto b = advance_mac_production(source, .002, 4096, options); const auto rb = last_mac_production_report(); audit(b, rb);
    check(rb.threads_used == 12 && ra.rounded_transfers > 0 && ra.rounded_transfers == rb.rounded_transfers &&
        a.world.checkpoint() == b.world.checkpoint(), "Thread count changed accepted trace parcels or rounding counts");
    check(ra.audits.size() == rb.audits.size(), "Thread count changed the audit schema");
    for (std::size_t i = 0; i < ra.audits.size(); ++i) {
        const auto& x = ra.audits[i]; const auto& y = rb.audits[i];
        check(x.rounded_transfers == y.rounded_transfers && x.transfer_roundoff.mantissa() == y.transfer_roundoff.mantissa() &&
            x.transfer_roundoff.exponent() == y.transfer_roundoff.exponent(), "Thread count changed exact rounding reduction");
    }
    balance(b.world, 1e-310);
    const auto restored = MacClosedWorld::from_checkpoint(s.registry, s.eos, b.world.checkpoint());
    set_mac_kernel_default_options(options);
    const auto resumed = restored.advanced(.002); const auto rr = last_mac_production_report(); audit(resumed, rr);
    options.threads = 1; set_mac_kernel_default_options(options);
    const auto expected = a.world.advanced(.002); const auto re = last_mac_production_report(); audit(expected, re);
    check(resumed.world.checkpoint() == expected.world.checkpoint() && rr.rounded_transfers == re.rounded_transfers &&
        source.checkpoint() == held, "Trace checkpoint continuation changed its trajectory or source");
    balance(resumed.world, 1e-310);
    if (!output.empty()) write(b.world, output);
    scene.settings.maximum_timestep_s = .001;
    const MacClosedWorld limited(s.registry, s.eos, scene.state, scene.settings); const auto before = limited.checkpoint(); bool exhausted = false;
    try { static_cast<void>(advance_mac_production(limited, .002, 1, options)); }
    catch (const std::runtime_error&) { exhausted = true; }
    check(exhausted && limited.checkpoint() == before && last_mac_production_report().rounded_transfers == 0 &&
        last_mac_production_report().unresolved_updates == 0,
        "Budget exhaustion published a partial trace state or ledger");
    const auto completed = advance_mac_production(limited, .002, 2, options); const auto rc = last_mac_production_report(); audit(completed, rc);
    const auto half = advance_mac_production(limited, .001, 1, options); const auto r0 = last_mac_production_report();
    const auto second = advance_mac_production(half.world, .001, 1, options); const auto r1 = last_mac_production_report();
    check(completed.world.state().cells.size() == second.world.state().cells.size() &&
        fingerprint(completed.world.state()) == fingerprint(second.world.state()) && rc.rounded_transfers == r0.rounded_transfers + r1.rounded_transfers &&
        rc.unresolved_updates == r0.unresolved_updates + r1.unresolved_updates,
        "Discarded interval work inflated the accepted rounding ledger");
    std::cout << "TTHREAD " << scene.state.cells.size() << ' ' << rb.threads_used << ' ' << rb.rounded_transfers << ' ' << rr.rounded_transfers << ' ' << rc.rounded_transfers << '\n';
}
void front() {
    const auto& s = setup(); auto scene = tracer(128, 64, 1e-28); kernel::set_velocity(s, scene.state, .6, -.3);
    auto world = MacClosedWorld(s.registry, s.eos, scene.state, scene.settings); std::size_t rounded = 0, substeps = 0;
    MacKernelOptions options; options.threads = 12;
    for (std::size_t frame = 0; frame < 160; ++frame) {
        auto after = advance_mac_production(world, .01, 4096, options); const auto report = last_mac_production_report(); audit(after, report);
        rounded += report.rounded_transfers; substeps += report.substeps; world = std::move(after.world);
    }
    balance(world, 1e-28); const auto helium = s.registry.index("helium"); std::size_t positive = 0;
    for (std::size_t i = 0; i < world.state().cells.size(); ++i) {
        const double value = world.state().cells[i].mixture.masses[helium]; positive += value != 0 ? 1U : 0U;
        std::cout << "TMASS " << i << ' ' << value << '\n';
    }
    check(rounded > 0 && positive > 100, "Room trace front did not exercise rounded transport");
    std::cout << "TFRONT 8192 160 " << substeps << ' ' << rounded << ' ' << positive << ' ' << 1e-28 << '\n';
}
}
int main(int argc, char** argv) {
    try {
        std::cout << std::setprecision(17);
        if (argc == 4 && std::string(argv[1]) == "--continue-checkpoint") {
            std::ifstream file(argv[2], std::ios::binary); check(static_cast<bool>(file), "Cannot read trace checkpoint");
            const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
            const auto& s = setup(); const auto world = MacClosedWorld::from_checkpoint(s.registry, s.eos, bytes);
            auto options = mac_kernel_default_options(); options.threads = 12; set_mac_kernel_default_options(options);
            const auto after = world.advanced(.002); audit(after, last_mac_production_report()); write(after.world, argv[3]); return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--write-checkpoint") { threaded(argv[2]); return 0; }
        check(argc == 1, "Unknown transport-rounding argument"); products(); threaded({}); front();
        std::cout << "PASS below-range transport products, accepted ledgers, threads, rollback, continuation and room front\n"; return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
