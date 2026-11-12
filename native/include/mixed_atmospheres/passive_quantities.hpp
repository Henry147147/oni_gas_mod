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
// Game profile 12 carries each passive with one phase of its cell, the gas or
// the liquid (MAC_GAME_KERNEL.md "Carried passives"): the top two bits of the
// identity name the carrier. An identity without one is plain and moves with
// the whole mass, as on every other profile.
inline constexpr std::uint64_t passive_gas_carrier = std::uint64_t{1} << 62, passive_liquid_carrier = std::uint64_t{1} << 63,
                               passive_carrier_mask = passive_gas_carrier | passive_liquid_carrier;
[[nodiscard]] constexpr std::uint64_t plain_passive_identity(std::uint64_t identity) noexcept { return identity & ~passive_carrier_mask; }
// The quantities by plain identity (carriers summed), sorted.
[[nodiscard]] PassiveQuantities plain_passive_quantities(std::span<const PassiveQuantity> values);
// The plain quantities, all given to one carrier.
[[nodiscard]] PassiveQuantities carried_passive_quantities(std::span<const PassiveQuantity> values, std::uint64_t carrier);
void validate_passive_quantities(std::span<const PassiveQuantity> values);
[[nodiscard]] PassiveQuantities scale_passive_quantities(std::span<const PassiveQuantity> values, double fraction);
void add_passive_quantities(PassiveQuantities& target, std::span<const PassiveQuantity> source, double fraction);
}
