// Colony owner benchmark (docs/performance-acceptance.md). Drives the actual
// colony owner (solver plus native adapter: inputs, flow, ledgers, projection
// and publication) on a generated asteroid with evolving gases, liquids,
// phase change, machines and duplicants, or replays a private world dump.
// The materials are illustrative physical values, not game data; this is
// reproducible benchmark evidence, not an in-game or managed-adapter result.
#include "../src/oni_colony.hpp"
#include "../src/oni_colony_diagnostics.hpp"
#include "../src/oni_colony_journal.hpp"
#include "../src/oni_catalogue.hpp"
#include "../src/mac_game/regional_execution.hpp"
#include "mixed_atmospheres/mac_closed_world.hpp"
#include "mixed_atmospheres/mac_kernel.hpp"
#include <algorithm>
#include <array>
#include <cfenv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <xmmintrin.h>

namespace {
namespace c = ma::oni::colony;
using Clock = std::chrono::steady_clock;
double since(Clock::time_point start) { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); }

// Resident and peak resident memory in bytes (Linux; zero elsewhere).
struct Memory { std::uint64_t resident = 0, peak = 0; };
Memory memory() {
    Memory m;
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        const auto read = [&](const char* key, std::uint64_t& out) {
            if (line.rfind(key, 0) == 0) out = std::strtoull(line.c_str() + std::strlen(key), nullptr, 10) * 1024;
        };
        read("VmRSS:", m.resident);
        read("VmHWM:", m.peak);
    }
    return m;
}

enum Native : std::uint32_t { oxygen = 1, nitrogen, carbon_dioxide, hydrogen, methane, chlorine, steam, water, dirty_water, crude_oil };
constexpr std::uint32_t vacuum = 999;
MAOniColonyMaterial material(std::uint32_t index, const char* id, bool gas, double molar, double cv, double k, double density,
                             double low_k = 0, std::int32_t low_target = 0, double high_k = 0, std::int32_t high_target = 0) {
    MAOniColonyMaterial m{};
    m.struct_size = sizeof m;
    m.abi_version = MA_ONI_COLONY_ABI_VERSION;
    m.element_hash = static_cast<std::int32_t>(100 + index);
    m.native_index = index;
    m.phase = gas ? 1 : 2;
    m.molar_mass_kg_per_mol = molar;
    m.specific_heat_j_per_kg_k = cv;
    m.conductivity_w_per_m_k = k;
    m.density_kg_per_m3 = density;
    m.low_temperature_k = low_k;
    m.high_temperature_k = high_k;
    m.low_target = low_target;
    m.high_target = high_target;
    std::snprintf(m.id, sizeof m.id, "%s", id);
    return m;
}
// Constant-volume heat capacities and conductivities near 300 K (public
// physical data, rounded); water and steam form one saturation pair.
std::vector<MAOniColonyMaterial> materials() {
    const auto hash = [](Native n) { return static_cast<std::int32_t>(100 + n); };
    return {material(oxygen, "Oxygen", true, .032, 659, .0263, 0),
            material(nitrogen, "Nitrogen", true, .028, 743, .026, 0),
            material(carbon_dioxide, "CarbonDioxide", true, .044, 655, .0166, 0),
            material(hydrogen, "Hydrogen", true, .002016, 10180, .187, 0),
            material(methane, "Methane", true, .016, 1700, .034, 0),
            material(chlorine, "ChlorineGas", true, .0709, 355, .0089, 0),
            material(steam, "Steam", true, .018, 1410, .025, 0, 373.15, hash(water)),
            material(water, "Water", false, .018, 4180, .6, 1000, 273.15, 0, 373.15, hash(steam)),
            material(dirty_water, "DirtyWater", false, .018, 4100, .58, 1010, 271.15, 0, 380, 0),
            material(crude_oil, "CrudeOil", false, .5, 1690, .15, 870, 233, 0, 673, 0)};
}

struct Room { std::uint32_t x0, y0, x1, y1, type; bool light_gas = true; };
struct Layout {
    std::uint32_t width = 0, height = 0;
    std::vector<MAOniColonyCell> cells;
    std::vector<Room> rooms;
    // Disturbance events (absent without --events).
    std::vector<std::uint32_t> vault_wall, tank_floor, boil_pool;
    // Late-game colonies (--preset late-*): duplicants pacing a floor,
    // machines and geysers with a duty cycle, cells that exchange heat with
    // the solids beside them, and cells duplicants dig out in order.
    struct Walker { std::uint32_t y, x0, x1, phase; };
    struct Emitter { MAOniColonyTransfer transfer; std::uint32_t period = 1, active = 1, phase = 0; };
    struct Heater { MAOniColonyHeat heat; std::uint32_t period = 1, active = 1, phase = 0; };
    bool late = false;
    std::vector<Walker> walkers;
    std::vector<Emitter> emitters;
    std::vector<Heater> heaters;
    std::vector<std::uint32_t> passive_heat, digs;
    std::uint32_t at(std::uint32_t x, std::uint32_t y) const { return x + y * width; }
};
void put(Layout& l, std::uint32_t x, std::uint32_t y, std::uint32_t kind, std::uint32_t species = vacuum, float mass = 0, float t = 0) {
    l.cells[l.at(x, y)] = {species, kind, mass, t, 0, 0};
}
void gas(Layout& l, std::uint32_t x, std::uint32_t y, std::uint32_t species, float mass, float t) { put(l, x, y, MA_ONI_COLONY_FLUID, species, mass, t); }
void wall(Layout& l, std::uint32_t x, std::uint32_t y) { put(l, x, y, MA_ONI_COLONY_SOLID); }

// One room's interior. Six kinds cycle so that every region evolves:
// stratifying gas mixtures, a hot/cold convection cell, a still pool, a
// boiling pool under steam, inverted oil/water layers and inverted H2/Cl2.
void fill_room(Layout& l, const Room& r) {
    for (auto y = r.y0; y < r.y1; ++y)
        for (auto x = r.x0; x < r.x1; ++x) {
            const auto row = y - r.y0, h = r.y1 - r.y0, column = x - r.x0, w = r.x1 - r.x0;
            const bool odd = ((x + y) & 1U) != 0;
            switch (r.type) {
            case 0: // breathing room: O2/N2 checker over a CO2 layer
                if (row < 3) gas(l, x, y, carbon_dioxide, 1.9f, 296);
                else gas(l, x, y, odd ? nitrogen : oxygen, odd ? 1.0f : 1.1f, 296);
                break;
            case 1: // convection: hot left half, cold right half
                gas(l, x, y, oxygen, 1.2f, column < w / 2 ? 340.f : 275.f);
                break;
            case 2: // still pool with humid air
                if (row < 8) gas(l, x, y, water, 1000, 300);
                else gas(l, x, y, (x * 7 + y) % 7 == 0 ? steam : oxygen, (x * 7 + y) % 7 == 0 ? .015f : 1.2f, 300);
                break;
            case 3: // boiling pool under steam
                if (row < 6) gas(l, x, y, water, 980, 371);
                else gas(l, x, y, odd ? steam : oxygen, odd ? .5f : .6f, 385);
                break;
            case 4: // oil under dirty water (they invert), methane/O2 above
                if (row < 4) gas(l, x, y, crude_oil, 850, 310);
                else if (row < 8) gas(l, x, y, dirty_water, 1000, 305);
                else gas(l, x, y, odd ? methane : oxygen, odd ? .7f : .9f, 300);
                break;
            default: // hydrogen under chlorine (stress) or carbon dioxide over oxygen
                if (r.light_gas) {
                    if (row < h / 2) gas(l, x, y, hydrogen, .08f, 290);
                    else gas(l, x, y, chlorine, 3.0f, 290);
                } else {
                    if (row < h / 2) gas(l, x, y, oxygen, 1.3f, 290);
                    else gas(l, x, y, carbon_dioxide, 1.8f, 290);
                }
                break;
            }
        }
}
bool liquid_room(std::uint32_t type) { return type >= 2 && type <= 4; }

constexpr std::uint32_t room_width = 40, room_height = 24;
Layout generate(std::uint32_t rooms_x, std::uint32_t rooms_y, bool events, bool hydrogen_rooms) {
    Layout l;
    l.width = rooms_x * (room_width + 1) + 1;
    l.height = rooms_y * (room_height + 1) + 1;
    if (events && (l.width < 310 || l.height < 130)) throw std::invalid_argument("Events need at least 8 x 6 rooms");
    l.cells.assign(static_cast<std::size_t>(l.width) * l.height, {});
    for (std::uint32_t y = 0; y < l.height; ++y)
        for (std::uint32_t x = 0; x < l.width; ++x) wall(l, x, y);
    for (std::uint32_t ry = 0; ry < rooms_y; ++ry)
        for (std::uint32_t rx = 0; rx < rooms_x; ++rx) {
            const Room r{1 + rx * (room_width + 1), 1 + ry * (room_height + 1), 1 + rx * (room_width + 1) + room_width,
                         1 + ry * (room_height + 1) + room_height, (rx + 3 * ry) % 6, hydrogen_rooms};
            l.rooms.push_back(r);
            fill_room(l, r);
        }
    // Upper doors between neighbours; floor hatches under gas-only rooms.
    for (const auto& r : l.rooms) {
        if (r.x1 + 1 < l.width)
            for (auto y = r.y1 - 6; y < r.y1 - 3; ++y) gas(l, r.x1, y, oxygen, 1.2f, 300);
        const auto above = std::find_if(l.rooms.begin(), l.rooms.end(), [&](const Room& o) { return o.x0 == r.x0 && o.y0 == r.y1 + 1; });
        if (above != l.rooms.end() && !liquid_room(above->type)) {
            const auto x = r.x0 + 5 + (r.x0 * 7) % (room_width - 10);
            gas(l, x, r.y1, oxygen, 1.2f, 300);
            gas(l, x + 1, r.y1, oxygen, 1.2f, 300);
        }
    }
    if (!events) return l;
    // Event structures replace the lower-left corner (x < 307, y < 102).
    for (std::uint32_t y = 0; y < 102; ++y)
        for (std::uint32_t x = 0; x < 307; ++x) wall(l, x, y);
    std::erase_if(l.rooms, [](const Room& r) { return r.x0 < 307 && r.y0 < 102; });
    // 10,000-cell decompression: a 100 x 100 air vault beside an equal vacuum chamber.
    for (std::uint32_t y = 1; y <= 100; ++y) {
        for (std::uint32_t x = 1; x <= 100; ++x) gas(l, x, y, ((x + y) & 1U) ? nitrogen : oxygen, ((x + y) & 1U) ? 1.0f : 1.1f, 296);
        for (std::uint32_t x = 102; x <= 201; ++x) put(l, x, y, MA_ONI_COLONY_FLUID, vacuum, 0, 0);
        l.vault_wall.push_back(l.at(101, y));
    }
    // 1,000-cell flood: a 50 x 20 water tank over a 50 x 58 air room.
    for (std::uint32_t x = 204; x <= 253; ++x) {
        for (std::uint32_t y = 1; y <= 58; ++y) gas(l, x, y, oxygen, 1.2f, 296);
        l.tank_floor.push_back(l.at(x, 59));
        for (std::uint32_t y = 60; y <= 79; ++y) gas(l, x, y, water, 1000, 290);
        for (std::uint32_t y = 80; y <= 81; ++y) gas(l, x, y, oxygen, 1.2f, 290);
    }
    // 1,000-cell phase event: a 50 x 20 warm pool heated past boiling at once.
    for (std::uint32_t x = 256; x <= 305; ++x) {
        for (std::uint32_t y = 1; y <= 20; ++y) {
            gas(l, x, y, water, 1000, 360);
            l.boil_pool.push_back(l.at(x, y));
        }
        for (std::uint32_t y = 21; y <= 59; ++y) gas(l, x, y, oxygen, 1.0f, 330);
    }
    return l;
}

