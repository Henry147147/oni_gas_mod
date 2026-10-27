// Colony owner benchmark (docs/performance-acceptance.md). Drives the actual
// colony owner (solver plus native adapter: inputs, flow, ledgers, projection
// and publication) on a generated asteroid with evolving gases, liquids,
// phase change, machines and duplicants, or replays a private world dump.
// The materials are illustrative physical values, not game data; this is
// reproducible benchmark evidence, not an in-game or managed-adapter result.
#include "../src/oni_colony.hpp"
#include "../src/oni_catalogue.hpp"
#include "mixed_atmospheres/mac_closed_world.hpp"
#include "mixed_atmospheres/mac_kernel.hpp"
#include <algorithm>
#include <cfenv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
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
    // Hydrogen under chlorine in every sixth room: fast sound and strong
    // buoyancy (the stiffest pressure systems); --no-hydrogen-rooms uses CO2 over O2.
    bool hydrogen_rooms = true;
    double budget_ms = 200;          // one 0.2 s frame at 1x
    std::uint32_t sustain_frames = 25; // recovery: this many consecutive frames within budget
    double event_limit_s = 60;       // wall seconds watched after each event
    std::string json_path, world_dump;
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
            else throw std::invalid_argument("Unknown preset");
        } else if (a == "--rooms") { o.rooms_x = static_cast<std::uint32_t>(number(i)); o.rooms_y = static_cast<std::uint32_t>(number(i)); }
        else if (a == "--frames") o.frames = static_cast<std::uint32_t>(number(i));
        else if (a == "--warmup") o.warmup = static_cast<std::uint32_t>(number(i));
        else if (a == "--workers") o.workers = static_cast<std::uint32_t>(number(i));
        else if (a == "--duplicants") o.duplicants = static_cast<std::uint32_t>(number(i));
        else if (a == "--pressure-iterations") o.pressure_iterations = static_cast<std::uint32_t>(number(i));
        else if (a == "--events") o.events = true;
        else if (a == "--no-hydrogen-rooms") o.hydrogen_rooms = false;
        else if (a == "--budget-ms") o.budget_ms = number(i);
        else if (a == "--sustain-frames") o.sustain_frames = static_cast<std::uint32_t>(number(i));
        else if (a == "--event-limit-s") o.event_limit_s = number(i);
        else if (a == "--json") { if (i + 1 >= argc) throw std::invalid_argument("Missing path"); o.json_path = argv[++i]; }
        else if (a == "--world-dump") { if (i + 1 >= argc) throw std::invalid_argument("Missing path"); o.world_dump = argv[++i]; }
        else throw std::invalid_argument("Unknown option " + a);
    }
    if (o.rooms_x < 1 || o.rooms_y < 1 || o.rooms_x > 24 || o.rooms_y > 40 || o.frames < 1 || o.warmup >= o.frames)
        throw std::invalid_argument("Invalid rooms, frames or warmup");
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
    std::vector<double> times, sparse, tick, eos, pressure, transport, layering, interface, heat, phase, audit, dense, iterations;
    for (std::uint32_t n = 0; n < o.frames; ++n) {
        const auto start = Clock::now();
        auto advanced = world.advanced(seconds, 65536, MacExecutionPolicy::production);
        world = std::move(advanced.world);
        times.push_back(since(start));
        const auto& g = advanced.report.game_stages;
        for (const auto& [out, value] : {std::pair{&sparse, g.sparse_ms}, {&tick, g.tick_ms}, {&eos, g.eos_ms}, {&pressure, g.pressure_ms},
                                  {&transport, g.transport_ms}, {&layering, g.layering_ms}, {&interface, g.interface_ms},
                                  {&heat, g.heat_ms}, {&phase, g.phase_ms}, {&audit, g.audit_ms}, {&dense, g.dense_ms},
                                  {&iterations, static_cast<double>(g.pressure_iterations)}})
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
    out << "{\"mode\": \"world-dump\", \"cells\": " << world.state().cells.size() << ", \"fluid_cells\": " << fluid
        << ", \"frame_seconds\": " << seconds << ", \"workers\": " << options.threads << ", \"frames\": " << o.frames
        << ", \"warmup\": " << o.warmup << ", \"flow_ms\": " << json(warm) << ", \"stages_ms\": {";
    const std::pair<const char*, const std::vector<double>*> parts[] = {{"sparse", &sparse}, {"tick", &tick}, {"eos", &eos},
        {"pressure", &pressure}, {"transport", &transport}, {"layering", &layering}, {"interface", &interface}, {"heat", &heat},
        {"phase", &phase}, {"audit_and_other", &audit}, {"dense", &dense}, {"pressure_iterations", &iterations}};
    for (std::size_t n = 0; n < std::size(parts); ++n) out << (n ? ", " : "") << "\"" << parts[n].first << "\": " << json(warm_part(*parts[n].second));
    out << "}, \"final_image_fnv\": \"" << line << "\"}";
    emit(o, out.str());
    return 0;
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
    const auto layout = generate(o.rooms_x, o.rooms_y, o.events, o.hydrogen_rooms);
    const auto records = materials();
    MAOniColonyConfig config{};
    config.struct_size = sizeof config;
    config.abi_version = MA_ONI_COLONY_ABI_VERSION;
    config.flags = MA_ONI_COLONY_PHYSICS_CONDUCTION | MA_ONI_COLONY_GAME_KERNEL | MA_ONI_COLONY_GAME_SURFACE;
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
    c::start(config, records, layout.cells, 1);
    const double setup_ms = since(setup_start);
    auto status = c::status();
    if (status.state != 1) throw std::runtime_error(std::string("Colony did not start: ") + status.error);
    const auto started = memory();
    const auto cells = layout.width * layout.height;
    std::vector<MAOniColonyCellView> views(cells), previous(cells);
    std::vector<MAOniColonyReceipt> receipts(1U << 16);
    const auto seconds = static_cast<double>(.2f);
    std::vector<double> frame_ms, owner_ms, copy_ms, changed_fraction, active_cells, flow_ms, transfer_ms, sink_ms, ledger_ms, publish_ms;
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
            event_start = Clock::now();
            within = 0;
        }
        if (!in.transfers.empty()) c::queue_transfers(in.transfers);
        if (!in.heat.empty()) c::queue_heat(in.heat);
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
        const auto count = c::copy_receipts(receipts.data(), static_cast<std::uint32_t>(receipts.size()));
        if (count) {
            const auto last = receipts[std::min<std::size_t>(count, receipts.size()) - 1].sequence;
            c::ack_receipts(last);
            receipts_acked += std::min<std::size_t>(count, receipts.size());
        }
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
            frame_ms.push_back(wall_ms + copy);
            owner_ms.push_back(status.last_advance_ms);
            copy_ms.push_back(copy);
            changed_fraction.push_back(active ? static_cast<double>(changed) / static_cast<double>(active) : 0);
            transfer_ms.push_back(status.stage_ms[0] + status.stage_ms[1] + status.stage_ms[2]);
            flow_ms.push_back(status.stage_ms[3]);
            sink_ms.push_back(status.stage_ms[4]);
            ledger_ms.push_back(status.stage_ms[5]);
            publish_ms.push_back(wall_ms - status.last_advance_ms);
            resident.push_back(memory().resident);
        }
        if (event_start) {
            auto& e = events[next_event];
            ++e.frames_watched;
            e.worst_ms = std::max(e.worst_ms, wall_ms + copy);
            e.frame_ms.push_back(wall_ms + copy);
            if (wall_ms + copy <= o.budget_ms) {
                if (within++ == 0) window_start = start;
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
    const auto peak = memory();
    c::stop();
    std::ostringstream out;
    const auto mean = [](const std::vector<double>& v) { double s = 0; for (double x : v) s += x; return v.empty() ? 0 : s / static_cast<double>(v.size()); };
    const auto frame_summary = summarize(frame_ms);
    out << "{\"mode\": \"" << (o.events ? "events" : "steady") << "\", \"grid\": [" << layout.width << ", " << layout.height << "]"
        << ", \"cells\": " << cells << ", \"active_cells_mean\": " << mean(active_cells)
        << ", \"active_cells_min\": " << (active_cells.empty() ? 0 : *std::min_element(active_cells.begin(), active_cells.end()))
        << ", \"hydrogen_rooms\": " << (o.hydrogen_rooms ? "true" : "false") << ", \"duplicants\": " << o.duplicants << ", \"workers\": " << ma::experimental::mac_kernel_default_options().threads
        << ", \"frames\": " << frame << ", \"warmup\": " << o.warmup << ", \"frame_seconds\": " << seconds
        << ", \"setup_ms\": " << setup_ms;
    if (!o.events) {
        out << ", \"frame_ms\": " << json(frame_summary) << ", \"owner_ms\": " << json(summarize(owner_ms))
            << ", \"per_0_1_s_ms\": {\"mean\": " << frame_summary.mean / 2 << ", \"p95\": " << frame_summary.p95 / 2
            << ", \"max\": " << frame_summary.maximum / 2 << "}"
            << ", \"stage_ms\": {\"inputs\": " << json(summarize(transfer_ms)) << ", \"flow\": " << json(summarize(flow_ms))
            << ", \"space_sink\": " << json(summarize(sink_ms)) << ", \"ledger_and_projection\": " << json(summarize(ledger_ms))
            << ", \"after_owner_timer\": " << json(summarize(publish_ms))
            << ", \"adapter_copy\": " << json(summarize(copy_ms)) << "}"
            << ", \"changed_view_fraction_mean\": " << mean(changed_fraction)
            << ", \"changed_view_fraction_min\": " << (changed_fraction.empty() ? 0 : *std::min_element(changed_fraction.begin(), changed_fraction.end()))
            << ", \"resident_growth_bytes\": " << (resident.empty() ? 0 : static_cast<double>(resident.back()) - static_cast<double>(resident.front()));
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
        return options.world_dump.empty() ? run(options) : replay(options);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "ma_colony_benchmark: %s\n", error.what());
        return 2;
    }
}
