// Face time integration is shared across both local time grids. These are
// component controls; full MAC subcycling does not use this helper yet.
#include "../src/mac_kernel_time.hpp"
#include "../src/mac_kernel_detail.hpp"
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace ma::experimental;
using namespace ma::experimental::mac_kernel;
namespace {
void check(bool yes, const char* why) { if (!yes) throw std::runtime_error(why); }
MacEnergy value(const TimeFaceFlux& f, unsigned which) {
    if (f.full) return which == 0 ? f.full->momentum : which == 1 ? f.full->volume : f.full->first_volume;
    return MacEnergy::from_double(which == 0 ? f.momentum : which == 1 ? f.volume : f.first_volume);
}
void scalar(const char* tag, unsigned id, unsigned piece, MacEnergy v) {
    std::cout << tag << ' ' << id << ' ' << piece << ' ' << v.mantissa() << ' ' << v.exponent().to_string() << '\n';
}
void face(unsigned id, double j0, double f0, double f1, double I, double A, double dt, unsigned level) {
    const auto f = integrate_time_face(j0, f0, f1, I, A, dt, level);
    MacEnergyAccount energy;
    energy.add(MacEnergy::kinetic(I, value(f, 0))); energy.add(MacEnergy::kinetic(I, j0).negated());
    energy.add(value(f, 1).multiplied(f0).shared(1, A).negated());
    energy.add(value(f, 2).multiplied(f1).shared(1, A).negated());
    check(energy.within_bound(), "Shared time moments lost kinetic work");
    std::cout << "TFACE " << id << ' ' << j0 << ' ' << f0 << ' ' << f1 << ' ' << I << ' ' << A << ' ' << dt << ' '
        << level << ' ' << static_cast<bool>(f.full) << '\n';
    scalar("TJ", id, 0, value(f, 0)); scalar("TQ", id, 0, value(f, 1)); scalar("TB", id, 0, value(f, 2));
    if (id == 4) check(f.volume == 0 && f.full && !f.full->volume.is_zero() &&
        f.full->volume.multiplied(0x1p450).project().value > 0, "Below-range face sweep lost a representable later product");
    if (id == 5 || id == 6 || id == 7) check(f.full && std::isnormal(f.volume), "Flagged intermediate erased a normal final sweep");
}
void pair(unsigned id, unsigned left, unsigned right) {
    const auto count = std::uint32_t{1} << std::max(left, right); constexpr double dt = .1, I = 2, A = .75, body = .03125;
    double j = .125; MacEnergySum shared_left, shared_right; std::vector<MacEnergySum> coarse(std::uint32_t{1} << left), fine(std::uint32_t{1} << right);
    for (std::uint32_t s = 0; s < count; ++s) {
        const auto piece = time_mortar_piece(left, right, s); const auto& l = piece.left; const auto& r = piece.right;
        const double lp = .5 + .03 * l.interval, ly = .125 - .01 * l.interval;
        const double rp = .25 + .02 * r.interval, ry = -.0625 + .015 * r.interval;
        const double f0 = A * ((lp + l.midpoint_p1 * ly) - (rp + r.midpoint_p1 * ry)) + body;
        const double f1 = A * (l.relative_duration * ly - r.relative_duration * ry);
        const auto flux = integrate_time_face(j, f0, f1, I, A, dt, piece.level);
        check(!flux.full, "Ordinary time interface unnecessarily used range arithmetic");
        const auto lb = time_cell_first_moment(l, flux.volume, flux.first_volume);
        const auto rb = time_cell_first_moment(r, flux.volume, flux.first_volume);
        check(!lb.full && !rb.full, "Ordinary basis conversion unnecessarily used range arithmetic");
        MacEnergyAccount account;
        account.add(MacEnergy::kinetic(I, flux.momentum)); account.add(MacEnergy::kinetic(I, j).negated());
        account.add(-lp * flux.volume); account.add(-ly * lb.value);
        account.add(rp * flux.volume); account.add(ry * rb.value); account.add(-body * flux.volume / A);
        check(account.within_bound(), "Different local bases lost shared-face energy work");
        shared_left.add(flux.volume); shared_right.add(-flux.volume); coarse[l.interval].add(lb.value); fine[r.interval].add(rb.value);
        std::cout << "TPIECE " << id << ' ' << left << ' ' << right << ' ' << s << ' ' << j << ' ' << lp << ' ' << ly << ' ' << rp << ' ' << ry
            << ' ' << I << ' ' << A << ' ' << body << ' ' << dt << ' ' << l.interval << ' ' << r.interval << ' ' << l.midpoint_p1 << ' '
            << l.relative_duration << ' ' << r.midpoint_p1 << ' ' << r.relative_duration << ' ' << flux.momentum << ' ' << flux.volume << ' '
            << flux.first_volume << ' ' << lb.value << ' ' << rb.value << '\n';
        j = flux.momentum;
    }
    for (std::size_t k = 0; k < shared_right.size(); ++k) shared_left.add(shared_right.component(k));
    check(shared_left.sign() == 0, "Time interface did not carry one antisymmetric volume flux");
    std::cout << "TPAIR " << id << ' ' << count << ' ' << j << '\n';
}
void metadata() {
    for (unsigned l = 0; l <= 20; ++l) for (unsigned r = 0; r <= 20; ++r) {
        const auto count = std::uint32_t{1} << std::max(l, r);
        for (const auto s : {std::uint32_t{0}, count / 2, count - 1}) {
            const auto p = time_mortar_piece(l, r, s);
            check(p.count == count && p.left.interval == (s >> (std::max(l, r) - l)) &&
                p.right.interval == (s >> (std::max(l, r) - r)), "Dyadic mortar mapped the wrong cell interval");
        }
    }
    const auto basis = time_mortar_piece(0, 20, (std::uint32_t{1} << 19) - 1).left;
    const auto small = time_cell_first_moment(basis, 0, 0x1p-1060);
    check(small.full && small.value == 0 && !small.full->is_zero(), "Basis conversion clipped a below-range time moment");
    scalar("TMOMENT", 0, 0, *small.full);
    std::cout << "PASS all dyadic level pairs and flagged local-basis products\n";
}
void threaded() {
    constexpr std::size_t count = 32768; std::vector<double> expected(count), actual(count); std::vector<std::uint8_t> flags(count), parallel_flags(count);
    std::vector<std::string> full_values(count), parallel_full(count);
    const auto job = [&](std::size_t i, auto& values, auto& marked, auto& full) {
        const auto f = integrate_time_face(i % 2 == 0 ? .125 : 0x1p-700, i % 2 == 0 ? .5 : 0, i % 2 == 0 ? -.125 : 0,
            i % 2 == 0 ? 2 : 0x1p420, 1, .002, static_cast<unsigned>(i % 8));
        values[i] = f.volume; marked[i] = f.full ? 1 : 0;
        if (f.full) { std::ostringstream text; text << std::setprecision(17) << f.full->volume.mantissa() << ' ' << f.full->volume.exponent().to_string(); full[i] = text.str(); }
    };
    for (std::size_t i = 0; i < count; ++i) job(i, expected, flags, full_values);
    Pool pool(12); pool.run(count / 256, [&](std::size_t b) { for (std::size_t i = b * 256; i < (b + 1) * 256; ++i) job(i, actual, parallel_flags, parallel_full); });
    check(expected == actual && flags == parallel_flags && full_values == parallel_full, "Face time integration depends on worker count");
    std::cout << "TTHREAD 32768 12 1\n";
}
void invalid() {
    unsigned rejected = 0;
    const auto rejects = [&](auto&& fn) { try { fn(); } catch (const std::exception&) { ++rejected; } };
    rejects([] { static_cast<void>(time_mortar_piece(21, 0, 0)); });
    rejects([] { static_cast<void>(time_mortar_piece(0, 0, 1)); });
    rejects([] { static_cast<void>(integrate_time_face(0, 1, 0, 0, 1, .1, 0)); });
    rejects([] { static_cast<void>(integrate_time_face(0, 1, 0, 1, 0, .1, 0)); });
    rejects([] { static_cast<void>(integrate_time_face(0, 1, 0, 1, 1, 0, 0)); });
    rejects([] { static_cast<void>(integrate_time_face(0, std::numeric_limits<double>::quiet_NaN(), 0, 1, 1, .1, 0)); });
    rejects([] { static_cast<void>(time_cell_first_moment(TimeBasisPiece{0, 0, 0}, 1., 1.)); });
    check(rejected == 7, "Malformed time mortar input was admitted");
    std::cout << "TINVALID 7\n";
}
}
int main() {
    try {
        require_mac_energy_environment(); std::cout << std::setprecision(17) << "TVERSION 1 " << std::sqrt(3.) << '\n';
        face(0, .125, .7, -.2, 2.3, .8, .1, 0); face(1, -.125, -.7, .2, 2.3, .8, .1, 3);
        face(2, .125, 0, .2, 2.3, .8, .1, 0); face(3, .125, -2.5, 0, 2.3, .8, .1, 0);
        face(4, 0x1p-700, 0, 0, 0x1p420, 1, .002, 0);
        face(5, 0x1p-700, 0, 0, 0x1p420, 0x1p500, .002, 0);
        face(6, 0, 0x1p1000, 0, 1, 0x1p1000, 0x1p-1000, 20);
        face(7, 0, 0x1p1000, 0, 1, 0x1p1000, 0x1p-1074, 20);
        face(8, 0, 0x1p-1074, 0, 1, 0x1p500, 0x1p500, 0);
        pair(0, 0, 0); pair(1, 0, 2); pair(2, 3, 0); pair(3, 2, 3);
        metadata(); threaded(); invalid(); std::cout << "PASS shared quadratic face paths and different local time bases\n"; return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
