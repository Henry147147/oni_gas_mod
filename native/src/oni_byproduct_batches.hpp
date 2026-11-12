#pragma once
// Byproduct batches (docs/colony-owner.md#byproduct-batches; user decision
// 2026-11-05). The solid byproducts of one cell and element (a hidden liquid
// freezing, salt from boiling brine) have already left the owner, booked as
// extracted. They gather here until they weigh `kg` or have waited `seconds`
// of owner time; then one receipt makes one debris item. Sums are compensated
// (mass and mass x temperature) and germ identities are summed, so the item
// gets the batch's mass-weighted temperature (ONI's own merge of two items of
// one element) and its most numerous germ. Delivery state only: nothing here
// is saved, and the owner's world and ledgers never read it.
#include "mixed_atmospheres/oni_colony.h"
#include "mixed_atmospheres/passive_quantities.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <span>
#include <utility>

namespace ma::oni::colony::detail {
// The most numerous germ among passives (plain identity <= 256 is a disease
// index + 1; a germ's gas-carried and liquid-carried counts add), and its
// count as ONI holds it. The lower identity wins a tie.
inline std::pair<std::uint32_t, std::int32_t> dominant_germ(std::span<const PassiveQuantity> passives) {
    std::uint32_t best = 0; double most = 0;
    for (std::size_t a = 0; a < passives.size(); ++a) {
        const auto identity = plain_passive_identity(passives[a].identity);
        if (identity > 256) continue;
        double count = passives[a].quantity; bool counted = false;
        for (std::size_t b = 0; b < passives.size() && !counted; ++b)
            if (b != a && plain_passive_identity(passives[b].identity) == identity) { if (b < a) counted = true; else count += passives[b].quantity; }
        if (!counted && (count > most || (count == most && best != 0 && identity < best))) { most = count; best = static_cast<std::uint32_t>(identity); }
    }
    if (best == 0 || !(most >= .5)) return {0, 0};
    return {best, static_cast<std::int32_t>(std::min(most, 2147483647.))};
}

class ByproductBatches {
public:
    ByproductBatches(double kg, double seconds) : kg_(kg), seconds_(seconds) {}
    double kilograms() const noexcept { return kg_; }
    double seconds() const noexcept { return seconds_; }
    std::size_t size() const noexcept { return batches_.size(); }
    void clear() noexcept { batches_.clear(); }
    // Adds one byproduct (mass > 0 kg at temperature K, carrying these germs)
    // made in owner frame `frame` at owner time now_s. Returns true and the
    // batch's receipt (sequence 0) when the batch is released.
    bool add(std::uint32_t public_cell, std::int32_t element, double mass, double temperature,
             std::span<const PassiveQuantity> germs, double now_s, std::uint64_t frame, MAOniColonyReceipt& released) {
        const auto key = static_cast<std::uint64_t>(public_cell) << 32 | static_cast<std::uint32_t>(element);
        auto [found, fresh] = batches_.try_emplace(key);
        auto& b = found->second;
        if (fresh) b.since_s = now_s;
        b.frame = frame;
        add_compensated(b.mass, b.mass_carry, mass); add_compensated(b.heat, b.heat_carry, mass * temperature);
        for (const auto& q : germs) {
            if (q.identity == 0 || q.identity > 256 || !(q.quantity > 0)) continue;
            const auto at = std::lower_bound(b.germs.begin(), b.germs.end(), q.identity,
                [](const PassiveQuantity& a, std::uint64_t id) { return a.identity < id; });
            if (at != b.germs.end() && at->identity == q.identity) at->quantity += q.quantity; else b.germs.insert(at, q);
        }
        if (!(b.mass + b.mass_carry >= kg_)) return false;
        released = receipt(key, b); batches_.erase(found);
        return true;
    }
    // Releases, in key order (cell, then element), every batch that has
    // waited `seconds` by now_s.
    template <class Emit> void release_due(double now_s, Emit&& emit) {
        for (auto b = batches_.begin(); b != batches_.end();)
            if (now_s - b->second.since_s >= seconds_) { emit(receipt(b->first, b->second)); b = batches_.erase(b); }
            else ++b;
    }
    // Releases every batch of one public cell (it is closing).
    template <class Emit> void release_cell(std::uint32_t public_cell, Emit&& emit) {
        for (auto b = batches_.lower_bound(static_cast<std::uint64_t>(public_cell) << 32); b != batches_.end() && b->first >> 32 == public_cell;) {
            emit(receipt(b->first, b->second)); b = batches_.erase(b);
        }
    }
    template <class Emit> void release_all(Emit&& emit) {
        for (const auto& [key, batch] : batches_) emit(receipt(key, batch));
        batches_.clear();
    }
    // The pending batches as receipts (sequence 0, completed_frame the frame
    // of their latest byproduct), in key order; returns how many are pending.
    std::uint32_t copy(MAOniColonyReceipt* out, std::uint32_t capacity) const {
        std::uint32_t pending = 0;
        for (const auto& [key, batch] : batches_) {
            if (out && pending < capacity) { out[pending] = receipt(key, batch); out[pending].completed_frame = batch.frame; }
            ++pending;
        }
        return pending;
    }
    // The pending mass (kg) and mass x temperature (kg K), compensated.
    std::pair<double, double> totals() const noexcept {
        double mass = 0, mass_carry = 0, heat = 0, heat_carry = 0;
        for (const auto& [key, b] : batches_) {
            add_compensated(mass, mass_carry, b.mass); add_compensated(mass, mass_carry, b.mass_carry);
            add_compensated(heat, heat_carry, b.heat); add_compensated(heat, heat_carry, b.heat_carry);
        }
        return {mass + mass_carry, heat + heat_carry};
    }
private:
    struct Batch {
        double mass = 0, mass_carry = 0, heat = 0, heat_carry = 0, since_s = 0;
        std::uint64_t frame = 0;  // the owner frame of its latest byproduct
        PassiveQuantities germs;  // disease identities, ascending
    };
    static void add_compensated(double& sum, double& carry, double x) noexcept {
        const double t = sum + x;
        carry += std::abs(sum) >= std::abs(x) ? (sum - t) + x : (x - t) + sum;
        sum = t;
    }
    static MAOniColonyReceipt receipt(std::uint64_t key, const Batch& b) noexcept {
        MAOniColonyReceipt r{};
        r.callback = static_cast<std::int32_t>(static_cast<std::uint32_t>(key)); r.kind = MA_ONI_COLONY_BYPRODUCT;
        r.public_cell = static_cast<std::uint32_t>(key >> 32);
        const double mass = b.mass + b.mass_carry;
        r.mass_kg = static_cast<float>(mass); r.temperature_k = static_cast<float>((b.heat + b.heat_carry) / mass);
        const auto germ = dominant_germ(b.germs); r.disease = germ.first; r.disease_count = germ.second;
        return r;
    }
    double kg_, seconds_;
    std::map<std::uint64_t, Batch> batches_; // key: public cell << 32 | element hash
};
}
