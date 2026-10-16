#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace ma {
// Disease hashes occupy the low 32 bits (nonzero, including signed hash bits).
// Radiation is the existing passive native metadata quantity, never energy/dose.
inline constexpr std::uint64_t radiation_passive_identity = 0x100000000ULL;
struct PassiveQuantity { std::uint64_t identity = 0; double quantity = 0; };
using PassiveQuantities = std::vector<PassiveQuantity>;
void validate_passive_quantities(std::span<const PassiveQuantity> values);
[[nodiscard]] PassiveQuantities scale_passive_quantities(std::span<const PassiveQuantity> values, double fraction);
void add_passive_quantities(PassiveQuantities& target, std::span<const PassiveQuantity> source, double fraction);
}