// ---- Late-game colonies -------------------------------------------------
// A generated colony shaped like a long-played one, at the size of the
// game's largest maps: on each asteroid a multi-floor base that is one
// connected atmosphere, a wing of sealed industrial rooms (electrolysers,
// steam, hydrogen, natural gas, tanks and pools, each with running machines),
// tamed geysers on a duty cycle, hundreds of natural caves that nothing
// touches, mined-out caverns where duplicants keep digging, and a surface
// open to space with a vacuum gallery, silos and (optionally) a hall whose
// thin air leaks to space. It is a workload, not a played colony: the
// proportions follow what the full-colony saves show costs the owner.
struct World { std::uint32_t x0, y0, w, h; bool main; };
struct Random {
    std::uint64_t state;
    std::uint64_t next() {
        state += 0x9e3779b97f4a7c15ULL;
        auto z = state;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }
    std::uint32_t below(std::uint32_t n) { return static_cast<std::uint32_t>(next() % n); }
    std::uint32_t range(std::uint32_t a, std::uint32_t b) { return a + below(b - a + 1); }
};
MAOniColonyTransfer transfer(std::uint32_t cell, std::uint32_t native, std::uint32_t kind, std::uint32_t radius, double kg, double t, std::int32_t callback);
void late_world(Layout& l, const World& w, Random& rng, std::uint32_t duplicants, bool vented) {
    const std::uint32_t W = w.w, H = w.h;
    const auto at = [&](std::uint32_t x, std::uint32_t y) { return l.at(w.x0 + x, w.y0 + y); };
    const auto fill = [&](std::uint32_t x, std::uint32_t y, std::uint32_t species, float mass, float t) { gas(l, w.x0 + x, w.y0 + y, species, mass, t); };
    const auto empty = [&](std::uint32_t x, std::uint32_t y) { put(l, w.x0 + x, w.y0 + y, MA_ONI_COLONY_FLUID, vacuum, 0, 0); };
    const auto solid = [&](std::uint32_t x, std::uint32_t y) { return l.cells[at(x, y)].kind == MA_ONI_COLONY_SOLID; };
    const auto machine = [&](std::uint32_t x, std::uint32_t y, std::uint32_t native, std::uint32_t kind, std::uint32_t radius, double kg, double t,
                             std::uint32_t period = 1, std::uint32_t active = 1, std::uint32_t phase = 0) {
        l.emitters.push_back({transfer(at(x, y), native, kind, radius, kg, t, 0), period, active, phase});
    };
    const auto heater = [&](std::uint32_t x, std::uint32_t y, double joules, double limit) { l.heaters.push_back({{at(x, y), 0, joules, limit}, 1, 1, 0}); };
    // Open sky over the surface, inside the asteroid's solid border.
    const std::uint32_t sky = std::max<std::uint32_t>(6, H / 10);
    for (std::uint32_t y = H - sky; y + 1 < H; ++y)
        for (std::uint32_t x = 1; x + 1 < W; ++x) put(l, w.x0 + x, w.y0 + y, MA_ONI_COLONY_SPACE);
    // A vacuum gallery under the surface, joined to the sky by two silos.
    const std::uint32_t gy = H - sky - 8;
    for (std::uint32_t y = gy; y < gy + 4; ++y)
        for (std::uint32_t x = W / 6; x < W - W / 6; ++x) empty(x, y);
    for (const std::uint32_t sx : {W / 4, 3 * W / 4 - 5})
        for (std::uint32_t y = gy + 4; y < H - sky; ++y)
            for (std::uint32_t x = sx; x < sx + 5; ++x) empty(x, y);
    // The vented hall: thin oxygen, a running source, a one-cell gap up to the gallery.
    if (vented) {
        const std::uint32_t hy = gy - 7, hx = W / 3, hw = std::min<std::uint32_t>(60, W / 3);
        for (std::uint32_t y = hy; y < hy + 4; ++y)
            for (std::uint32_t x = hx; x < hx + hw; ++x) fill(x, y, oxygen, .002f, 290);
        for (std::uint32_t y = hy + 4; y < gy; ++y) empty(hx + 2, y);
        machine(hx + hw - 3, hy + 1, oxygen, MA_ONI_COLONY_INJECT, 0, .022, 300);
    }
    // The base: living floors on the left, sealed industrial rooms on the right.
    const std::uint32_t bx0 = W * 19 / 100, bx1 = W * 81 / 100, by0 = H * 30 / 100, by1 = std::min(H * 72 / 100, gy - 12);
    const std::uint32_t bxm = bx0 + (bx1 - bx0) * 6 / 10;
    std::vector<std::uint32_t> floors;
    for (std::uint32_t fy = by0; fy + 5 <= by1; fy += 5) {
        const auto f = static_cast<std::uint32_t>(floors.size());
        floors.push_back(fy);
        for (std::uint32_t x = bx0; x < bxm; ++x) {
            const bool partition = (x - bx0) % 18 == 17, shaft = (x - bx0) % 22 == 10 || (x - bx0) % 22 == 11;
            for (std::uint32_t y = fy; y < fy + 4; ++y)
                if (!(partition && y >= fy + 2)) fill(x, y, f == 0 ? carbon_dioxide : oxygen, 1.8f, f == 0 ? 295.f : 298.f + static_cast<float>(f % 3));
            if (shaft && fy + 9 <= by1) fill(x, fy + 4, oxygen, 1.8f, 298);
            if ((x - bx0) % 3 == 0) l.passive_heat.push_back(at(x, fy));
        }
    }
    std::vector<std::array<std::uint32_t, 3>> chlorine_rooms; // x0, x1, floor row
    const std::uint32_t rw = 16, rh = 9;
    std::uint32_t row = 0;
    for (std::uint32_t ry = by0; ry + rh <= by1; ry += rh + 1, ++row) {
        std::uint32_t column = 0;
        for (std::uint32_t rx = bxm + 1; rx + rw <= bx1; rx += rw + 1, ++column) {
            const auto kind = (column + 3 * row) % 8;
            const auto each = [&](auto&& f) { for (std::uint32_t y = 0; y < rh; ++y) for (std::uint32_t x = 0; x < rw; ++x) f(rx + x, ry + y, y); };
            const std::uint32_t left = rx + 2, right = rx + rw - 3, low = ry + 1, high = ry + rh - 2;
            switch (kind) {
            case 0: // electrolysers: hydrogen over oxygen, a pump for each
                each([&](auto x, auto y, auto r) { if (r < 5) fill(x, y, oxygen, 1.5f, 343); else fill(x, y, hydrogen, .12f, 343); });
                machine(rx + rw / 2, ry + 3, oxygen, MA_ONI_COLONY_INJECT, 0, .1776, 343.15);
                machine(rx + rw / 2 + 1, ry + 3, hydrogen, MA_ONI_COLONY_INJECT, 0, .0224, 343.15);
                machine(left, low, 0, MA_ONI_COLONY_EXTRACT_GAS, 2, .1776, 0);
                machine(right, high, 0, MA_ONI_COLONY_EXTRACT_GAS, 2, .0224, 0);
                break;
            case 1: // steam room: a heater below, steam drawn off above, its water returned
                each([&](auto x, auto y, auto) { fill(x, y, steam, 2.f, 430); });
                heater(rx + rw / 2, low, 20000, 480);
                machine(right, high, 0, MA_ONI_COLONY_EXTRACT_GAS, 2, .2, 0);
                machine(left, low, steam, MA_ONI_COLONY_INJECT, 0, .2, 400);
                break;
            case 2: // hydrogen generators: gas in at one end, burnt at the other
                each([&](auto x, auto y, auto) { fill(x, y, hydrogen, .5f, 310); });
                machine(left, high, hydrogen, MA_ONI_COLONY_INJECT, 0, .02, 300);
                machine(right, low, 0, MA_ONI_COLONY_EXTRACT_GAS, 2, .02, 0);
                break;
            case 3: // water tank: pumped from the bottom, refilled from a vent above
                each([&](auto x, auto y, auto r) { if (r < 6) fill(x, y, water, 1000, 300); else fill(x, y, oxygen, 1.2f, 300); });
                machine(left, low, 0, MA_ONI_COLONY_EXTRACT_LIQUID, 2, 2, 0);
                machine(right, ry + 7, water, MA_ONI_COLONY_INJECT, 0, 2, 300);
                break;
            case 4: // refinery exhaust: natural gas over carbon dioxide, both pumped away
                each([&](auto x, auto y, auto r) { if (r < 4) fill(x, y, carbon_dioxide, 1.5f, 320); else fill(x, y, methane, .5f, 320); });
                machine(rx + rw / 2, ry + 2, methane, MA_ONI_COLONY_INJECT, 0, .018, 348);
                machine(rx + rw / 2 + 1, ry + 2, carbon_dioxide, MA_ONI_COLONY_INJECT, 0, .004, 348);
                machine(right, high, 0, MA_ONI_COLONY_EXTRACT_GAS, 2, .018, 0);
                machine(left, low, 0, MA_ONI_COLONY_EXTRACT_GAS, 2, .004, 0);
                break;
            case 5: // cooling pool: heated at one end and cooled at the other
                each([&](auto x, auto y, auto r) { if (r < 4) fill(x, y, water, 1000, 305); else fill(x, y, oxygen, 1.2f, 305); });
                heater(left, low, 30000, 340);
                heater(right, low, -30000, 0);
                break;
            case 6: // ranch in chlorine, visited by a duplicant
                each([&](auto x, auto y, auto) { fill(x, y, chlorine, 1.5f, 300); });
                chlorine_rooms.push_back({rx + 1, rx + rw - 1, ry});
                break;
            default: // oil over water, the oil pumped and returned
                each([&](auto x, auto y, auto r) { if (r < 2) fill(x, y, water, 1000, 310); else if (r < 5) fill(x, y, crude_oil, 870, 310); else fill(x, y, carbon_dioxide, 1.2f, 310); });
                machine(left, ry + 3, 0, MA_ONI_COLONY_EXTRACT_LIQUID, 1, 1, 0);
                machine(right, ry + 6, crude_oil, MA_ONI_COLONY_INJECT, 0, 1, 312);
                break;
            }
        }
    }
    // Mined-out caverns beside the base, still being dug.
    std::vector<std::array<std::uint32_t, 3>> caverns; // x0, x1, floor row
    const std::uint32_t margin = W * 3 / 100 + 1;
    for (const auto& [cx0, cx1] : {std::pair{margin, bx0 - 3}, std::pair{bx1 + 3, W - margin}}) {
        if (cx1 < cx0 + 12 || by0 + 15 > by1) continue;
        const std::uint32_t cy0 = by0 + 5, cy1 = std::min(by0 + 35, by1);
        for (std::uint32_t y = cy0; y < cy1; ++y)
            for (std::uint32_t x = cx0; x < cx1; ++x) fill(x, y, y < cy0 + (cy1 - cy0) / 3 ? carbon_dioxide : oxygen, y < cy0 + (cy1 - cy0) / 3 ? .5f : .4f, 290);
        caverns.push_back({cx0, cx1, cy0});
        if (w.main) for (std::uint32_t x = cx0; x < cx1; ++x) l.digs.push_back(at(x, cy1));
    }
    // Natural caves: gas pockets, some over a pool, a few with a tamed geyser.
    const std::uint32_t attempts = W * H / 40;
    for (std::uint32_t n = 0; n < attempts; ++n) {
        const std::uint32_t cw = rng.range(6, 28), ch = rng.range(3, 9);
        if (cw + 6 > W || ch + 18 > gy) continue;
        const std::uint32_t cx = rng.range(2, W - cw - 3), cy = rng.range(3, gy - 12 - ch);
        const auto pick = rng.next();
        bool free = true;
        for (std::uint32_t y = cy - 1; y <= cy + ch && free; ++y)
            for (std::uint32_t x = cx - 1; x <= cx + cw && free; ++x) free = solid(x, y);
        if (!free) continue;
        const std::uint32_t species[] = {oxygen, carbon_dioxide, hydrogen, chlorine, methane, oxygen};
        const float masses[] = {1.2f, 1.6f, .08f, 1.4f, .7f, .9f};
        const auto g = static_cast<std::uint32_t>(pick % 6);
        const float t = 350.f - 80.f * static_cast<float>(cy) / static_cast<float>(H);
        const bool pool = (pick >> 8) % 100 < 35;
        const std::uint32_t liquid = cy < H / 6 ? crude_oil : (pick >> 16) % 2 ? water : dirty_water;
        for (std::uint32_t y = cy; y < cy + ch; ++y)
            for (std::uint32_t x = cx; x < cx + cw; ++x) {
                const bool corner = (x < cx + 2 || x + 2 >= cx + cw) && (y == cy || y + 1 == cy + ch);
                if (corner && rng.below(2)) continue;
                if (pool && y < cy + ch / 3) fill(x, y, liquid, liquid == crude_oil ? 870.f : 1000.f, t);
                else fill(x, y, species[g], masses[g], t);
            }
        if ((pick >> 24) % 100 < 12 && cw >= 10) {
            // A geyser erupting 30 % of the time, and a pump taking as much away.
            const std::uint32_t gases[] = {steam, methane, carbon_dioxide, hydrogen};
            const double rates[] = {.3, .02, .03, .02}, hot[] = {383, 423, 500, 773};
            const auto k = static_cast<std::uint32_t>((pick >> 32) % 4);
            const auto phase = static_cast<std::uint32_t>((pick >> 40) % 1500);
            machine(cx + cw / 2, cy + ch - 2, gases[k], MA_ONI_COLONY_INJECT, 0, rates[k], hot[k], 1500, 450, phase);
            machine(cx + 2, cy + ch - 2, 0, MA_ONI_COLONY_EXTRACT_GAS, 2, rates[k], 0, 1500, 450, phase);
        }
    }
    // Duplicants: most pace a living floor, some a cavern or a ranch.
    for (std::uint32_t d = 0; d < duplicants; ++d) {
        if (d % 5 == 4 && !caverns.empty()) { const auto& c = caverns[(d / 5) % caverns.size()]; l.walkers.push_back({w.y0 + c[2] + 1, w.x0 + c[0] + 1, w.x0 + c[1] - 2, d * 37}); }
        else if (d % 5 == 3 && !chlorine_rooms.empty()) { const auto& c = chlorine_rooms[(d / 5) % chlorine_rooms.size()]; l.walkers.push_back({w.y0 + c[2] + 1, w.x0 + c[0], w.x0 + c[1] - 1, d * 37}); }
        else if (!floors.empty()) l.walkers.push_back({w.y0 + floors[1 + d % (floors.size() - 1)] + 1, w.x0 + bx0 + 1, w.x0 + bxm - 2, d * 37});
    }
}
// The worlds of a preset, packed side by side as the game packs a cluster.
Layout generate_late(const std::string& preset, std::uint32_t duplicants, bool vented) {
    std::vector<World> worlds;
    if (preset == "late-base") worlds = {{0, 0, 256, 384, true}};
    else if (preset == "late-largest") worlds = {{0, 0, 384, 384, true}};
    else if (preset == "late-spacedout")
        // The classic-size start asteroid and seven more (170,000 cells of worlds).
        worlds = {{0, 0, 240, 380, true}, {242, 0, 160, 176, false}, {242, 178, 80, 174, false}, {324, 178, 78, 174, false},
                  {404, 0, 96, 96, false}, {404, 98, 96, 96, false}, {404, 196, 96, 96, false}, {404, 294, 96, 96, false}};
    else if (preset == "late-smoke") worlds = {{0, 0, 96, 120, true}};
    else throw std::invalid_argument("Unknown late preset");
    Layout l;
    l.late = true;
    for (const auto& w : worlds) { l.width = std::max(l.width, w.x0 + w.w); l.height = std::max(l.height, w.y0 + w.h); }
    l.cells.assign(static_cast<std::size_t>(l.width) * l.height, {});
    for (std::uint32_t y = 0; y < l.height; ++y)
        for (std::uint32_t x = 0; x < l.width; ++x) wall(l, x, y);
    Random rng{0x4d41636f6c6f6e79ULL};
    // Three in five duplicants live on the main asteroid; the rest share the others.
    const auto others = static_cast<std::uint32_t>(worlds.size() - 1);
    const std::uint32_t away = others ? duplicants * 2 / 5 : 0;
    std::uint32_t n = 0;
    for (const auto& w : worlds) {
        const std::uint32_t here = w.main ? duplicants - away : away / others + (n < away % others ? 1U : 0U);
        if (!w.main) ++n;
        late_world(l, w, rng, here, vented && (w.main || n == 1));
    }
    return l;
}

