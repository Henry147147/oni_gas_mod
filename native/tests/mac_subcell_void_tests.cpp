#include "mac_fixture.hpp"
#include "mixed_atmospheres/mac_kernel.hpp"
#include <bit>
#include <iomanip>

namespace {
using namespace ma::testing;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class F> void rejects(F&& f, const char* message) {
    bool rejected = false;
    try { f(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, message);
}
MacClosedSettings settings() {
    MacClosedSettings s;
    s.numerical_kernel = MacNumericalKernel::production;
    s.vacuum = MacVacuumRepresentation::void_phase;
    s.reconstruction = MacMaterialReconstruction::interface_plic;
    s.subcell_void = MacSubcellVoidPolicy::expand_unresolved_gas;
    return s;
}
MacTransportState isolated(double volume, bool gas, bool liquid, std::size_t height = 2) {
    const auto& s = setup();
    MacTransportState state;
    state.grid = {2, height, 1, 1, false, false, std::vector<std::uint8_t>(2 * height, 1)};
    state.occupied_volumes_m3.assign(2 * height, 0);
    for (std::size_t i = 0; i < 2 * height; ++i) {
        MacMaterialCell cell{MixtureInventory(s.registry.size()), {}};
        if (i % 4 == 0) {
            state.grid.walls[i] = 0;
            if (gas) cell.mixture.masses[s.registry.index("oxygen")] = 1e-20;
            if (liquid) cell.mixture.masses[s.registry.index("water")] = gas ? 1e-20 : 1000 * volume;
            if (gas || liquid) {
                cell.mixture = s.eos->initialize_at_temperature(cell.mixture.masses, 300, volume).mixture;
                cell.passive_quantities = {{17, 1e-30}};
                state.occupied_volumes_m3[i] = volume;
            }
        }
        state.cells.push_back(std::move(cell));
    }
    const auto inertia = mac_face_inertias(s.registry, state, *s.eos);
    state.momentum_x.assign(inertia.x.size(), 0); state.momentum_y.assign(inertia.y.size(), 0);
    return state;
}
void conservation_and_entropy() {
    const auto& s = setup();
    for (const auto liquid : {false, true}) {
        const auto state = isolated(1e-18, true, liquid);
        const MacClosedWorld world(s.registry, s.eos, state, settings());
        const auto before = s.eos->evaluate(world.state().cells[0].mixture, 1e-18);
        const auto changed = world.advanced(1e-4);
        const auto& next = changed.world.state();
        require(changed.report.subcell_void_events == 1 && changed.report.subcell_void_entropy_j_per_k > 0,
            "Unresolved expansion was not counted with positive entropy");
        require(next.occupied_volumes_m3.empty(), "Filled endpoint did not use canonical occupied volumes");
        require(next.momentum_x == state.momentum_x && next.momentum_y == state.momentum_y,
            "Unresolved expansion changed canonical momentum");
        for (std::size_t i = 0; i < state.cells.size(); ++i) {
            const auto& a = state.cells[i]; const auto& b = next.cells[i];
            require(a.mixture.masses == b.mixture.masses && a.mixture.energy == b.mixture.energy &&
                same_mac_cell_internal_energy(a, b) && a.passive_quantities.size() == b.passive_quantities.size(),
                "Unresolved expansion changed an inventory or primary U");
            for (std::size_t p = 0; p < a.passive_quantities.size(); ++p)
                require(a.passive_quantities[p].identity == b.passive_quantities[p].identity &&
                    a.passive_quantities[p].quantity == b.passive_quantities[p].quantity,
                    "Unresolved expansion changed a passive quantity");
        }
        const auto after = s.eos->evaluate(next.cells[0].mixture, 1);
        const double increase = after.entropy_j_per_k - before.entropy_j_per_k;
        require(std::abs(increase - changed.report.subcell_void_entropy_j_per_k) <=
            128 * std::numeric_limits<double>::epsilon() * std::abs(increase),
            "Subcell entropy account disagrees with the complete reference EOS");
        require(std::abs(changed.report.energy_residual_j) <= changed.report.energy_bound_j,
            "Subcell expansion failed its unchanged interval energy audit");
        std::cout << "SUBCELL liquid=" << liquid << " events=" << changed.report.subcell_void_events
            << " entropy=" << changed.report.subcell_void_entropy_j_per_k << " reference_entropy=" << increase << '\n';
    }
}
void admission_and_threshold() {
    const auto& s = setup(); const auto base = settings();
    for (const auto volume : {16 * std::numeric_limits<double>::epsilon(),
            std::nextafter(16 * std::numeric_limits<double>::epsilon(), 1.0)}) {
        const auto state = isolated(volume, true, false);
        const auto result = MacClosedWorld(s.registry, s.eos, state, base).advanced(1e-4);
        require(result.report.subcell_void_events == (volume == 16 * std::numeric_limits<double>::epsilon() ? 1U : 0U),
            "Unresolved expansion changed its authorized threshold");
    }
    for (const auto gas : {false, true}) {
        auto exact = base; exact.subcell_void = MacSubcellVoidPolicy::exact_geometry;
        const auto state = isolated(1e-18, gas, !gas);
        const auto kept = MacClosedWorld(s.registry, s.eos, state, exact).advanced(1e-4);
        require(kept.report.subcell_void_events == 0 && kept.world.state().occupied_volumes_m3 == state.occupied_volumes_m3,
            "Exact policy changed subcell geometry");
        if (!gas) {
            const auto liquid = MacClosedWorld(s.registry, s.eos, state, base).advanced(1e-4);
            require(liquid.report.subcell_void_events == 0 && liquid.world.state().occupied_volumes_m3 == state.occupied_volumes_m3,
                "Liquid-only material expanded into its void");
        }
    }
    auto bad = base; bad.subcell_void = static_cast<MacSubcellVoidPolicy>(2);
    rejects([&] { bad.validate(); }, "Unknown subcell policy passed validation");
    bad = base; bad.vacuum = MacVacuumRepresentation::empty_cells;
    rejects([&] { bad.validate(); }, "Subcell expansion without the void phase passed validation");
    bad = base; bad.numerical_kernel = MacNumericalKernel::reference;
    rejects([&] { bad.validate(); }, "Reference kernel admitted unresolved expansion");
    bad = base; bad.momentum_reconstruction = MacMomentumReconstruction::swept_nodal_1d;
    rejects([&] { bad.validate(); }, "Unsupported production selector admitted unresolved expansion");
    rejects([&] { (void)advance_mac_closed_substep(s.registry, *s.eos, isolated(1e-18, true, false), 1e-4, base); },
        "Reference substep silently ignored unresolved expansion");
    auto range = isolated(1e-18, true, false);
    range.cells[0].mixture.masses[s.registry.index("oxygen")] = 0x1p460;
    range.cells[0].mixture = s.eos->initialize_at_temperature(range.cells[0].mixture.masses, 300, 1e-18).mixture;
    const MacClosedWorld flagged(s.registry, s.eos, range, base); const auto original = flagged.checkpoint();
    rejects([&] { (void)flagged.advanced(1e-4); }, "Unresolved expansion silently fell back to the reference path");
    require(flagged.checkpoint() == original, "Rejected reference fallback changed the owner");
}
void put(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint64_t value, std::size_t count = 4) {
    for (std::size_t i = 0; i < count; ++i) bytes.at(at + i) = static_cast<std::uint8_t>(value >> (8 * i));
}
void seal(std::vector<std::uint8_t>& bytes) {
    std::uint64_t hash = UINT64_C(14695981039346656037);
    for (const auto b : std::span(bytes).first(bytes.size() - 8)) hash = (hash ^ b) * UINT64_C(1099511628211);
    put(bytes, bytes.size() - 8, hash, 8);
}
void checkpoints_and_threads() {
    const auto& s = setup();
    for (const auto carried : {false, true}) {
        auto state = isolated(1e-18, true, true);
        if (carried) state.cells[0].energy_remainder_j.projected = std::ldexp(state.cells[0].mixture.energy, -55);
        const MacClosedWorld source(s.registry, s.eos, state, settings()); const auto image = source.checkpoint();
        require(image[8] == 12 && image[9] == 0 && image[10] == 0 && image[11] == 0, "Subcell policy did not select version 12");
        const auto restored = MacClosedWorld::from_checkpoint(s.registry, s.eos, image);
        require(restored.settings().subcell_void == MacSubcellVoidPolicy::expand_unresolved_gas &&
            restored.settings().phase_change == MacPhaseChange::none && restored.checkpoint() == image,
            "Saved subcell policy or primary U changed on restore");
        const auto expected = source.advanced(1e-4); const auto resumed = restored.advanced(1e-4);
        require(expected.world.checkpoint() == resumed.world.checkpoint() &&
            expected.report.subcell_void_events == resumed.report.subcell_void_events &&
            expected.report.subcell_void_entropy_j_per_k == resumed.report.subcell_void_entropy_j_per_k,
            "Subcell checkpoint lost exact continuation or its entropy account");
        const auto endpoint = expected.world.checkpoint();
        const auto filled = MacClosedWorld::from_checkpoint(s.registry, s.eos, endpoint);
        require(filled.checkpoint() == endpoint && filled.advanced(1e-4).world.checkpoint() == expected.world.advanced(1e-4).world.checkpoint(),
            "Filled subcell endpoint lost canonical checkpoint continuation");
        std::uint32_t identity = 0;
        for (std::size_t i = 0; i < 4; ++i) identity |= static_cast<std::uint32_t>(image[12 + i]) << (8 * i);
        const std::size_t policy = 16 + identity + 16 + 26 + 89;
        require(image.at(policy) == 1, "Subcell policy has an unexpected wire position");
        for (const auto invalid : {0U, 2U, UINT32_MAX}) {
            auto corrupt = image; put(corrupt, policy, invalid); seal(corrupt);
            rejects([&] { (void)MacClosedWorld::from_checkpoint(s.registry, s.eos, corrupt); }, "Unknown/noncanonical saved subcell policy was accepted");
        }
        for (const auto offset : {policy - 4, policy - 8, policy - 28}) {
            auto corrupt = image; put(corrupt, offset, 99); seal(corrupt);
            rejects([&] { (void)MacClosedWorld::from_checkpoint(s.registry, s.eos, corrupt); }, "Malformed complete subcell settings were accepted");
        }
        auto truncated = image; truncated.erase(truncated.end() - 9); seal(truncated);
        rejects([&] { (void)MacClosedWorld::from_checkpoint(s.registry, s.eos, truncated); }, "Truncated subcell image was accepted");
        require(source.checkpoint() == image, "Failed checkpoint admission changed its source");
    }
    // 12 fixed blocks of isolated cells exercise the actual 12-thread path.
    const MacClosedWorld source(s.registry, s.eos, isolated(1e-18, true, true, 6144), settings());
    MacKernelOptions one, twelve; twelve.threads = 12;
    const auto a = advance_mac_production_flow(source, 1e-4, 1024, one);
    const auto b = advance_mac_production_flow(source, 1e-4, 1024, twelve);
    require(last_mac_production_report().threads_used == 12 && a.world.checkpoint() == b.world.checkpoint() &&
        a.report.subcell_void_events == 3072 && a.report.subcell_void_events == b.report.subcell_void_events &&
        a.report.subcell_void_entropy_j_per_k == b.report.subcell_void_entropy_j_per_k,
        "Unresolved expansion depends on the execution thread count");
    std::cout << "SUBCELL threads=1,12 events=" << a.report.subcell_void_events << " exact_continuation=1\n";
}
}
int main() {
    try {
        std::cout << std::setprecision(17);
        conservation_and_entropy(); admission_and_threshold(); checkpoints_and_threads();
        std::cout << "PASS audited opt-in subcell expansion, unchanged conserved fields, complete-EOS entropy and fixed threshold\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
