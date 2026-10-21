// Import settle control (docs/colony-owner.md#import). Made-up materials and
// inventories, not game data.
#include "../src/oni_colony_import.hpp"
#include "mixed_atmospheres/mac_closed_world.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace ma;
using namespace ma::oni;
using namespace ma::experimental;
void require(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
GameFluidElement element(const char* id, std::int32_t hash, std::uint32_t index, bool gas, double molar, double c, double density = 0) {
    GameFluidElement e; e.id = id; e.hash = hash; e.native_index = index; e.gas = gas; e.molar_mass_kg_per_mol = molar;
    e.specific_heat_j_per_kg_k = c; e.conductivity_w_per_m_k = gas ? .02 : .5; e.density_kg_per_m3 = density;
    e.low_temperature_k = gas ? 50 : 250; e.high_temperature_k = gas ? 1e4 : 700;
    return e;
}
}

int main() {
    try {
        const auto catalogue = build_catalogue({element("Oxygen", 1, 0, true, .016, 1000), element("Hydrogen", 2, 1, true, .002, 2000),
            element("Water", 3, 2, false, .018, 4000, 1000), element("CrudeOil", 4, 3, false, .3, 1700, 900)});
        const auto oxygen = static_cast<std::uint32_t>(catalogue.species_of_hash(1)), hydrogen = static_cast<std::uint32_t>(catalogue.species_of_hash(2));
        const auto water = static_cast<std::uint32_t>(catalogue.species_of_hash(3)), oil = static_cast<std::uint32_t>(catalogue.species_of_hash(4));
        constexpr std::size_t W = 12, H = 10;
        std::vector<ImportCell> cells(W * H);
        const auto at = [&](std::size_t x, std::size_t y) -> ImportCell& { return cells[x + y * W]; };
        for (std::size_t y = 1; y <= 6; ++y) for (std::size_t x = 1; x <= 10; ++x) at(x, y) = {ImportCellKind::fluid, oxygen, 1, 300};
        for (std::size_t x = 1; x <= 6; ++x) {
            at(x, 1) = {ImportCellKind::fluid, water, 1000 * (1.01 + .01 * static_cast<double>(x)), 290};
            at(x, 2) = {ImportCellKind::fluid, water, 1000 * (1.05 - .005 * static_cast<double>(x)), 295};
            at(x, 3) = {ImportCellKind::fluid, water, 300, 300};
        }
        for (std::size_t y = 1; y <= 3; ++y) at(7, y) = {};  // the pool's basin wall
        at(9, 5) = {ImportCellKind::fluid, hydrogen, 5, 320};
        at(8, 2) = {ImportCellKind::fluid, import_vacuum, 0, 0};
        for (std::size_t x = 1; x <= 3; ++x) at(x, 8) = {ImportCellKind::fluid, oil, 2000, 350};
        for (std::size_t x = 1; x <= 10; ++x) at(x, 9) = {ImportCellKind::space, import_vacuum, 0, 0};
        long double imported_water = 0, imported_oil = 0;
        for (const auto& c : cells) { if (c.species == water) imported_water += c.mass_kg; if (c.species == oil) imported_oil += c.mass_kg; }
        const auto result = settle_import(catalogue, W, H, cells, 9.81);
        const auto& r = result.report; const auto& st = result.state;
        std::cout << "regions " << r.regions << " bodies " << r.liquid_bodies << " raised " << r.raised_cells << " headspaces " << r.headspaces_filled
                  << " vacuum filled " << r.vacuum_cells_filled << " reservoir " << r.reservoir_kg << " kg moved " << r.moved_gas_mol
                  << " mol spread " << r.maximum_relative_pressure_spread << " mass error " << r.maximum_relative_mass_error << '\n';
        require(r.regions == 2 && r.liquid_bodies == 2, "regions and bodies");
        require(r.maximum_relative_mass_error <= 1e-14, "species mass conserved");
        // The sealed oil pocket keeps 3 full cells; the rest is booked, not deleted.
        require(result.reservoir.size() == 1 && result.reservoir[0].species == oil, "oil reservoir");
        const double oil_kept = st.cells[1 + 8 * W].mixture.masses[oil] + st.cells[2 + 8 * W].mixture.masses[oil] + st.cells[3 + 8 * W].mixture.masses[oil];
        require(std::abs(static_cast<double>(imported_oil) - oil_kept - result.reservoir[0].mass_kg) <= 1e-9 * 6000, "oil booked");
        require(oil_kept >= 2700 && oil_kept < 2700.01, "sealed pocket full at hydrostatic mass");
        // The pool: covered cells at hydrostatic mass, the surface at one level, gas in its headspace.
        const double surface = st.cells[1 + 3 * W].mixture.masses[water];
        for (std::size_t x = 1; x <= 6; ++x) {
            require(std::abs(st.cells[x + 3 * W].mixture.masses[water] - surface) <= 1e-9 * surface, "level surface");
            require(st.cells[x + 3 * W].mixture.masses[oxygen] + st.cells[x + 3 * W].mixture.masses[hydrogen] > 0, "headspace gas");
            require(st.cells[x + W].mixture.masses[water] > 1000 && st.cells[x + W].mixture.masses[water] < 1001, "hydrostatic bottom");
        }
        long double water_after = 0; for (const auto& c : st.cells) water_after += c.mixture.masses[water];
        require(std::abs(water_after - imported_water) <= 1e-12L * imported_water, "water conserved");
        require(st.cells[8 + 2 * W].mixture.masses[oxygen] > 0, "vacuum pocket filled");
        for (std::size_t x = 1; x <= 10; ++x) require(st.cells[x + 9 * W].mixture.mass() == 0, "space stays empty");
        require(r.maximum_relative_pressure_spread < 1e-12, "one gas pressure per region");
        // Gas is layered by density, not mixed: hydrogen above oxygen, with at
        // most one cell holding both.
        std::size_t shared = 0, lowest_hydrogen = H, highest_oxygen = 0;
        for (std::size_t i = 0; i < W * H; ++i) {
            const double o = st.cells[i].mixture.masses[oxygen], h = st.cells[i].mixture.masses[hydrogen];
            if (o > 0 && h > 0) ++shared;
            if (h > 0) lowest_hydrogen = std::min(lowest_hydrogen, i / W);
            if (o > 0 && h == 0) highest_oxygen = std::max(highest_oxygen, i / W);
        }
        require(shared <= 1 && lowest_hydrogen >= highest_oxygen, "gas layered by density");
        // The settled state is close to rest: a void-phase production owner
        // advances 0.2 s with small speeds.
        MacClosedSettings settings; settings.numerical_kernel = MacNumericalKernel::production; settings.gravity_m_per_s2 = 9.81;
        settings.vacuum = MacVacuumRepresentation::void_phase; settings.reconstruction = MacMaterialReconstruction::interface_plic;
        settings.subcell_void = MacSubcellVoidPolicy::expand_unresolved_gas; settings.thermal_conduction = true;
        MacClosedWorld world(catalogue.registry, catalogue.eos, st, settings);
        const auto advanced = world.advanced(.2, 65536, MacExecutionPolicy::production);
        const auto& next = advanced.world.state(); const auto inertia = mac_face_inertias(catalogue.registry, next, *catalogue.eos);
        double speed = 0;
        for (std::size_t f = 0; f < next.momentum_y.size(); ++f) if (inertia.y[f] > 1e-2) speed = std::max(speed, std::abs(next.momentum_y[f] / inertia.y[f]));
        for (std::size_t f = 0; f < next.momentum_x.size(); ++f) if (inertia.x[f] > 1e-2) speed = std::max(speed, std::abs(next.momentum_x[f] / inertia.x[f]));
        std::cout << "settled advance: substeps " << advanced.report.substeps << " rejected " << advanced.report.rejected_attempts << " max speed " << speed << " m/s\n";
        require(speed < 5, "settled state stays near rest");
        std::cout << "IMPORT PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "IMPORT FAIL: " << e.what() << '\n';
        return 1;
    }
}
