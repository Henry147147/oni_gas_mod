#include "oni_world_biology.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace ma::oni::colony::biology {
namespace {
void require(bool ok, const char* why) { if (!ok) throw std::invalid_argument(why); }
constexpr float largest = std::numeric_limits<float>::max();
constexpr double int_limit = 2147483647.0;
bool whole(double value) noexcept { return value >= 0 && value <= int_limit && value == std::floor(value); }
// The material part of one frame's change for a whole-germ culture in one
// element, in binary32 as the game computes it: proportional growth or decay
// inside and above the element's density window, an absolute death rate
// below it.
float whole_material(const Growth& g, float count, float mass) {
    const float minimum = g.minimum_per_kg * mass, maximum = mass * g.maximum_per_kg;
    if (maximum >= count && count >= minimum) return (frame_rate(g.population_half_life) - 1.0f) * count;
    if (minimum > count) return -(frame_seconds * g.death_rate);
    return (frame_rate(g.over_population_half_life) - 1.0f) * count;
}
// The same rule for an expected (non-integer) population, in binary64.
double expected_material(const Growth& g, double population, float mass) {
    const float minimum = g.minimum_per_kg * mass, maximum = mass * g.maximum_per_kg;
    if (static_cast<double>(maximum) >= population && population >= static_cast<double>(minimum))
        return population * static_cast<double>(frame_rate(g.population_half_life) - 1.0f);
    if (static_cast<double>(minimum) > population) return -static_cast<double>(frame_seconds * g.death_rate);
    return population * static_cast<double>(frame_rate(g.over_population_half_life) - 1.0f);
}
double count_of(const std::vector<Culture>& list, std::uint64_t identity) noexcept {
    const auto it = std::lower_bound(list.begin(), list.end(), identity, [](const Culture& c, std::uint64_t id) { return c.identity < id; });
    return it != list.end() && it->identity == identity ? it->quantity : 0;
}
}

const Growth& Table::at(std::size_t disease, std::size_t element) const {
    require(disease < diseases.size() && element < elements, "Unknown disease or element for world germs");
    return growth[disease * elements + element];
}

void validate(const Table& t) {
    require(t.diseases.size() <= 256 && t.elements > 0 && t.elements <= 65536 && t.growth.size() == t.diseases.size() * t.elements,
        "Invalid world disease table size");
    const auto finite = [](float v) { return !std::isnan(v); };
    for (const auto& d : t.diseases) {
        require(std::isfinite(d.strength), "Invalid disease strength");
        // The game's idempotent temperature rule uses +infinity for all
        // eight fields (for example, a disease with no environmental effect).
        // Its multiplier is exactly one; mixed/noncanonical infinite bounds
        // still fail the ordinary finite-range validation below.
        const auto positive_infinity = [](float v) { return v == std::numeric_limits<float>::infinity(); };
        if (std::all_of(d.range.begin(), d.range.end(), positive_infinity) &&
            std::all_of(d.half_lives.begin(), d.half_lives.end(), positive_infinity)) continue;
        for (int k = 0; k < 4; ++k) require(std::isfinite(d.range[k]) && finite(d.half_lives[k]) && d.half_lives[k] >= 0, "Invalid disease temperature rules");
    }
    for (const auto& g : t.growth)
        require(std::isfinite(g.death_rate) && g.death_rate >= 0 && finite(g.population_half_life) && finite(g.over_population_half_life) &&
            std::isfinite(g.diffusion_scale) && g.diffusion_scale >= 0 && g.diffusion_scale <= 2 &&
            finite(g.minimum_per_kg) && finite(g.maximum_per_kg) && g.minimum_diffusion_count >= 0, "Invalid disease growth rules");
}

float temperature_half_life(const Disease& d, float t) {
    const auto& r = d.range; const auto& h = d.half_lives;
    std::size_t low = 3, high = 3;
    if (!(t > r[0])) low = high = 0;
    else if (!(t > r[1])) { low = 0; high = 1; }
    else if (!(t > r[2])) return std::numeric_limits<float>::infinity(); // growth window: no temperature effect
    else if (!(t > r[3])) { low = 2; high = 3; }
    const float a = h[low], b = h[high];
    if (a > largest || b > largest) return std::numeric_limits<float>::infinity();
    const float span = r[high] - r[low];
    if (!(span > 0)) return a;
    const float along = (t - r[low]) / span;
    const float rest = 1.0f - along;
    const float from_low = a * rest, from_high = along * b;
    return from_low + from_high;
}

