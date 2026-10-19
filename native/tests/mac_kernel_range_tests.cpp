// A below-range velocity/sweep can carry representable momentum and material.
// These controls retain the physical inputs and compare with the strict oracle.
#include "mac_kernel_fixture.hpp"
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>

using namespace ma::testing;
namespace {
constexpr std::uint64_t tracer_id = UINT64_C(0x80000000);
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
double packet(const MacClosedWorld& world, std::size_t cell) {
    for (const auto& p : world.state().cells[cell].passive_quantities) if (p.identity == tracer_id) return p.quantity;
    return 0;
}
MacClosedWorld source(std::size_t w, std::size_t h, int axis, double momentum, double amount, bool wide_area = false, std::size_t position = 0) {
    const auto& s = setup(); auto state = s.state(w, h, true);
    for (auto& cell : state.cells) {
        auto masses = cell.mixture.masses; for (auto& mass : masses) mass = std::ldexp(mass, 420);
        cell.mixture = s.eos->initialize_at_temperature(masses, 300, 1).mixture; cell.passive_quantities.clear();
    }
    if (amount != 0) state.cells.at(position).passive_quantities = {{tracer_id, amount}};
    for (auto& j : axis == 0 ? state.momentum_x : state.momentum_y) j = momentum;
    if (wide_area) { state.grid.spacing_m = 0x1p-500; state.grid.depth_m = 0x1p1000; }
    return MacClosedWorld(s.registry, s.eos, state, {});
}
void audited(const MacClosedAdvance& next, const MacKernelReport& report) {
    check(!report.reference_fallback && next.report.interval_reference_fallbacks == 0 &&
        next.report.interval_kernel_intervals == 1, "Flagged transport unnecessarily left production");
    std::size_t rounded = 0;
    for (const auto& a : report.audits) { check(a.passed, "Flagged transport failed its interval audit"); rounded += a.rounded_transfers; }
    check(rounded == report.rounded_transfers && next.report.interval_rounded_updates == rounded + report.rounded_range_inputs,
        "Flagged product counts did not reach the owner");
    check(next.report.interval_unresolved_updates == report.unresolved_updates, "Accepted write counts did not reach the owner");
}
void export_ledgers(int id, const MacKernelReport& report) {
    for (const auto& a : report.audits)
        std::cout << "RLEDGER " << id << ' ' << a.quantity << ' ' << a.rounded_transfers << ' '
            << a.transfer_roundoff.mantissa() << ' ' << a.transfer_roundoff.exponent() << ' '
            << a.transfer_roundoff_bound.mantissa() << ' ' << a.transfer_roundoff_bound.exponent() << '\n';
    std::cout << "RWRITE " << id << ' ' << report.unresolved_updates << '\n';
}
void small(int id, int axis, double momentum, double amount, double quantized = 0) {
    const auto before = source(3, 2, axis, momentum, amount); const auto held = before.checkpoint();
    const auto next = advance_mac_production(before, .002, 1, {}); const auto report = last_mac_production_report(); audited(next, report);
    const auto target = axis == 0 ? (momentum > 0 ? 1U : 2U) : 3U;
    const auto& j = axis == 0 ? next.world.state().momentum_x : next.world.state().momentum_y;
    check(std::all_of(j.begin(), j.end(), [&](double value) { return value == momentum; }), "Derived velocity erased canonical momentum");
    check(before.checkpoint() == held && report.rounded_transfers > 0 && report.attempts == 1,
        "Flagged interval changed its source or failed its shared budget");
    double reference_arrival = 0;
    if (amount >= 0x1p400) {
        const auto reference = before.advanced(.002, 1); reference_arrival = packet(reference.world, target);
        check(reference_arrival > 0 && packet(next.world, target) == reference_arrival,
            "Below-range sweep erased a representable constituent transfer");
        check(fingerprint(next.world.state()) == fingerprint(reference.world.state()), "Flagged endpoint differs from the strict reference");
    } else if (amount != 0) {
        const auto a = std::find_if(report.audits.begin(), report.audits.end(), [](const auto& entry) { return entry.quantity == "passive:2147483648"; });
        check(a != report.audits.end() && a->rounded_transfers == 1 && packet(next.world, 0) == amount && packet(next.world, target) == quantized,
            "Below-range material transfer was not rounded once through a shared parcel");
    }
    std::cout << "RCASE " << id << ' ' << axis << ' ' << before.state().cells[0].mixture.mass() << ' ' << momentum
        << ' ' << .002 << ' ' << amount << ' ' << packet(next.world, target) << ' ' << reference_arrival
        << ' ' << report.rounded_transfers << '\n';
    export_ledgers(id, report);
}
void write(const MacClosedWorld& world, const std::filesystem::path& path) {
    const auto bytes = world.checkpoint(); std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    check(static_cast<bool>(file), "Cannot write flagged-transport checkpoint");
}
MacClosedWorld threaded() {
    const auto before = source(193, 173, 0, 0x1p-700, 0x1p450); const auto held = before.checkpoint();
    MacKernelOptions options; options.threads = 1;
    const auto normal = kernel::wave(193, 173); const auto& s = setup();
    const MacClosedWorld ordinary(s.registry, s.eos, normal.state, normal.settings);
    const auto ordinary_expected = advance_mac_production(ordinary, .002, 1, options).world.checkpoint();
    const auto a = advance_mac_production(before, .002, 1, options); const auto ra = last_mac_production_report(); audited(a, ra);
    options.threads = 12;
    const auto b = advance_mac_production(before, .002, 1, options); const auto rb = last_mac_production_report(); audited(b, rb);
    check(rb.threads_used == 12 && a.world.checkpoint() == b.world.checkpoint() && ra.rounded_transfers == rb.rounded_transfers,
        "Worker count changed flagged transport");
    check(ra.unresolved_updates == rb.unresolved_updates, "Worker count changed accepted primary-write counts");
    for (std::size_t i = 0; i < ra.audits.size(); ++i) {
        const auto& x = ra.audits[i]; const auto& y = rb.audits[i];
        check(x.rounded_transfers == y.rounded_transfers && x.transfer_roundoff.mantissa() == y.transfer_roundoff.mantissa() &&
            x.transfer_roundoff.exponent() == y.transfer_roundoff.exponent(), "Worker count changed a flagged error ledger");
    }
    check(advance_mac_production(ordinary, .002, 1, options).world.checkpoint() == ordinary_expected,
        "Flagged sweep cache contaminated an ordinary owner of the same geometry");
    const auto restored = MacClosedWorld::from_checkpoint(s.registry, s.eos, b.world.checkpoint());
    set_mac_kernel_default_options(options); const auto resumed = restored.advanced(.002);
    options.threads = 1; set_mac_kernel_default_options(options); const auto expected = a.world.advanced(.002);
    check(resumed.world.checkpoint() == expected.world.checkpoint() && before.checkpoint() == held,
        "Ordinary flagged checkpoint continuation changed its trajectory or source");
    std::cout << "RTHREAD 33389 " << rb.threads_used << ' ' << rb.rounded_transfers << ' ' << packet(b.world, 1) << '\n';
    export_ledgers(100, rb);
    return b.world;
}
void fallback_and_concurrency() {
    const auto before = source(3, 2, 0, 0x1p-700, 0x1p450, true); const auto held = before.checkpoint(); bool failed = false;
    try { static_cast<void>(advance_mac_production(before, .002, 1, {})); } catch (const std::runtime_error&) { failed = true; }
    check(failed && before.checkpoint() == held, "Intermediate range refinement reset its budget or published a candidate");
    const auto next = advance_mac_production(before, .002, 2, {}); const auto report = last_mac_production_report();
    const auto reference = before.advanced(.002, 1);
    check(report.reference_fallback && next.report.attempts == 2 && next.report.rejected_attempts == 1 &&
        next.report.interval_reference_fallbacks == 1 && fingerprint(next.world.state()) == fingerprint(reference.world.state()),
        "Representable sweep refinement changed the strict result or consumed-attempt count");
    const auto a = source(3, 2, 0, 0x1p-700, 0x1p450), b = source(4, 3, 1, -0x1p-700, 0x1p450);
    const auto ea = advance_mac_production(a, .002, 1, {}).world.checkpoint(), eb = advance_mac_production(b, .002, 1, {}).world.checkpoint();
    auto fa = std::async(std::launch::async, [&]() { return advance_mac_production(a, .002, 1, {}).world.checkpoint(); });
    auto fb = std::async(std::launch::async, [&]() { return advance_mac_production(b, .002, 1, {}).world.checkpoint(); });
    check(fa.get() == ea && fb.get() == eb, "Concurrent owners shared flagged sweep storage");
    std::cout << "RREFINE " << next.report.attempts << ' ' << next.report.rejected_attempts << ' ' << packet(next.world, 1) << ' ' << packet(reference.world, 1) << '\n';
    std::cout << "RCONTROLS 1 1 1\n";
}
void cross_block() {
    constexpr std::size_t donor = 4 * 193 + 5, target = 5 * 193 + 5;
    const auto before = source(193, 173, 1, 0x1p-700, 0x1p450, false, donor);
    const auto a = advance_mac_production(before, .002, 1, {}); const auto ra = last_mac_production_report(); audited(a, ra);
    MacKernelOptions options; options.threads = 12;
    const auto b = advance_mac_production(before, .002, 1, options); const auto rb = last_mac_production_report(); audited(b, rb);
    check(rb.threads_used == 12 && packet(b.world, target) > 0 && a.world.checkpoint() == b.world.checkpoint() &&
        ra.rounded_transfers == rb.rounded_transfers && ra.unresolved_updates == rb.unresolved_updates,
        "Flagged material did not cross a deterministic row-block boundary");
    std::cout << "RCROSS 33389 12 " << donor << ' ' << target << ' ' << rb.rounded_transfers << ' ' << packet(b.world, target) << '\n';
    export_ledgers(101, rb);
}
void share_case(int id, int axis, double momentum, double amount, double quantized = 0) {
    const auto initial = source(3, 2, axis, momentum, amount); auto state = initial.state();
    state.grid.spacing_m = 0x1p450; state.grid.depth_m = 0x1p-450; // V=2^450, area=1
    const auto& s = setup(); const MacClosedWorld before(s.registry, s.eos, state, initial.settings());
    const auto held = before.checkpoint(); const auto next = advance_mac_production(before, .002, 1, {});
    const auto report = last_mac_production_report(); audited(next, report);
    const std::size_t target = axis == 0 ? (momentum > 0 ? 1U : 2U) : 3U;
    double reference_arrival = 0;
    if (amount >= 0x1p400) {
        const auto reference = before.advanced(.002, 1); reference_arrival = packet(reference.world, target);
        check(reference_arrival > 0 && packet(next.world, target) == reference_arrival &&
            fingerprint(next.world.state()) == fingerprint(reference.world.state()),
            "Normal sweep lost a representable parcel through its below-range share");
    } else check(packet(next.world, target) == quantized, "Flagged parcel share used the wrong storage quantum");
    check(before.checkpoint() == held && report.attempts == 1, "Parcel-share refinement changed its source or shared budget");
    std::cout << "RSHARE " << id << ' ' << axis << ' ' << state.cells[0].mixture.mass() << ' ' << momentum << ' ' << .002
        << ' ' << state.grid.volume_m3() << ' ' << amount << ' ' << packet(next.world, target) << ' ' << reference_arrival
        << ' ' << report.rounded_transfers << '\n';
    export_ledgers(200 + id, report);
}
}
int main(int argc, char** argv) {
    try {
        std::cout << std::setprecision(17);
        if (argc == 4 && std::string(argv[1]) == "--continue-checkpoint") {
            std::ifstream file(argv[2], std::ios::binary); check(static_cast<bool>(file), "Cannot read flagged checkpoint");
            const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
            const auto& s = setup(); const auto world = MacClosedWorld::from_checkpoint(s.registry, s.eos, bytes);
            auto options = mac_kernel_default_options(); options.threads = 12; set_mac_kernel_default_options(options);
            const auto next = world.advanced(.002); audited(next, last_mac_production_report()); write(next.world, argv[3]); return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--write-checkpoint") { write(threaded(), argv[2]); return 0; }
        check(argc == 1, "Unknown flagged-transport argument");
        std::cout << "RVERSION 2\n";
        small(0, 0, 0x1p-700, 0); small(1, 0, 0x1p-700, 0x1p450); small(2, 0, -0x1p-700, 0x1p450);
        small(3, 1, 0x1p-700, 0x1p450); small(4, 1, -0x1p-700, 0x1p450); small(5, 0, 0x1p-700, 0x1p-600);
        small(6, 0, 0x1p-700, 0x1p54); small(7, 0, 0x1p-700, 0x1p55, std::numeric_limits<double>::denorm_min());
        small(8, 0, 0x1p-700, 0x1p56, 2 * std::numeric_limits<double>::denorm_min());
        static_cast<void>(threaded()); cross_block(); fallback_and_concurrency();
        share_case(0, 0, 0x1p-200, 0x1p450); share_case(1, 0, 0x1p-200, 16);
        share_case(2, 0, -0x1p-200, 32, std::numeric_limits<double>::denorm_min());
        share_case(3, 1, 0x1p-200, 64, 2 * std::numeric_limits<double>::denorm_min());
        std::cout << "PASS flagged swept volumes, canonical momentum, quantized parcels, threads, cache lifetime, rollback and continuation\n"; return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
