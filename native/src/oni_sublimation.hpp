#pragma once
// Natural sublimation and off-gassing (docs/colony-owner.md#natural-sublimation).
// The pure rules here describe what ONI's native simulation does each 0.2 s
// frame; the colony owner applies them to owned cells and books the products.
#include <algorithm>
#include <cstdint>

namespace ma::oni::sublimation {

inline constexpr std::uint16_t no_element = 0xffff;
// A cell holding this much gas (kg) or more takes no more.
inline constexpr float pressure_cap_kg = 1.8f;
// A liquid cell that would be left with less than this converts whole.
inline constexpr float film_kg = .01f;
// The most one off-gassing event takes from a liquid cell.
inline constexpr float offgas_limit_kg = 1.f;

// ONI's per-element rule, indexed by native element. A solid sublimates up to
// rate x 0.2 s of itself per frame and face; a liquid gives off `offgas` of its
// cell's mass. `efficiency` of the removed mass becomes `target`; the rest is
// lost, as in ONI. Each event happens with `probability` per frame.
enum class Kind : std::uint8_t { none, solid, liquid };
struct Rule {
    Kind kind = Kind::none;
    std::uint16_t target = no_element;
    float rate = 0, efficiency = 0, probability = 0, offgas = 0;
};

// ONI's generator, the C runtime's linear congruential one: advances the seed
// and returns a value in [0, 1].
inline float draw(std::uint32_t& seed) noexcept {
    seed = seed * 214013u + 2531011u;
    return static_cast<float>((seed >> 16) & 0x7fffu) * (1.f / 32767.f);
}

// One sublimating neighbour before and after ONI's own step: the mass it lost,
// the gas that mass made and the germs it carried. A cell that now holds
// something else was used up and converted in place; vacuum means ONI removed
// the last trace after a take.
struct Yield { float removed = 0; double gas = 0; std::int32_t germs = 0; bool converted = false; };
inline Yield yield(const Rule& rule, std::uint16_t vacuum,
                   std::uint16_t element, float mass, std::uint8_t disease, std::int32_t germs,
                   std::uint16_t element_after, float mass_after, std::uint8_t disease_after, std::int32_t germs_after) noexcept {
    Yield out;
    if (element_after != element && element_after != vacuum) { out.converted = true; return out; }
    out.removed = element_after == vacuum ? mass : mass - mass_after;
    if (!(out.removed > 0)) { out.removed = 0; return out; }
    out.gas = static_cast<double>(rule.efficiency) * out.removed;
    if (disease != 255 && germs > 0) {
        const auto left = element_after == element && disease_after == disease ? std::max(germs_after, 0) : 0;
        out.germs = std::max(germs - left, 0);
    }
    return out;
}

// A liquid cell's off-gassing once its probability has passed. cell_kg: the
// liquid cell's mass; below_kg: the cell below's when it holds the same liquid
// (else 0); above_kg: the gas in the cell above. A film is converted in place
// (from_below more is taken from the cell below into it); otherwise `amount`
// moves to the cell above as `produced`, held to the pressure cap there.
struct Offgas {
    enum class Kind : std::uint8_t { none, surface, film } kind = Kind::none;
    float amount = 0, produced = 0, from_below = 0, produced_below = 0;
};
inline Offgas offgas(const Rule& rule, float cell_kg, float below_kg, float above_kg) noexcept {
    Offgas out;
    float amount = std::min(rule.offgas * cell_kg, offgas_limit_kg);
    const bool deeper = below_kg > cell_kg;
    if (deeper) amount = std::min(rule.offgas * below_kg, offgas_limit_kg);
    if (cell_kg - amount < film_kg) {
        out.kind = Offgas::Kind::film; out.amount = cell_kg; out.produced = rule.efficiency * cell_kg;
        const float rest = amount - cell_kg;
        if (deeper && rest > 0) { out.from_below = rest; out.produced_below = rule.efficiency * rest; }
        return out;
    }
    if (!(amount > 0)) return out;
    const float made = rule.efficiency * amount, room = pressure_cap_kg - above_kg;
    if (made > room) amount *= room / made;
    out.kind = Offgas::Kind::surface; out.amount = amount; out.produced = rule.efficiency * amount;
    return out;
}

} // namespace ma::oni::sublimation
