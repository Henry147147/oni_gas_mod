#include "mac_fixture.hpp"
#include "../src/mac_interval_audit.hpp"
#include <bit>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <numbers>
#include <string_view>

namespace {
using namespace ma;
using namespace ma::experimental;
void check(bool yes, const char* message) { if (!yes) throw std::runtime_error(message); }
MacClosedSettings settings() {
    MacClosedSettings s; s.numerical_kernel = MacNumericalKernel::interval_audited;
    s.reconstruction = MacMaterialReconstruction::donor_cell; s.momentum_reconstruction = MacMomentumReconstruction::donor_cell;
    s.compression_viscosity = MacCompressionViscosity::none; s.shock_heat = MacShockHeatRegularization::none;
    s.maximum_timestep_s = .001; return s;
}
MacTransportState scene(std::size_t w, std::size_t h, bool periodic, bool walls = false) {
    const auto& fixture = ma::testing::setup(); auto state = fixture.state(w, h, periodic, false, walls);
    for (std::size_t i = 0; i < state.cells.size(); ++i) if (!state.grid.walls[i]) {
        const double phase = 2 * std::numbers::pi * (static_cast<double>(i % w) + .5) / static_cast<double>(w);
        state.cells[i].mixture = fixture.mixture(1, 100000 + 20 * std::sin(phase), 300 + .01 * std::cos(phase), 0, .22 + .001 * std::cos(phase));
    }
    const auto m = mac_face_inertias(fixture.registry, state, *fixture.eos); const auto& g = state.grid;
    for (std::size_t y = 0; y < g.height; ++y) for (std::size_t x = 0; x < g.x_width(); ++x) {
        const auto l = (x + w - 1) % w + y * w, r = (x % w) + y * w;
        if ((x > 0 || g.periodic_x) && (x < w || g.periodic_x) && !g.walls[l] && !g.walls[r])
            state.momentum_x[g.x_face(x, y)] = m.x[g.x_face(x, y)] * (.03 + .01 * std::cos(static_cast<double>(x + 2 * y)));
    }
    for (std::size_t y = 0; y < g.y_height(); ++y) for (std::size_t x = 0; x < g.width; ++x) {
        const auto l = x + ((y + h - 1) % h) * w, r = x + (y % h) * w;
        if ((y > 0 || g.periodic_y) && (y < h || g.periodic_y) && !g.walls[l] && !g.walls[r])
            state.momentum_y[g.y_face(x, y)] = m.y[g.y_face(x, y)] * (.02 * std::sin(static_cast<double>(x + y)));
    }
    return state;
}
void export_state(int id, int endpoint, const MacClosedWorld& world) {
    const auto& state = world.state();
    for (std::size_t i = 0; i < state.cells.size(); ++i) {
        const auto& c = state.cells[i]; const auto t = state.grid.walls[i] ? ReferenceEquilibriumState{} : world.thermodynamics().evaluate(c.mixture, state.grid.volume_m3());
        std::cout << "ICELL " << id << ' ' << endpoint << ' ' << i << ' ' << static_cast<int>(state.grid.walls[i]) << ' ' << c.mixture.energy << ' '
            << t.temperature_k << ' ' << t.mechanical_pressure_pa << '\n';
        for (std::size_t k = 0; k < c.mixture.masses.size(); ++k) if (c.mixture.masses[k] != 0)
            std::cout << "IMASS " << id << ' ' << endpoint << ' ' << i << ' ' << k << ' ' << c.mixture.masses[k] << '\n';
        for (const auto& p : c.passive_quantities) std::cout << "IPASSIVE " << id << ' ' << endpoint << ' ' << i << ' ' << p.identity << ' ' << p.quantity << '\n';
    }
    for (int a = 0; a < 2; ++a) {
        const auto& j = a == 0 ? state.momentum_x : state.momentum_y;
        for (std::size_t f = 0; f < j.size(); ++f) std::cout << "IFACE " << id << ' ' << endpoint << ' ' << a << ' ' << f << ' ' << j[f] << '\n';
    }
}
void export_models(int id, const MacClosedWorld& world) {
    for (const auto& m : world.thermodynamics().reference().materials())
        std::cout << "IGAS " << id << ' ' << world.registry().index(m.id) << ' ' << static_cast<int>(m.phase) << ' ' << m.molar_mass_kg_per_mol << ' '
            << m.cv_j_per_kg_k << ' ' << m.energy_offset_j_per_kg << '\n';
}
void export_interval(int id, const MacClosedWorld& before, const MacClosedAdvance& after) {
    const auto& g = before.state().grid; const auto& r = after.report;
    std::cout << "ICASE " << id << ' ' << g.width << ' ' << g.height << ' ' << g.periodic_x << ' ' << g.periodic_y << ' ' << g.volume_m3() << ' '
        << r.substeps << ' ' << r.rejected_attempts << ' ' << r.interval_kernel_intervals << ' ' << r.energy_residual_j << ' ' << r.energy_bound_j << ' '
        << r.entropy_change_j_per_k << ' ' << r.entropy_roundoff_j_per_k << ' ' << r.wall_impulse_x << ' ' << r.wall_impulse_y << '\n';
    export_models(id, before); export_state(id, 0, before); export_state(id, 1, after.world);
}
void compare(int id, std::size_t w, std::size_t h, bool periodic, bool walls) {
    const auto& fixture = ma::testing::setup(); auto input = scene(w, h, periodic, walls);
    if (id == 6) {
        for (std::size_t y = 0; y < h; ++y) {
            const auto i = w / 2 + y * w; input.grid.walls[i] = 1;
            input.cells[i].mixture = MixtureInventory(fixture.registry.size()); input.cells[i].passive_quantities.clear();
            input.momentum_x[input.grid.x_face(w / 2, y)] = input.momentum_x[input.grid.x_face(w / 2 + 1, y)] = 0;
            input.momentum_y[input.grid.y_face(w / 2, y)] = input.momentum_y[input.grid.y_face(w / 2, y + 1)] = 0;
        }
    }
    auto s = settings(); if (id >= 5) s.maximum_timestep_s = .01;
    const double duration = id >= 5 ? .02 : .005, continuation = id >= 5 ? .01 : .003;
    const MacClosedWorld owner(fixture.registry, fixture.eos, input, s); const auto held = owner.checkpoint();
    auto next = owner.advanced(duration); check(next.report.interval_kernel_intervals == 1 && next.report.interval_reference_fallbacks == 0, "Supported scene declined interval kernel");
    s.numerical_kernel = MacNumericalKernel::reference; const MacClosedWorld reference(fixture.registry, fixture.eos, input, s);
    const auto expected = reference.advanced(duration);
    double maximum = 0;
    for (std::size_t i = 0; i < input.cells.size(); ++i) if (!input.grid.walls[i]) {
        const auto a = fixture.eos->evaluate(next.world.state().cells[i].mixture, 1), b = fixture.eos->evaluate(expected.world.state().cells[i].mixture, 1);
        maximum = std::max({maximum, std::abs(a.temperature_k - b.temperature_k) / 300, std::abs(a.mechanical_pressure_pa - b.mechanical_pressure_pa) / 20});
        for (std::size_t k = 0; k < fixture.registry.size(); ++k) if (input.cells[i].mixture.masses[k] > 0)
            maximum = std::max(maximum, std::abs(next.world.state().cells[i].mixture.masses[k] - expected.world.state().cells[i].mixture.masses[k]) / input.cells[i].mixture.masses[k]);
    }
    check(maximum < 1e-8, "Interval port differs from selected reference laws");
    check(owner.checkpoint() == held, "Interval kernel mutated source owner");
    const auto bytes = next.world.checkpoint(); check(bytes[8] == 4, "Interval kernel checkpoint lacks versioned setting");
    const auto restored = MacClosedWorld::from_checkpoint(fixture.registry, fixture.eos, bytes);
    check(restored.settings().numerical_kernel == MacNumericalKernel::interval_audited && restored.checkpoint() == bytes, "Interval checkpoint lost kernel or state");
    const auto warm = next.world.advanced(continuation), cold = restored.advanced(continuation);
    check(warm.world.checkpoint() == cold.world.checkpoint(), "Restored interval continuation differs");
    if (id >= 5) {
        check(next.report.interval_multigrid_solves > 0 && next.report.interval_multigrid_levels >= 2 && next.report.interval_multigrid_fallbacks == 0 &&
            next.report.interval_topology_builds == 1 && warm.report.interval_topology_builds == 0 && cold.report.interval_topology_builds == 1,
            "Automatic multigrid or immutable topology reuse was not exercised");
        bool failed = false;
        try { static_cast<void>(owner.advanced(duration, 1)); } catch (const std::runtime_error&) { failed = true; }
        check(failed && owner.checkpoint() == held && owner.advanced(duration).world.checkpoint() == bytes,
            "Failed interval changed cached-topology continuation or published a partial state");
        auto js = owner.settings(); js.preconditioner = PressurePreconditioner::jacobi;
        const auto jacobi = MacClosedWorld(fixture.registry, fixture.eos, input, js).advanced(duration);
        check(jacobi.report.interval_multigrid_solves == 0 && jacobi.report.substeps == next.report.substeps && jacobi.report.rejected_attempts == 0,
            "Multigrid changed nominal timestep admission or ignored explicit Jacobi");
        double difference = 0;
        for (std::size_t i = 0; i < input.cells.size(); ++i) if (!input.grid.walls[i]) {
            const auto a = fixture.eos->evaluate(next.world.state().cells[i].mixture, 1), b = fixture.eos->evaluate(jacobi.world.state().cells[i].mixture, 1);
            difference = std::max({difference, std::abs(a.mechanical_pressure_pa - b.mechanical_pressure_pa) / 20, std::abs(a.temperature_k - b.temperature_k) / 300});
            for (std::size_t k = 0; k < fixture.registry.size(); ++k) if (input.cells[i].mixture.masses[k] > 0)
                difference = std::max(difference, std::abs(next.world.state().cells[i].mixture.masses[k] - jacobi.world.state().cells[i].mixture.masses[k]) / input.cells[i].mixture.masses[k]);
        }
        check(difference < 1e-8, "Automatic multigrid changed the resolved selected physics");
        const std::array<MacCellHeat, 1> edits{{{0, .01, 1e6}}}; const auto heated = next.world.with_cell_heat(edits);
        const auto hot = heated.advanced(continuation); const auto hot_cold = MacClosedWorld::from_checkpoint(fixture.registry, fixture.eos, heated.checkpoint()).advanced(continuation);
        check(hot.report.interval_topology_builds == 0 && hot.world.checkpoint() == hot_cold.world.checkpoint(), "Heat edit retained stale coefficients or lost topology reuse");
        if (id == 5) {
            auto bare = input; for (auto& cell : bare.cells) cell.passive_quantities.clear();
            auto active = MacClosedWorld(fixture.registry, fixture.eos, bare, owner.settings()).advanced(duration).world;
            const std::array<MacGasTransfer, 1> transfer{{{0, fixture.registry.index("oxygen"), true, .0001, 300}}};
            const auto changed = active.with_gas_transfers(transfer).world;
            const auto after = changed.advanced(continuation), fresh = MacClosedWorld::from_checkpoint(fixture.registry, fixture.eos, changed.checkpoint()).advanced(continuation);
            check(after.report.interval_topology_builds == 0 && after.world.checkpoint() == fresh.world.checkpoint(), "Mass edit retained stale pressure coefficients or lost topology reuse");
        }
        std::cout << "IMG " << id << ' ' << next.report.interval_multigrid_solves << ' ' << next.report.interval_multigrid_cycles << ' '
            << next.report.interval_multigrid_levels << ' ' << next.report.interval_topology_builds << ' ' << warm.report.interval_topology_builds << ' '
            << cold.report.interval_topology_builds << ' ' << difference << " 1\n";
    }
    export_interval(id, owner, next); std::cout << "ICOMPARISON " << id << ' ' << maximum << " 1\n";
}
void audit_boundaries() {
    using interval_detail::Account;
    std::size_t exact_count = 0;
    for (const double value : {std::nextafter(0x1p-45, 0.), 0x1p-45, std::nextafter(0x1p-45, 1.), 1e-300}) {
        Account account; const std::array<double, 3> operands{1., -1., value};
        for (const double operand : operands) account.add(operand);
        const auto replay = [&](auto& exact) { for (const double operand : operands) exact.add(operand); };
        const auto result = account.decide(replay); MacEnergyAccount exact; replay(exact);
        check(result.passed == exact.within_bound(), "Double-double audit changed an exact decision"); exact_count += result.exact ? 1U : 0U;
    }
    // The exact operand-scale bound here is 2^-44/(1-2^-45).
    for (int direction : {-1, 1}) {
        double v = 0x1p-44;
        for (int step = 0; step < 257; ++step) {
            Account account; account.add(1); account.add(-1); account.add(v);
            const auto replay = [&](auto& exact) { exact.add(1); exact.add(-1); exact.add(v); };
            MacEnergyAccount exact; replay(exact); const auto result = account.decide(replay);
            check(result.passed == exact.within_bound(), "Near-bound interval audit changed exact admission"); exact_count += result.exact ? 1U : 0U;
            v = std::nextafter(v, direction < 0 ? 0. : 1.);
        }
    }
    check(exact_count > 0, "Interval audit tests missed exact expansion escalation");
    interval_detail::StoredSum stored;
    for (const double v : {1., 0x1p-70, 0x1p-140, -0x1p-70}) stored.add(v);
    check(stored.discarded && stored.value() == 1, "Stored gather hid a discarded tail after cancellation");
    std::cout << "IAUDITS 518 " << exact_count << '\n';
}
void fallback_and_rollback() {
    const auto& fixture = ma::testing::setup();
    for (int id = 0; id < 4; ++id) {
        auto input = scene(4, 3, true); auto s = settings();
        if (id == 0) s.reconstruction = MacMaterialReconstruction::limited_linear;
        if (id == 1) for (auto& c : input.cells) c.mixture.masses[fixture.registry.index("helium")] = 1e-310;
        if (id == 2) input = fixture.state(4, 3, true, true);
        if (id == 3) { input.cells[0].mixture = MixtureInventory(fixture.registry.size()); input.cells[0].passive_quantities.clear(); }
        const MacClosedWorld owner(fixture.registry, fixture.eos, input, s); const auto held = owner.checkpoint(); const auto next = owner.advanced(.00001);
        s.numerical_kernel = MacNumericalKernel::reference; const MacClosedWorld reference(fixture.registry, fixture.eos, input, s);
        const auto expected = reference.advanced(.00001);
        check(next.report.interval_reference_fallbacks == 1 && next.report.interval_kernel_intervals == 0 &&
            ma::testing::fingerprint(next.world.state()) == ma::testing::fingerprint(expected.world.state()) && owner.checkpoint() == held,
            "Interval fallback changed selected reference physics or source state");
    }
    auto input = scene(4, 3, true); for (auto& j : input.momentum_x) j *= 1e6;
    auto s = settings(); s.maximum_timestep_s = 1;
    const MacClosedWorld owner(fixture.registry, fixture.eos, input, s); const auto held = owner.checkpoint(); bool failed = false;
    try { static_cast<void>(owner.advanced(1, 1)); } catch (const std::runtime_error&) { failed = true; }
    check(failed && owner.checkpoint() == held, "Exhausted interval published a partial candidate");
    auto bad = settings(); bad.numerical_kernel = static_cast<MacNumericalKernel>(99); failed = false;
    try { bad.validate(); } catch (const std::invalid_argument&) { failed = true; }
    check(failed, "Invalid interval kernel setting admitted"); std::cout << "IFALLBACK 4 1\n";
    const auto bytes = MacClosedWorld(fixture.registry, fixture.eos, scene(4, 3, true), settings()).checkpoint();
    std::size_t model_bytes = 0;
    for (std::size_t k = 0; k < 4; ++k) model_bytes |= static_cast<std::size_t>(bytes[12 + k]) << (8 * k);
    const auto kernel_offset = 16 + model_bytes + 103;
    check(bytes.at(kernel_offset) == 1, "Checkpoint control missed version-4 kernel field");
    for (const std::uint32_t value : {0U, 99U}) {
        auto corrupt = bytes;
        for (std::size_t k = 0; k < 4; ++k) corrupt[kernel_offset + k] = static_cast<std::uint8_t>(value >> (8 * k));
        std::uint64_t checksum = 14695981039346656037ULL;
        for (std::size_t k = 0; k + 8 < corrupt.size(); ++k) { checksum ^= corrupt[k]; checksum *= 1099511628211ULL; }
        for (std::size_t k = 0; k < 8; ++k) corrupt[corrupt.size() - 8 + k] = static_cast<std::uint8_t>(checksum >> (8 * k));
        failed = false;
        try { static_cast<void>(MacClosedWorld::from_checkpoint(fixture.registry, fixture.eos, corrupt)); } catch (const std::invalid_argument&) { failed = true; }
        check(failed, "Noncanonical/unknown saved numerical kernel admitted");
    }
    auto viscous = settings(); viscous.viscosity = {.1, .2};
    const auto viscous_bytes = MacClosedWorld(fixture.registry, fixture.eos, scene(4, 3, true), viscous).checkpoint();
    check(MacClosedWorld::from_checkpoint(fixture.registry, fixture.eos, viscous_bytes).checkpoint() == viscous_bytes,
        "Version-4 viscosity/settings continuation lost identity");
    std::cout << "ISAVEGUARDS 3 1\n";
    auto models = fixture.eos->reference().materials();
    for (auto& model : models) if (model.phase == ReferencePhase::ideal_gas) model.minimum_temperature_k = 300 - 1e-12;
    const auto boundary_eos = std::make_shared<const FluidThermodynamics>(fixture.registry, "interval-thermal-boundary-v1", models);
    const MacClosedWorld boundary(fixture.registry, boundary_eos, fixture.state(4, 3, true), settings());
    const auto boundary_before = boundary.checkpoint(); const auto boundary_after = boundary.advanced(.001);
    check(boundary_after.report.interval_reference_fallbacks == 1 && boundary.checkpoint() == boundary_before,
        "Uncertain material temperature boundary did not refine through reference EOS");
    std::cout << "IEOSGUARD 1 1\n";
    auto ranged = scene(4, 3, true);
    for (auto& c : ranged.cells) c.mixture.masses[fixture.registry.index("helium")] = 0x1p-450;
    const MacClosedWorld ranged_owner(fixture.registry, fixture.eos, ranged, settings()); const auto ranged_held = ranged_owner.checkpoint();
    failed = false;
    try { static_cast<void>(ranged_owner.advanced(.00001, 1)); } catch (const std::runtime_error&) { failed = true; }
    check(failed && ranged_owner.checkpoint() == ranged_held, "Mid-step range fallback reset its attempt budget or published partial state");
    const auto ranged_after = ranged_owner.advanced(.00001, 2);
    auto reference_settings = settings(); reference_settings.numerical_kernel = MacNumericalKernel::reference;
    const auto ranged_reference = MacClosedWorld(fixture.registry, fixture.eos, ranged, reference_settings).advanced(.00001, 1);
    check(ranged_after.report.attempts == 2 && ranged_after.report.rejected_attempts == 1 && ranged_after.report.interval_reference_fallbacks == 1 &&
        ma::testing::fingerprint(ranged_after.world.state()) == ma::testing::fingerprint(ranged_reference.world.state()),
        "Mid-step fallback lost consumed attempts or selected reference continuation");
    std::cout << "IPOSTFALLBACK 2 1\n";
}
void endurance() {
    const auto& fixture = ma::testing::setup(); auto s = settings(); s.maximum_timestep_s = .0001;
    const MacClosedWorld initial(fixture.registry, fixture.eos, scene(4, 2, true), s); auto world = initial;
    for (int i = 0; i < 10000; ++i) {
        auto next = world.advanced(.0001); check(next.report.interval_kernel_intervals == 1 && next.report.substeps == 1, "Endurance left the interval path");
        world = std::move(next.world);
    }
    MacClosedAdvance final{world, {}}; final.report.substeps = 10000; final.report.interval_kernel_intervals = 1;
    // Export the aggregate accounts, independently from per-interval reports.
    const auto a = mac_face_inertias(fixture.registry, initial.state(), *fixture.eos), b = mac_face_inertias(fixture.registry, world.state(), *fixture.eos);
    MacEnergyAccount energy; interval_detail::Sum entropy, allowance;
    for (std::size_t i = 0; i < world.state().cells.size(); ++i) {
        const auto& c0 = initial.state().cells[i].mixture; const auto& c1 = world.state().cells[i].mixture;
        energy.add(c1.energy); energy.add(-c0.energy);
        const auto t0 = fixture.eos->evaluate(c0, 1), t1 = fixture.eos->evaluate(c1, 1);
        entropy.add(t1.entropy_j_per_k); entropy.add(-t0.entropy_j_per_k);
        allowance.add(128 * std::numeric_limits<double>::epsilon() * (std::abs(c0.energy) / t0.temperature_k + std::abs(c1.energy) / t1.temperature_k));
    }
    for (int axis = 0; axis < 2; ++axis) {
        const auto& j0 = axis == 0 ? initial.state().momentum_x : initial.state().momentum_y; const auto& j1 = axis == 0 ? world.state().momentum_x : world.state().momentum_y;
        const auto& m0 = axis == 0 ? a.x : a.y; const auto& m1 = axis == 0 ? b.x : b.y;
        for (std::size_t f = 0; f < j0.size(); ++f) { energy.add(MacEnergy::kinetic(m1[f], j1[f])); energy.add(MacEnergy::kinetic(m0[f], j0[f]).negated()); }
    }
    check(energy.within_bound(), "10000-step endpoint energy account failed");
    final.report.energy_residual_j = energy.residual().project().value; final.report.energy_bound_j = energy.bound().project().value;
    final.report.entropy_change_j_per_k = entropy.value(); final.report.entropy_roundoff_j_per_k = allowance.value();
    export_interval(3, initial, final); std::cout << "IENDURANCE 10000 1\n";
}
void catalogue() {
    SpeciesRegistry registry; std::vector<ReferenceMaterial> models;
    const auto& fixture = ma::testing::setup(); const auto n2 = fixture.registry[fixture.registry.index("nitrogen")];
    for (std::size_t k = 0; k < 81; ++k) {
        auto gas = k == 80 ? fixture.registry[fixture.registry.index("helium")] : n2;
        gas.id = "interval-gas-" + std::to_string(k); registry.add(gas);
        ReferenceMaterial m; m.id = gas.id; m.molar_mass_kg_per_mol = gas.molar_mass; m.cv_j_per_kg_k = gas.cv();
        m.energy_offset_j_per_kg = gas.internal_energy(0); m.minimum_temperature_k = 1; m.maximum_temperature_k = 1e6;
        m.provenance = "Catalogue-size numerical control, same declared ideal-gas reference"; models.push_back(m);
    }
    const auto eos = std::make_shared<const FluidThermodynamics>(registry, "interval-catalogue-control-v1", models);
    MacTransportState input; input.grid = {10, 8, 1, 1, true, true, std::vector<std::uint8_t>(80)};
    for (std::size_t i = 0; i < 80; ++i) {
        std::vector<double> mass(registry.size()); mass[i] = 1 + .01 * std::cos(static_cast<double>(i)); mass[80] = mass[i] * 1e-28;
        auto mixture = eos->initialize_at_temperature(mass, 300, 1).mixture;
        input.cells.push_back({std::move(mixture), {{17, 1.}, {1000 + i, .5}, {UINT64_C(0x80000000), 1e-30}}});
    }
    input.momentum_x.assign(80, .01); input.momentum_y.assign(80, -.02);
    const MacClosedWorld owner(registry, eos, input, settings()); const auto next = owner.advanced(.005);
    check(next.report.interval_kernel_intervals == 1, "Catalogue-size scene declined interval kernel");
    const auto restored = MacClosedWorld::from_checkpoint(registry, eos, next.world.checkpoint());
    check(next.world.advanced(.001).world.checkpoint() == restored.advanced(.001).world.checkpoint(), "Large catalogue continuation differs");
    export_interval(4, owner, next);
}
void benchmark(std::size_t w, std::size_t h, int count, double maximum_dt, bool reference, bool jacobi) {
    const auto& fixture = ma::testing::setup(); auto s = settings(); s.maximum_timestep_s = maximum_dt;
    if (reference) s.numerical_kernel = MacNumericalKernel::reference;
    if (jacobi) s.preconditioner = PressurePreconditioner::jacobi;
    MacClosedWorld world(fixture.registry, fixture.eos, scene(w, h, true), s); std::vector<double> milliseconds;
    std::size_t substeps = 0, rejected = 0, minimum_changed = w * h;
    std::size_t pressure_iterations = 0, nonlinear_iterations = 0, multigrid_solves = 0, multigrid_cycles = 0, topology_builds = 0;
    for (int interval = -1; interval < count; ++interval) {
        const auto start = std::chrono::steady_clock::now(); auto next = world.advanced(.1);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        check(reference || next.report.interval_kernel_intervals == 1, "Benchmark left interval kernel");
        std::size_t changed = 0;
        for (std::size_t i = 0; i < world.state().cells.size(); ++i)
            if (world.state().cells[i].mixture.masses != next.world.state().cells[i].mixture.masses ||
                world.state().cells[i].mixture.energy != next.world.state().cells[i].mixture.energy) ++changed;
        minimum_changed = std::min(minimum_changed, changed);
        if (interval >= 0) {
            milliseconds.push_back(ms); substeps += next.report.substeps; rejected += next.report.rejected_attempts;
            pressure_iterations += next.report.pressure_iterations; nonlinear_iterations += next.report.nonlinear_iterations;
            multigrid_solves += next.report.interval_multigrid_solves; multigrid_cycles += next.report.interval_multigrid_cycles;
            topology_builds += next.report.interval_topology_builds;
        }
        world = std::move(next.world);
    }
    std::sort(milliseconds.begin(), milliseconds.end());
    std::cout << "IBENCHMARK " << w * h << ' ' << count << ' ' << milliseconds[milliseconds.size() / 2] << ' '
        << milliseconds.back() << ' ' << substeps << ' ' << rejected << ' ' << maximum_dt << ' ' << reference << ' ' << minimum_changed << ' '
        << jacobi << ' ' << pressure_iterations << ' ' << nonlinear_iterations << ' ' << multigrid_solves << ' ' << multigrid_cycles << ' ' << topology_builds << '\n';
}
void write_image(const MacClosedWorld& world, const char* path) {
    const auto bytes = world.checkpoint(); std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())); file.close();
    check(static_cast<bool>(file), "Cannot write test checkpoint");
}
}
int main(int argc, char** argv) {
    try {
        require_mac_energy_environment(); std::cout << std::setprecision(17);
        if (argc == 3 && (std::string_view(argv[1]) == "--write-checkpoint" || std::string_view(argv[1]) == "--write-multigrid-checkpoint")) {
            const bool mg = std::string_view(argv[1]) == "--write-multigrid-checkpoint";
            const auto& fixture = ma::testing::setup(); auto s = settings(); if (mg) s.maximum_timestep_s = .01;
            const MacClosedWorld owner(fixture.registry, fixture.eos, scene(mg ? 16U : 12U, mg ? 16U : 4U, true), s);
            const auto next = owner.advanced(mg ? .02 : .005);
            check(!mg || next.report.interval_multigrid_solves > 0, "Checkpoint source did not use multigrid");
            write_image(next.world, argv[2]); return 0;
        }
        if (argc == 4 && (std::string_view(argv[1]) == "--continue-checkpoint" || std::string_view(argv[1]) == "--continue-multigrid-checkpoint")) {
            const bool mg = std::string_view(argv[1]) == "--continue-multigrid-checkpoint";
            const auto& fixture = ma::testing::setup(); std::ifstream file(argv[2], std::ios::binary);
            check(static_cast<bool>(file), "Cannot read test checkpoint");
            const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
            const auto owner = MacClosedWorld::from_checkpoint(fixture.registry, fixture.eos, bytes);
            check(owner.settings().numerical_kernel == MacNumericalKernel::interval_audited, "Continuation lost interval setting");
            const auto next = owner.advanced(mg ? .01 : .003); check(next.report.interval_kernel_intervals == 1, "Continuation left interval kernel");
            check(!mg || next.report.interval_multigrid_solves > 0, "Checkpoint continuation did not use multigrid");
            write_image(next.world, argv[3]); export_state(9, 1, next.world); return 0;
        }
        if ((argc == 5 || argc == 6) && (std::string_view(argv[1]) == "--benchmark" || std::string_view(argv[1]) == "--reference-benchmark" || std::string_view(argv[1]) == "--jacobi-benchmark")) {
            const auto w = static_cast<std::size_t>(std::stoull(argv[2])), h = static_cast<std::size_t>(std::stoull(argv[3]));
            const int count = std::stoi(argv[4]); check(w > 1 && h > 1 && w <= 1000000 / h && count > 0 && count <= 1000, "Invalid benchmark dimensions/count");
            const double dt = argc == 6 ? std::stod(argv[5]) : .01;
            benchmark(w, h, count, dt, std::string_view(argv[1]) == "--reference-benchmark", std::string_view(argv[1]) == "--jacobi-benchmark"); return 0;
        }
        check(argc == 1, "Unknown interval kernel test arguments"); audit_boundaries();
        compare(0, 12, 4, true, false); compare(1, 9, 5, false, true); compare(2, 7, 1, false, false);
        compare(5, 16, 16, true, false); compare(6, 19, 17, false, true);
        fallback_and_rollback(); endurance(); catalogue(); std::cout << "PASS interval kernel, reference comparisons, audited accounts and continuation\n"; return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