// Per-frame inputs: duplicants breathe and walk, machines run.
struct Inputs {
    std::vector<MAOniColonyTransfer> transfers;
    std::vector<MAOniColonyHeat> heat;
};
MAOniColonyTransfer transfer(std::uint32_t cell, std::uint32_t native, std::uint32_t kind, std::uint32_t radius, double kg, double t, std::int32_t callback) {
    MAOniColonyTransfer e{};
    e.public_cell = cell; e.native_index = native; e.kind = kind; e.radius = radius; e.callback = callback;
    e.mass_kg = kg; e.temperature_k = t;
    return e;
}
Inputs inputs(const Layout& l, std::uint32_t duplicants, std::uint64_t frame) {
    Inputs in;
    std::int32_t callback = 0;
    if (l.late) {
        const auto on = [&](std::uint32_t period, std::uint32_t active, std::uint32_t phase) { return (frame + phase) % period < active; };
        for (const auto& d : l.walkers) {
            // Half a cell a frame: 2.5 cells a second.
            const auto span = d.x1 - d.x0, step = static_cast<std::uint32_t>((d.phase + frame / 2) % (2 * span));
            const auto x = d.x0 + (step < span ? step : 2 * span - step - 1);
            in.transfers.push_back(transfer(l.at(x, d.y), oxygen, MA_ONI_COLONY_EXTRACT, 2, .02, 0, callback++));
            in.transfers.push_back(transfer(l.at(x, d.y + 1), carbon_dioxide, MA_ONI_COLONY_INJECT, 0, .0004, 310, callback++));
        }
        for (const auto& e : l.emitters)
            if (on(e.period, e.active, e.phase)) { in.transfers.push_back(e.transfer); in.transfers.back().callback = callback++; }
        for (const auto& h : l.heaters)
            if (on(h.period, h.active, h.phase)) in.heat.push_back(h.heat);
        // Solids beside the base's floors trade a little heat with them every frame.
        for (std::size_t n = 0; n < l.passive_heat.size(); ++n)
            in.heat.push_back({l.passive_heat[n], MA_ONI_COLONY_HEAT_PASSIVE, ((n + frame) & 1U) ? 40. : -40., 0});
        return in;
    }
    std::vector<const Room*> living;
    for (const auto& r : l.rooms) if (r.type <= 1) living.push_back(&r);
    for (std::uint32_t d = 0; d < duplicants && !living.empty(); ++d) {
        const auto& r = *living[(d * 5) % living.size()];
        const auto span = r.x1 - r.x0 - 4, step = static_cast<std::uint32_t>((d * 7 + frame) % (2 * span));
        const auto x = r.x0 + 2 + (step < span ? step : 2 * span - step - 1), y = r.y0 + 4;
        // 100 g/s oxygen in, 2 g/s carbon dioxide out, in a 0.2 s frame.
        in.transfers.push_back(transfer(l.at(x, y), oxygen, MA_ONI_COLONY_EXTRACT, 2, .02, 0, callback++));
        in.transfers.push_back(transfer(l.at(x, y + 1), carbon_dioxide, MA_ONI_COLONY_INJECT, 0, .0004, 310, callback++));
    }
    for (std::size_t n = 0; n < l.rooms.size(); n += 3) {
        const auto& r = l.rooms[n];
        const auto mid = (r.x0 + r.x1) / 2;
        switch (r.type) {
        case 0: case 1: case 5: // electrolyser, gas pump, scrubber
            in.transfers.push_back(transfer(l.at(mid, r.y0 + 10), oxygen, MA_ONI_COLONY_INJECT, 0, .1776, 343.15, callback++));
            in.transfers.push_back(transfer(l.at(mid + 1, r.y0 + 10), hydrogen, MA_ONI_COLONY_INJECT, 0, .0224, 343.15, callback++));
            in.transfers.push_back(transfer(l.at(r.x1 - 3, r.y1 - 2), 0, MA_ONI_COLONY_EXTRACT_GAS, 2, .1, 0, callback++));
            if (r.type == 0) in.transfers.push_back(transfer(l.at(r.x0 + 3, r.y0 + 1), carbon_dioxide, MA_ONI_COLONY_EXTRACT, 3, .02, 0, callback++));
            if (r.type == 1) in.heat.push_back({l.at(r.x0 + 4, r.y0 + 4), 0, 1800, 350});
            break;
        case 2: case 3: // liquid pump and its outlet
            in.transfers.push_back(transfer(l.at(r.x0 + 3, r.y0 + 1), 0, MA_ONI_COLONY_EXTRACT_LIQUID, 2, 2, 0, callback++));
            in.transfers.push_back(transfer(l.at(r.x1 - 4, r.y0 + 10), water, MA_ONI_COLONY_INJECT, 0, 2, 300, callback++));
            if (r.type == 3) in.heat.push_back({l.at(mid, r.y0 + 1), 0, 3600, 400});
            break;
        default: // oil well
            in.transfers.push_back(transfer(l.at(r.x0 + 3, r.y0 + 1), 0, MA_ONI_COLONY_EXTRACT_LIQUID, 2, 1, 0, callback++));
            in.transfers.push_back(transfer(l.at(r.x1 - 4, r.y0 + 6), crude_oil, MA_ONI_COLONY_INJECT, 0, 1, 310, callback++));
            break;
        }
    }
    return in;
}

