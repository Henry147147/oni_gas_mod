#include "mixed_atmospheres/coupled_api.h"
#include "mixed_atmospheres/mixture.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
void check(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
void ok(ma_status status) { if (status != MA_OK) throw std::runtime_error(ma_coupled_last_error()); }
using Owner = std::unique_ptr<ma_coupled_world, decltype(&ma_coupled_destroy)>;
struct Capture {
    ma_coupled_snapshot header{};
    std::vector<ma_coupled_cell> cells = std::vector<ma_coupled_cell>(4);
    std::vector<double> masses;
    std::array<uint64_t, 5> offsets{};
    std::vector<ma_coupled_passive> entries;
    Capture(const ma_coupled_world* source, std::size_t species) : masses(4 * species) {
        uint64_t count = 0; ok(ma_coupled_passive_count(source, &count)); entries.resize(static_cast<std::size_t>(count));
        ok(ma_coupled_copy_full_snapshot(source, &header, cells.data(), cells.size(), masses.data(), masses.size(),
            offsets.data(), offsets.size(), entries.data(), entries.size()));
    }
    bool same(const Capture& b) const {
        return std::memcmp(&header, &b.header, sizeof(header)) == 0 && masses == b.masses && offsets == b.offsets &&
            std::memcmp(cells.data(), b.cells.data(), cells.size() * sizeof(cells[0])) == 0 && entries.size() == b.entries.size() &&
            (entries.empty() || std::memcmp(entries.data(), b.entries.data(), entries.size() * sizeof(entries[0])) == 0);
    }
};
struct Fixture {
    ma::SpeciesRegistry registry = ma::SpeciesRegistry::standard();
    ma_coupled_config config{sizeof(ma_coupled_config), 1, 2, 2, 1, 1, 9.81, 0, 0, .001, .45, 0, 0, 1, 0, 0};
    std::array<ma_coupled_cell_input, 4> cells{};
    std::vector<double> masses = std::vector<double>(4 * registry.size());
    std::array<uint64_t, 5> offsets{0, 2, 2, 2, 2};
    std::array<ma_coupled_passive, 2> entries{{{UINT64_C(0x80000000), 1e-30}, {UINT64_C(0x100000000), .125}}};
    Fixture() {
        for (std::size_t i = 0; i < cells.size(); ++i) {
            const double alpha = .1 + .05 * static_cast<double>(i), t = 298 + 2 * static_cast<double>(i);
            const double n = 100000 * (1 - alpha) / (ma::gas_constant * t);
            ma::MixtureInventory mixture(registry.size());
            mixture.masses[registry.index("nitrogen")] = .79 * n * registry[registry.index("nitrogen")].molar_mass;
            mixture.masses[registry.index("oxygen")] = .21 * n * registry[registry.index("oxygen")].molar_mass;
            mixture.masses[registry.index("water")] = alpha * registry[registry.index("water")].density;
            mixture.masses[registry.index("helium")] = 1e-30; mixture.set_temperature(registry, t);
            cells[i] = {mixture.energy, 0, 0, 100000, 0, 0};
            std::copy(mixture.masses.begin(), mixture.masses.end(), masses.begin() + static_cast<std::ptrdiff_t>(i * registry.size()));
        }
    }
    Owner create() const {
        ma_coupled_world* pointer = nullptr;
        ok(ma_coupled_create_full(&config, cells.data(), cells.size(), masses.data(), masses.size(), offsets.data(), offsets.size(),
            entries.data(), entries.size(), &pointer)); return {pointer, ma_coupled_destroy};
    }
};
struct Request {
    uint64_t revision;
    std::vector<ma_coupled_cell_edit> edits;
    std::vector<double> masses, mass_deltas;
    std::vector<uint64_t> offsets{0};
    std::vector<ma_coupled_passive> entries, passive_deltas;
    ma_coupled_mutation_budget budget{};
    Request(const Capture& source, std::initializer_list<std::size_t> rows) : revision(source.header.revision), mass_deltas(source.header.species) {
        for (const auto i : rows) {
            const auto& old = source.cells[i];
            edits.push_back({i, {old.internal_energy, old.momentum_x, old.momentum_y, old.pressure, 0, 0}});
            const auto first = source.masses.begin() + static_cast<std::ptrdiff_t>(i * source.header.species);
            masses.insert(masses.end(), first, first + source.header.species);
            entries.insert(entries.end(), source.entries.begin() + static_cast<std::ptrdiff_t>(source.offsets[i]),
                source.entries.begin() + static_cast<std::ptrdiff_t>(source.offsets[i + 1])); offsets.push_back(entries.size());
        }
    }
    ma_status call(const Owner& source, ma_coupled_world** output, ma_coupled_mutation_result* result) const {
        return ma_coupled_prepare_mutation(source.get(), revision, edits.data(), edits.size(), masses.data(), masses.size(),
            offsets.data(), offsets.size(), entries.data(), entries.size(), &budget, mass_deltas.data(), mass_deltas.size(),
            passive_deltas.data(), passive_deltas.size(), output, result);
    }
};
double kinetic(double mass, double px, double py) { return (px * px + py * py) / (2 * mass); }
void export_case(int id, const Fixture& f, const Capture& before, const Capture& after, const Request& request,
    const ma_coupled_mutation_result& report) {
    std::cout << "CASE " << id << ' ' << request.budget.material_energy << ' ' << request.budget.shaft_work << ' ' << request.budget.heat <<
        ' ' << request.budget.momentum_x << ' ' << request.budget.momentum_y << ' ' << report.energy_residual << ' ' << report.energy_bound <<
        ' ' << report.momentum_x_residual << ' ' << report.momentum_y_residual << ' ' << report.internal_energy_change <<
        ' ' << report.kinetic_energy_change << ' ' << report.potential_energy_change << '\n';
    for (const auto& edit : request.edits) std::cout << "EDIT " << id << ' ' << edit.cell_index << '\n';
    for (std::size_t s = 0; s < request.mass_deltas.size(); ++s)
        if (request.mass_deltas[s] != 0) std::cout << "MASS_DELTA " << id << ' ' << s << ' ' << request.mass_deltas[s] << '\n';
    for (const auto& delta : request.passive_deltas) std::cout << "PASSIVE_DELTA " << id << ' ' << delta.identity << ' ' << delta.quantity << '\n';
    for (int endpoint = 0; endpoint < 2; ++endpoint) {
        const auto& snapshot = endpoint == 0 ? before : after;
        for (std::size_t i = 0; i < snapshot.cells.size(); ++i) {
            const auto& c = snapshot.cells[i];
            const double phi = f.config.gravity * (static_cast<double>(i / f.config.width) + .5) * f.config.cell_size;
            std::cout << "CELL " << id << ' ' << endpoint << ' ' << i << ' ' << c.internal_energy << ' ' << c.momentum_x << ' ' <<
                c.momentum_y << ' ' << c.total_mass << ' ' << kinetic(c.total_mass, c.momentum_x, c.momentum_y) << ' ' <<
                c.total_mass * phi << ' ' << c.temperature << ' ' << c.pressure << '\n';
            for (std::size_t s = 0; s < f.registry.size(); ++s) if (snapshot.masses[i * f.registry.size() + s] != 0)
                std::cout << "MASS " << id << ' ' << endpoint << ' ' << i << ' ' << s << ' ' << snapshot.masses[i * f.registry.size() + s] << '\n';
            for (auto p = snapshot.offsets[i]; p < snapshot.offsets[i + 1]; ++p)
                std::cout << "PASSIVE " << id << ' ' << endpoint << ' ' << i << ' ' << snapshot.entries[static_cast<std::size_t>(p)].identity <<
                    ' ' << snapshot.entries[static_cast<std::size_t>(p)].quantity << '\n';
        }
    }
}
Owner prepare(const Owner& source, const Fixture& f, const Capture& before, const Request& request, int id = -1) {
    ma_coupled_world* pointer = nullptr; ma_coupled_mutation_result result{}; ok(request.call(source, &pointer, &result));
    Owner candidate(pointer, ma_coupled_destroy); Capture after(candidate.get(), f.registry.size());
    check(Capture(source.get(), f.registry.size()).same(before), "Mutation changed its immutable source");
    check(after.header.revision == before.header.revision + 1 && after.header.time == before.header.time &&
        result.revision == after.header.revision && result.edited_cells == request.edits.size(), "Mutation revision/time differs");
    if (id >= 0) export_case(id, f, before, after, request, result);
    return candidate;
}
void rejected(const Owner& source, const Fixture& f, const Capture& before, const Request& request) {
    ma_coupled_mutation_result result; std::memset(&result, 0x5a, sizeof(result)); const auto held = result;
    auto* pointer = source.get(); const auto status = request.call(source, &pointer, &result);
    check(status != MA_OK && pointer == nullptr && std::memcmp(&held, &result, sizeof(result)) == 0 &&
        Capture(source.get(), f.registry.size()).same(before), "Rejected mutation changed source/output/diagnostics");
}
void heat_and_exact_momentum() {
    Fixture f; auto source = f.create(); const Capture before(source.get(), f.registry.size()); Request request(before, {0, 2});
    const double p = std::bit_cast<double>(UINT64_C(0x3fd999999999999c)); const double mass = before.cells[0].total_mass;
    check(p / mass * mass != p, "Exact-momentum control no longer exercises the known round trip");
    request.edits[0].value.internal_energy += 1024; request.edits[0].value.momentum_x = p;
    request.budget.heat = 1024; request.budget.shaft_work = kinetic(mass, p, 0); request.budget.momentum_x = p;
    auto candidate = prepare(source, f, before, request, 0); const Capture after(candidate.get(), f.registry.size());
    check(after.cells[0].momentum_x == p && after.cells[0].internal_energy == request.edits[0].value.internal_energy,
        "Mutation rounded primary U or momentum through a display projection");
    for (const auto i : {std::size_t(1), std::size_t(2), std::size_t(3)})
        check(std::memcmp(&before.cells[i], &after.cells[i], sizeof(ma_coupled_cell)) == 0, "Unchanged cell was rewritten");
    auto held = after; candidate.reset(); source.reset(); check(held.cells[0].momentum_x == p, "Owned capture borrowed a retired handle");
}
void balanced_bulk_transfer() {
    Fixture f; f.cells[0].momentum_x = .5; f.cells[1].momentum_x = -.25;
    auto source = f.create(); const Capture before(source.get(), f.registry.size()); Request request(before, {0, 1});
    const auto count = f.registry.size(); const double fraction = .125;
    for (std::size_t s = 0; s < count; ++s) {
        const auto parcel = fraction * before.masses[s]; request.masses[s] -= parcel; request.masses[count + s] += parcel;
    }
    request.edits[0].value.momentum_x *= 1 - fraction;
    request.edits[1].value.momentum_x += fraction * before.cells[0].momentum_x;
    double new_mass0 = 0, new_mass1 = 0;
    for (std::size_t s = 0; s < count; ++s) { new_mass0 += request.masses[s]; new_mass1 += request.masses[count + s]; }
    const double heat = kinetic(before.cells[0].total_mass, before.cells[0].momentum_x, 0) +
        kinetic(before.cells[1].total_mass, before.cells[1].momentum_x, 0) -
        kinetic(new_mass0, request.edits[0].value.momentum_x, 0) - kinetic(new_mass1, request.edits[1].value.momentum_x, 0);
    check(heat > 0, "Bulk mixing control contains no mechanical mixing heat");
    const double parcel_u = fraction * before.cells[0].internal_energy;
    request.edits[0].value.internal_energy -= parcel_u; request.edits[1].value.internal_energy += parcel_u + heat;
    for (auto& value : request.entries) value.quantity *= 1 - fraction;
    request.entries.push_back({f.entries[0].identity, fraction * f.entries[0].quantity});
    request.entries.push_back({f.entries[1].identity, fraction * f.entries[1].quantity}); request.offsets[2] = request.entries.size();
    auto candidate = prepare(source, f, before, request, 1); const Capture after(candidate.get(), count);
    check(after.masses[f.registry.index("helium")] > 0 && after.masses[count + f.registry.index("helium")] > 0,
        "Positive constituent trace disappeared during bulk transfer");
    check(after.offsets[2] - after.offsets[1] == 2, "Disease/radiation did not reach the receiver");
}
void supplied_material_and_accounts() {
    Fixture f; auto source = f.create(); const Capture before(source.get(), f.registry.size()); Request request(before, {2});
    const auto oxygen = f.registry.index("oxygen"); constexpr double amount = .125, p = .0625;
    const double packet_u = amount * f.registry[oxygen].internal_energy(310), packet_k = kinetic(amount, p, 0);
    request.masses[oxygen] += amount; request.mass_deltas[oxygen] = amount;
    request.edits[0].value.momentum_x = p;
    request.edits[0].value.internal_energy += packet_u + packet_k - kinetic(before.cells[2].total_mass + amount, p, 0);
    request.budget.material_energy = packet_u + packet_k + amount * f.config.gravity * 1.5; request.budget.momentum_x = p;
    request.entries = {{47, 1e-32}, {UINT64_C(0x100000000), .05}}; request.offsets[1] = 2;
    request.passive_deltas = request.entries;
    auto candidate = prepare(source, f, before, request, 2);
    ma_coupled_step_result step{}; ma_coupled_world* pointer = nullptr;
    ok(ma_coupled_advance(candidate.get(), 1.0 / 65536, 1000, &pointer, &step)); Owner advanced(pointer, ma_coupled_destroy);
    const Capture after(advanced.get(), f.registry.size());
    check(after.header.revision == 2 && after.header.time == 1.0 / 65536 && step.substeps > 0,
        "Physical solver did not continue from the edited native owner");
}
void declared_metadata_removal() {
    Fixture f; auto source = f.create(); const Capture before(source.get(), f.registry.size()); Request request(before, {0});
    request.entries.clear(); request.offsets[1] = 0;
    request.passive_deltas = {{f.entries[0].identity, -f.entries[0].quantity}, {f.entries[1].identity, -f.entries[1].quantity}};
    auto candidate = prepare(source, f, before, request, 3); const Capture after(candidate.get(), f.registry.size());
    check(after.entries.empty() && before.entries.size() == 2, "Explicit metadata removal affected its source or remained hidden");
}
void late_failure() {
    Fixture f; auto source = f.create(); const Capture before(source.get(), f.registry.size()); Request request(before, {0, 3});
    request.edits[0].value.internal_energy += 1024; request.budget.heat = 1024;
    request.edits[1].value.internal_energy = -1e300; rejected(source, f, before, request);
}
void undeclared_losses() {
    Fixture f; auto source = f.create(); const Capture before(source.get(), f.registry.size()); const Request basis(before, {0});
    auto request = basis; request.masses[f.registry.index("helium")] = 0; rejected(source, f, before, request);
    request = basis; request.entries.erase(request.entries.begin()); request.offsets[1] = 1; rejected(source, f, before, request);
    request = basis; request.edits[0].value.internal_energy += 1024; rejected(source, f, before, request);
    request = basis; request.edits[0].value.momentum_x = 1; rejected(source, f, before, request);
    request = basis; request.mass_deltas[f.registry.index("helium")] = 1e-200; rejected(source, f, before, request);
    request = basis; request.passive_deltas = {{f.entries[0].identity, 1e-200}}; rejected(source, f, before, request);
    request = basis; request.budget.heat = 1e-200; rejected(source, f, before, request);
}
void malformed_requests() {
    Fixture f; auto source = f.create(); const Capture before(source.get(), f.registry.size()); const Request basis(before, {0, 1});
    const std::array<std::function<void(Request&)>, 9> changes{{
        [](auto& r) { r.revision = 1; }, [](auto& r) { r.edits[1].cell_index = 0; },
        [](auto& r) { r.edits[1].cell_index = 4; }, [](auto& r) { r.edits[0].value.wall = 1; },
        [](auto& r) { r.budget.reserved[2] = 1; }, [](auto& r) { r.offsets[1] = 3; },
        [](auto& r) { r.mass_deltas.pop_back(); }, [](auto& r) { r.passive_deltas = {{0, 1}}; },
        [](auto& r) { std::fill(r.masses.begin(), r.masses.begin() + static_cast<std::ptrdiff_t>(r.mass_deltas.size()), 0); }
    }};
    for (const auto& change : changes) { auto request = basis; change(request); rejected(source, f, before, request); }
}
}
int main() {
    try {
        check(ma_coupled_mutation_abi_version() == 1, "Mutation extension ABI differs");
        const auto registry = ma::SpeciesRegistry::standard(); std::cout << std::setprecision(17);
        for (std::size_t s = 0; s < registry.size(); ++s) {
            const auto& material = registry[s];
            std::cout << "SPECIES " << s << ' ' << material.id << ' ' << material.molar_mass << ' ' << material.cv() << ' ' << material.internal_energy(0) <<
                ' ' << material.density << ' ' << static_cast<int>(material.phase) << '\n';
        }
        heat_and_exact_momentum(); std::cout << "PASS exact physical edits, heat/work and immutable captures\n";
        balanced_bulk_transfer(); std::cout << "PASS balanced bulk transfer with mixing heat and positive traces\n";
        supplied_material_and_accounts(); std::cout << "PASS material energy, elevation, metadata and subsequent physical advance\n";
        declared_metadata_removal(); std::cout << "PASS explicitly accounted signed metadata removal\n";
        late_failure(); std::cout << "PASS late constitutive failure with whole-call rollback\n";
        undeclared_losses(); std::cout << "PASS seven undeclared or erased conservation accounts reject\n";
        malformed_requests(); std::cout << "PASS nine stale, geometry and malformed requests reject\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
