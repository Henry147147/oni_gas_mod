// World germ frames, flat against plain (docs/colony-owner.md#germs). The owner
// runs biology::Evolution over sorted arrays with the media read once per cell;
// the rules it must reproduce bit for bit are the plain per-cell ones on maps
// that this file keeps in `reference` (the frame as it was written before the
// flat form, unchanged). Random made-up disease tables, media, cultures, clocks
// (stale ones too), activity masks, queued edits and accounts; after every frame
// the cultures, clocks, accounts (every bit) and the order and amounts of the
// edits must be equal. Nothing here is game data.
#include "../src/oni_world_biology.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace bio = ma::oni::colony::biology;

namespace reference {
using namespace bio;
using Clocks = std::map<std::pair<std::size_t, std::uint64_t>, Clock>;
namespace {
void require(bool ok, const char* why) { if (!ok) throw std::invalid_argument(why); }
constexpr double int_limit = 2147483647.0;
bool whole(double value) noexcept { return value >= 0 && value <= int_limit && value == std::floor(value); }
float whole_material(const Growth& g, float count, float mass) {
    const float minimum = g.minimum_per_kg * mass, maximum = mass * g.maximum_per_kg;
    if (maximum >= count && count >= minimum) return (frame_rate(g.population_half_life) - 1.0f) * count;
    if (minimum > count) return -(frame_seconds * g.death_rate);
    return (frame_rate(g.over_population_half_life) - 1.0f) * count;
}
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
        const float kill = m.radiation > 0 ? m.radiation * table.diseases.at(disease).radiation_kill_rate : 0.0f;
        if (kill > 0) total = total - kill;
        if (std::isfinite(total) && total >= -2147483648.0f && total < 2147483648.0f) {
            const auto change = static_cast<std::int32_t>(total);
            const float residue = total - static_cast<float>(change);
            const auto next = static_cast<std::int64_t>(n) + change;
            if (next <= 0) return {0, -population, Clock{native_mode, clock.ticks, 0}};
            if (next <= static_cast<std::int64_t>(int_limit)) return {static_cast<double>(next), static_cast<double>(change), Clock{native_mode, clock.ticks, residue}};
        }
    }
    double material = 0;
    for (const auto& p : m.parts) material += (p.mass_kg / m.mass_kg) * expected_material(table.at(disease, p.element), population, mass);
    double change = material + population * (static_cast<double>(temperature_rate) - 1.0) + static_cast<double>(clock.residue);
    if (m.radiation > 0) change -= static_cast<double>(m.radiation * table.diseases.at(disease).radiation_kill_rate);
    if (change < 0 && population + change <= 0) change = -population;
    return {population + change, change, Clock{expected_mode, clock.ticks, 0}};
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
    Cells& cells, Clocks& clocks, Accounts& accounts, std::vector<Edit>& edits, std::span<const std::uint8_t> active) {
    require(width > 0 && height > 0 && (active.empty() || active.size()==width*height), "Invalid world germ grid");
    const auto ticks_of = [&](std::size_t cell, std::uint64_t identity) -> std::uint8_t {
        const auto it = clocks.find({cell, identity}); return it == clocks.end() ? std::uint8_t{0} : it->second.ticks; };
    const auto holds_fluid = [](const Medium& m) { return m.open && m.state != 0 && m.mass_kg > 0; };
    std::map<std::pair<std::size_t, std::uint64_t>, double> moved;
    for (const auto& [i, list] : cells) {
        if(!active.empty() && !active[i])continue;
        const auto& here = medium(i);
        if (!holds_fluid(here)) continue;
        const auto x = i % width, y = i / width;
        const std::array<std::size_t, 4> around{x + 1 < width ? i + 1 : i, y + 1 < height ? i + width : i, x > 0 ? i - 1 : i, y > 0 ? i - width : i};
        for (const auto& c : list) {
            if (!disease_identity(c.identity) || c.identity > table.diseases.size() || !(c.quantity > 0)) continue;
            for (const auto n : around) {
                if (n == i || (!active.empty() && !active[n])) continue;
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
        if (reference::apply(cells, e, true)) edits.push_back(e);
    }
    Clocks next;
    if(!active.empty())for(const auto& [key,clock]:clocks)if(!active[key.first])next.emplace(key,clock);
    std::vector<std::size_t> visited; visited.reserve(cells.size());
    for (const auto& entry : cells) visited.push_back(entry.first);
    for (const auto i : visited) {
        if(!active.empty() && !active[i])continue;
        const auto found = cells.find(i);
        if (found == cells.end()) continue;
        const auto list = found->second;
        const auto& here = medium(i);
        for (const auto& c : list) {
            if (!disease_identity(c.identity) || c.identity > table.diseases.size()) continue;
            const auto key = std::make_pair(i, c.identity);
            const auto clock_it = clocks.find(key);
            Clock clock = clock_it != clocks.end() ? clock_it->second : Clock{whole(c.quantity) ? native_mode : expected_mode, 0, 0};
            if (!(here.open && here.mass_kg > 0) || here.parts.empty()) { next[key] = clock; continue; }
            const auto step = reference::grow(table, c.identity - 1, here, c.quantity, clock);
            double after = c.quantity;
            auto& account = accounts[c.identity];
            if (step.change != 0) {
                const Edit e{i, c.identity, step.change};
                if (reference::apply(cells, e, true)) {
                    edits.push_back(e); after = c.quantity + step.change;
                    if (step.change > 0) account.born += step.change; else account.died -= step.change;
                }
            }
            if (after > 0 && after < 1) {
                const Edit e{i, c.identity, -after};
                if (reference::apply(cells, e, true)) { edits.push_back(e); account.culled += after; after = 0; }
            }
            if (after > 0) next[key] = Clock{step.clock.mode, static_cast<std::uint8_t>(std::min<int>(clock.ticks + 1, maximum_ticks)), step.clock.residue};
        }
    }
    clocks = std::move(next);
}
}

namespace {
void require(bool ok, const std::string& what) { if (!ok) throw std::runtime_error(what); }
std::uint64_t bits(double v) { return std::bit_cast<std::uint64_t>(v); }
std::uint32_t bits(float v) { return std::bit_cast<std::uint32_t>(v); }
bool same(long double a, long double b) { return a == b && std::signbit(a) == std::signbit(b); }
// Raw generator outputs only (no library distributions), so every platform draws the same worlds.
using Generator = std::mt19937_64;
template <class T> T pick(Generator& g, std::initializer_list<T> values) { return *(values.begin() + static_cast<std::ptrdiff_t>(g() % values.size())); }
double uniform(Generator& g, double lo, double hi) { return lo + (hi - lo) * (static_cast<double>(g() >> 11) / 9007199254740992.0); }

struct World {
    std::size_t width = 0, height = 0;
    bio::Table table;
    std::vector<bio::Medium> media;
};
struct Plain { reference::Cells cells; reference::Clocks clocks; bio::Accounts accounts; std::vector<bio::Edit> edits; };
struct Flat { bio::Field field; bio::Clocks clocks; bio::Accounts accounts; std::vector<bio::Edit> edits; };

void make_table(Generator& g, World& w, std::size_t diseases, std::size_t elements) {
    constexpr float inf = std::numeric_limits<float>::infinity();
    w.table = {}; w.table.elements = elements;
    for (std::size_t d = 0; d < diseases; ++d) {
        bio::Disease disease;
        disease.strength = 1;
        disease.radiation_kill_rate = pick<float>(g, {0.f, 0.f, .5f, 2.5f});
        const bool inert = g() % 8 == 0; // the game's no-effect rule: all eight bounds infinite
        float bound = static_cast<float>(uniform(g, 230, 270));
        for (std::size_t k = 0; k < 4; ++k) {
            disease.range[k] = inert ? inf : bound;
            disease.half_lives[k] = inert ? inf : pick<float>(g, {15.f, 30.f, 200.f, 0.f, 8.f, 1000.f});
            bound += static_cast<float>(uniform(g, 5, 40));
        }
        w.table.diseases.push_back(disease);
    }
    for (std::size_t d = 0; d < diseases * elements; ++d) {
        bio::Growth growth;
        growth.death_rate = pick<float>(g, {0.f, .1f, 1.f, 3.f});
        growth.population_half_life = pick<float>(g, {-100.f, -20.f, 25.f, 200.f, inf, 0.f});
        growth.over_population_half_life = pick<float>(g, {-50.f, 20.f, 60.f, inf});
        growth.diffusion_scale = pick<float>(g, {0.f, .25f, .5f, 1.f, 2.f, .75f});
        growth.minimum_per_kg = pick<float>(g, {0.f, 1.f, 3.f, 20.f});
        growth.maximum_per_kg = pick<float>(g, {50.f, 500.f, 1e5f, inf});
        growth.minimum_diffusion_count = pick<std::int32_t>(g, {0, 2, 10, 50});
        growth.minimum_diffusion_ticks = pick<std::uint8_t>(g, {0, 0, 1, 3});
        w.table.growth.push_back(growth);
    }
    bio::validate(w.table);
}

void make_media(Generator& g, World& w, std::size_t elements) {
    w.media.assign(w.width * w.height, bio::Medium{});
    for (auto& m : w.media) {
        if (g() % 100 < 12) continue; // closed
        m.open = true; m.state = pick<std::uint8_t>(g, {1, 1, 1, 2, 0});
        m.element = static_cast<std::uint32_t>(g() % elements);
        m.temperature_k = static_cast<float>(uniform(g, 235, 380));
        m.mass_kg = g() % 100 < 8 ? 0.0 : uniform(g, .001, 12);
        m.radiation = g() % 100 < 8 ? static_cast<float>(uniform(g, .5, 90)) : 0.f;
        const auto parts = g() % 100 < 4 ? 0 : 1 + g() % 3;
        for (std::size_t k = 0; k < static_cast<std::size_t>(parts); ++k) m.parts.push_back({static_cast<std::uint32_t>(g() % elements), uniform(g, .001, 8)});
    }
}

void seed(Generator& g, const World& w, Plain& plain, Flat& flat) {
    const std::size_t cells = w.width * w.height, diseases = w.table.diseases.size();
    const auto density = pick<int>(g, {5, 20, 40, 70});
    for (std::size_t i = 0; i < cells; ++i) {
        if (static_cast<int>(g() % 100) >= density) continue;
        std::uint64_t identity = 0;
        for (std::size_t k = 0, count = 1 + g() % 3; k < count; ++k) {
            identity += 1 + g() % 2;
            if (identity > diseases + (g() % 8 == 0 ? 1 : 0)) break; // sometimes an identity the table does not know
            double quantity;
            const auto kind = g() % 10;
            if (kind < 7) quantity = static_cast<double>(1 + g() % static_cast<std::uint64_t>(pick<int>(g, {20, 800, 6000})));
            else if (kind < 9) quantity = uniform(g, 1, 4000);
            else quantity = uniform(g, .05, .95);
            plain.cells[i].push_back({identity, quantity}); flat.field.push_back({i, identity, quantity});
            if (g() % 100 < 65) {
                bio::Clock c; c.mode = g() % 6 == 0 ? bio::expected_mode : bio::native_mode; c.ticks = static_cast<std::uint8_t>(g() % 8);
                c.residue = c.mode == bio::native_mode ? static_cast<float>(uniform(g, -.99, .99)) : 0.f;
                plain.clocks.emplace(std::pair{i, identity}, c);
            }
        }
    }
    for (std::size_t k = 0; k < cells / 8; ++k) { // clocks of cultures that are not there
        bio::Clock c; c.ticks = static_cast<std::uint8_t>(g() % 8); c.residue = static_cast<float>(uniform(g, -.9, .9));
        plain.clocks.emplace(std::pair{static_cast<std::size_t>(g() % cells), static_cast<std::uint64_t>(1 + g() % diseases)}, c);
    }
    for (const auto& [key, c] : plain.clocks) flat.clocks.push_back({key.first, key.second, c});
    for (std::uint64_t identity = 1; identity <= diseases; ++identity) if (g() % 2) {
        bio::Account a; a.born = static_cast<long double>(uniform(g, 0, 1e6)); a.died = static_cast<long double>(uniform(g, 0, 1e5)); a.moved = static_cast<long double>(uniform(g, 0, 1e7));
        plain.accounts[identity] = a; flat.accounts[identity] = a;
    }
}

void compare(const Plain& plain, const Flat& flat, const std::string& where) {
    const auto cells = bio::to_cells(flat.field);
    require(cells.size() == plain.cells.size(), where + ": occupied cells");
    for (auto a = cells.begin(), b = plain.cells.begin(); a != cells.end(); ++a, ++b) {
        require(a->first == b->first && a->second.size() == b->second.size(), where + ": cultures of cell " + std::to_string(b->first));
        for (std::size_t k = 0; k < a->second.size(); ++k)
            require(a->second[k].identity == b->second[k].identity && bits(a->second[k].quantity) == bits(b->second[k].quantity), where + ": culture quantity in cell " + std::to_string(b->first));
    }
    require(flat.clocks.size() == plain.clocks.size(), where + ": clock count");
    std::size_t k = 0;
    for (const auto& [key, c] : plain.clocks) {
        const auto& f = flat.clocks[k++];
        require(f.cell == key.first && f.identity == key.second && f.clock.mode == c.mode && f.clock.ticks == c.ticks && bits(f.clock.residue) == bits(c.residue),
            where + ": clock of cell " + std::to_string(key.first));
    }
    require(flat.accounts.size() == plain.accounts.size(), where + ": account count");
    auto f = flat.accounts.begin();
    for (const auto& [identity, a] : plain.accounts) {
        require(f->first == identity, where + ": account identities");
        const auto& b = f->second; ++f;
        require(same(a.born, b.born) && same(a.died, b.died) && same(a.culled, b.culled) && same(a.moved, b.moved) && same(a.edited_in, b.edited_in) && same(a.edited_out, b.edited_out),
            where + ": account " + std::to_string(identity));
    }
    require(flat.edits.size() == plain.edits.size(), where + ": edit count");
    for (std::size_t e = 0; e < plain.edits.size(); ++e)
        require(flat.edits[e].cell == plain.edits[e].cell && flat.edits[e].identity == plain.edits[e].identity && bits(flat.edits[e].change) == bits(plain.edits[e].change),
            where + ": edit " + std::to_string(e));
}

void equivalence(std::uint64_t seeds) {
    std::size_t frames = 0, edits = 0, expected_cultures = 0, filled = 0;
    for (std::uint64_t number = 1; number <= seeds; ++number) {
        Generator g(number * 7919);
        World w;
        const bool large = number % 4 == 0; // large enough for the parallel media fill
        w.width = large ? 40 : pick<std::size_t>(g, {1, 2, 3, 7, 11, 16}); w.height = large ? 30 : pick<std::size_t>(g, {1, 3, 8, 9});
        const std::size_t diseases = 1 + g() % 4, elements = 5, cells = w.width * w.height;
        make_table(g, w, diseases, elements);
        make_media(g, w, elements);
        Plain plain; Flat flat;
        seed(g, w, plain, flat);
        bio::Evolution evolution;
        for (int round = 0; round < 6; ++round) {
            if (round > 0) make_media(g, w, elements); // a new series: the media changed
            const bool through_map = round % 3 == 2;   // frame() on maps instead of the flat form
            bio::Evolution::Fill fill = [&](std::size_t i, bio::Fluid& f, std::vector<bio::Part>& parts) {
                const auto& m = w.media[i];
                f.open = m.open; f.state = m.state; f.element = m.element; f.temperature_k = m.temperature_k; f.mass_kg = m.mass_kg; f.radiation = m.radiation;
                parts.insert(parts.end(), m.parts.begin(), m.parts.end());
                ++filled;
            };
            // Blocks in reverse order: the result must not depend on which thread fills what or when.
            bio::Evolution::Runner runner;
            if (round % 2 == 0) runner = [](std::size_t blocks, const std::function<void(std::size_t)>& job) { for (std::size_t b = blocks; b-- > 0;) job(b); };
            if (!through_map) evolution.begin(w.table, w.width, w.height, fill, runner);
            for (int step = 0; step < 5; ++step) {
                for (std::size_t q = g() % 3 == 0 ? 1 + g() % 4 : 0; q > 0; --q) { // queued edits, applied before the frame
                    const bio::Edit e{static_cast<std::size_t>(g() % cells), 1 + g() % diseases, pick<double>(g, {5, 40, 700, -3, -50, -2000, .5, -.25})};
                    const bool mass = w.media[e.cell].open && w.media[e.cell].mass_kg > 0;
                    const bool a = reference::apply(plain.cells, e, mass), b = bio::apply(flat.field, e, mass);
                    require(a == b, "queued edit result");
                    if (a) { plain.edits.push_back(e); flat.edits.push_back(e); }
                }
                std::vector<std::uint8_t> active;
                if (g() % 2) { active.resize(cells); for (auto& a : active) a = static_cast<std::uint8_t>(g() % 100 < 65 ? 1 : 0); }
                reference::frame(w.table, w.width, w.height, [&](std::size_t i) -> const bio::Medium& { return w.media[i]; }, plain.cells, plain.clocks, plain.accounts, plain.edits, active);
                if (!through_map) {
                    if (g() % 2) evolution.prefill(flat.field, active);
                    evolution.frame(flat.field, flat.clocks, flat.accounts, flat.edits, active);
                } else {
                    auto map = bio::to_cells(flat.field);
                    bio::frame(w.table, w.width, w.height, [&](std::size_t i) -> const bio::Medium& { return w.media[i]; }, map, flat.clocks, flat.accounts, flat.edits, active);
                    flat.field = bio::to_field(map);
                }
                ++frames;
                compare(plain, flat, "world " + std::to_string(number) + " round " + std::to_string(round) + " frame " + std::to_string(step));
            }
        }
        edits += plain.edits.size();
        for (const auto& [key, c] : plain.clocks) if (c.mode == bio::expected_mode) ++expected_cultures;
    }
    // The control would pass vacuously without enough work: it must have grown, diffused and filled.
    require(frames == seeds * 30 && edits > frames * 20 && expected_cultures > 0 && filled > frames, "equivalence control did too little");
    std::cout << "flat germ frames equal the plain rules: " << seeds << " worlds, " << frames << " frames, " << edits << " edits, every bit\n";
}
}

int main() {
    try {
        equivalence(300);
        std::cout << "BIOLOGY EQUIVALENCE PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << "\n";
        return 1;
    }
}