// Full synthetic actor census: the existing breathing and machine input cells,
// including idle/refused actors. No new physical inputs or all-cell actors.
std::vector<std::uint32_t> actor_cells(const Inputs& in) {
    std::vector<std::uint32_t> out;
    for (const auto& e : in.transfers) out.push_back(e.public_cell);
    for (const auto& e : in.heat) if (e.reserved != MA_ONI_COLONY_HEAT_PASSIVE) out.push_back(e.public_cell);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

struct Summary { double mean = 0, p50 = 0, p95 = 0, maximum = 0; };
Summary summarize(std::vector<double> values) {
    Summary s;
    if (values.empty()) return s;
    double sum = 0;
    for (double v : values) sum += v;
    std::sort(values.begin(), values.end());
    const auto rank = [&](double q) { return values[static_cast<std::size_t>(std::ceil(q * static_cast<double>(values.size()))) - 1]; };
    s.mean = sum / static_cast<double>(values.size());
    s.p50 = rank(.5);
    s.p95 = rank(.95);
    s.maximum = values.back();
    return s;
}
std::string json(const Summary& s) {
    char text[256];
    std::snprintf(text, sizeof text, "{\"mean\": %.3f, \"p50\": %.3f, \"p95\": %.3f, \"max\": %.3f}", s.mean, s.p50, s.p95, s.maximum);
    return text;
}

struct Options {
    std::uint32_t rooms_x = 8, rooms_y = 13, frames = 60, warmup = 5, workers = 0, duplicants = 32;
    std::uint32_t pressure_iterations = 0; // 0: the default saved cap
    bool events = false;
    std::uint32_t profile = 9;
    bool always_simulate = false, explicit_profile = false;
    // Hydrogen under chlorine in every sixth room: fast sound and strong
    // buoyancy (the stiffest pressure systems); --no-hydrogen-rooms uses CO2 over O2.
    bool hydrogen_rooms = true;
    double budget_ms = 200;          // one 0.2 s frame at 1x
    std::uint32_t sustain_frames = 25; // recovery: this many consecutive frames within budget
    double event_limit_s = 60;       // wall seconds watched after each event
    std::string json_path, world_dump;
    // --preset late-*: a generated late-game colony; --no-vented-hall leaves
    // out its hall leaking to space; a dig every --dig-frames frames (0: none).
    std::string late_preset;
    bool vented_hall = true;
    std::uint32_t dig_frames = 25;
    // --owner-replay OWNER JOURNAL DISEASES: a saved owner fed its journaled inputs.
    std::string replay_owner, replay_journal, replay_diseases;
    // Experiments on a replay (docs/performance-acceptance.md): option
    // overrides applied after the first replayed frame, and the final
    // published cell views written out for comparison with another run.
    std::vector<std::pair<std::string, double>> replay_set;
    std::string views_dump;
    std::uint32_t top_regions = 12;
    // The part of ONI's native frame that precedes each hand-off to the
    // owner (measured in game on the Linux harness, 18 to 27 ms); only the
    // estimate of game speed uses it.
    double native_overhead_ms = 25;
};
Options parse(int argc, char** argv) {
    Options o;
    const auto number = [&](int& i) {
        if (i + 1 >= argc) throw std::invalid_argument(std::string("Missing value for ") + argv[i]);
        return std::strtod(argv[++i], nullptr);
    };
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--preset") {
            if (i + 1 >= argc) throw std::invalid_argument("Missing preset");
            const std::string p = argv[++i];
            if (p == "100k") { o.rooms_x = 8; o.rooms_y = 13; }
            else if (p == "250k") { o.rooms_x = 13; o.rooms_y = 20; }
            else if (p == "smoke") { o.rooms_x = 2; o.rooms_y = 2; o.frames = 3; o.warmup = 1; o.duplicants = 4; }
            else if (p == "late-smoke") { o.late_preset = p; o.frames = 6; o.warmup = 2; o.duplicants = 4; }
            else if (p == "late-base" || p == "late-largest" || p == "late-spacedout") { o.late_preset = p; o.frames = 300; o.warmup = 50; o.duplicants = 48; }
            else throw std::invalid_argument("Unknown preset");
        } else if (a == "--profile") {
            if (i + 1 >= argc) throw std::invalid_argument("Missing profile");
            const std::string p = argv[++i];
            if (p != "8" && p != "9") throw std::invalid_argument("Profile must be 8 or 9");
            o.profile = p == "9" ? 9U : 8U; o.explicit_profile = true;
        } else if (a == "--always-simulate") o.always_simulate = true;
        else if (a == "--rooms") { o.rooms_x = static_cast<std::uint32_t>(number(i)); o.rooms_y = static_cast<std::uint32_t>(number(i)); }
        else if (a == "--frames") o.frames = static_cast<std::uint32_t>(number(i));
        else if (a == "--warmup") o.warmup = static_cast<std::uint32_t>(number(i));
        else if (a == "--workers") o.workers = static_cast<std::uint32_t>(number(i));
        else if (a == "--duplicants") o.duplicants = static_cast<std::uint32_t>(number(i));
        else if (a == "--pressure-iterations") o.pressure_iterations = static_cast<std::uint32_t>(number(i));
        else if (a == "--events") o.events = true;
        else if (a == "--no-hydrogen-rooms") o.hydrogen_rooms = false;
        else if (a == "--no-vented-hall") o.vented_hall = false;
        else if (a == "--dig-frames") o.dig_frames = static_cast<std::uint32_t>(number(i));
        else if (a == "--budget-ms") o.budget_ms = number(i);
        else if (a == "--sustain-frames") o.sustain_frames = static_cast<std::uint32_t>(number(i));
        else if (a == "--event-limit-s") o.event_limit_s = number(i);
        else if (a == "--json") { if (i + 1 >= argc) throw std::invalid_argument("Missing path"); o.json_path = argv[++i]; }
        else if (a == "--world-dump") { if (i + 1 >= argc) throw std::invalid_argument("Missing path"); o.world_dump = argv[++i]; }
        else if (a == "--owner-replay") {
            if (i + 3 >= argc) throw std::invalid_argument("--owner-replay needs OWNER JOURNAL DISEASES");
            o.replay_owner = argv[++i]; o.replay_journal = argv[++i]; o.replay_diseases = argv[++i];
        }
        else if (a == "--set") {
            if (i + 1 >= argc) throw std::invalid_argument("--set needs NAME=VALUE");
            const std::string text = argv[++i]; const auto eq = text.find('=');
            if (eq == std::string::npos) throw std::invalid_argument("--set needs NAME=VALUE");
            o.replay_set.push_back({text.substr(0, eq), std::stod(text.substr(eq + 1))});
        }
        else if (a == "--views-dump") { if (i + 1 >= argc) throw std::invalid_argument("Missing path"); o.views_dump = argv[++i]; }
        else if (a == "--top-regions") o.top_regions = static_cast<std::uint32_t>(number(i));
        else if (a == "--native-overhead-ms") o.native_overhead_ms = number(i);
        else throw std::invalid_argument("Unknown option " + a);
    }
    if (o.rooms_x < 1 || o.rooms_y < 1 || o.rooms_x > 24 || o.rooms_y > 40 || o.frames < 1 || o.warmup >= o.frames)
        throw std::invalid_argument("Invalid rooms, frames or warmup");
    if (!o.late_preset.empty() && (o.events || o.profile != 9)) throw std::invalid_argument("A late-game colony runs the game's own profile, without --events");
    if (o.profile == 8 && o.always_simulate) throw std::invalid_argument("Profile 8 already always simulates; omit --always-simulate");
    if (!o.world_dump.empty() && (o.explicit_profile || o.always_simulate))
        throw std::invalid_argument("A world dump keeps its saved profile and activity policy");
    if (!o.replay_owner.empty() && (o.explicit_profile || o.always_simulate || !o.world_dump.empty()))
        throw std::invalid_argument("An owner replay keeps its saved profile and activity policy");
    return o;
}
void emit(const Options& o, const std::string& text) {
    if (o.json_path.empty()) { std::printf("%s\n", text.c_str()); return; }
    std::ofstream out(o.json_path, std::ios::binary);
    out << text << '\n';
    if (!out) throw std::runtime_error("Could not write the JSON result");
}

