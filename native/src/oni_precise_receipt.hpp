#pragma once
// Optional adapter receipt payload. This is not a solver checkpoint and never
// projects canonical energy or momentum into the binary64 value range.
#include "mixed_atmospheres/mac_closed_world.hpp"
#include <bit>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace ma::oni::colony::detail {
struct PreciseReceiptWriter {
    std::vector<std::uint8_t> bytes;
    bool observations=false;
    void u32(std::uint32_t v) { for (unsigned n = 0; n < 4; ++n) bytes.push_back(static_cast<std::uint8_t>(v >> (8 * n))); }
    void u64(std::uint64_t v) { for (unsigned n = 0; n < 8; ++n) bytes.push_back(static_cast<std::uint8_t>(v >> (8 * n))); }
    void f64(double v) { u64(std::bit_cast<std::uint64_t>(v)); }
    void scaled(const experimental::MacEnergy& v) {
        f64(v.mantissa());
        const auto exponent = v.exponent().to_string();
        u32(static_cast<std::uint32_t>(exponent.size()));
        bytes.insert(bytes.end(), exponent.begin(), exponent.end());
    }
    void header(std::uint64_t sequence, std::uint64_t frame, std::size_t count, bool observe=false) {
        if (count > std::numeric_limits<std::uint32_t>::max()) throw std::length_error("Too many precise receipt parcels");
        observations=observe; const char* magic=observe?"MARECP02":"MARECP01"; bytes.insert(bytes.end(), magic, magic + 8);
        u64(sequence); u64(frame); u32(static_cast<std::uint32_t>(count));
    }
    void parcel(const experimental::MacFluidParcel& p, std::uint32_t native_index, std::uint32_t public_cell) {
        parcel(p,native_index,public_cell,p.passives);
    }
    void parcel(const experimental::MacFluidParcel& p, std::uint32_t native_index, std::uint32_t public_cell,
        std::span<const ma::PassiveQuantity> carried) {
        u32(native_index); u32(public_cell); u32(p.injected ? 1 : 0);
        f64(p.mass_kg); f64(p.temperature_k); f64(p.internal_energy_j);
        scaled(p.resolved_kinetic_energy); scaled(p.potential_energy); scaled(p.momentum_x); scaled(p.momentum_y);
        if (carried.size() > std::numeric_limits<std::uint32_t>::max()) throw std::length_error("Too many precise receipt passives");
        u32(static_cast<std::uint32_t>(carried.size()));
        for (const auto& passive : carried) { u64(passive.identity); f64(passive.quantity); }
        if (observations) {
            const auto count_at=bytes.size(); u32(0); std::uint32_t count=0; std::size_t i=0,j=0;
            while (i<p.passives_before.size() || j<p.passives_after.size()) {
                const auto identity=j==p.passives_after.size() || (i<p.passives_before.size() && p.passives_before[i].identity<p.passives_after[j].identity)?
                    p.passives_before[i].identity:p.passives_after[j].identity;
                double before=0,after=0;
                if (i<p.passives_before.size() && p.passives_before[i].identity==identity) before=p.passives_before[i++].quantity;
                if (j<p.passives_after.size() && p.passives_after[j].identity==identity) after=p.passives_after[j++].quantity;
                u64(identity); f64(before); f64(after); ++count;
            }
            for (unsigned n=0;n<4;++n) bytes[count_at+n]=static_cast<std::uint8_t>(count>>(8*n));
        }
    }
};
}
