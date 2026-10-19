// Resolved temporal references for the opt-in two-moment pressure stage.
// Existing default/equal-schedule controls remain unchanged.
#include "mac_kernel_fixture.hpp"
#include <filesystem>
#include <fstream>
#include <iomanip>

using namespace ma::testing;
using namespace ma::testing::kernel;
namespace {
MacPressureIntegration selection = MacPressureIntegration::two_moment;
unsigned wire_version() { return selection == MacPressureIntegration::two_moment ? 7U : 8U; }
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
void audit() {
    const auto& r = last_mac_production_report();
    check(!r.reference_fallback && !r.audits.empty(), "Moment stage bypassed the production interval audit");
    for (const auto& a : r.audits) check(a.passed, "Moment interval failed its original account");
    check(r.entropy_change_j_per_k >= -r.entropy_allowance_j_per_k, "Moment interval lost entropy");
    check(r.maximum_volume_closure <= 32 * std::numeric_limits<double>::epsilon(), "Moment stage lost geometric closure");
}
MacClosedWorld owner(Scene scene, double dt, bool moments) {
    const auto& s = setup(); scene.settings.maximum_timestep_s = dt;
    scene.settings.numerical_kernel = MacNumericalKernel::production;
    scene.settings.pressure_integration = moments ? selection : MacPressureIntegration::discrete_gradient_midpoint;
    return MacClosedWorld(s.registry, s.eos, std::move(scene.state), scene.settings);
}
struct Evolution { std::size_t substeps = 0, attempts = 0, rejected = 0; double maximum_dt = 0; };
MacClosedWorld evolve(MacClosedWorld world, std::size_t frames = 5, Evolution* totals = nullptr) {
    for (std::size_t i = 0; i < frames; ++i) {
        auto next = advance_mac_production(world, .1, 65536, mac_kernel_default_options()); audit();
        check(next.report.interval_kernel_intervals == 1, "Temporal reference used an equilibrium certificate");
        if (totals) {
            totals->substeps += next.report.substeps; totals->attempts += next.report.attempts; totals->rejected += next.report.rejected_attempts;
            totals->maximum_dt = std::max(totals->maximum_dt, last_mac_production_report().maximum_timestep_s);
        }
        world = std::move(next.world);
    }
    return world;
}
double passive_error(const MacClosedWorld& reference, const MacClosedWorld& candidate) {
    double error = 0;
    for (std::size_t i = 0; i < reference.state().cells.size(); ++i) {
        const auto& a = reference.state().cells[i].passive_quantities; const auto& b = candidate.state().cells[i].passive_quantities;
        check(a.size() == b.size(), "Moment candidate changed passive coverage");
        for (std::size_t p = 0; p < a.size(); ++p) {
            check(a[p].identity == b[p].identity, "Moment candidate changed passive identities");
            if (a[p].quantity > 0) error = std::max(error, std::abs(b[p].quantity - a[p].quantity) / a[p].quantity);
        }
    }
    return error;
}
void export_state(int id, char kind, const MacClosedWorld& world) {
    const auto& s = setup();
    const auto nitrogen = s.registry.index("nitrogen"), oxygen = s.registry.index("oxygen"), helium = s.registry.index("helium");
    for (std::size_t i = 0; i < world.state().cells.size(); ++i) {
        const auto& cell = world.state().cells[i]; const auto& mixture = cell.mixture;
        const auto eos = s.eos->evaluate(mixture, 1);
        const double o2 = eos.gas_pressure_pa * (mixture.masses[oxygen] / s.registry[oxygen].molar_mass) / mixture.gas_moles(s.registry);
        std::cout << "MSTATE " << id << ' ' << kind << ' ' << i << ' ' << mixture.energy << ' ' << eos.temperature_k << ' '
            << eos.mechanical_pressure_pa << ' ' << o2 << ' ' << mixture.masses[nitrogen] << ' ' << mixture.masses[oxygen] << ' '
            << mixture.masses[helium] << ' ' << cell.passive_quantities.at(0).quantity << ' ' << cell.passive_quantities.at(1).quantity << '\n';
    }
}
void wave_case(int id, std::size_t w, std::size_t h, double dt, double fine_dt, bool refine) {
    const auto scene = wave(w, h); const auto source = owner(scene, dt, true); const auto held = source.checkpoint();
    const auto reference = evolve(owner(scene, fine_dt, false));
    if (refine) {
        const auto finer = evolve(owner(scene, fine_dt / 2, false)); const auto m = compare(finer, reference, 20);
        check(m.temperature_k <= .01 && m.pressure_over_disturbance <= .001 && m.oxygen_relative <= .001 &&
            m.constituent_relative <= .001 && passive_error(finer, reference) <= .001, "Temporal oracle is not resolved");
        std::cout << "MREFERENCE " << id << ' ' << fine_dt << ' ' << fine_dt / 2 << ' ' << m.temperature_k << ' '
            << m.pressure_over_disturbance << ' ' << m.oxygen_relative << ' ' << m.constituent_relative << '\n';
        export_state(id, 'F', finer);
    }
    Evolution totals; const auto candidate = evolve(source, 5, &totals); const auto m = compare(reference, candidate, 20); const double passive = passive_error(reference, candidate);
    check(m.temperature_k <= .1 && m.pressure_over_disturbance <= .01 && m.oxygen_relative <= .01 &&
        m.constituent_relative <= .01 && passive <= .01, "Moment pressure failed the original temporal accuracy bounds");
    check(source.checkpoint() == held, "Moment stepping changed its source");
    check(totals.rejected == 0 && totals.attempts == totals.substeps && totals.maximum_dt >= dt * (1 - 1e-12) &&
        totals.substeps <= 5 * (static_cast<std::size_t>(std::ceil(.1 / dt)) + 1), "Moment wave silently reverted to small steps");
    std::cout << "MWAVE " << id << ' ' << w << ' ' << h << ' ' << dt << ' ' << fine_dt << ' ' << m.temperature_k << ' '
        << m.pressure_over_disturbance << ' ' << m.oxygen_relative << ' ' << m.constituent_relative << ' ' << passive << ' '
        << totals.substeps << ' ' << totals.attempts << '\n';
    export_state(id, 'R', reference); export_state(id, 'C', candidate);
}
void small_physics() {
    const auto& s = setup();
    for (auto scene : {hydrostatic(8, 16), room(12, 8), mixed(8, 8), liquid(8, 8), vacuum(8, 4)}) {
        if (scene.name == "mixed_gas_liquid") set_velocity(s, scene.state, .02, -.01);
        const auto source = owner(scene, .0001, true);
        const auto next = advance_mac_production(source, .002, 4096, {}); audit();
        check(next.report.interval_kernel_intervals == 1, "Small physical control bypassed the moment stage");
        const auto& candidate = next.world;
        const auto reference = advance_mac_production(owner(scene, .0001, false), .002, 4096, {}).world;
        const auto m = compare(reference, candidate, scene.disturbance_pa);
        const double p = scene.disturbance_pa > 0 ? m.pressure_over_disturbance : m.pressure_relative;
        check(m.temperature_k <= .1 && p <= .01 && m.oxygen_relative <= .01 && m.constituent_relative <= .01,
            "Moment stage changed the resolved small-step physical laws");
        std::cout << "MPHYSICS " << scene.name << ' ' << m.temperature_k << ' ' << p << ' ' << m.oxygen_relative << ' ' << m.constituent_relative << '\n';
    }
    // A fine production reference must still match the strict stage oracle.
    auto scene = wave(16, 2); scene.settings.maximum_timestep_s = .00005;
    const MacClosedWorld strict(s.registry, s.eos, scene.state, scene.settings);
    const auto exact = strict.advanced(.002, 4096).world;
    const auto fine = owner(scene, .00005, false).advanced(.002, 4096).world;
    const auto m = compare(exact, fine, 20);
    check(m.temperature_k < 1e-8 && m.pressure_over_disturbance < 1e-8 && m.constituent_relative < 1e-10,
        "Fine production reference departed from the strict oracle");
    std::cout << "MORACLE " << m.temperature_k << ' ' << m.pressure_over_disturbance << ' ' << m.constituent_relative << '\n';
}
void write(const MacClosedWorld& world, const std::filesystem::path& file) {
    const auto bytes = world.checkpoint(); std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())); check(static_cast<bool>(out), "Cannot write moment checkpoint");
}
void trace_control() {
    auto scene = wave(64, 64); const auto& s = setup(); const auto helium = s.registry.index("helium");
    for (std::size_t i = 0; i < scene.state.cells.size(); ++i) {
        auto& cell = scene.state.cells[i]; auto masses = cell.mixture.masses;
        masses[helium] = i % 64 < 32 ? cell.mixture.mass() * 0x1p-1030 : 0;
        cell.mixture = s.eos->initialize_at_temperature(masses, 300, 1).mixture;
        // The amount is zero on the empty half; canonical passive storage
        // represents zero by absence, never by a zero-valued entry.
        if (masses[helium] > 0) cell.passive_quantities[1].quantity = masses[helium];
        else cell.passive_quantities.pop_back();
    }
    const auto source = owner(scene, .01, true); const auto held = source.checkpoint();
    auto next = advance_mac_production(source, .02, 32, {}); audit();
    const auto& report = last_mac_production_report(); std::size_t arrived = 0;
    for (std::size_t i = 0; i < scene.state.cells.size(); ++i)
        if (source.state().cells[i].mixture.masses[helium] == 0 && next.world.state().cells[i].mixture.masses[helium] > 0) ++arrived;
    check(arrived > 0 && report.rounded_transfers > 0 && source.checkpoint() == held,
        "Moment stepping lost its spreading subnormal front or rounding audit");
    std::cout << "MTRACE 4096 " << report.rounded_transfers << ' ' << arrived << '\n';
}
MacClosedWorld threaded() {
    const auto source = owner(wave(193, 173), .002, true); const auto held = source.checkpoint();
    auto options = mac_kernel_default_options(); options.threads = 1; set_mac_kernel_default_options(options);
    const auto first = source.advanced(.002, 256); audit(); const auto r1 = last_mac_production_report();
    options.threads = 12; set_mac_kernel_default_options(options);
    const auto parallel = source.advanced(.002, 256); audit(); const auto r12 = last_mac_production_report();
    check(r12.threads_used == 12 && first.world.checkpoint() == parallel.world.checkpoint() &&
        r1.unresolved_updates == r12.unresolved_updates && r1.rounded_transfers == r12.rounded_transfers,
        "Moment state or rounding depends on thread count");
    const auto image = first.world.checkpoint(); const auto& s = setup();
    const auto restored = MacClosedWorld::from_checkpoint(s.registry, s.eos, image);
    check(image.at(8) == wire_version() && restored.checkpoint() == image && restored.settings().pressure_integration == selection,
        "Moment checkpoint lost its persisted temporal selection");
    const auto resumed = restored.advanced(.002, 256); audit();
    options.threads = 1; set_mac_kernel_default_options(options);
    check(resumed.world.checkpoint() == first.world.advanced(.002, 256).world.checkpoint() && source.checkpoint() == held,
        "Moment ordinary restore lost exact continuation or source atomicity");
    const auto limited = owner(wave(16, 16), .001, true); const auto before = limited.checkpoint(); bool failed = false;
    try { static_cast<void>(limited.advanced(.01, 1)); } catch (const std::runtime_error&) { failed = true; }
    check(failed && limited.checkpoint() == before && last_mac_production_report().audits.empty(), "Failed moment budget published a partial interval");
    std::cout << "MTHREAD 33389 12 " << r12.newton_iterations << ' ' << r12.linear_iterations << ' ' << r12.multigrid_cycles << '\n';
    return first.world;
}
void invalid() {
    auto scene = wave(4, 4); auto settings = scene.settings;
    settings.pressure_integration = selection;
    bool failed = false; try { settings.validate(); } catch (const std::invalid_argument&) { failed = true; }
    check(failed, "Reference settings admitted a moment selection");
    auto world = owner(scene, .001, true); const auto held = world.checkpoint();
    // Recompute checksums to exercise semantic admission.
    std::size_t model = 0; for (std::size_t i = 0; i < 4; ++i) model |= static_cast<std::size_t>(held.at(12 + i)) << (8 * i);
    const auto field = 16 + model + 103 + 16;
    for (const auto value : {0U, wire_version() == 7 ? 2U : 1U, 99U, UINT32_MAX}) {
        auto bytes = held; for (std::size_t i = 0; i < 4; ++i) bytes.at(field + i) = static_cast<std::uint8_t>(value >> (8 * i));
        std::uint64_t hash = UINT64_C(14695981039346656037);
        for (std::size_t i = 0; i + 8 < bytes.size(); ++i) hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
        for (std::size_t i = 0; i < 8; ++i) bytes[bytes.size() - 8 + i] = static_cast<std::uint8_t>(hash >> (8 * i));
        failed = false; try { static_cast<void>(MacClosedWorld::from_checkpoint(setup().registry, setup().eos, bytes)); }
        catch (const std::invalid_argument&) { failed = true; } check(failed, "Unknown/noncanonical moment checkpoint was admitted");
    }
    scene.settings.numerical_kernel = MacNumericalKernel::production;
    scene.settings.pressure_integration = selection;
    scene.settings.preconditioner = PressurePreconditioner::incomplete_cholesky;
    world = MacClosedWorld(setup().registry, setup().eos, scene.state, scene.settings); const auto unsupported = world.checkpoint();
    failed = false; try { static_cast<void>(world.advanced(.001)); } catch (const std::invalid_argument&) { failed = true; }
    check(failed && world.checkpoint() == unsupported, "Unsupported moment settings silently changed temporal method");
    std::cout << "PASS version-" << wire_version() << " temporal admission, rollback and legacy fallback guards\n";
}
void checkpoint_modes() {
    const auto& s = setup();
    for (const auto pressure : {PressurePreconditioner::automatic, PressurePreconditioner::jacobi}) {
        for (const auto diffusion : {MacDiffusionSettings{}, MacDiffusionSettings{MacDiffusionMode::accelerated, 100}}) {
            auto scene = wave(19, 17); scene.settings.preconditioner = pressure; scene.settings.diffusion = diffusion;
            for (std::size_t i = 0; i < scene.state.cells.size(); ++i) {
                const auto state = s.eos->evaluate(scene.state.cells[i].mixture, 1);
                scene.state.cells[i].mixture = s.mixture(1, state.mechanical_pressure_pa, 300, 0, i % 19 < 9 ? .22 : .18);
            }
            const auto source = owner(scene, .002, true); const auto held = source.checkpoint();
            const auto first = source.advanced(.002, 4096); const auto image = first.world.checkpoint();
            const auto restored = MacClosedWorld::from_checkpoint(s.registry, s.eos, image);
            check(first.report.diffusion.exchanged_mass_kg > 0 && image.at(8) == wire_version() && restored.checkpoint() == image && restored.settings().preconditioner == pressure &&
                restored.settings().diffusion.mode == diffusion.mode && restored.settings().diffusion.multiplier == diffusion.multiplier &&
                restored.advanced(.002, 4096).world.checkpoint() == first.world.advanced(.002, 4096).world.checkpoint() &&
                source.checkpoint() == held, "Moment checkpoint lost composed diffusion/preconditioner continuation");
        }
    }
    std::cout << "PASS version-" << wire_version() << " physical/accelerated diffusion and automatic/Jacobi continuation\n";
}
}
int main(int argc, char** argv) {
    try {
        std::cout << std::unitbuf << std::setprecision(17);
        if (argc > 1 && std::string(argv[1]) == "--coupled") { selection = MacPressureIntegration::two_moment_coupled; --argc; ++argv; }
        if (argc == 4 && std::string(argv[1]) == "--continue") {
            std::ifstream in(argv[2], std::ios::binary); const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in), {}};
            const auto restored = MacClosedWorld::from_checkpoint(setup().registry, setup().eos, bytes);
            write(restored.advanced(.002, 256).world, argv[3]); return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--write-checkpoint") { write(threaded(), argv[2]); return 0; }
        if (argc == 2 && std::string(argv[1]) == "--trace") { trace_control(); return 0; }
        if (argc == 2 && std::string(argv[1]) == "--checkpoint-modes") { checkpoint_modes(); return 0; }
        check(argc == 1, "Unknown moment test arguments");
        std::cout << "MVERSION " << (wire_version() == 7 ? 1 : 2) << ' ' << gas_constant << '\n';
        for (const std::string name : {"nitrogen", "oxygen", "helium"}) {
            const auto& sp = setup().registry[setup().registry.index(name)];
            std::cout << "MTHERMO " << name << ' ' << sp.cv() << ' ' << sp.internal_energy(0) << ' ' << sp.molar_mass << '\n';
        }
        invalid(); checkpoint_modes(); small_physics();
        wave_case(0, 16, 16, .004, .00005, true); wave_case(1, 32, 2, .008, .0001, false);
        wave_case(2, 64, 2, .02, .0001, false); wave_case(3, 256, 2, .1, .0001, true); wave_case(4, 512, 2, .1, .0001, false);
        if (wire_version() == 8) wave_case(5, 500, 2, .1, .0001, false);
        trace_control(); static_cast<void>(threaded()); std::cout << "PASS resolved moment waves, physical controls, threads and persisted continuation\n"; return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
