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
    void u32(std::uint32_t v) { for (unsigned n = 0; n < 4; ++n) bytes.push_back(static_cast<std::uint8_t>(v >> (8 * n))); }
    void u64(std::uint64_t v) { for (unsigned n = 0; n < 8; ++n) bytes.push_back(static_cast<std::uint8_t>(v >> (8 * n))); }
    void f64(double v) { u64(std::bit_cast<std::uint64_t>(v)); }
    void scaled(const experimental::MacEnergy& v) {
        f64(v.mantissa());
        const auto exponent = v.exponent().to_string();
        u32(static_cast<std::uint32_t>(exponent.size()));
        bytes.insert(bytes.end(), exponent.begin(), exponent.end());
    }
    void header(std::uint64_t sequence, std::uint64_t frame, std::size_t count) {
        if (count > std::numeric_limits<std::uint32_t>::max()) throw std::length_error("Too many precise receipt parcels");
        constexpr char magic[] = "MARECP01"; bytes.insert(bytes.end(), magic, magic + 8);
        u64(sequence); u64(frame); u32(static_cast<std::uint32_t>(count));
    }
    void parcel(const experimental::MacFluidParcel& p, std::uint32_t native_index, std::uint32_t public_cell) {
        u32(native_index); u32(public_cell); u32(p.injected ? 1 : 0);
        f64(p.mass_kg); f64(p.temperature_k); f64(p.internal_energy_j);
        scaled(p.resolved_kinetic_energy); scaled(p.potential_energy); scaled(p.momentum_x); scaled(p.momentum_y);
        if (p.passives.size() > std::numeric_limits<std::uint32_t>::max()) throw std::length_error("Too many precise receipt passives");
        u32(static_cast<std::uint32_t>(p.passives.size()));
        for (const auto& passive : p.passives) { u64(passive.identity); f64(passive.quantity); }
    }
};
}