float frame_rate(float half_life) {
    if (half_life == 0 || std::isnan(half_life)) return half_life;
    if (half_life > largest) return 1.0f;
    return std::exp2(-frame_seconds / half_life);
}

Step grow(const Table& table, std::size_t disease, const Medium& m, double population, Clock clock) {
    require(population > 0 && std::isfinite(population) && m.mass_kg > 0 && !m.parts.empty(), "Invalid world germ culture");
    const float temperature_rate = frame_rate(temperature_half_life(table.diseases.at(disease), m.temperature_k));
    const float mass = static_cast<float>(m.mass_kg);
    if (clock.mode == native_mode && population >= 1 && whole(population)) {
        const auto n = static_cast<std::int32_t>(population);
        const float count = static_cast<float>(n);
        float total = (clock.residue - count) + temperature_rate * count;
        float material;
        if (m.parts.size() == 1) material = whole_material(table.at(disease, m.parts[0].element), count, mass);
        else {
            double blend = 0;
            for (const auto& p : m.parts) blend += (p.mass_kg / m.mass_kg) * static_cast<double>(whole_material(table.at(disease, p.element), count, mass));
            material = static_cast<float>(blend);
        }
        total = total + material;
        if (std::isfinite(total) && total >= -2147483648.0f && total < 2147483648.0f) {
            const auto change = static_cast<std::int32_t>(total);
            const float residue = total - static_cast<float>(change);
            const auto next = static_cast<std::int64_t>(n) + change;
            if (next <= 0) return {0, -population, Clock{native_mode, clock.ticks, 0}};
            if (next <= static_cast<std::int64_t>(int_limit)) return {static_cast<double>(next), static_cast<double>(change), Clock{native_mode, clock.ticks, residue}};
        }
        // Outside the game's integer range: continue as an expected population.
    }
    double material = 0;
    for (const auto& p : m.parts) material += (p.mass_kg / m.mass_kg) * expected_material(table.at(disease, p.element), population, mass);
    double change = material + population * (static_cast<double>(temperature_rate) - 1.0) + static_cast<double>(clock.residue);
    if (change < 0 && population + change <= 0) change = -population;
    return {population + change, change, Clock{expected_mode, clock.ticks, 0}};
}

double diffused(double a, double b, float scale) {
    const double difference = b - a;
    if (whole(a) && whole(b)) {
        const float moved = (static_cast<float>(static_cast<std::int32_t>(difference)) * .125f) * scale;
        if (moved > -2147483648.0f && moved < 2147483648.0f) return static_cast<double>(static_cast<std::int32_t>(moved));
    }
    return std::trunc(difference * .125 * static_cast<double>(scale));
}

float diffusion_scale(const Table& table, std::size_t disease, std::uint32_t element, double count, std::uint8_t ticks) {
    const auto& g = table.at(disease, element);
    if (static_cast<double>(g.minimum_diffusion_count) > count || g.minimum_diffusion_ticks > ticks) return 0;
    return g.diffusion_scale;
}

bool apply(Cells& cells, const Edit& e, bool cell_has_mass) {
    if (!cell_has_mass || e.change == 0 || !std::isfinite(e.change)) return false;
    auto found = cells.find(e.cell);
    if (found == cells.end()) {
        if (!(e.change > 0)) return false;
        cells.emplace(e.cell, std::vector<Culture>{{e.identity, e.change}});
        return true;
    }
    auto& list = found->second;
    const auto it = std::lower_bound(list.begin(), list.end(), e.identity, [](const Culture& c, std::uint64_t id) { return c.identity < id; });
    if (it == list.end() || it->identity != e.identity) {
        if (!(e.change > 0)) return false;
        list.insert(it, Culture{e.identity, e.change});
        return true;
    }
    it->quantity += e.change;
    if (!(it->quantity > 0)) { list.erase(it); if (list.empty()) cells.erase(found); }
    return true;
}

