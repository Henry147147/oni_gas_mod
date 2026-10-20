// Independent gather operands exercise counted normal inventory storage.
#include "mac_kernel_fixture.hpp"
#include <array>
#include <iomanip>
#include <map>
using namespace ma::testing;
namespace {
void check(bool okay, const char* why) { if (!okay) throw std::runtime_error(why); }
void controlled_gather() {
    const auto& s = setup(); auto state = s.state(4, 2, true);
    const std::array<double, 8> amounts{1., std::nextafter(1., 2.), 1e-40, .25, 0x1p100, 0x1p-100, 3., 7.};
    for (std::size_t i = 0; i < state.cells.size(); ++i)
        state.cells[i].passive_quantities = {{17, amounts[i]}, {UINT64_C(0x80000000), std::ldexp(amounts[(i + 3) % amounts.size()], -100)}};
    kernel::set_velocity(s, state, 1, .5);
    MacClosedSettings settings; settings.numerical_kernel = MacNumericalKernel::production;
    settings.reconstruction = MacMaterialReconstruction::donor_cell;
    const MacClosedWorld source(s.registry, s.eos, state, settings); const auto held = source.checkpoint();
    constexpr double dt = 0x1p-10;
    MacKernelOptions options; options.threads = 1;
    const auto next = advance_mac_production(source, dt, 64, options); const auto report = last_mac_production_report();
    check(!report.reference_fallback && report.substeps == 1 && report.rejected_attempts == 0, "Controlled gather changed its schedule");
    const auto passive = [](const MacMaterialCell& cell, std::uint64_t identity) {
        for (const auto& q : cell.passive_quantities) if (q.identity == identity) return q.quantity;
        return 0.;
    };
    std::map<std::string, std::size_t> expected; std::size_t id = 0;
    const auto field = [&](const std::string& name, const auto& value) {
        for (std::size_t i = 0; i < state.cells.size(); ++i) {
            const auto x = i % 4, y = i / 4;
            const auto left = (x == 0 ? 3 : x - 1) + y * 4, bottom = x + (y == 0 ? 1 : 0) * 4;
            const double original = value(state.cells[i]);
            std::cout << "GINIT " << name << ' ' << i << ' ' << original << '\n';
            double v = original;
            for (const double outgoing : {original * dt, original * (dt * .5)}) {
                const double before = v; v -= outgoing;
                MacEnergySum difference; difference.add(before); difference.add(-outgoing); difference.add(-v);
                const bool inexact = difference.sign() != 0; expected[name] += inexact ? 1U : 0U;
                std::cout << "GADD " << id++ << ' ' << name << ' ' << before << ' ' << -outgoing << ' ' << v << ' ' << inexact << '\n';
            }
            for (const double incoming : {value(state.cells[left]) * dt, value(state.cells[bottom]) * (dt * .5)}) {
                const double before = v; v += incoming;
                MacEnergySum difference; difference.add(before); difference.add(incoming); difference.add(-v);
                const bool inexact = difference.sign() != 0; expected[name] += inexact ? 1U : 0U;
                std::cout << "GADD " << id++ << ' ' << name << ' ' << before << ' ' << incoming << ' ' << v << ' ' << inexact << '\n';
            }
            check(value(next.world.state().cells[i]) == v, "Gather differs from independent donor-cell operands");
            std::cout << "GEND " << name << ' ' << i << ' ' << value(next.world.state().cells[i]) << '\n';
        }
    };
    for (std::size_t k = 0; k < s.registry.size(); ++k) if (state.cells[0].mixture.masses[k] != 0)
        field("mass:" + std::to_string(k), [k](const MacMaterialCell& cell) { return cell.mixture.masses[k]; });
    for (const auto identity : {UINT64_C(17), UINT64_C(0x80000000)})
        field("passive:" + std::to_string(identity), [&](const MacMaterialCell& cell) { return passive(cell, identity); });
    std::size_t total = 0;
    for (const auto& audit : report.audits) {
        check(audit.passed && audit.rounding_allowance <= audit.bound, "Gather charge exceeded the unchanged interval budget");
        check(audit.rounded_inventory_writes == expected[audit.quantity], "Transport did not count its retained and incoming inventory writes");
        total += audit.rounded_inventory_writes;
        std::cout << "GQUANTITY " << audit.quantity << ' ' << audit.rounded_inventory_writes << ' ' << audit.rounding_allowance << ' '
            << audit.bound << ' ' << audit.stage_operand_magnitude << ' ' << audit.below_range_allowance << '\n';
    }
    check(total > 0 && total == report.rounded_inventory_writes && next.report.interval_rounded_updates ==
        total + report.rounded_primary_writes + report.rounded_transfers + report.rounded_range_inputs + report.rounded_energy_writes + report.discarded_energy_remainders, "Gather counts did not reach the owner");
    options.threads = 12;
    const auto repeated = advance_mac_production(source, dt, 64, options); const auto repeated_report = last_mac_production_report();
    check(next.world.checkpoint() == repeated.world.checkpoint() && repeated_report.rounded_inventory_writes == total, "Gather changed with execution/cache history");
    bool rejected = false;
    try { (void)advance_mac_production(source, .02, 1, options); } catch (const std::runtime_error&) { rejected = true; }
    check(rejected && source.checkpoint() == held && last_mac_production_report().rounded_inventory_writes == 0, "Discarded interval published gather counts");
    const auto retried = advance_mac_production(source, dt, 64, options);
    check(retried.world.checkpoint() == next.world.checkpoint() && last_mac_production_report().rounded_inventory_writes == total, "Discarded work changed the next gather");
    std::cout << "GTOTAL " << id << ' ' << total << " 1 1 1\n";
}
}
int main() {
    try {
        require_mac_energy_environment(); std::cout << std::setprecision(17) << "GCONTRACT 128\n";
        controlled_gather(); std::cout << "GPASS\n"; return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
