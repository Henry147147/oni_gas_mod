// Byproduct batches (native/src/oni_byproduct_batches.hpp): release by mass
// and by owner time, compensated sums, mass-weighted temperature, summed germ
// identities, key order, closing cells, the census copy and the unbatched mode.
#include "oni_byproduct_batches.hpp"
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace {
using ma::PassiveQuantity;
using ma::oni::colony::detail::ByproductBatches;
void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
std::vector<MAOniColonyReceipt> due(ByproductBatches& b, double now) {
    std::vector<MAOniColonyReceipt> out; b.release_due(now, [&](const MAOniColonyReceipt& r) { out.push_back(r); }); return out;
}
void mass_threshold() {
    ByproductBatches b(1e-3, 10);
    MAOniColonyReceipt r{};
    require(!b.add(77, 123, 4e-4, 250, {}, .2, 1, r) && !b.add(77, 123, 4e-4, 260, {}, .4, 2, r), "a light batch was released");
    require(b.add(77, 123, 3e-4, 270, {}, .6, 3, r) && b.size() == 0, "a 1 g batch was held");
    const double mass = 4e-4 + 4e-4 + 3e-4, heat = 4e-4 * 250 + 4e-4 * 260 + 3e-4 * 270;
    require(r.sequence == 0 && r.kind == MA_ONI_COLONY_BYPRODUCT && r.callback == 123 && r.public_cell == 77, "receipt identity");
    require(r.mass_kg == static_cast<float>(mass) && std::abs(r.temperature_k - static_cast<float>(heat / mass)) <= 1e-4f, "batch mass or mass-weighted temperature");
    // A single heavy byproduct passes at once, joined with what its cell held.
    require(!b.add(77, 123, 2e-4, 280, {}, 1, 4, r) && b.add(77, 123, 5, 290, {}, 1.2, 5, r), "a heavy byproduct waited");
    require(r.mass_kg == static_cast<float>(5 + 2e-4) && b.size() == 0, "a heavy byproduct lost its cell's batch");
    std::puts("PASS byproduct batch: released at 1 g with its mass-weighted temperature; heavy byproducts at once");
}
void time_window() {
    ByproductBatches b(1e-3, 10);
    MAOniColonyReceipt r{};
    // Two cells and two elements (one hash negative): key order is cell, then element bits.
    require(!b.add(9, -5, 1e-6, 200, {}, 1, 5, r) && !b.add(9, 4, 2e-6, 210, {}, 3, 15, r) && !b.add(8, 4, 3e-6, 220, {}, 2, 10, r), "trace batches");
    require(due(b, 10.9).empty(), "a batch left before its window");
    const auto first = due(b, 12);
    require(first.size() == 2 && first[0].public_cell == 8 && first[1].public_cell == 9 && first[1].callback == -5 && first[1].mass_kg == 1e-6f, "window or key order");
    const auto second = due(b, 13);
    require(second.size() == 1 && second[0].callback == 4 && second[0].public_cell == 9 && b.size() == 0, "the later batch");
    std::puts("PASS byproduct batch: a light batch leaves after 10 s of owner time, in cell and element order");
}
void compensated() {
    ByproductBatches b(1e12, 1e9);
    MAOniColonyReceipt r{};
    // 2^30 kg then 2^20 parts of 2^-24 kg, each below half an ulp of the sum.
    require(!b.add(1, 2, std::ldexp(1., 30), 300, {}, 0, 1, r), "large part");
    for (int k = 0; k < 1 << 20; ++k) (void)b.add(1, 2, std::ldexp(1., -24), 300, {}, 0, 1, r);
    const auto [mass, heat] = b.totals();
    require(mass == std::ldexp(1., 30) + std::ldexp(1., -4) && heat == 300 * mass, "compensated sums lost small parts");
    std::puts("PASS byproduct batch: compensated mass and heat keep every part (2^30 + 2^20 x 2^-24 kg exact)");
}
void germs() {
    ByproductBatches b(1e-3, 10);
    MAOniColonyReceipt r{};
    const std::vector<PassiveQuantity> a{{3, 400}, {5, 200}}, c{{3, 350}, {5, 400}, {0x100000000ULL, 9e9}}, d{{7, -1}};
    require(!b.add(4, 6, 1e-5, 260, a, 0, 1, r) && !b.add(4, 6, 1e-5, 260, c, 0, 2, r) && !b.add(4, 6, 1e-5, 260, d, 0, 3, r), "germ batch");
    require(b.add(4, 6, 1e-3, 260, {}, 0, 4, r) && r.disease == 3 && r.disease_count == 750, "summed germ identities or most numerous germ");
    std::puts("PASS byproduct batch: germ identities summed; the item gets the most numerous (750 of identity 3)");
}
void cells_census_and_modes() {
    ByproductBatches b(1e-3, 10);
    MAOniColonyReceipt r{};
    (void)b.add(7, 1, 1e-6, 250, {}, 0, 1, r); (void)b.add(7, 2, 2e-6, 250, {}, 0, 2, r); (void)b.add(6, 1, 3e-6, 250, {}, 0, 3, r);
    (void)b.add(8, 1, 4e-6, 250, {}, 0, 4, r);
    MAOniColonyReceipt rows[4]{};
    require(b.copy(rows, 2) == 4 && rows[0].public_cell == 6 && rows[0].completed_frame == 3 && rows[1].public_cell == 7 && rows[1].callback == 1, "census order or truncation");
    require(b.copy(nullptr, 0) == 4 && b.size() == 4, "a census changed the batches");
    std::vector<MAOniColonyReceipt> closed; b.release_cell(7, [&](const MAOniColonyReceipt& x) { closed.push_back(x); });
    require(closed.size() == 2 && closed[0].callback == 1 && closed[1].callback == 2 && b.size() == 2, "closing cell release");
    std::vector<MAOniColonyReceipt> all; b.release_all([&](const MAOniColonyReceipt& x) { all.push_back(x); });
    require(all.size() == 2 && all[0].public_cell == 6 && all[1].public_cell == 8 && b.size() == 0, "release all");
    ByproductBatches unbatched(0, 10);
    require(unbatched.add(5, 1, 1e-9, 250, {}, 0, 1, r) && r.mass_kg == 1e-9f && unbatched.size() == 0, "0 kg must release every byproduct at once");
    std::puts("PASS byproduct batch: census copy, closing-cell and full release, and the unbatched 0 kg mode");
}
}

int main() {
    try { mass_threshold(); time_window(); compensated(); germs(); cells_census_and_modes(); }
    catch (const std::exception& error) { std::fprintf(stderr, "FAIL %s\n", error.what()); return 1; }
    return 0;
}
