#include "mac_fixture.hpp"
#include <iomanip>

namespace {
using namespace ma;
using namespace ma::experimental;
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
MacMassFluxes sweeps(const MacTransportState& source, double x) {
    return {std::vector<double>(source.grid.x_width() * source.grid.height, x),
        std::vector<double>(source.grid.width * source.grid.y_height())};
}
MacTransportState gas(double nitrogen, double helium, double volume = 1) {
    const auto& set = ma::testing::setup(); auto state = set.state(2, 2, true);
    state.grid.spacing_m = std::sqrt(volume);
    for (auto& cell : state.cells) {
        std::vector<double> masses(set.registry.size());
        masses[set.registry.index("nitrogen")] = nitrogen; masses[set.registry.index("helium")] = helium;
        cell.mixture = set.eos->initialize_at_temperature(masses, 300, volume).mixture;
        cell.passive_quantities = {{17, nitrogen}, {UINT64_C(0x80000000), nitrogen * 1e-30}};
    }
    return state;
}
std::string fingerprint(const MacParcelPlan& plan) {
    std::ostringstream out; out << std::hexfloat;
    const auto cell = [&](const MacMaterialCell& c) {
        out << ' ' << c.mixture.energy; for (auto x : c.mixture.masses) out << ' ' << x;
        for (const auto& p : c.passive_quantities) out << ' ' << p.identity << ' ' << p.quantity;
    };
    for (auto x : plan.source_volumes_m3) out << ' ' << x;
    for (auto x : plan.retained_volumes_m3) out << ' ' << x;
    for (auto x : plan.limiter) out << ' ' << x;
    for (const auto& c : plan.retained) cell(c);
    for (const auto& p : plan.parcels) {
        out << ' ' << p.axis << ' ' << p.face << ' ' << p.donor << ' ' << p.target << ' ' << p.volume_m3 << ' ' << p.reference_volume_m3;
        cell(p.contents);
    }
    return out.str();
}
void export_case(int id, const MacTransportState& source, const MacParcelPlan& plan, const MacTransportAdvance& result) {
    ma::testing::export_transport(id, source, result, plan);
    std::cout << "PRIMARY " << id << ' ' << result.report.scaled_primary_parcels << '\n';
    const auto& set = ma::testing::setup();
    for (int endpoint = 0; endpoint < 2; ++endpoint) {
        const auto& state = endpoint == 0 ? source : result.state;
        for (std::size_t i = 0; i < state.cells.size(); ++i) {
            const auto eos = set.eos->evaluate(state.cells[i].mixture, state.grid.volume_m3());
            std::cout << "PEOS " << id << ' ' << endpoint << ' ' << i << ' ' << eos.mechanical_pressure_pa << ' ' << eos.temperature_k << '\n';
        }
    }
}
void original_range() {
    const auto& set = ma::testing::setup(); const auto he = set.registry.index("helium");
    constexpr double volume = 0.000244140625, mass = 3.0517578125e-5, transfer_volume = 6.8694555346832573e-296;
    for (int i = 0; i < 3; ++i) {
        const auto source = gas(mass, 3.0517578124999999e-33, volume); const auto held = ma::testing::fingerprint(source);
        const auto plan = reconstruct_mac_parcels(set.registry, *set.eos, source, std::vector<double>(4, volume), sweeps(source, transfer_volume * std::ldexp(1., 3 * i)));
        const auto pheld = fingerprint(plan); const auto result = advance_mac_parcel_transport(set.registry, source, *set.eos, plan);
        check(result.report.scaled_primary_parcels == plan.parcels.size() && !plan.parcels.empty(), "Primary range scaling was bypassed");
        for (const auto& p : plan.parcels) {
            const auto trace = mac_parcel_quantity(p, p.contents.mixture.masses[he]);
            check(trace.mantissa() > 0 && trace.project().rounded && (i != 0 || trace.project().value == 0), "Primary helium contribution lost its range or projection flag");
        }
        check(ma::testing::fingerprint(source) == held && fingerprint(plan) == pheld && ma::testing::fingerprint(result.state) == held,
            "Uniform small transfer changed its source/plan or stored uniform state");
        export_case(i, source, plan, result);
    }
}
void sparse_passive() {
    const auto& set = ma::testing::setup(); auto source = gas(1, 1e-28);
    for (auto& c : source.cells) c.passive_quantities = {{17, 1}, {UINT64_C(0x80000000), 1e-250}};
    const auto held = ma::testing::fingerprint(source);
    const auto plan = reconstruct_mac_parcels(set.registry, *set.eos, source, std::vector<double>(4, 1), sweeps(source, 1e-100));
    const auto result = advance_mac_parcel_transport(set.registry, source, *set.eos, plan);
    check(result.report.scaled_primary_parcels == plan.parcels.size() && ma::testing::fingerprint(source) == held &&
        ma::testing::fingerprint(result.state) == held, "Below-range sparse passive changed or erased inventory");
    for (const auto& p : plan.parcels) check(mac_parcel_quantity(p, p.contents.passive_quantities.back().quantity).project().value == 0,
        "Sparse passive control did not exercise a subrange contribution");
    export_case(3, source, plan, result);
}
void accumulated_new_trace() {
    const auto& set = ma::testing::setup(); auto source = set.state(3, 3, true); const auto he = set.registry.index("helium");
    for (auto& c : source.cells) {
        std::vector<double> masses(set.registry.size()); masses[set.registry.index("nitrogen")] = 1; masses[he] = std::ldexp(1., -100);
        c.mixture = set.eos->initialize_at_temperature(masses, 300, 1).mixture; c.passive_quantities = {{17, 1}};
    }
    auto& central = source.cells[4]; central.mixture.masses[he] = 0;
    central.mixture = set.eos->initialize_at_temperature(central.mixture.masses, 300, 1).mixture;
    auto plan = reconstruct_mac_parcels(set.registry, *set.eos, source, std::vector<double>(9, 1), sweeps(source, 0));
    std::fill(plan.limiter.begin(), plan.limiter.end(), 0);
    const auto add = [&](std::size_t axis, std::size_t face, std::size_t donor) {
        plan.parcels.push_back({axis, face, donor, 4, std::ldexp(1., -976), source.cells[donor], 1});
    };
    add(0, source.grid.x_face(1, 1), 3); add(0, source.grid.x_face(2, 1), 5);
    add(1, source.grid.y_face(1, 1), 1); add(1, source.grid.y_face(1, 2), 7);
    const auto held = ma::testing::fingerprint(source), pheld = fingerprint(plan);
    const auto result = advance_mac_parcel_transport(set.registry, source, *set.eos, plan);
    check(result.state.cells[4].mixture.masses[he] == std::numeric_limits<double>::denorm_min() && result.report.scaled_primary_parcels == 4,
        "Four subrange contributions did not form the exactly representable new trace");
    check(ma::testing::fingerprint(source) == held && fingerprint(plan) == pheld, "New trace accumulation changed source/plan");
    export_case(4, source, plan, result);
}
template<class F> void rejects(const MacTransportState& source, F action) {
    const auto held = ma::testing::fingerprint(source); bool rejected = false;
    try { action(); } catch (const FluidFatalError& error) { rejected = error.code() == FluidFatalCode::numerical_precision; }
    check(rejected && ma::testing::fingerprint(source) == held, "Unrepresentable primary update was accepted or mutated its source");
}
void unsupported_primary_storage() {
    const auto& set = ma::testing::setup(); auto source = gas(1, std::ldexp(1., -100)); const auto he = set.registry.index("helium");
    for (std::size_t i = 1; i < source.cells.size(); ++i) {
        source.cells[i].mixture.masses[he] = 0;
        source.cells[i].mixture = set.eos->initialize_at_temperature(source.cells[i].mixture.masses, 300, 1).mixture;
    }
    rejects(source, [&] {
        const auto p = reconstruct_mac_parcels(set.registry, *set.eos, source, std::vector<double>(4, 1), sweeps(source, std::ldexp(1., -976)));
        static_cast<void>(advance_mac_parcel_transport(set.registry, source, *set.eos, p));
    });
    source = gas(1, 1e-28); source.cells[0].passive_quantities = {{17, std::numeric_limits<double>::denorm_min()}};
    rejects(source, [&] { static_cast<void>(reconstruct_mac_parcels(set.registry, *set.eos, source, std::vector<double>(4, 1), sweeps(source, .125))); });
    source = gas(1e-250, 1e-278);
    const auto held = ma::testing::fingerprint(source);
    const auto plan = reconstruct_mac_parcels(set.registry, *set.eos, source, std::vector<double>(4, 1), sweeps(source, 1e-100));
    const auto result = advance_mac_parcel_transport(set.registry, source, *set.eos, plan);
    check(!result.report.primal_mass_fluxes_kg.full_x.empty() && ma::testing::fingerprint(source) == held && ma::testing::fingerprint(result.state) == held,
        "Full bulk flux was not retained or changed uniform state");
    export_case(5, source, plan, result);
}
void invalid_scale() {
    const auto& set = ma::testing::setup(); const auto source = gas(1, 1e-28); const auto held = ma::testing::fingerprint(source);
    const auto plan = reconstruct_mac_parcels(set.registry, *set.eos, source, std::vector<double>(4, 1), sweeps(source, .125));
    for (const double reference : {-1., std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), 2.}) {
        auto bad = plan; bad.parcels[0].reference_volume_m3 = reference; const auto pheld = fingerprint(bad); bool rejected = false;
        try { static_cast<void>(advance_mac_parcel_transport(set.registry, source, *set.eos, bad)); } catch (const std::exception&) { rejected = true; }
        check(rejected && ma::testing::fingerprint(source) == held && fingerprint(bad) == pheld, "Invalid parcel scale was accepted or mutated inputs");
    }
}
}
int main() {
    try {
        require_mac_energy_environment(); std::cout << std::setprecision(17);
        const auto& set = ma::testing::setup();
        for (const auto& m : set.eos->reference().materials()) std::cout << "MATERIAL " << set.registry.index(m.id) << ' ' <<
            m.molar_mass_kg_per_mol << ' ' << m.cv_j_per_kg_k << ' ' << m.energy_offset_j_per_kg << '\n';
        original_range(); std::cout << "PASS original below-range and rounded-subnormal helium contributions\n";
        sparse_passive(); std::cout << "PASS sparse passive below binary64 range\n";
        accumulated_new_trace(); std::cout << "PASS four subrange parcels form a positive new stored trace\n";
        unsupported_primary_storage(); std::cout << "PASS unresolved cell storage rejects and full bulk flux is retained\n";
        invalid_scale(); std::cout << "PASS invalid and inconsistent parcel scales reject atomically\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