// Private world dumps (MA_COLONY_DUMP_FRAME / MA_COLONY_FAULT_DUMP): the
// flow advance alone, without owner inputs, ledgers or publication.
int replay(const Options& o) {
    std::ifstream f(o.world_dump, std::ios::binary);
    const std::vector<char> bytes((std::istreambuf_iterator<char>(f)), {});
    if (bytes.size() < 12) throw std::runtime_error("Invalid world dump");
    std::uint32_t count = 0;
    std::memcpy(&count, bytes.data(), 4);
    const auto header = 4 + static_cast<std::size_t>(count) * sizeof(MAOniColonyMaterial) + 8;
    if (count == 0 || bytes.size() <= header) throw std::runtime_error("Invalid world dump");
    std::vector<MAOniColonyMaterial> records(count);
    std::memcpy(records.data(), bytes.data() + 4, count * sizeof(MAOniColonyMaterial));
    double seconds = 0;
    std::memcpy(&seconds, bytes.data() + header - 8, 8);
    std::vector<ma::oni::GameFluidElement> elements;
    for (const auto& m : records) {
        ma::oni::GameFluidElement e;
        e.id = m.id; e.hash = m.element_hash; e.native_index = m.native_index; e.gas = m.phase == 1;
        e.molar_mass_kg_per_mol = m.molar_mass_kg_per_mol; e.specific_heat_j_per_kg_k = m.specific_heat_j_per_kg_k;
        e.conductivity_w_per_m_k = m.conductivity_w_per_m_k; e.density_kg_per_m3 = m.density_kg_per_m3;
        e.low_temperature_k = m.low_temperature_k; e.high_temperature_k = m.high_temperature_k;
        e.low_target = m.low_target; e.high_target = m.high_target; e.low_byproduct = m.low_byproduct; e.high_byproduct = m.high_byproduct;
        e.low_byproduct_fraction = m.low_byproduct_fraction; e.high_byproduct_fraction = m.high_byproduct_fraction;
        elements.push_back(e);
    }
    const auto catalogue = ma::oni::build_catalogue(elements);
    const std::vector<std::uint8_t> image(bytes.begin() + static_cast<std::ptrdiff_t>(header), bytes.end());
    using namespace ma::experimental;
    auto world = MacClosedWorld::from_checkpoint(catalogue.registry, catalogue.eos, image);
    auto options = mac_kernel_default_options();
    options.threads = o.workers ? o.workers : c::physical_core_count();
    set_mac_kernel_default_options(options);
    std::size_t fluid = 0;
    for (std::size_t i = 0; i < world.state().cells.size(); ++i)
        if (!world.state().grid.walls[i] && world.state().cells[i].mixture.mass() > 0) ++fluid;
    std::vector<double> times, sparse, tick, eos, pressure, transport, layering, interface, heat, phase, audit, dense, iterations, attempts, refinements, refused, r_setup, r_capture, r_run, r_merge, r_rebuild;
    for (std::uint32_t n = 0; n < o.frames; ++n) {
        const auto start = Clock::now();
        auto advanced = world.advanced(seconds, 65536, MacExecutionPolicy::production);
        world = std::move(advanced.world);
        times.push_back(since(start));
        const auto& g = advanced.report.game_stages;
        for (const auto& [out, value] : {std::pair{&sparse, g.sparse_ms}, {&tick, g.tick_ms}, {&eos, g.eos_ms}, {&pressure, g.pressure_ms},
                                  {&transport, g.transport_ms}, {&layering, g.layering_ms}, {&interface, g.interface_ms},
                                  {&heat, g.heat_ms}, {&phase, g.phase_ms}, {&audit, g.audit_ms}, {&dense, g.dense_ms},
                                  {&iterations, static_cast<double>(g.pressure_iterations)},
                                  {&attempts, static_cast<double>(g.pressure_attempts)}, {&refinements, static_cast<double>(g.pressure_refinements)},
                                  {&refused, g.refused_trial_ms}, {&r_setup, g.regional_setup_ms}, {&r_capture, g.regional_capture_ms},
                                  {&r_run, g.regional_run_ms}, {&r_merge, g.regional_merge_ms}, {&r_rebuild, g.regional_rebuild_ms}})
            out->push_back(value);
    }
    const auto warm_part = [&](const std::vector<double>& v) { return summarize(std::vector<double>(v.begin() + o.warmup, v.end())); };
    const auto image_after = world.checkpoint();
    std::uint64_t hash = 1469598103934665603ULL;
    for (auto b : image_after) { hash ^= b; hash *= 1099511628211ULL; }
    const auto warm = summarize(std::vector<double>(times.begin() + o.warmup, times.end()));
    std::ostringstream out;
    char line[160];
    std::snprintf(line, sizeof line, "%016llx", static_cast<unsigned long long>(hash));
    out << "{\"mode\": \"world-dump\", \"profile_source\": \"checkpoint\", \"profile\": " << static_cast<unsigned>(world.settings().game_profile)
        << ", \"activity_tiers_enabled\": " << (world.settings().game.activity_tiers ? "true" : "false")
        << ", \"cells\": " << world.state().cells.size() << ", \"fluid_cells\": " << fluid
        << ", \"frame_seconds\": " << seconds << ", \"workers\": " << options.threads << ", \"frames\": " << o.frames
        << ", \"warmup\": " << o.warmup << ", \"flow_ms\": " << json(warm) << ", \"stages_ms\": {";
    const std::pair<const char*, const std::vector<double>*> parts[] = {{"sparse", &sparse}, {"tick", &tick}, {"eos", &eos},
        {"pressure", &pressure}, {"transport", &transport}, {"layering", &layering}, {"interface", &interface}, {"heat", &heat},
        {"phase", &phase}, {"audit_and_other", &audit}, {"dense", &dense}, {"pressure_iterations", &iterations},
        {"pressure_attempts", &attempts}, {"pressure_refinements", &refinements}, {"refused_trial", &refused},
        {"regional_setup", &r_setup}, {"regional_capture", &r_capture}, {"regional_run", &r_run}, {"regional_merge", &r_merge}, {"regional_rebuild", &r_rebuild}};
    for (std::size_t n = 0; n < std::size(parts); ++n) out << (n ? ", " : "") << "\"" << parts[n].first << "\": " << json(warm_part(*parts[n].second));
    out << "}, \"final_image_fnv\": \"" << line << "\"}";
    emit(o, out.str());
    return 0;
}

// What the flow stage's regions cost over the measured frames, and how the
// kernel's own stage timers add up (performance tools only).
struct FlowTally {
    struct Region { double ms = 0, seconds = 0, worst_ms = 0; std::uint64_t frames = 0, substeps = 0, attempts = 0, refinements = 0, iterations = 0;
        double pressure_ms = 0, transport_ms = 0, layering_ms = 0, interface_ms = 0, heat_ms = 0, phase_ms = 0, audit_ms = 0;
        std::size_t x = 0, y = 0, width = 0, height = 0, cells = 0; };
    std::map<std::size_t, Region> regions;
    std::vector<double> stepped_regions, stepped_cells, region_sum_ms, region_max_ms, run_ms, tick_ms, sparse_ms, dense_ms, setup_ms, capture_ms, merge_ms, rebuild_ms,
        pressure_ms, transport_ms, layering_ms, interface_ms, heat_ms, phase_ms, audit_ms, eos_ms, refused_ms;
    std::uint64_t iterations = 0, attempts = 0, refinements = 0, geometric = 0, direct = 0, intervals = 0, frames = 0;
    void add(const c::FlowDiagnostics& d) {
        ++frames;
        double sum = 0, worst = 0, cells = 0;
        std::map<std::size_t, double> frame_ms;
        for (const auto& r : d.regions) {
            auto& t = regions[r.representative];
            t.ms += r.ms; t.seconds += r.seconds; t.substeps += r.substeps; t.attempts += r.attempts; t.refinements += r.refinements; t.iterations += r.iterations;
            t.pressure_ms += r.pressure_ms; t.transport_ms += r.transport_ms; t.layering_ms += r.layering_ms; t.interface_ms += r.interface_ms;
            t.heat_ms += r.heat_ms; t.phase_ms += r.phase_ms; t.audit_ms += r.audit_ms;
            t.x = r.x; t.y = r.y; t.width = r.width; t.height = r.height; t.cells = r.cells;
            frame_ms[r.representative] += r.ms; sum += r.ms; cells += static_cast<double>(r.cells); intervals += r.substeps;
        }
        for (const auto& [key, value] : frame_ms) { auto& t = regions[key]; ++t.frames; t.worst_ms = std::max(t.worst_ms, value); worst = std::max(worst, value); }
        stepped_regions.push_back(static_cast<double>(frame_ms.size())); stepped_cells.push_back(cells); region_sum_ms.push_back(sum); region_max_ms.push_back(worst);
        const auto& g = d.stages;
        run_ms.push_back(g.regional_run_ms); tick_ms.push_back(g.tick_ms); sparse_ms.push_back(g.sparse_ms); dense_ms.push_back(g.dense_ms);
        setup_ms.push_back(g.regional_setup_ms); capture_ms.push_back(g.regional_capture_ms); merge_ms.push_back(g.regional_merge_ms); rebuild_ms.push_back(g.regional_rebuild_ms);
        pressure_ms.push_back(g.pressure_ms); transport_ms.push_back(g.transport_ms); layering_ms.push_back(g.layering_ms); interface_ms.push_back(g.interface_ms);
        heat_ms.push_back(g.heat_ms); phase_ms.push_back(g.phase_ms); audit_ms.push_back(g.audit_ms); eos_ms.push_back(g.eos_ms); refused_ms.push_back(g.refused_trial_ms);
        iterations += g.pressure_iterations; attempts += g.pressure_attempts; refinements += g.pressure_refinements; geometric += g.pressure_geometric; direct += g.pressure_direct;
    }
    std::string json_text(std::uint32_t top, std::size_t grid_width) const {
        std::ostringstream out;
        const auto per_frame = [&](std::uint64_t n) { return frames ? static_cast<double>(n) / static_cast<double>(frames) : 0.; };
        out << "{\"scope\": \"wall ms per frame; region and kernel-stage figures are summed over regions that run side by side, so they can exceed the wall time\""
            << ", \"wall_ms\": {\"sparse\": " << json(summarize(sparse_ms)) << ", \"tick\": " << json(summarize(tick_ms)) << ", \"dense\": " << json(summarize(dense_ms))
            << ", \"regional_setup\": " << json(summarize(setup_ms)) << ", \"regional_capture\": " << json(summarize(capture_ms))
            << ", \"regional_run\": " << json(summarize(run_ms)) << ", \"regional_merge\": " << json(summarize(merge_ms))
            << ", \"regional_rebuild\": " << json(summarize(rebuild_ms)) << "}"
            << ", \"summed_region_ms\": {\"all_regions\": " << json(summarize(region_sum_ms)) << ", \"slowest_region\": " << json(summarize(region_max_ms))
            << ", \"pressure\": " << json(summarize(pressure_ms)) << ", \"transport\": " << json(summarize(transport_ms))
            << ", \"layering\": " << json(summarize(layering_ms)) << ", \"interface\": " << json(summarize(interface_ms))
            << ", \"heat\": " << json(summarize(heat_ms)) << ", \"phase\": " << json(summarize(phase_ms))
            << ", \"audit_and_other\": " << json(summarize(audit_ms)) << ", \"eos\": " << json(summarize(eos_ms))
            << ", \"refused_trials\": " << json(summarize(refused_ms)) << "}"
            << ", \"stepped_regions\": " << json(summarize(stepped_regions)) << ", \"stepped_cells\": " << json(summarize(stepped_cells))
            << ", \"per_frame\": {\"flow_intervals\": " << per_frame(intervals) << ", \"pressure_attempts\": " << per_frame(attempts)
            << ", \"pressure_refinements\": " << per_frame(refinements) << ", \"pressure_iterations\": " << per_frame(iterations)
            << ", \"geometric_solves\": " << per_frame(geometric) << ", \"direct_solves\": " << per_frame(direct) << "}, \"top_regions\": [";
        std::vector<std::pair<std::size_t, Region>> order(regions.begin(), regions.end());
        std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.second.ms != b.second.ms ? a.second.ms > b.second.ms : a.first < b.first; });
        for (std::size_t n = 0; n < order.size() && n < top; ++n) {
            const auto& [key, r] = order[n];
            char text[640];
            const double f = frames ? static_cast<double>(frames) : 1.;
            std::snprintf(text, sizeof text,
                "%s{\"cell\": %zu, \"x\": %zu, \"y\": %zu, \"box\": [%zu, %zu], \"box_origin\": [%zu, %zu], \"cells\": %zu, \"frames_stepped\": %llu, \"ms_per_frame\": %.3f, \"worst_frame_ms\": %.3f, "
                "\"game_seconds_per_frame\": %.4f, \"intervals_per_frame\": %.2f, \"attempts_per_frame\": %.2f, \"refinements_per_frame\": %.2f, \"iterations_per_frame\": %.1f, "
                "\"ms\": {\"pressure\": %.3f, \"transport\": %.3f, \"layering\": %.3f, \"interface\": %.3f, \"heat\": %.3f, \"phase\": %.3f, \"audit_and_other\": %.3f}}",
                n ? ", " : "", key, grid_width ? key % grid_width : 0, grid_width ? key / grid_width : 0, r.width, r.height, r.x, r.y, r.cells,
                static_cast<unsigned long long>(r.frames), r.ms / f, r.worst_ms, r.seconds / f, static_cast<double>(r.substeps) / f,
                static_cast<double>(r.attempts) / f, static_cast<double>(r.refinements) / f, static_cast<double>(r.iterations) / f,
                r.pressure_ms / f, r.transport_ms / f, r.layering_ms / f, r.interface_ms / f, r.heat_ms / f, r.phase_ms / f, r.audit_ms / f);
            out << text;
        }
        out << "]}";
        return out.str();
    }
};
// Game seconds per real second for an owner frame of this length: overlapped
// pacing posts the next tick once the native frame before the hand-off has
// run, so a tick takes the owner frame plus that part of the native frame.
// An estimate from Linux harness runs, not a measurement of the game.
double estimated_speed(double owner_ms, double native_overhead_ms, double frame_seconds) {
    return owner_ms + native_overhead_ms > 0 ? 1000 * frame_seconds / (owner_ms + native_overhead_ms) : 0;
}

