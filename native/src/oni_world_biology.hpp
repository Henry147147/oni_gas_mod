#pragma once
// World germ evolution of the colony owner (docs/colony-owner.md#germs).
// Every disease identity in every owned cell follows the game's world rules
// once per 0.2 s native frame: diffusion towards neighbouring cells of the
// same phase, then growth or death by medium and temperature. The rules are
// the game's own disease table (Db diseases, handed over at start/restore);
// nothing from the game is compiled in.
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <utility>
#include <vector>

namespace ma::oni::colony::biology {
// One disease's temperature rules: four temperature bounds (no growth below
// the first and above the last; full viability between the middle two) and
// the half-life at each bound.
struct Disease {
    float strength = 0;
    std::array<float, 4> range{}, half_lives{};
};
// One disease in one element.
struct Growth {
    float death_rate = 0, population_half_life = 0, over_population_half_life = 0, diffusion_scale = 0;
    float minimum_per_kg = 0, maximum_per_kg = 0;
    std::int32_t minimum_diffusion_count = 0;
    std::uint8_t minimum_diffusion_ticks = 0;
};
struct Table {
    std::vector<Disease> diseases;
    std::size_t elements = 0;
    std::vector<Growth> growth; // [disease * elements + element]
    [[nodiscard]] bool empty() const noexcept { return diseases.empty(); }
    [[nodiscard]] const Growth& at(std::size_t disease, std::size_t element) const;
};
// Rejects inconsistent sizes, invalid rates and diffusion scales above 2
// (which would let a cell send more germs than it holds). The game's exact
// all-positive-infinity temperature rule is an explicit no-effect sentinel.
void validate(const Table&);

inline constexpr float frame_seconds = .2f;
// Half-life (s) of a disease at a temperature: infinite between the growth
// bounds, the bound's own value outside the outer bounds, linear between.
[[nodiscard]] float temperature_half_life(const Disease&, float temperature_k);
// Population multiplier over one 0.2 s frame for a half-life (0 kills, an
// infinite half-life keeps the population).
[[nodiscard]] float frame_rate(float half_life_s);

// A culture's saved clock. Integer cultures follow the game's binary32
// accounting with its fractional residue; a culture whose count is not an
// integer (flow splits counts by mass) follows the expected population, and
// stays there until it dies out. Ticks count the frames the identity has
// been in the cell (the game's infestation clock), up to 254.
inline constexpr std::uint8_t native_mode = 0, expected_mode = 1, maximum_ticks = 254;
struct Clock { std::uint8_t mode = native_mode, ticks = 0; float residue = 0; };

// What a cell offers its germs. State is the game's phase of the cell
// (0 none, 1 gas, 2 liquid); element is the projected element the game shows.
struct Part { std::uint32_t element = 0; double mass_kg = 0; };
struct Medium {
    bool open = false;
    std::uint8_t state = 0;
    std::uint32_t element = 0;
    float temperature_k = 0;
    double mass_kg = 0;
    std::vector<Part> parts;
};
struct Culture { std::uint64_t identity = 0; double quantity = 0; };
// Disease identities are the game's disease index + 1 (1..256).
[[nodiscard]] inline bool disease_identity(std::uint64_t identity) noexcept { return identity >= 1 && identity <= 256; }

struct Step { double population = 0, change = 0; Clock clock{}; };
// Growth or death of one culture over one frame, before the sub-germ rule.
[[nodiscard]] Step grow(const Table&, std::size_t disease, const Medium&, double population, Clock);
// Germs moved into cell a from its neighbour b (negative: out of a) in one
// frame, given the source's diffusion scale.
[[nodiscard]] double diffused(double count_a, double count_b, float scale);
// The source cell's diffusion scale for its identity: zero below the
// element's minimum count or before its minimum ticks.
[[nodiscard]] float diffusion_scale(const Table&, std::size_t disease, std::uint32_t element, double count, std::uint8_t ticks);

using Cells = std::map<std::size_t, std::vector<Culture>>; // cell -> cultures sorted by identity
using Clocks = std::map<std::pair<std::size_t, std::uint64_t>, Clock>;
struct Account { long double born = 0, died = 0, culled = 0, moved = 0, edited_in = 0, edited_out = 0; };
using Accounts = std::map<std::uint64_t, Account>;
struct Edit { std::size_t cell = 0; std::uint64_t identity = 0; double change = 0; };

// Applies a passive edit to the local copy exactly as the owner's world does
// (MacClosedWorld::with_passive_edits): a cell without mass takes no edit, an
// absent identity only a positive one, and a culture at or below zero leaves.
// Returns false when the edit was not applied.
bool apply(Cells&, const Edit&, bool cell_has_mass);

// One 0.2 s frame over the owned grid (width x height, row-major from the
// bottom): per-identity diffusion between open neighbours of the same state,
// computed from the frame's starting counts, then growth/death of every
// disease culture; a culture left below one germ dies (the game keeps whole
// germs). Appends the edits in the order they must be applied and updates
// cultures, clocks and accounts to match.
void frame(const Table&, std::size_t width, std::size_t height, const std::function<const Medium&(std::size_t)>& medium,
    Cells&, Clocks&, Accounts&, std::vector<Edit>&);
}