void frame(const Table& table, std::size_t width, std::size_t height, const std::function<const Medium&(std::size_t)>& medium,
    Cells& cells, Clocks& clocks, Accounts& accounts, std::vector<Edit>& edits) {
    require(width > 0 && height > 0, "Invalid world germ grid");
    const auto ticks_of = [&](std::size_t cell, std::uint64_t identity) -> std::uint8_t {
        const auto it = clocks.find({cell, identity}); return it == clocks.end() ? std::uint8_t{0} : it->second.ticks; };
    const auto holds_fluid = [](const Medium& m) { return m.open && m.state != 0 && m.mass_kg > 0; };
    // Diffusion from the frame's starting counts: each pair of neighbours of
    // the same state exchanges an eighth of its difference, scaled by the
    // richer cell's element. Counted once per pair (from the lower cell when
    // both hold the identity).
    std::map<std::pair<std::size_t, std::uint64_t>, double> moved;
    for (const auto& [i, list] : cells) {
        const auto& here = medium(i);
        if (!holds_fluid(here)) continue;
        const auto x = i % width, y = i / width;
        const std::array<std::size_t, 4> around{x + 1 < width ? i + 1 : i, y + 1 < height ? i + width : i, x > 0 ? i - 1 : i, y > 0 ? i - width : i};
        for (const auto& c : list) {
            if (!disease_identity(c.identity) || c.identity > table.diseases.size() || !(c.quantity > 0)) continue;
            for (const auto n : around) {
                if (n == i) continue;
                const auto& there = medium(n);
                if (!holds_fluid(there) || there.state != here.state) continue;
                const auto other = cells.find(n);
                const double held = other == cells.end() ? 0 : count_of(other->second, c.identity);
                if (held > 0 && n < i) continue;
                const auto a = std::min(i, n), b = std::max(i, n);
                const double count_a = a == i ? c.quantity : held, count_b = b == i ? c.quantity : held;
                if (count_a == count_b) continue;
                const auto source = count_b > count_a ? b : a;
                const float scale = diffusion_scale(table, c.identity - 1, medium(source).element, std::max(count_a, count_b), ticks_of(source, c.identity));
                if (!(scale > 0)) continue;
                const double into_a = diffused(count_a, count_b, scale);
                if (into_a == 0) continue;
                moved[{a, c.identity}] += into_a; moved[{b, c.identity}] -= into_a;
                accounts[c.identity].moved += std::abs(into_a);
            }
        }
    }
    for (const auto& [key, change] : moved) {
        if (change == 0) continue;
        const Edit e{key.first, key.second, change};
        if (apply(cells, e, true)) edits.push_back(e);
    }
    // Growth and death of every culture after diffusion.
    Clocks next;
    std::vector<std::size_t> visited; visited.reserve(cells.size());
    for (const auto& entry : cells) visited.push_back(entry.first);
    for (const auto i : visited) {
        const auto found = cells.find(i);
        if (found == cells.end()) continue;
        const auto list = found->second; // edits below may change the cell's list
        const auto& here = medium(i);
        for (const auto& c : list) {
            // An identity the game's table does not know (none in the base game) is kept unchanged.
            if (!disease_identity(c.identity) || c.identity > table.diseases.size()) continue;
            const auto key = std::make_pair(i, c.identity);
            const auto clock_it = clocks.find(key);
            Clock clock = clock_it != clocks.end() ? clock_it->second : Clock{whole(c.quantity) ? native_mode : expected_mode, 0, 0};
            if (!(here.open && here.mass_kg > 0) || here.parts.empty()) { next[key] = clock; continue; }
            const auto step = grow(table, c.identity - 1, here, c.quantity, clock);
            double after = c.quantity;
            auto& account = accounts[c.identity];
            if (step.change != 0) {
                const Edit e{i, c.identity, step.change};
                if (apply(cells, e, true)) {
                    edits.push_back(e); after = c.quantity + step.change;
                    if (step.change > 0) account.born += step.change; else account.died -= step.change;
                }
            }
            if (after > 0 && after < 1) {
                const Edit e{i, c.identity, -after};
                if (apply(cells, e, true)) { edits.push_back(e); account.culled += after; after = 0; }
            }
            if (after > 0) next[key] = Clock{step.clock.mode, static_cast<std::uint8_t>(std::min<int>(clock.ticks + 1, maximum_ticks)), step.clock.residue};
        }
    }
    clocks = std::move(next);
}
}