// A saved owner fed the inputs the game journaled after that save
// (MA_COLONY_INPUT_JOURNAL_DIR): the owner's whole frame (inputs, flow,
// sinks, ledgers, projection, publication) under the game's own workload,
// without the game. The files are private captures and stay out of the
// repository.

// How the regional intervals of this process were dispatched (diagnostic).
std::string regional_execution_json() {
    namespace counts = ma::experimental::game_kernel::execution_counts;
    std::ostringstream out;
    out << "{\"intervals_by_regions_with_own_workers\": [";
    for (std::size_t i = 0; i < counts::intervals().size(); ++i) out << (i ? ", " : "") << counts::intervals()[i].load();
    out << "], \"inner_workers_handed_out\": " << counts::inner_threads().load() << "}";
    return out.str();
}
int owner_replay(const Options& o) {
    namespace jr = c::journal_replay;
    const auto saved = jr::read_file(o.replay_owner);
    if (saved.size() < 64) throw std::runtime_error("Truncated saved owner");
    const auto frames = jr::journal(o.replay_journal, std::string(saved.end() - 64, saved.end()));
    const auto diseases = jr::read_file(o.replay_diseases);
    if (diseases.size() < 12) throw std::runtime_error("Invalid disease table");
    std::uint32_t counts[3];
    std::memcpy(counts, diseases.data(), 12);
    if (diseases.size() != 12 + counts[0] * sizeof(MAOniColonyDisease) + counts[1] * sizeof(MAOniColonyDiseaseGrowth))
        throw std::runtime_error("Disease table size differs");
    std::vector<MAOniColonyDisease> table(counts[0]);
    std::vector<MAOniColonyDiseaseGrowth> growth(counts[1]);
    if (counts[0]) std::memcpy(table.data(), diseases.data() + 12, counts[0] * sizeof(MAOniColonyDisease));
    if (counts[1]) std::memcpy(growth.data(), diseases.data() + 12 + counts[0] * sizeof(MAOniColonyDisease), counts[1] * sizeof(MAOniColonyDiseaseGrowth));
    c::set_diseases(table, growth, counts[2]);
    c::set_worker_count(o.workers);
    c::enable_flow_diagnostics(true);
    const auto parts = jr::parse(saved);
    std::vector<double> owner_ms, wall_ms, stage[6], frame_seconds;
    FlowTally tally;
    std::uint64_t ticket = 0, measured = 0, first_frame = 0, last_frame = 0;
    auto mark = Clock::now();
    const auto advanced = jr::replay(saved, frames, o.frames, false, ticket, [&](std::uint64_t number, const jr::Frame& frame) {
        const double wall = since(mark);
        const auto status = c::status();
        if (!first_frame) {
            first_frame = number;
            if (!o.replay_set.empty()) {
                MAOniColonyOptions changed{};
                if (!c::copy_options(changed)) throw std::runtime_error("The replayed colony has no changeable options");
                for (const auto& [name, value] : o.replay_set) {
                    const auto u = static_cast<std::uint32_t>(value);
                    if (name == "flow_parts") changed.flow_parts = u;
                    else if (name == "pressure_relative_tolerance") changed.pressure_relative_tolerance = value;
                    else if (name == "maximum_timestep_s") changed.maximum_timestep_s = value;
                    else if (name == "fluid_tick_stride") changed.fluid_tick_stride = u;
                    else if (name == "pressure_maximum_iterations") changed.pressure_maximum_iterations = u;
                    else if (name == "sharpen_liquid_interfaces") changed.sharpen_liquid_interfaces = u;
                    else if (name == "liquid_stiffness_ratio") changed.liquid_stiffness_ratio = value;
                    else if (name == "interface_detail_fraction") changed.interface_detail_fraction = value;
                    else if (name == "gas_heat_mixing") changed.gas_heat_mixing = value;
                    else if (name == "phase_relaxation_time_s") changed.phase_relaxation_time_s = value;
                    else if (name == "trace_sink_threshold_kg") changed.trace_sink_threshold_kg = value;
                    else throw std::invalid_argument("Unknown --set option " + name);
                }
                if (!c::change_options(status.session, changed)) throw std::runtime_error("The replayed colony refused the --set options");
            }
        }
        last_frame = number;
        if (++measured > o.warmup) {
            owner_ms.push_back(status.last_advance_ms);
            wall_ms.push_back(wall);
            frame_seconds.push_back(frame.seconds);
            for (std::size_t k = 0; k < 6; ++k) stage[k].push_back(status.stage_ms[k]);
            const auto flow = c::flow_diagnostics();
            if (flow.frame == status.completed_frames) tally.add(flow);
        }
        mark = Clock::now();
    });
    if (advanced <= o.warmup) throw std::runtime_error("The journal covers no measured frame after the warm-up");
    const auto final_status = c::status();
    if (const char* box = std::getenv("MA_COLONY_REPLAY_DUMP_BOX")) {
        // "X Y W H": the final frame's cells of that rectangle on stderr, one
        // line a cell (constituents by native index), for looking at what a
        // slow region holds. A diagnostic; not part of any measurement.
        unsigned bx = 0, by = 0, bw = 0, bh = 0;
        if (std::sscanf(box, "%u %u %u %u", &bx, &by, &bw, &bh) != 4) throw std::invalid_argument("MA_COLONY_REPLAY_DUMP_BOX needs X Y W H");
        std::vector<MAOniColonyCellView> views(static_cast<std::size_t>(parts.config.width) * parts.config.height);
        auto shown = c::status(); c::copy(shown, views.data(), static_cast<std::uint32_t>(views.size()));
        for (unsigned y = by; y < by + bh; ++y) for (unsigned x = bx; x < bx + bw; ++x) {
            if (x < parts.config.offset_x || y < parts.config.offset_y || x >= parts.config.offset_x + parts.config.width || y >= parts.config.offset_y + parts.config.height) continue;
            const auto& v = views[(x - parts.config.offset_x) + static_cast<std::size_t>(y - parts.config.offset_y) * parts.config.width];
            std::uint32_t indices[8]{}; double masses[8]{};
            const auto n = (v.flags & MA_ONI_COLONY_VIEW_OWNED) ? c::composition(x + y * parts.config.grid_width, indices, masses, 8) : 0u;
            std::fprintf(stderr, "CELL %u %u flags %u element %u kg %.6g K %.2f Pa %.4g liquid %.3g :", x, y, v.flags, v.element_index, v.mass_kg, v.temperature_k, v.pressure_pa, v.liquid_fraction);
            for (std::uint32_t k = 0; k < std::min<std::uint32_t>(n, 8); ++k) std::fprintf(stderr, " %u=%.6g", indices[k], masses[k]);
            std::fputc('\n', stderr);
        }
    }
    if (!o.views_dump.empty()) {
        // width, height, then the final frame's cell views (MAOniColonyCellView).
        std::vector<MAOniColonyCellView> views(static_cast<std::size_t>(parts.config.width) * parts.config.height);
        auto shown = c::status(); c::copy(shown, views.data(), static_cast<std::uint32_t>(views.size()));
        std::ofstream dump(o.views_dump, std::ios::binary);
        const std::uint32_t size[2]{parts.config.width, parts.config.height};
        dump.write(reinterpret_cast<const char*>(size), sizeof size);
        dump.write(reinterpret_cast<const char*>(views.data()), static_cast<std::streamsize>(views.size() * sizeof(MAOniColonyCellView)));
        if (!dump) throw std::runtime_error("Could not write the views dump");
    }
    const auto image = c::checkpoint();
    std::uint64_t hash = 1469598103934665603ULL;
    for (auto b : image) { hash ^= b; hash *= 1099511628211ULL; }
    const auto workers = ma::experimental::mac_kernel_default_options().threads;
    MAOniColonyOptions replay_options{};
    const bool has_options = c::copy_options(replay_options);
    c::stop();
    const auto owner = summarize(owner_ms);
    double seconds = 0;
    for (double v : frame_seconds) seconds += v;
    seconds /= static_cast<double>(frame_seconds.size());
    std::ostringstream out;
    char digest[32], estimate[512];
    std::snprintf(digest, sizeof digest, "%016llx", static_cast<unsigned long long>(hash));
    out << "{\"mode\": \"owner-replay\", \"grid\": [" << parts.config.grid_width << ", " << parts.config.grid_height << "]"
        << ", \"workers\": " << workers << ", \"first_frame\": " << first_frame << ", \"last_frame\": " << last_frame
        << ", \"frames\": " << advanced << ", \"warmup\": " << o.warmup << ", \"frame_seconds\": " << seconds
        << ", \"owner_ms\": " << json(owner) << ", \"replay_wall_ms\": " << json(summarize(wall_ms))
        << ", \"stage_ms\": {\"terrain\": " << json(summarize(stage[0])) << ", \"heat\": " << json(summarize(stage[1]))
        << ", \"transfers\": " << json(summarize(stage[2])) << ", \"flow\": " << json(summarize(stage[3]))
        << ", \"space_sink\": " << json(summarize(stage[4])) << ", \"ledger_and_projection\": " << json(summarize(stage[5])) << "}"
        << ", \"flow\": " << tally.json_text(o.top_regions, parts.config.grid_width)
        << ", \"regional_execution\": " << regional_execution_json();
    std::snprintf(estimate, sizeof estimate,
        ", \"estimate\": {\"scope\": \"game seconds per real second if a tick takes the owner frame plus native_overhead_ms; calibrated on Linux harness runs, not measured in game\", "
        "\"native_overhead_ms\": %.1f, \"game_per_real_mean\": %.3f, \"game_per_real_p95_frame\": %.3f, \"owner_ms_budget_for_3x\": %.1f}",
        o.native_overhead_ms, estimated_speed(owner.mean, o.native_overhead_ms, seconds), estimated_speed(owner.p95, o.native_overhead_ms, seconds),
        1000 * seconds / 3 - o.native_overhead_ms);
    out << estimate;
    if (has_options) out << ", \"fluid_tick_stride\": " << replay_options.fluid_tick_stride
        << ", \"options\": {\"flow_parts\": " << replay_options.flow_parts << ", \"maximum_timestep_s\": " << replay_options.maximum_timestep_s
        << ", \"pressure_relative_tolerance\": " << replay_options.pressure_relative_tolerance
        << ", \"pressure_maximum_iterations\": " << replay_options.pressure_maximum_iterations
        << ", \"sharpen_liquid_interfaces\": " << replay_options.sharpen_liquid_interfaces
        << ", \"interface_detail_fraction\": " << replay_options.interface_detail_fraction
        << ", \"liquid_stiffness_ratio\": " << replay_options.liquid_stiffness_ratio
        << ", \"gas_heat_mixing\": " << replay_options.gas_heat_mixing
        << ", \"phase_relaxation_time_s\": " << replay_options.phase_relaxation_time_s
        << ", \"trace_sink_threshold_kg\": " << replay_options.trace_sink_threshold_kg
        << ", \"evaporation_below_boiling\": " << replay_options.evaporation_below_boiling << "}";
    char errors[256];
    std::snprintf(errors, sizeof errors, ", \"relative_mass_error\": %.3e, \"relative_energy_error\": %.3e", final_status.maximum_relative_mass_error,
                  final_status.maximum_relative_energy_error);
    out << errors << ", \"native_substeps\": " << final_status.substeps << ", \"rejected_attempts\": " << final_status.rejected_attempts
        << ", \"faulted\": " << (final_status.state != 1 ? "true" : "false") << ", \"final_owner_fnv\": \"" << digest << "\"}";
    emit(o, out.str());
    return final_status.state != 1 ? 1 : 0;
}

