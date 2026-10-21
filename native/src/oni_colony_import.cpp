#include "oni_colony_import.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>

namespace ma::oni {
namespace {
using namespace experimental;
constexpr std::size_t none = std::numeric_limits<std::size_t>::max();
constexpr double R = gas_constant;
void require(bool ok, const char* why) { if (!ok) throw std::invalid_argument(why); }

// Sparse gas held in one cell: species masses and the sensible heat content
// sum(cv m T) with its capacity sum(cv m). Moving a fraction of a cell's gas
// moves that fraction of every species and of the heat.
struct Gas {
    std::vector<std::pair<std::uint32_t, double>> masses;
    double heat = 0, capacity = 0;
    [[nodiscard]] double moles(const SpeciesRegistry& r) const {
        double n = 0; for (const auto& [k, m] : masses) n += m / r[k].molar_mass; return n;
    }
    void add(std::uint32_t k, double m) {
        for (auto& [s, value] : masses) if (s == k) { value += m; return; }
        masses.emplace_back(k, m);
    }
};
struct Liquid { std::uint32_t species = import_vacuum; double mass = 0, temperature = 0; };

struct Grid {
    std::size_t w, h;
    [[nodiscard]] std::size_t up(std::size_t i) const { return i / w + 1 < h ? i + w : none; }
    [[nodiscard]] std::size_t down(std::size_t i) const { return i >= w ? i - w : none; }
    template<class F> void neighbours(std::size_t i, F&& f) const {
        const auto x = i % w, y = i / w;
        if (x > 0) f(i - 1);
        if (x + 1 < w) f(i + 1);
        if (y > 0) f(i - w);
        if (y + 1 < h) f(i + w);
    }
};
}

ImportResult settle_import(const Catalogue& catalogue, std::size_t width, std::size_t height,
    std::span<const ImportCell> input, double gravity) {
    const auto& registry = catalogue.registry;
    require(width >= 1 && height >= 1 && width <= 4096 && height <= 4096 && input.size() == width * height, "Invalid import geometry");
    require(std::isfinite(gravity) && gravity >= 0, "Invalid import gravity");
    const auto N = input.size(); const Grid g{width, height};
    std::vector<Gas> gas(N); std::vector<Liquid> liquid(N);
    std::vector<long double> imported(registry.size());
    for (std::size_t i = 0; i < N; ++i) {
        const auto& c = input[i];
        require(c.kind == ImportCellKind::solid || c.kind == ImportCellKind::fluid || c.kind == ImportCellKind::space, "Invalid import cell kind");
        require(std::isfinite(c.mass_kg) && c.mass_kg >= 0 && std::isfinite(c.temperature_k) && c.temperature_k >= 0, "Invalid import cell inventory");
        if (c.kind != ImportCellKind::fluid || c.species == import_vacuum || c.mass_kg == 0) {
            require(c.kind == ImportCellKind::fluid || c.species == import_vacuum || c.mass_kg == 0, "Solid or space cells cannot hold fluid at import");
            continue;
        }
        require(c.species < registry.size() && c.temperature_k > 0, "Import fluid needs a catalogue species and a positive temperature");
        const auto& s = registry[c.species];
        imported[c.species] += c.mass_kg;
        if (s.phase == Phase::gas) { gas[i].masses.emplace_back(c.species, c.mass_kg); gas[i].capacity = s.cv() * c.mass_kg; gas[i].heat = gas[i].capacity * c.temperature_k; }
        else liquid[i] = {c.species, c.mass_kg, c.temperature_k};
    }
    const auto open = [&](std::size_t i) { return i != none && input[i].kind == ImportCellKind::fluid; };
    const auto rho0 = [&](std::uint32_t k) { return catalogue.eos->reference().materials()[k].zero_pressure_density_kg_per_m3; };
    const auto bulk = [&](std::uint32_t k) { return catalogue.eos->reference().materials()[k].reference_bulk_modulus_pa; };
    ImportResult result; auto& report = result.report;

    // Regions: connected open (non-solid, non-space) cells.
    std::vector<std::size_t> region(N, none); std::vector<std::vector<std::size_t>> regions;
    for (std::size_t s = 0; s < N; ++s) {
        if (!open(s) || region[s] != none) continue;
        regions.emplace_back(); auto& members = regions.back(); members.push_back(s); region[s] = regions.size() - 1;
        for (std::size_t q = 0; q < members.size(); ++q) g.neighbours(members[q], [&](std::size_t n) {
            if (open(n) && region[n] == none) { region[n] = regions.size() - 1; members.push_back(n); }
        });
    }
    report.regions = regions.size();
    // First estimate of each region's gas pressure, for the liquid columns.
    std::vector<double> region_pressure(regions.size(), 0), region_temperature(regions.size(), 300);
    for (std::size_t r = 0; r < regions.size(); ++r) {
        double moles = 0, heat = 0, capacity = 0, volume = 0;
        for (const auto i : regions[r]) {
            moles += gas[i].moles(registry); heat += gas[i].heat; capacity += gas[i].capacity;
            volume += liquid[i].mass > 0 ? std::max(0., 1 - liquid[i].mass / rho0(liquid[i].species)) : 1;
        }
        if (capacity > 0) region_temperature[r] = heat / capacity;
        if (volume > 0) region_pressure[r] = moles * R * region_temperature[r] / volume;
    }
    const auto gas_volume = [&](std::size_t i) {
        if (liquid[i].mass == 0) return 1.;
        const double v = 1 - liquid[i].mass / rho0(liquid[i].species);
        return v > 1e-9 ? v : 0.;
    };
    std::vector<double> hydrostatic(N, 0), temperature(N, 0), excess(N, 0);
    std::vector<std::size_t> parent(N, none);
    std::vector<std::uint8_t> visited(N);
    std::vector<std::uint64_t> owner(N, 0), queued(N, 0); std::uint64_t stamp = 0;
    std::vector<double> own_temperature(N, 0);
    // Liquid and gas pressures depend on each other: a covered liquid cell's
    // mass follows the gas pressure above its body (bulk modulus times the
    // relative compression), so the two passes repeat with the settled gas.
    for (int outer = 0; outer < 3; ++outer) {
    std::fill(visited.begin(), visited.end(), 0);
    // Liquid bodies: connected cells of one liquid species.
    for (std::size_t s = 0; s < N; ++s) {
        if (!open(s) || liquid[s].mass == 0 || visited[s]) continue;
        const auto species = liquid[s].species; if (outer == 0) ++report.liquid_bodies;
        std::vector<std::size_t> body{s}; visited[s] = 1;
        for (std::size_t q = 0; q < body.size(); ++q) g.neighbours(body[q], [&](std::size_t n) {
            if (open(n) && !visited[n] && liquid[n].mass > 0 && liquid[n].species == species) { visited[n] = 1; body.push_back(n); }
        });
        const double density = rho0(species), modulus = bulk(species), weight = density * gravity;
        long double total = 0, heat = 0;
        for (const auto i : body) { total += liquid[i].mass; heat += liquid[i].mass * liquid[i].temperature; own_temperature[i] = liquid[i].temperature; }
        const double mean = static_cast<double>(heat / total);
        // The body empties into one pool that fills its basin from the bottom:
        // the lowest open cells reachable from it first, one shared level, the
        // last layer at a common fraction. Gas displaced from flooded cells is
        // moved by the gas pass. The region keeps room for its gas at no more
        // than ten times its pressure (at least 1 MPa); liquid that cannot fit
        // goes to the reservoir.
        const auto body_stamp = ++stamp;
        for (const auto i : body) { liquid[i] = {}; owner[i] = body_stamp; }
        const auto r = region[s];
        double moles = 0, room = 0;
        for (const auto i : regions[r]) {
            moles += gas[i].moles(registry);
            room += liquid[i].mass > 0 ? std::max(0., 1 - liquid[i].mass / rho0(liquid[i].species)) : 1;
        }
        const double needed = moles > 0 ? moles * R * region_temperature[r] / std::max(10 * region_pressure[r], 1e6) : 0;
        const long double allowed = std::max(0., room - needed);
        struct Flood { std::vector<std::size_t> full, top; double fraction = 0, level = 0; long double overflow = 0; bool surface = false; };
        // capacity(i): nominal liquid volume a full cell holds at its pressure.
        const auto flood = [&](const auto& capacity) {
            Flood out; ++stamp;
            using Entry = std::pair<std::size_t, std::size_t>;
            std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> heap;
            for (const auto i : body) { heap.emplace(i / width, i); queued[i] = stamp; }
            long double left = total / density, budget = allowed;
            std::vector<std::size_t> layer;
            while (!heap.empty() && left > 0 && budget > 0) {
                const auto y = heap.top().first; layer.clear();
                while (!heap.empty() && heap.top().first == y) { layer.push_back(heap.top().second); heap.pop(); }
                for (std::size_t q = 0; q < layer.size(); ++q) {
                    const auto i = layer[q];
                    for (const auto n : {i % width > 0 ? i - 1 : none, i % width + 1 < width ? i + 1 : none})
                        if (open(n) && liquid[n].mass == 0 && queued[n] != stamp) { queued[n] = stamp; layer.push_back(n); }
                    const auto d = g.down(i);
                    if (open(d) && liquid[d].mass == 0 && queued[d] != stamp) { queued[d] = stamp; heap.emplace(d / width, d); }
                }
                if (!heap.empty() && heap.top().first < y) { for (const auto i : layer) heap.emplace(y, i); continue; }
                long double layer_capacity = 0; for (const auto i : layer) layer_capacity += capacity(i);
                const long double room_needed = static_cast<long double>(layer.size());
                if (left >= layer_capacity && budget >= room_needed) {
                    left -= layer_capacity; budget -= room_needed;
                    out.full.insert(out.full.end(), layer.begin(), layer.end());
                    for (const auto i : layer) {
                        const auto u = g.up(i);
                        if (open(u) && liquid[u].mass == 0 && queued[u] != stamp) { queued[u] = stamp; heap.emplace(u / width, u); }
                    }
                    continue;
                }
                // The surface layer: as much as fits, within the room budget.
                const long double fill = std::min(left, std::min(layer_capacity, budget * layer_capacity / room_needed));
                out.top = layer; out.fraction = static_cast<double>(fill / layer_capacity); left -= fill; out.surface = true;
                break;
            }
            out.overflow = left * density;
            std::size_t top_row = 0;
            for (const auto i : out.full) top_row = std::max(top_row, i / width);
            out.level = out.surface ? static_cast<double>(out.top.front() / width) - .5 + out.fraction : static_cast<double>(top_row) + .5;
            return out;
        };
        auto result_flood = flood([](std::size_t) { return 1.; });
        // Hydrostatic compression of the full cells, at the first level.
        const double reference = result_flood.surface ? region_pressure[r] : 0;
        const double first_level = result_flood.level;
        const auto pressure_at = [&](std::size_t i) { return reference + weight * std::max(0., first_level - static_cast<double>(i / width)); };
        result_flood = flood([&](std::size_t i) { return 1 + pressure_at(i) / modulus; });
        long double kept = 0, kept_heat = 0; std::size_t flooded = 0;
        const auto capacity_at = [&](std::size_t i) { return 1 + pressure_at(i) / modulus; };
        const auto place = [&](std::size_t i, double mass) {
            liquid[i] = {species, mass, owner[i] == body_stamp ? own_temperature[i] : mean};
            if (owner[i] != body_stamp) ++flooded;
            visited[i] = 1;
            kept += mass; kept_heat += mass * liquid[i].temperature;
        };
        for (const auto i : result_flood.full) place(i, density * (1 + pressure_at(i) / modulus));
        for (const auto i : result_flood.top) if (result_flood.fraction > 0) place(i, density * result_flood.fraction * capacity_at(i));
        // Rounding of the level arithmetic stays in the pool, never deleted:
        // the remainder of the mass goes to the reservoir with the overflow.
        const long double booked = total - kept;
        require(booked >= -1e-9L * total, "Import liquid flood placed more liquid than the body holds");
        // Heat: kept cells keep their temperature (flooded cells take the
        // body's mean), shifted uniformly so the body's sum(m T) is unchanged.
        const double shift = kept > 0 ? static_cast<double>((heat - kept_heat - std::max<long double>(0, booked) * mean) / kept) : 0;
        for (const auto i : result_flood.full) liquid[i].temperature += shift;
        for (const auto i : result_flood.top) if (liquid[i].mass > 0) liquid[i].temperature += shift;
        report.raised_cells += flooded;
        if (booked > 1e-12L * total) {
            result.reservoir.push_back({s, species, static_cast<double>(booked), mean});
            report.reservoir_kg += static_cast<double>(booked);
        } else if (booked > 0) {
            // A below-rounding remainder joins the top surface cell.
            const auto i = result_flood.top.empty() ? result_flood.full.back() : result_flood.top.front();
            liquid[i].mass += static_cast<double>(booked);
        }
        for (const auto i : body) require(liquid[i].mass == 0 || (std::isfinite(liquid[i].temperature) && liquid[i].temperature > 0),
            "Import liquid levelling left a nonpositive temperature");
    }
    // Gas: per connected component of open cells, one pressure, without
    // mixing. ONI keeps one element per cell and its own simulation sorts
    // gases by density; spreading every species through the region would mix
    // a room's CO2 into its air. The region's gas keeps its heat at one common
    // temperature; each species takes the volume n R T / p of a layer, layers
    // fill the region's gas room from the bottom, heaviest molar mass first,
    // and only the cells where two layers meet hold a mixture.
    report.maximum_relative_pressure_spread = 0;
    for (std::size_t r = 0; r < regions.size(); ++r) {
        std::vector<std::size_t> order;
        for (const auto i : regions[r]) if (gas_volume(i) > 0) order.push_back(i);
        std::vector<long double> moles(registry.size()), heat(registry.size());
        std::vector<std::uint32_t> present;
        for (const auto i : regions[r]) for (const auto& [k, m] : gas[i].masses) {
            if (moles[k] == 0) present.push_back(k);
            moles[k] += m / registry[k].molar_mass;
            heat[k] += gas[i].capacity > 0 ? gas[i].heat * (registry[k].cv() * m / gas[i].capacity) : 0;
        }
        if (present.empty() || order.empty()) continue;
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return a / width < b / width; });
        std::sort(present.begin(), present.end(), [&](std::uint32_t a, std::uint32_t b) {
            return registry[a].molar_mass != registry[b].molar_mass ? registry[a].molar_mass > registry[b].molar_mass : a < b; });
        long double room = 0, nrt = 0;
        for (const auto i : order) room += gas_volume(i);
        long double total_heat = 0, total_capacity = 0;
        for (const auto k : present) { total_heat += heat[k]; total_capacity += registry[k].cv() * moles[k] * registry[k].molar_mass; }
        const long double common = total_heat > 0 ? total_heat / total_capacity : region_temperature[r];
        for (const auto k : present) nrt += moles[k] * R * common;
        const long double pressure = nrt / room;
        for (const auto i : regions[r]) for (const auto& [k, m] : gas[i].masses) report.moved_gas_mol += m / registry[k].molar_mass;
        for (const auto i : regions[r]) gas[i] = Gas{};
        // Lay each species' volume into the ordered cells; the last cell takes
        // the remainder of every species, so no mass is left over.
        std::size_t at = 0; long double free = order.empty() ? 0 : gas_volume(order[0]);
        for (std::size_t q = 0; q < present.size(); ++q) {
            const auto k = present[q]; long double left = moles[k];
            const long double per_volume = pressure / (R * common); // mol per m^3
            while (left > 0) {
                const bool last = at + 1 == order.size();
                const long double take = last ? left : std::min(left, free * per_volume);
                if (take > 0) {
                    const auto mass = static_cast<double>(take * registry[k].molar_mass);
                    auto& cell = gas[order[at]]; cell.add(k, mass);
                    cell.capacity += registry[k].cv() * mass; cell.heat += registry[k].cv() * mass * static_cast<double>(common);
                    left -= take; free -= take / per_volume;
                }
                if (left > 0 && !last) { ++at; free = gas_volume(order[at]); }
                else if (left > 0) break;
            }
        }
        // Spread over cells with gas: boundary cells mix at one temperature.
        double lo = std::numeric_limits<double>::infinity(), hi = 0;
        for (const auto i : order) {
            if (gas[i].capacity <= 0) continue;
            const double p = gas[i].moles(registry) * R * (gas[i].heat / gas[i].capacity) / gas_volume(i);
            lo = std::min(lo, p); hi = std::max(hi, p);
        }
        if (hi > 0) {
            report.maximum_relative_pressure_spread = std::max(report.maximum_relative_pressure_spread, (hi - lo) / hi);
            region_pressure[r] = static_cast<double>(pressure);
        }
    }
    }
    // Owner state: void phase, every open cell evaluated at its temperature.
    auto& st = result.state;
    st.grid = {width, height, 1, 1, false, false, std::vector<std::uint8_t>(N)};
    st.cells.reserve(N); st.occupied_volumes_m3.assign(N, 0);
    std::vector<long double> settled(registry.size());
    for (const auto& entry : result.reservoir) settled[entry.species] += entry.mass_kg;
    for (std::size_t i = 0; i < N; ++i) {
        MacMaterialCell cell{MixtureInventory(registry.size()), {}};
        if (input[i].kind == ImportCellKind::solid) { st.grid.walls[i] = 1; st.cells.push_back(std::move(cell)); continue; }
        const bool has_gas = !gas[i].masses.empty() && gas[i].capacity > 0, has_liquid = liquid[i].mass > 0;
        if (has_gas && gas_volume(i) <= 0 && has_liquid) {
            // Gas left behind in a full liquid cell cannot occur after settling.
            require(gas[i].moles(registry) <= 1e-12, "Import left gas in a full liquid cell");
        }
        if (!has_gas && !has_liquid) {
            st.cells.push_back(std::move(cell)); continue;
        }
        std::vector<double> masses(registry.size());
        double heat = 0, capacity = 0;
        if (has_gas) { for (const auto& [k, m] : gas[i].masses) masses[k] += m; heat += gas[i].heat; capacity += gas[i].capacity; }
        if (has_liquid) {
            const auto c = registry[liquid[i].species].cv();
            masses[liquid[i].species] += liquid[i].mass; heat += c * liquid[i].mass * liquid[i].temperature; capacity += c * liquid[i].mass;
        }
        for (std::size_t k = 0; k < masses.size(); ++k) settled[k] += masses[k];
        if (has_gas && has_liquid && input[i].species != import_vacuum && registry[input[i].species].phase == Phase::liquid) ++report.headspaces_filled;
        if (has_gas && input[i].kind == ImportCellKind::fluid && input[i].species == import_vacuum) ++report.vacuum_cells_filled;
        const double volume = has_gas ? 1 : std::min(1., liquid[i].mass / rho0(liquid[i].species));
        const double t = heat / capacity;
        require(std::isfinite(t) && t > 0, "Import left a cell without a positive temperature");
        cell.mixture = catalogue.eos->initialize_at_temperature(masses, t, volume).mixture;
        st.occupied_volumes_m3[i] = volume;
        st.cells.push_back(std::move(cell));
    }
    for (std::size_t k = 0; k < registry.size(); ++k) if (imported[k] > 0)
        report.maximum_relative_mass_error = std::max(report.maximum_relative_mass_error,
            static_cast<double>(std::abs(settled[k] - imported[k]) / imported[k]));
    const auto inertia = mac_face_inertias(registry, st, *catalogue.eos);
    st.momentum_x.assign(inertia.x.size(), 0); st.momentum_y.assign(inertia.y.size(), 0);
    return result;
}
}
