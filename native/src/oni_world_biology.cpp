#include "oni_world_biology.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
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
        require(std::isfinite(d.radiation_kill_rate) && d.radiation_kill_rate >= 0, "Invalid disease radiation kill rate");
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

namespace {
// grow() on a medium given as its fields, so the flat frames need no Medium.
Step grow_in(const Table& table, std::size_t disease, float temperature_k, double mass_kg, float radiation, std::span<const Part> parts,
    double population, Clock clock) {
    require(population > 0 && std::isfinite(population) && mass_kg > 0 && !parts.empty(), "Invalid world germ culture");
    const float temperature_rate = frame_rate(temperature_half_life(table.diseases.at(disease), temperature_k));
    const float mass = static_cast<float>(mass_kg);
    if (clock.mode == native_mode && population >= 1 && whole(population)) {
        const auto n = static_cast<std::int32_t>(population);
        const float count = static_cast<float>(n);
        float total = (clock.residue - count) + temperature_rate * count;
        float material;
        if (parts.size() == 1) material = whole_material(table.at(disease, parts[0].element), count, mass);
        else {
            double blend = 0;
            for (const auto& p : parts) blend += (p.mass_kg / mass_kg) * static_cast<double>(whole_material(table.at(disease, p.element), count, mass));
            material = static_cast<float>(blend);
        }
        total = total + material;
        const float kill = radiation > 0 ? radiation * table.diseases.at(disease).radiation_kill_rate : 0.0f;
        if (kill > 0) total = total - kill;
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
    for (const auto& p : parts) material += (p.mass_kg / mass_kg) * expected_material(table.at(disease, p.element), population, mass);
    double change = material + population * (static_cast<double>(temperature_rate) - 1.0) + static_cast<double>(clock.residue);
    if (radiation > 0) change -= static_cast<double>(radiation * table.diseases.at(disease).radiation_kill_rate);
    if (change < 0 && population + change <= 0) change = -population;
    return {population + change, change, Clock{expected_mode, clock.ticks, 0}};
}
}

Step grow(const Table& table, std::size_t disease, const Medium& m, double population, Clock clock) {
    return grow_in(table, disease, m.temperature_k, m.mass_kg, m.radiation, m.parts, population, clock);
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

namespace {
constexpr std::uint32_t none = std::numeric_limits<std::uint32_t>::max();
std::uint32_t narrow(std::size_t value) noexcept { return static_cast<std::uint32_t>(value); }
// (cell, identity) order of every sorted container here.
bool before_key(std::size_t cell, std::uint64_t identity, std::size_t other_cell, std::uint64_t other_identity) noexcept {
    return cell != other_cell ? cell < other_cell : identity < other_identity;
}
bool holds_fluid(const Fluid& m) noexcept { return m.open && m.state != 0 && m.mass_kg > 0; }
// The position of a clock in the sorted clocks, or none.
std::uint32_t find_clock(const Clocks& clocks, std::size_t cell, std::uint64_t identity) noexcept {
    const auto it = std::lower_bound(clocks.begin(), clocks.end(), 0, [&](const ClockEntry& c, int) { return before_key(c.cell, c.identity, cell, identity); });
    return it != clocks.end() && it->cell == cell && it->identity == identity ? narrow(static_cast<std::size_t>(it - clocks.begin())) : none;
}
}

Field to_field(const Cells& cells) {
    Field field;
    for (const auto& [cell, list] : cells) for (const auto& c : list) field.push_back({cell, c.identity, c.quantity});
    return field;
}
Cells to_cells(const Field& field) {
    Cells cells;
    for (const auto& e : field) {
        if (cells.empty() || cells.rbegin()->first != e.cell) cells.emplace_hint(cells.end(), e.cell, std::vector<Culture>{});
        cells.rbegin()->second.push_back({e.identity, e.quantity});
    }
    return cells;
}
double quantity_in(const Field& field, std::size_t cell, std::uint64_t identity) noexcept {
    const auto it = std::lower_bound(field.begin(), field.end(), 0, [&](const Entry& e, int) { return before_key(e.cell, e.identity, cell, identity); });
    return it != field.end() && it->cell == cell && it->identity == identity ? it->quantity : 0.0;
}
bool apply(Field& field, const Edit& e, bool cell_has_mass) {
    if (!cell_has_mass || e.change == 0 || !std::isfinite(e.change)) return false;
    const auto it = std::lower_bound(field.begin(), field.end(), 0, [&](const Entry& held, int) { return before_key(held.cell, held.identity, e.cell, e.identity); });
    if (it == field.end() || it->cell != e.cell || it->identity != e.identity) {
        if (!(e.change > 0)) return false;
        field.insert(it, Entry{e.cell, e.identity, e.change});
        return true;
    }
    it->quantity += e.change;
    if (!(it->quantity > 0)) field.erase(it);
    return true;
}

void Evolution::begin(const Table& rules, std::size_t columns, std::size_t rows, Fill filler, Runner runner) {
    require(columns > 0 && rows > 0, "Invalid world germ grid");
    table = &rules; width = columns; height = rows; fill = std::move(filler); run = std::move(runner);
    if (slots.size() != columns * rows) { slots.assign(columns * rows, Slot{}); series_stamp = 0; frame_stamp = 0; }
    if (++series_stamp == 0) { slots.assign(slots.size(), Slot{}); series_stamp = 1; frame_stamp = 0; }
    fluids.clear(); parts.clear();
}

namespace {
// The four neighbours of a cell, as the frame visits them (a missing one is the cell itself).
std::array<std::size_t, 4> ring_of(std::size_t i, std::size_t width, std::size_t height) noexcept {
    const auto y = i / width, x = i - y * width;
    return {x + 1 < width ? i + 1 : i, y + 1 < height ? i + width : i, x > 0 ? i - 1 : i, y > 0 ? i - width : i};
}
}

void Evolution::prefill(const Field& field, std::span<const std::uint8_t> active) {
    constexpr std::size_t block_cells = 64, smallest = 2 * block_cells;
    if (!run || !fill) return;
    require(table != nullptr && width > 0 && height > 0 && (active.empty() || active.size() == width * height), "Invalid world germ grid");
    const bool filtered = !active.empty();
    const std::size_t cell_count = width * height;
    wanted.clear();
    const auto want = [&](std::size_t cell) {
        auto& slot = slots[cell];
        if (slot.fluid_stamp == series_stamp || slot.queue_stamp == series_stamp) return;
        slot.queue_stamp = series_stamp; wanted.push_back(cell);
    };
    for (std::size_t e = 0; e < field.size();) {
        const auto cell = field[e].cell;
        require(cell < cell_count, "World germ cell outside the grid");
        while (e < field.size() && field[e].cell == cell) ++e;
        if (filtered && !active[cell]) continue;
        want(cell);
        for (const auto n : ring_of(cell, width, height)) if (n != cell && (!filtered || active[n])) want(n);
    }
    if (wanted.size() < smallest) { for (const auto cell : wanted) slots[cell].queue_stamp = 0; return; } // too few to be worth a parallel run
    const std::size_t blocks = (wanted.size() + block_cells - 1) / block_cells;
    made.assign(wanted.size(), Fluid{});
    if (made_parts.size() < blocks) made_parts.resize(blocks);
    for (std::size_t b = 0; b < blocks; ++b) made_parts[b].clear();
    run(blocks, [&](std::size_t block) {
        auto& local = made_parts[block];
        for (std::size_t k = block * block_cells; k < std::min(wanted.size(), (block + 1) * block_cells); ++k) {
            Fluid f; const auto first = local.size();
            fill(wanted[k], f, local);
            f.first_part = narrow(first); f.parts = narrow(local.size() - first);
            made[k] = f;
        }
    });
    for (std::size_t b = 0; b < blocks; ++b) {
        const auto base = parts.size();
        parts.insert(parts.end(), made_parts[b].begin(), made_parts[b].end());
        for (std::size_t k = b * block_cells; k < std::min(wanted.size(), (b + 1) * block_cells); ++k) {
            Fluid f = made[k]; f.first_part += narrow(base);
            auto& slot = slots[wanted[k]];
            slot.fluid_stamp = series_stamp; slot.fluid = narrow(fluids.size());
            fluids.push_back(f);
        }
    }
}

Fluid Evolution::fluid_of(std::size_t cell) {
    auto& slot = slots[cell];
    if (slot.fluid_stamp != series_stamp) {
        Fluid filled; const auto first = parts.size();
        fill(cell, filled, parts);
        filled.first_part = narrow(first); filled.parts = narrow(parts.size() - first);
        slot.fluid_stamp = series_stamp; slot.fluid = narrow(fluids.size());
        fluids.push_back(filled);
        return filled;
    }
    return fluids[slot.fluid];
}

void Evolution::frame(Field& field, Clocks& clocks, Accounts& accounts, std::vector<Edit>& edits, std::span<const std::uint8_t> active) {
    require(table != nullptr && width > 0 && height > 0 && (active.empty() || active.size() == width * height), "Invalid world germ grid");
    const Table& rules = *table;
    const bool filtered = !active.empty();
    const auto is_active = [&](std::size_t cell) { return !filtered || active[cell] != 0; };
    const std::size_t cell_count = width * height, count = field.size(), disease_count = rules.diseases.size();
    // The accounts by identity (map nodes do not move); a missing one is made
    // when the first thing is booked to it, as operator[] did.
    std::array<Account*, 257> account_of{};
    for (auto& [identity, held] : accounts) if (identity < account_of.size()) account_of[identity] = &held;
    const auto account = [&](std::uint64_t identity) -> Account& {
        auto*& entry = account_of[identity];
        if (!entry) entry = &accounts[identity];
        return *entry;
    };
    // Where each cell's cultures are in the field, and the clock of each.
    if (++frame_stamp == 0) { for (auto& slot : slots) slot.frame_stamp = 0; frame_stamp = 1; }
    for (std::size_t e = 0; e < count;) {
        const auto cell = field[e].cell;
        require(cell < cell_count, "World germ cell outside the grid");
        std::size_t end = e + 1;
        while (end < count && field[end].cell == cell) ++end;
        auto& slot = slots[cell]; slot.frame_stamp = frame_stamp; slot.first = narrow(e); slot.end = narrow(end);
        e = end;
    }
    clock_of.assign(count, none);
    for (std::size_t e = 0, k = 0; e < count; ++e) {
        while (k < clocks.size() && before_key(clocks[k].cell, clocks[k].identity, field[e].cell, field[e].identity)) ++k;
        if (k < clocks.size() && clocks[k].cell == field[e].cell && clocks[k].identity == field[e].identity) clock_of[e] = narrow(k);
    }
    // Diffusion from the frame's starting counts: each pair of neighbours of
    // the same state exchanges an eighth of its difference, scaled by the
    // richer cell's element. Counted once per pair (from the lower cell when
    // both hold the identity). The sums per (cell, identity) add in the order
    // the pairs are visited: onto the culture's own slot when it exists, else
    // in a list that is summed per key in that order afterwards.
    moved.assign(count, 0.0);
    fresh.clear();
    std::size_t sequence = 0;
    const auto move_into = [&](std::uint32_t entry, std::size_t cell, std::uint64_t identity, double change) {
        if (entry != none) moved[entry] += change;
        else fresh.push_back({cell, identity, change, sequence++});
    };
    struct Neighbour { std::size_t cell; std::uint32_t element, first, end; };
    for (std::size_t first = 0; first < count;) {
        const std::size_t i = field[first].cell, end = slots[i].end, group = first;
        first = end;
        if (!is_active(i)) continue;
        const Fluid here = fluid_of(i);
        if (!holds_fluid(here)) continue;
        std::array<Neighbour, 4> around{};
        std::size_t around_count = 0;
        bool resolved = false;
        for (std::size_t e = group; e < end; ++e) {
            const Entry c = field[e];
            if (!disease_identity(c.identity) || c.identity > disease_count || !(c.quantity > 0)) continue;
            if (!resolved) {
                resolved = true;
                for (const auto n : ring_of(i, width, height)) {
                    if (n == i || !is_active(n)) continue;
                    const Fluid there = fluid_of(n);
                    if (!holds_fluid(there) || there.state != here.state) continue;
                    const auto& slot = slots[n];
                    const bool held = slot.frame_stamp == frame_stamp;
                    around[around_count++] = {n, there.element, held ? slot.first : 0u, held ? slot.end : 0u};
                }
            }
            for (std::size_t q = 0; q < around_count; ++q) {
                const auto& adjacent = around[q];
                const std::size_t n = adjacent.cell;
                double held = 0; std::uint32_t held_entry = none;
                for (std::uint32_t k = adjacent.first; k < adjacent.end; ++k) {
                    if (field[k].identity == c.identity) { held = field[k].quantity; held_entry = k; break; }
                    if (field[k].identity > c.identity) break;
                }
                if (held > 0 && n < i) continue;
                const auto a = std::min(i, n), b = std::max(i, n);
                const double count_a = a == i ? c.quantity : held, count_b = b == i ? c.quantity : held;
                if (count_a == count_b) continue;
                const auto source = count_b > count_a ? b : a;
                const std::uint32_t source_entry = source == i ? narrow(e) : held_entry;
                std::uint8_t ticks = 0;
                const std::uint32_t source_clock = source_entry != none ? clock_of[source_entry] : find_clock(clocks, source, c.identity);
                if (source_clock != none) ticks = clocks[source_clock].clock.ticks;
                const float scale = diffusion_scale(rules, c.identity - 1, source == i ? here.element : adjacent.element, std::max(count_a, count_b), ticks);
                if (!(scale > 0)) continue;
                const double into_a = diffused(count_a, count_b, scale);
                if (into_a == 0) continue;
                move_into(a == i ? narrow(e) : held_entry, a, c.identity, into_a);
                move_into(b == i ? narrow(e) : held_entry, b, c.identity, -into_a);
                account(c.identity).moved += std::abs(into_a);
            }
        }
    }
    // Apply the sums in (cell, identity) order, exactly as apply() would on
    // the map: a culture driven to zero or below leaves, a new one needs a
    // positive amount.
    std::sort(fresh.begin(), fresh.end(), [](const Contribution& l, const Contribution& r) {
        if (l.cell != r.cell) return l.cell < r.cell;
        if (l.identity != r.identity) return l.identity < r.identity;
        return l.sequence < r.sequence;
    });
    next.clear(); next.reserve(count + fresh.size());
    next_clock_of.clear(); next_clock_of.reserve(count + fresh.size());
    for (std::size_t e = 0, f = 0; e < count || f < fresh.size();) {
        const bool new_key = f < fresh.size() && (e == count || before_key(fresh[f].cell, fresh[f].identity, field[e].cell, field[e].identity));
        if (!new_key) {
            const Entry held = field[e]; const double change = moved[e];
            if (change == 0 || !std::isfinite(change)) { next.push_back(held); next_clock_of.push_back(clock_of[e]); }
            else {
                edits.push_back({held.cell, held.identity, change});
                Entry grown = held; grown.quantity += change;
                if (grown.quantity > 0) { next.push_back(grown); next_clock_of.push_back(clock_of[e]); }
            }
            ++e;
            continue;
        }
        const auto cell = fresh[f].cell; const auto identity = fresh[f].identity;
        double change = 0;
        while (f < fresh.size() && fresh[f].cell == cell && fresh[f].identity == identity) change += fresh[f++].change;
        if (change == 0 || !std::isfinite(change) || !(change > 0)) continue;
        edits.push_back({cell, identity, change});
        next.push_back({cell, identity, change});
        next_clock_of.push_back(find_clock(clocks, cell, identity));
    }
    // Growth and death of every culture after diffusion. `next` now holds the
    // cultures; survivors are packed down in place.
    next_clocks.clear();
    const std::size_t after_diffusion = next.size();
    std::size_t kept = 0;
    for (std::size_t first = 0; first < after_diffusion;) {
        const std::size_t i = next[first].cell;
        std::size_t end = first + 1;
        while (end < after_diffusion && next[end].cell == i) ++end;
        const std::size_t group = first;
        first = end;
        if (!is_active(i)) {
            for (std::size_t e = group; e < end; ++e) { next[kept] = next[e]; next_clock_of[kept] = next_clock_of[e]; ++kept; }
            continue;
        }
        const Fluid here = fluid_of(i);
        for (std::size_t e = group; e < end; ++e) {
            Entry c = next[e];
            const std::uint32_t held_clock = next_clock_of[e];
            const auto keep = [&](bool survives) { if (survives) { next[kept] = c; next_clock_of[kept] = held_clock; ++kept; } };
            // An identity the game's table does not know (none in the base game) is kept unchanged.
            if (!disease_identity(c.identity) || c.identity > disease_count) { keep(true); continue; }
            const Clock clock = held_clock != none ? clocks[held_clock].clock : Clock{whole(c.quantity) ? native_mode : expected_mode, 0, 0};
            if (!(here.open && here.mass_kg > 0) || here.parts == 0) { next_clocks.push_back({i, c.identity, clock}); keep(true); continue; }
            const auto step = grow_in(rules, c.identity - 1, here.temperature_k, here.mass_kg, here.radiation,
                std::span<const Part>(parts.data() + here.first_part, here.parts), c.quantity, clock);
            double after = c.quantity;
            auto& booked = account(c.identity);
            if (step.change != 0 && std::isfinite(step.change)) {
                edits.push_back({i, c.identity, step.change});
                c.quantity += step.change; after = c.quantity;
                if (step.change > 0) booked.born += step.change; else booked.died -= step.change;
            }
            if (after > 0 && after < 1) {
                edits.push_back({i, c.identity, -after});
                c.quantity += -after; booked.culled += after; after = 0;
            }
            if (after > 0) next_clocks.push_back({i, c.identity,
                Clock{step.clock.mode, static_cast<std::uint8_t>(std::min<int>(clock.ticks + 1, maximum_ticks)), step.clock.residue}});
            keep(c.quantity > 0);
        }
    }
    next.resize(kept);
    field.swap(next);
    // The clocks: those of cells that were not active stay as they are.
    if (!filtered) clocks.swap(next_clocks);
    else {
        kept_clocks.clear();
        for (const auto& c : clocks) if (c.cell < active.size() && !active[c.cell]) kept_clocks.push_back(c);
        clocks.clear(); clocks.reserve(kept_clocks.size() + next_clocks.size());
        std::merge(kept_clocks.begin(), kept_clocks.end(), next_clocks.begin(), next_clocks.end(), std::back_inserter(clocks),
            [](const ClockEntry& l, const ClockEntry& r) { return before_key(l.cell, l.identity, r.cell, r.identity); });
    }
}

void frame(const Table& table, std::size_t width, std::size_t height, const std::function<const Medium&(std::size_t)>& medium,
    Cells& cells, Clocks& clocks, Accounts& accounts, std::vector<Edit>& edits, std::span<const std::uint8_t> active) {
    require(width > 0 && height > 0 && (active.empty() || active.size() == width * height), "Invalid world germ grid");
    Evolution evolution;
    evolution.begin(table, width, height, [&](std::size_t cell, Fluid& fluid, std::vector<Part>& parts) {
        const Medium& m = medium(cell);
        fluid.open = m.open; fluid.state = m.state; fluid.element = m.element; fluid.temperature_k = m.temperature_k;
        fluid.mass_kg = m.mass_kg; fluid.radiation = m.radiation;
        parts.insert(parts.end(), m.parts.begin(), m.parts.end());
    });
    auto field = to_field(cells);
    evolution.frame(field, clocks, accounts, edits, active);
    cells = to_cells(field);
}
}