struct Event {
    const char* name = "";
    std::uint64_t frame = 0;
    double recovery_s = -1; // -1: not recovered within the limit
    double worst_ms = 0;
    std::uint64_t frames_watched = 0;
    std::vector<double> frame_ms;
};

int run(const Options& o) {
    const auto before = memory();
    const auto setup_start = Clock::now();
    const auto layout = o.late_preset.empty() ? generate(o.rooms_x, o.rooms_y, o.events, o.hydrogen_rooms) : generate_late(o.late_preset, o.duplicants, o.vented_hall);
    const auto records = materials();
    MAOniColonyConfig config{};
    config.struct_size = sizeof config;
    config.abi_version = MA_ONI_COLONY_ABI_VERSION;
    config.flags = MA_ONI_COLONY_PHYSICS_CONDUCTION | MA_ONI_COLONY_GAME_KERNEL | MA_ONI_COLONY_GAME_SURFACE |
        (o.profile == 9 ? MA_ONI_COLONY_GAME_ACTIVITY : 0U);
    config.vacuum_index = vacuum;
    config.grid_width = config.width = layout.width;
    config.grid_height = config.height = layout.height;
    config.gravity_m_per_s2 = 9.81;
    config.maximum_timestep_s = static_cast<double>(.1f);
    c::set_worker_count(o.workers);
    if (o.pressure_iterations) {
        auto options = c::default_options();
        options.pressure_maximum_iterations = o.pressure_iterations;
        c::set_options(&options);
    }
    for (const char* name : {"MA_COLONY_KERNEL3_PROFILE6", "MA_COLONY_KERNEL3_PROFILE7"})
        if (const char* value = std::getenv(name); value && std::strcmp(value, "1") == 0)
            throw std::invalid_argument("Unset legacy profile selectors; select --profile 8 or 9 explicitly");
    if (o.profile == 9) c::set_activity_tiers(0, !o.always_simulate);
    c::start(config, records, layout.cells, 1);
    const double setup_ms = since(setup_start);
    auto status = c::status();
    if (status.state != 1) throw std::runtime_error(std::string("Colony did not start: ") + status.error);
    c::enable_flow_diagnostics(true);
    FlowTally tally;
    std::size_t next_dig = 0;
    MAOniColonyOptions effective{};
    if (!c::copy_options(effective)) throw std::runtime_error("Missing effective saved game options");
    const auto started = memory();
    const auto cells = layout.width * layout.height;
    std::vector<MAOniColonyCellView> views(cells), previous(cells);
    std::vector<MAOniColonyReceipt> receipts(1U << 16);
    const auto seconds = static_cast<double>(.2f);
    std::vector<std::uint32_t> previous_actors;
    std::uint64_t census_sequence = 0;
    std::vector<double> census_ms;
    std::vector<double> frame_ms, legacy_frame_ms, owner_ms, copy_ms, changed_fraction, active_cells, flow_ms, transfer_ms, sink_ms, ledger_ms, publish_ms;
    std::vector<std::uint64_t> resident;
    std::vector<Event> events;
    if (o.events)
        for (const char* name : {"decompression_10000_cells", "flood_1000_cells", "boil_1000_cells"}) {
            Event e;
            e.name = name;
            events.push_back(e);
        }
    std::size_t next_event = 0;
    std::optional<Clock::time_point> event_start;
    std::uint32_t within = 0;
    Clock::time_point window_start{};
    std::uint64_t frame = 0, receipts_acked = 0;
    bool faulted = false;
    // Each event starts once the previous one has recovered or timed out.
    const auto event_due = [&](std::uint64_t f) { return o.events && next_event < events.size() && !event_start && f > o.warmup; };
    const std::uint64_t frame_limit = o.events ? 100000 : o.frames;
    while (frame < frame_limit) {
        ++frame;
        const auto in = inputs(layout, o.duplicants, frame);
        // Acceptance cost includes every native queue, census, owner, copy,
        // commit check and receipt ACK. Fixture generation above is separate;
        // event construction below is conservatively included when present.
        const auto adapter_start = Clock::now();
        if (event_due(frame)) {
            auto& e = events[next_event];
            e.frame = frame;
            if (next_event == 0) {
                std::vector<MAOniColonyTerrain> open;
                for (auto cell : layout.vault_wall) open.push_back({cell, MA_ONI_COLONY_OPEN});
                c::queue_terrain(open);
            } else if (next_event == 1) {
                std::vector<MAOniColonyTerrain> open;
                for (auto cell : layout.tank_floor) open.push_back({cell, MA_ONI_COLONY_OPEN});
                c::queue_terrain(open);
            } else {
                // Each 1,000 kg cell from 360 K to the 400 K cap: about 1.7e8 J.
                std::vector<MAOniColonyHeat> heat;
                for (auto cell : layout.boil_pool) heat.push_back({cell, 0, 1000. * 4180 * 40, 400});
                c::queue_heat(heat);
            }
            event_start = adapter_start;
            within = 0;
        }
        if (layout.late && o.dig_frames && frame % o.dig_frames == 0 && next_dig < layout.digs.size()) {
            const MAOniColonyTerrain dig{layout.digs[next_dig++], MA_ONI_COLONY_OPEN};
            c::queue_terrain(std::span(&dig, 1));
        }
        if (!in.transfers.empty()) c::queue_transfers(in.transfers);
        if (!in.heat.empty()) c::queue_heat(in.heat);
        if (o.profile == 9) {
            const auto census_start = Clock::now();
            auto actors = actor_cells(in);
            std::vector<std::uint32_t> changed;
            std::set_symmetric_difference(previous_actors.begin(), previous_actors.end(), actors.begin(), actors.end(), std::back_inserter(changed));
            census_sequence = c::queue_activity(status.session, seconds, actors, changed);
            previous_actors = std::move(actors);
            if (frame > o.warmup) census_ms.push_back(since(census_start));
        }
        const auto start = Clock::now();
        c::advance(frame, seconds, 2);
        c::wait();
        const double wall_ms = since(start);
        // The adapter's copy of the completed publication.
        const auto copy_start = Clock::now();
        std::swap(views, previous);
        c::copy(status, views.data(), cells);
        const double copy = since(copy_start);
        if (status.state != 1) { faulted = true; break; }
        if (status.completed_frames != frame) throw std::runtime_error("The completed frame did not publish");
        if (o.profile == 9) {
            MAOniColonyActivityState activity{};
            if (!c::copy_activity(activity) || activity.session != status.session ||
                activity.last_committed_sequence != census_sequence || activity.enabled != (o.always_simulate ? 0U : 1U))
                throw std::runtime_error("The configured activity frame did not commit");
        }
        const auto count = c::copy_receipts(receipts.data(), static_cast<std::uint32_t>(receipts.size()));
        if (count) {
            const auto last = receipts[std::min<std::size_t>(count, receipts.size()) - 1].sequence;
            c::ack_receipts(last);
            receipts_acked += std::min<std::size_t>(count, receipts.size());
        }
        const double adapter_ms = since(adapter_start);
        std::size_t active = 0, changed = 0;
        for (std::size_t i = 0; i < cells; ++i) {
            const auto& v = views[i];
            if (!(v.flags & MA_ONI_COLONY_VIEW_OWNED) || v.element_index == vacuum || !(v.mass_kg > 0)) continue;
            ++active;
            const auto& p = previous[i];
            if (frame > 1 && (v.element_index != p.element_index || v.mass_kg != p.mass_kg || v.temperature_k != p.temperature_k ||
                              v.pressure_pa != p.pressure_pa))
                ++changed;
        }
        const bool warm = frame > o.warmup;
        if (warm) active_cells.push_back(static_cast<double>(active));
        if (warm && !o.events) {
            frame_ms.push_back(adapter_ms);
            legacy_frame_ms.push_back(wall_ms + copy);
            owner_ms.push_back(status.last_advance_ms);
            copy_ms.push_back(copy);
            changed_fraction.push_back(active ? static_cast<double>(changed) / static_cast<double>(active) : 0);
            transfer_ms.push_back(status.stage_ms[0] + status.stage_ms[1] + status.stage_ms[2]);
            flow_ms.push_back(status.stage_ms[3]);
            sink_ms.push_back(status.stage_ms[4]);
            ledger_ms.push_back(status.stage_ms[5]);
            publish_ms.push_back(wall_ms - status.last_advance_ms);
            const auto flow = c::flow_diagnostics();
            if (flow.frame == status.completed_frames) tally.add(flow);
            resident.push_back(memory().resident);
        }
        if (event_start) {
            auto& e = events[next_event];
            ++e.frames_watched;
            e.worst_ms = std::max(e.worst_ms, adapter_ms);
            e.frame_ms.push_back(adapter_ms);
            if (adapter_ms <= o.budget_ms) {
                if (within++ == 0) window_start = adapter_start;
            } else within = 0;
            if (within >= o.sustain_frames) {
                e.recovery_s = std::chrono::duration<double>(window_start - *event_start).count();
                event_start.reset();
                ++next_event;
            } else if (std::chrono::duration<double>(Clock::now() - *event_start).count() > o.event_limit_s) {
                event_start.reset();
                ++next_event;
            }
        }
        if (o.events && next_event == events.size() && !event_start) break;
    }
    const auto final_status = c::status();
    // Read actual published clocks once, outside the measured frame loop. These
    // classify the next tick's policy, not historical per-cell evolved time.
    const auto observation_start = Clock::now();
    std::uint64_t observed = 0, actor_count = 0, full_rate = 0, remote = 0, paused = 0;
    MAOniColonyActivityState final_activity{};
    if (o.profile == 9) {
        if (!c::copy_activity(final_activity)) throw std::runtime_error("Missing published activity state");
        std::vector<std::uint32_t> selected;
        std::vector<MAOniColonyActivityCell> rows;
        const auto observe = [&] {
            if (selected.empty()) return;
            rows.resize(selected.size()); MAOniColonyStatus pinned{};
            if (!c::sample_activity(final_status.session, final_status.completed_frames, selected, pinned, rows))
                throw std::runtime_error("Activity publication changed during observation");
            for (const auto& row : rows) {
                ++observed; actor_count += row.actors ? 1U : 0U;
                const auto age = final_status.time_seconds - row.last_activity_s;
                if (!final_activity.enabled || row.actors || (row.last_activity_s >= 0 && age < ma::experimental::MacGameSettings{}.activity_hold_s)) ++full_rate;
                else if (row.last_activity_s < 0 || age >= ma::experimental::MacGameSettings{}.activity_pause_s) ++paused;
                else ++remote;
            }
            selected.clear();
        };
        for (std::uint32_t i = 0; i < cells; ++i) if (views[i].flags & MA_ONI_COLONY_VIEW_OWNED) {
            selected.push_back(i); if (selected.size() == 4096) observe();
        }
        observe();
    }
    const double observation_ms = since(observation_start);
    std::uint64_t views_hash = 1469598103934665603ULL;
    for (const auto byte : std::as_bytes(std::span(views))) { views_hash ^= std::to_integer<std::uint8_t>(byte); views_hash *= 1099511628211ULL; }
    const auto peak = memory();
    c::stop();
    std::ostringstream out;
    const auto mean = [](const std::vector<double>& v) { double s = 0; for (double x : v) s += x; return v.empty() ? 0 : s / static_cast<double>(v.size()); };
    const auto frame_summary = summarize(frame_ms);
    out << "{\"mode\": \"" << (o.events ? "events" : "steady") << "\", \"profile\": " << o.profile
        << ", \"owner_flags\": " << config.flags << ", \"activity_tiers_enabled\": " << (final_activity.enabled ? "true" : "false")
        << ", \"frame_ms_scope\": \"native queues+census+advance+wait+view-copy+commit-check+receipt-copy/ack; excludes fixture input generation and managed/UI work\""
        << ", \"fully_active_stress\": " << (o.profile == 8 || o.always_simulate ? "true" : "false")
        << ", \"native_substeps\": " << final_status.substeps << ", \"census_committed\": " << final_activity.last_committed_sequence
        << ", \"final_views_fnv\": \"" << std::hex << views_hash << std::dec << "\""
        << ", \"census_preparation_ms\": " << json(summarize(census_ms))
        << ", \"activity_observation\": {\"scope\": \"final published clocks; next-tick policy, not historical evolved-cell time\", \"cells\": " << observed
        << ", \"actor_cells\": " << actor_count << ", \"full_rate\": " << full_rate << ", \"remote\": " << remote << ", \"paused\": " << paused
        << ", \"outside_frame_timer_ms\": " << observation_ms << "}"
        << ", \"effective_options\": {\"pressure_cap\": " << effective.pressure_maximum_iterations << ", \"heat_cap\": " << effective.heat_maximum_iterations
        << ", \"flow_parts\": " << effective.flow_parts << ", \"sharpen_interfaces\": " << effective.sharpen_liquid_interfaces
        << ", \"fluid_tick_stride\": " << effective.fluid_tick_stride
        << ", \"maximum_dt_s\": " << effective.maximum_timestep_s << ", \"pressure_relative_tolerance\": " << effective.pressure_relative_tolerance
        << ", \"trace_threshold_kg\": " << effective.trace_sink_threshold_kg << ", \"liquid_stiffness_ratio\": " << effective.liquid_stiffness_ratio
        << ", \"gas_heat_mixing\": " << effective.gas_heat_mixing << ", \"interface_detail_fraction\": " << effective.interface_detail_fraction
        << ", \"phase_relaxation_s\": " << effective.phase_relaxation_time_s << ", \"evaporation_below_boiling\": " << effective.evaporation_below_boiling << "}"
        << ", \"grid\": [" << layout.width << ", " << layout.height << "]"
        << ", \"cells\": " << cells << ", \"active_cells_metric\": \"occupied nonvacuum views, not evolved cells\", \"active_cells_mean\": " << mean(active_cells)
        << ", \"active_cells_min\": " << (active_cells.empty() ? 0 : *std::min_element(active_cells.begin(), active_cells.end()))
        << ", \"hydrogen_rooms\": " << (o.hydrogen_rooms ? "true" : "false") << ", \"duplicants\": " << o.duplicants << ", \"workers\": " << ma::experimental::mac_kernel_default_options().threads
        << ", \"frames\": " << frame << ", \"warmup\": " << o.warmup << ", \"frame_seconds\": " << seconds
        << ", \"setup_ms\": " << setup_ms;
    if (!o.events) {
        out << ", \"frame_ms\": " << json(frame_summary) << ", \"owner_ms\": " << json(summarize(owner_ms))
            << ", \"legacy_advance_wait_copy_ms\": " << json(summarize(legacy_frame_ms))
            << ", \"per_0_1_s_ms\": {\"mean\": " << frame_summary.mean / 2 << ", \"p95\": " << frame_summary.p95 / 2
            << ", \"max\": " << frame_summary.maximum / 2 << "}"
            << ", \"stage_ms\": {\"inputs\": " << json(summarize(transfer_ms)) << ", \"flow\": " << json(summarize(flow_ms))
            << ", \"space_sink\": " << json(summarize(sink_ms)) << ", \"ledger_and_projection\": " << json(summarize(ledger_ms))
            << ", \"after_owner_timer\": " << json(summarize(publish_ms))
            << ", \"adapter_copy\": " << json(summarize(copy_ms)) << "}"
            << ", \"changed_view_fraction_mean\": " << mean(changed_fraction)
            << ", \"changed_view_fraction_min\": " << (changed_fraction.empty() ? 0 : *std::min_element(changed_fraction.begin(), changed_fraction.end()))
            << ", \"resident_growth_bytes\": " << (resident.empty() ? 0 : static_cast<double>(resident.back()) - static_cast<double>(resident.front()));
        const auto owner = summarize(owner_ms);
        char estimate[512];
        std::snprintf(estimate, sizeof estimate,
            ", \"estimate\": {\"scope\": \"game seconds per real second if a tick takes the owner frame plus native_overhead_ms; calibrated on Linux harness runs, not measured in game\", "
            "\"native_overhead_ms\": %.1f, \"game_per_real_mean\": %.3f, \"game_per_real_p95_frame\": %.3f, \"owner_ms_budget_for_3x\": %.1f}",
            o.native_overhead_ms, estimated_speed(owner.mean, o.native_overhead_ms, seconds), estimated_speed(owner.p95, o.native_overhead_ms, seconds),
            1000 * seconds / 3 - o.native_overhead_ms);
        out << ", \"preset\": \"" << o.late_preset << "\", \"flow\": " << tally.json_text(o.top_regions, layout.width)
            << ", \"regional_execution\": " << regional_execution_json() << estimate;
    } else {
        out << ", \"budget_ms\": " << o.budget_ms << ", \"sustain_frames\": " << o.sustain_frames << ", \"events\": [";
        for (std::size_t n = 0; n < events.size(); ++n) {
            const auto& e = events[n];
            out << (n ? ", " : "") << "{\"name\": \"" << e.name << "\", \"frame\": " << e.frame << ", \"recovery_s\": " << e.recovery_s
                << ", \"worst_frame_ms\": " << e.worst_ms << ", \"frames_watched\": " << e.frames_watched << ", \"frame_ms\": [";
            for (std::size_t k = 0; k < e.frame_ms.size(); ++k) out << (k ? ", " : "") << std::round(e.frame_ms[k] * 10) / 10;
            out << "]}";
        }
        out << "]";
    }
    char errors[256];
    std::snprintf(errors, sizeof errors, ", \"relative_mass_error\": %.3e, \"relative_energy_error\": %.3e", final_status.maximum_relative_mass_error,
                  final_status.maximum_relative_energy_error);
    out << errors << ", \"faulted\": " << (faulted || final_status.state != 1 ? "true" : "false")
        << ", \"error\": \"" << (faulted || final_status.state != 1 ? std::string(final_status.error) : std::string()) << "\""
        << ", \"rejected_attempts\": " << final_status.rejected_attempts << ", \"skipped_relaxations\": " << final_status.skipped_relaxations
        << ", \"receipts\": " << receipts_acked << ", \"evaporated_kg\": " << final_status.evaporated_kg
        << ", \"condensed_kg\": " << final_status.condensed_kg << ", \"trace_sink_kg\": " << final_status.trace_sink_kg
        << ", \"memory_bytes\": {\"before\": " << before.resident << ", \"started\": " << started.resident << ", \"peak\": " << peak.peak
        << ", \"extra_peak\": " << static_cast<double>(peak.peak) - static_cast<double>(before.resident) << "}}";
    emit(o, out.str());
    return faulted || final_status.state != 1 ? 1 : 0;
}
} // namespace

int main(int argc, char** argv) {
    try {
        std::fesetround(FE_TONEAREST);
        _mm_setcsr((_mm_getcsr() & ~0xe040U) | 0x1f80U);
        const auto options = parse(argc, argv);
        return !options.replay_owner.empty() ? owner_replay(options) : options.world_dump.empty() ? run(options) : replay(options);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "ma_colony_benchmark: %s\n", error.what());
        return 2;
    }
}
