#include "mixed_atmospheres/fluid_world.hpp"
#include "mixed_atmospheres/volume_work.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

// Test-only allocation fault injection exercises the public world's strong
// exception guarantee, including an exception after an accepted substep.
namespace faults {
std::int64_t remaining = -1;
std::size_t allocations = 0;
bool count = false;
}
void* operator new(std::size_t bytes) {
    if (faults::count) ++faults::allocations;
    if (faults::remaining >= 0 && faults::remaining-- == 0) {
        faults::remaining = -1;
        throw std::bad_alloc();
    }
    if (void* pointer = std::malloc(bytes == 0 ? 1 : bytes)) return pointer;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void near(double actual, double expected, double relative = 1e-9, double absolute = 1e-12) {
    check(std::isfinite(actual) && std::abs(actual - expected) <= absolute + relative * std::abs(expected),
          "Pressure regression value differs");
}
void residual_verified(const ma::PressureSystem& system, const std::vector<double>& rhs,
                       const ma::PressureSolveResult& result) {
    const auto applied = system.apply(result.pressure);
    long double square = 0;
    for (std::size_t i = 0; i < rhs.size(); ++i) {
        const double residual = rhs[i] - applied[i];
        square += static_cast<long double>(residual) * residual;
    }
    const double actual = std::sqrt(static_cast<double>(square));
    check(actual <= result.residual_threshold, "Original equation did not meet unchanged tolerance");
    near(result.residual_norm, actual, 1e-12, 1e-30);
}
void grid_reduces_iterations_without_relaxing_residual() {
    constexpr std::size_t width = 48, height = 32, count = width * height;
    std::vector<double> compliance(count), expected(count);
    std::vector<ma::PressureFace> faces;
    for (std::size_t y = 0; y < height; ++y) for (std::size_t x = 0; x < width; ++x) {
        const auto i = y * width + x;
        compliance[i] = 0.01 + 0.002 * static_cast<double>(i % 7);
        expected[i] = 2 + std::sin(0.13 * static_cast<double>(x)) * std::cos(0.17 * static_cast<double>(y));
        if (x + 1 < width) faces.push_back({i, i + 1, 0.5 + static_cast<double>(i % 5)});
        if (y + 1 < height) faces.push_back({i, i + width, 1 + static_cast<double>(i % 3)});
    }
    const ma::PressureSystem jacobi(compliance, faces, ma::PressurePreconditioner::jacobi);
    const ma::PressureSystem ic(compliance, faces, ma::PressurePreconditioner::automatic);
    const auto rhs = jacobi.apply(expected);
    const std::vector<double> initial(count, 0);
    const auto reference = jacobi.solve(rhs, initial, 1e-11, 1e-13);
    const auto improved = ic.solve(rhs, initial, 1e-11, 1e-13);
    residual_verified(jacobi, rhs, reference); residual_verified(ic, rhs, improved);
    check(improved.preconditioner_used == ma::PressurePreconditioner::incomplete_cholesky && !improved.preconditioner_fallback,
          "Large bounded-degree graph did not use IC(0)");
    check(improved.iterations * 2 < reference.iterations, "IC(0) did not materially reduce representative iterations");
    near(improved.residual_threshold, reference.residual_threshold, 0, 0);
    for (std::size_t i = 0; i < count; ++i) near(improved.pressure[i], expected[i], 1e-8);
    check(std::all_of(initial.begin(), initial.end(), [](double value) { return value == 0; }), "Solve mutated initial field");
    bool rejected = false;
    try { (void)ic.solve(rhs, initial, 1e-11, 1e-13, 1); } catch (const std::runtime_error&) { rejected = true; }
    check(rejected && std::all_of(initial.begin(), initial.end(), [](double value) { return value == 0; }),
          "Exhausted IC(0) solve published or changed its initial field");
    std::cout << "  variable grid Jacobi=" << reference.iterations << ", IC(0)=" << improved.iterations << '\n';
}
// A grid with holes, its compact rows in grid order, and an IC sweep plan of
// whole-row bands cut into column chunks (as the game pressure builds it).
struct HoledGrid {
    std::size_t width = 0;
    std::vector<std::size_t> cells; // compact row -> grid cell
    std::vector<double> compliance;
    std::vector<ma::PressureFace> faces;
};
HoledGrid holed_grid(std::size_t width, std::size_t height) {
    HoledGrid grid; grid.width = width;
    std::vector<std::size_t> row(width * height, SIZE_MAX);
    for (std::size_t i = 0; i < width * height; ++i)
        if ((i % width * 7 + i / width * 3) % 11 != 0) { row[i] = grid.cells.size(); grid.cells.push_back(i); }
    for (const auto i : grid.cells) grid.compliance.push_back(1e-4 * (1 + static_cast<double>(i % 5)));
    for (std::size_t i = 0; i < width * height; ++i) // x faces, then y faces
        if (i % width + 1 < width && row[i] != SIZE_MAX && row[i + 1] != SIZE_MAX)
            grid.faces.push_back({row[i], row[i + 1], 0.5 + static_cast<double>(i % 3)});
    for (std::size_t i = 0; i + width < width * height; ++i)
        if (row[i] != SIZE_MAX && row[i + width] != SIZE_MAX)
            grid.faces.push_back({row[i], row[i + width], 1 + static_cast<double>(i % 4)});
    return grid;
}
ma::PressureParallelPlan sweep_plan(const HoledGrid& grid, std::size_t bands, std::size_t chunks, std::size_t reduction) {
    ma::PressureParallelPlan plan; plan.bands = bands; plan.chunks = chunks; plan.reduction_rows = reduction;
    const auto rows = grid.cells.back() / grid.width + 1;
    const auto task = [&](std::size_t cell) {
        return (cell / grid.width) * bands / rows * chunks + (cell % grid.width) * chunks / grid.width;
    };
    plan.start.assign(bands * chunks + 1, 0);
    for (const auto cell : grid.cells) ++plan.start[task(cell) + 1];
    for (std::size_t t = 0; t + 1 < plan.start.size(); ++t) plan.start[t + 1] += plan.start[t];
    plan.order.resize(grid.cells.size());
    auto cursor = plan.start;
    for (std::size_t k = 0; k < grid.cells.size(); ++k) plan.order[cursor[task(grid.cells[k])]++] = k;
    return plan;
}
// Executors: tasks in order on this thread, or every task on its own thread.
void sequential(const void*, std::size_t count, ma::PressureParallelPlan::Task task, const void* context) {
    for (std::size_t t = 0; t < count; ++t) task(context, t);
}
void concurrent(const void*, std::size_t count, ma::PressureParallelPlan::Task task, const void* context) {
    std::vector<std::thread> threads;
    for (std::size_t t = 0; t < count; ++t) threads.emplace_back([=] { task(context, t); });
    for (auto& thread : threads) thread.join();
}
bool same_solve(const ma::PressureSolveResult& a, const ma::PressureSolveResult& b) {
    return a.iterations == b.iterations && std::bit_cast<std::uint64_t>(a.residual_norm) == std::bit_cast<std::uint64_t>(b.residual_norm) &&
        a.preconditioner_used == b.preconditioner_used && a.pressure.size() == b.pressure.size() &&
        std::equal(a.pressure.begin(), a.pressure.end(), b.pressure.begin(), [](double x, double y) {
            return std::bit_cast<std::uint64_t>(x) == std::bit_cast<std::uint64_t>(y);
        });
}
void parallel_plan_keeps_serial_rows_and_worker_independence() {
    const auto grid = holed_grid(61, 37);
    const auto count = grid.cells.size();
    std::vector<double> expected(count);
    for (std::size_t k = 0; k < count; ++k)
        expected[k] = 1e5 + 300 * std::sin(0.21 * static_cast<double>(grid.cells[k] % 61)) * std::cos(0.13 * static_cast<double>(grid.cells[k] / 61));
    for (const auto preconditioner : {ma::PressurePreconditioner::incomplete_cholesky, ma::PressurePreconditioner::jacobi}) {
        const ma::PressureSystem system(grid.compliance, grid.faces, preconditioner);
        const auto rhs = system.apply(expected);
        const std::vector<double> initial(count, 0);
        const auto serial = system.solve(rhs, initial, 0, 1e-6, 5000);
        residual_verified(system, rhs, serial);
        // One reduction block: the serial solve, bit for bit, in any schedule.
        for (const auto& [bands, chunks] : {std::pair<std::size_t, std::size_t>{1, 1}, {4, 6}, {9, 13}})
            for (const auto executor : {&sequential, &concurrent}) {
                auto plan = sweep_plan(grid, bands, chunks, count);
                plan.executor = executor;
                check(same_solve(serial, system.solve(rhs, initial, 0, 1e-6, 5000, &plan)),
                      "A one-block parallel solve differs from the serial solve");
            }
        // Fixed smaller blocks change only the reduction order: one result for
        // every schedule and worker count, meeting the unchanged tolerance.
        auto plan = sweep_plan(grid, 1, 1, 128); plan.executor = &sequential;
        const auto blocked = system.solve(rhs, initial, 0, 1e-6, 5000, &plan);
        residual_verified(system, rhs, blocked);
        check(blocked.iterations + 3 >= serial.iterations && blocked.iterations <= serial.iterations + 3,
              "Blocked reductions changed the convergence");
        for (const auto& [bands, chunks] : {std::pair<std::size_t, std::size_t>{3, 2}, {8, 11}, {37, 61}})
            for (const auto executor : {&sequential, &concurrent}) {
                auto other = sweep_plan(grid, bands, chunks, 128);
                other.executor = executor;
                check(same_solve(blocked, system.solve(rhs, initial, 0, 1e-6, 5000, &other)),
                      "Parallel pressure solve depends on its schedule");
            }
    }
    // A schedule that visits a row before its lower neighbour is refused.
    const ma::PressureSystem system(grid.compliance, grid.faces, ma::PressurePreconditioner::incomplete_cholesky);
    auto plan = sweep_plan(grid, 1, 1, 128); plan.executor = &sequential;
    std::reverse(plan.order.begin(), plan.order.end());
    bool refused = false;
    try { (void)system.solve(system.apply(expected), std::vector<double>(count), 0, 1e-6, 5000, &plan); }
    catch (const std::invalid_argument&) { refused = true; }
    check(refused, "An IC sweep plan that breaks a dependency was accepted");
}
void irregular_graph_preserves_references_barriers_and_parallel_faces() {
    // First two components have a reference only at one node. The second is
    // an explicit pressure boundary diagonal. Zero edges are true barriers;
    // node 8 is an isolated referenced cell. Triangles exercise factor fill
    // intersections, and reversed parallel faces exercise CSR coalescing.
    const std::vector<double> diagonal{2, 0, 0, 0, 0, 3, 0, 0, 4};
    const std::vector<ma::PressureFace> faces{{0,1,2},{1,0,0.5},{1,2,3},{0,2,1},{2,3,2},
        {3,4,0},{4,5,3},{5,6,4},{4,6,0.5},{6,7,2},{4,7,1},{7,8,0}};
    const std::vector<double> expected{3, 2, -4, 1, 6, -2, 2, 1, 8};
    const ma::PressureSystem system(diagonal, faces, ma::PressurePreconditioner::incomplete_cholesky);
    const auto rhs = system.apply(expected);
    const auto solved = system.solve(rhs, std::vector<double>(9), 1e-12, 1e-13);
    check(solved.preconditioner_used == ma::PressurePreconditioner::incomplete_cholesky, "Irregular IC(0) unexpectedly failed");
    residual_verified(system, rhs, solved);
    for (std::size_t i = 0; i < expected.size(); ++i) near(solved.pressure[i], expected[i], 1e-10);
    near(system.apply(std::vector<double>(9, 1))[3], 0, 0, 0);
}
void unsafe_or_dense_factors_fall_back_to_original_jacobi() {
    const ma::PressureSystem tiny({1e-14, 0}, {{0,1,1}}, ma::PressurePreconditioner::incomplete_cholesky);
    const std::vector<double> rhs{1,-1};
    const auto tiny_result = tiny.solve(rhs, std::vector<double>(2), 1e-10, 1e-12);
    check(tiny_result.preconditioner_fallback && tiny_result.preconditioner_used == ma::PressurePreconditioner::jacobi,
          "Unsafe tiny pivot did not select Jacobi");
    residual_verified(tiny, rhs, tiny_result);
    std::vector<ma::PressureFace> faces;
    std::vector<double> diagonal(67, 1), expected(67);
    for (std::size_t i = 0; i < 66; ++i) faces.push_back({i,66,1 + 0.1 * static_cast<double>(i % 3)});
    for (std::size_t i = 0; i < 67; ++i) expected[i] = std::sin(static_cast<double>(i));
    const ma::PressureSystem dense(diagonal, faces, ma::PressurePreconditioner::incomplete_cholesky);
    const auto dense_rhs = dense.apply(expected);
    const auto result = dense.solve(dense_rhs, std::vector<double>(67), 1e-12, 1e-13);
    check(result.preconditioner_fallback && result.preconditioner_used == ma::PressurePreconditioner::jacobi,
          "Unbounded lower degree did not select bounded fallback");
    residual_verified(dense, dense_rhs, result);
    for (std::size_t i = 0; i < 67; ++i) near(result.pressure[i], expected[i], 1e-10);
}
void incompressible_gauge_and_gravity_remain_physical() {
    const auto registry = ma::SpeciesRegistry::standard();
    ma::MixtureInventory water(registry.size());
    water.add(registry, registry.index("water"), registry[registry.index("water")].density, 300);
    const std::vector<ma::MaterialCell> cells{{water}, {water}, {water}};
    const std::vector<ma::AcousticFace> faces{{0,1,1,water.mass(),0,-9.81}, {1,2,1,water.mass(),0,-9.81}};
    const auto reference = ma::advance_material_pressure(registry, cells, faces, 0.1, false, ma::PressurePreconditioner::jacobi);
    const auto result = ma::advance_material_pressure(registry, cells, faces, 0.1, false, ma::PressurePreconditioner::incomplete_cholesky);
    check(result.incomplete_cholesky_solves > 0, "Liquid projection bypassed IC(0)");
    check(result.maximum_linear_residual_ratio <= 1 && result.maximum_volume_residual_m3 <= 2e-12,
          "Liquid pressure residual changed");
    for (std::size_t i = 0; i < cells.size(); ++i) {
        near(result.cells[i].volume_m3, 1, 0, 0);
        check(result.cells[i].mixture.masses == water.masses && result.cells[i].mixture.energy == water.energy,
              "Incompressible gauge modified thermodynamic inventory");
        near(result.work_pressures_pa[i], reference.work_pressures_pa[i], 1e-12);
        if (i + 1 < cells.size()) near(result.velocities_m_per_s[i], 0, 0, 1e-10);
    }
    near(result.work_pressures_pa[0], 101325, 1e-12);
    near(result.work_pressures_pa[0] - result.work_pressures_pa[1], water.mass() * 9.81, 1e-12);
}
ma::FluidSolverSettings quiet(ma::PressurePreconditioner preconditioner = ma::PressurePreconditioner::automatic) {
    ma::FluidSolverSettings settings; settings.gravity = 0; settings.conduction = false;
    settings.diffusivity_m2_per_s = 0; settings.viscosity_m2_per_s = 0;
    settings.pressure_preconditioner = preconditioner; return settings;
}
void fill(ma::FluidWorld& world) {
    const auto& r = world.registry();
    for (std::size_t y = 0; y < world.height(); ++y) for (std::size_t x = 0; x < world.width(); ++x) {
        auto inventory = ma::make_air(r, 0.8, 300, 100000 + 20 * std::sin(static_cast<double>(x + y)));
        inventory.add(r, r.index("water"), 0.2 * r[r.index("water")].density, 300);
        inventory.add(r, r.index("helium"), 1e-28, 300);
        world.set_cell(x, y, inventory);
    }
}
void coupled_world_preserves_conservation_and_reference_solution() {
    ma::FluidWorld reference(20,16,1,1,quiet(ma::PressurePreconditioner::jacobi));
    ma::FluidWorld improved(20,16,1,1,quiet());
    fill(reference); fill(improved);
    const auto before = improved.totals();
    const auto old = reference.step(0.1), result = improved.step(0.1);
    check(result.incomplete_cholesky_solves > 0 && result.maximum_linear_residual_ratio <= 1,
          "Connected nonlinear solve did not use validated IC(0)");
    check(result.substeps == old.substeps && result.rejected_substeps == old.rejected_substeps,
          "Optimization changed accepted timestep");
    const auto after = improved.totals();
    for (std::size_t s = 0; s < before.masses.size(); ++s) near(after.masses[s], before.masses[s], 1e-13, 0);
    near(after.energy(), before.energy(), 1e-13, 0);
    for (std::size_t y = 0; y < 16; ++y) for (std::size_t x = 0; x < 20; ++x) {
        const auto& a = reference.cell(x,y); const auto& b = improved.cell(x,y);
        for (std::size_t s = 0; s < before.masses.size(); ++s) near(b.mixture.masses[s], a.mixture.masses[s], 1e-11, 0);
        near(improved.pressure(x,y), reference.pressure(x,y), 1e-11);
        near(b.momentum_x, a.momentum_x, 1e-8, 1e-10); near(b.momentum_y, a.momentum_y, 1e-8, 1e-10);
    }
}
std::vector<ma::FluidCell> capture(const ma::FluidWorld& world) {
    std::vector<ma::FluidCell> cells;
    for (std::size_t y = 0; y < world.height(); ++y) for (std::size_t x = 0; x < world.width(); ++x)
        cells.push_back(world.cell(x,y));
    return cells;
}
void exact_state(const ma::FluidWorld& world, const std::vector<ma::FluidCell>& original) {
    const auto current = capture(world);
    check(world.time() == 0 && current.size() == original.size(), "Failed call advanced time or geometry");
    for (std::size_t i = 0; i < current.size(); ++i) {
        const auto& a = original[i]; const auto& b = current[i];
        check(a.wall == b.wall && a.mixture.masses == b.mixture.masses && a.mixture.energy == b.mixture.energy &&
              a.momentum_x == b.momentum_x && a.momentum_y == b.momentum_y && a.liquid_pressure_pa == b.liquid_pressure_pa,
              "Exception failed exact full-state rollback");
    }
}
void allocation_failure_before_and_after_first_commit_rolls_back_exactly() {
    ma::FluidWorld reference(4,3,1,1,quiet()); fill(reference);
    faults::allocations = 0; faults::count = true;
    const auto one = reference.step(0.1);
    faults::count = false;
    check(one.substeps == 1, "Fault calibration unexpectedly subcycled");
    const auto first_substep_allocations = faults::allocations;
    for (const auto position : {std::size_t{0}, first_substep_allocations + 2}) {
        ma::FluidWorld world(4,3,1,1,quiet()); fill(world);
        const auto original = capture(world);
        faults::remaining = static_cast<std::int64_t>(position);
        bool rejected = false;
        try { (void)world.step(0.2); } catch (const std::bad_alloc&) { rejected = true; }
        faults::remaining = -1;
        check(rejected, "Allocation fault did not propagate as its original exception");
        exact_state(world, original);
    }
}
void wall_metadata_and_budget_rollback_survive_move_owned_backup() {
    ma::FluidWorld world(4,3,1,1,quiet()); fill(world);
    ma::MixtureInventory empty(world.registry().size());
    world.set_cell(1,1,empty,0,0,12345); world.set_wall(1,1,true);
    const auto original = capture(world);
    bool rejected = false;
    try { (void)world.step(0.2,1); } catch (const std::runtime_error&) { rejected = true; }
    check(rejected, "Multi-step budget did not reject after first commit");
    exact_state(world, original);
    (void)world.step(0.1);
    const auto& wall = world.cell(1,1);
    check(wall.wall && wall.liquid_pressure_pa == 12345 && wall.mixture.masses == empty.masses && wall.mixture.energy == 0,
          "Untouched wall metadata was not preserved");
}
}

int main() {
    const std::vector<std::pair<const char*,std::function<void()>>> tests{
        {"IC(0) variable grid residual and iteration reduction", grid_reduces_iterations_without_relaxing_residual},
        {"IC(0) irregular referenced disconnected graph", irregular_graph_preserves_references_barriers_and_parallel_faces},
        {"parallel plan keeps serial rows and worker independence", parallel_plan_keeps_serial_rows_and_worker_independence},
        {"IC(0) unsafe/dense Jacobi fallback", unsafe_or_dense_factors_fall_back_to_original_jacobi},
        {"IC(0) incompressible gauge and gravity", incompressible_gauge_and_gravity_remain_physical},
        {"IC(0) coupled world reference and conservation", coupled_world_preserves_conservation_and_reference_solution},
        {"allocation exception exact rollback before and after commit", allocation_failure_before_and_after_first_commit_rolls_back_exactly},
        {"wall metadata and budget rollback", wall_metadata_and_budget_rollback_survive_move_owned_backup}};
    int failures = 0;
    for (const auto& [name,test] : tests) {
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failures; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
    }
    return failures == 0 ? 0 : 1;
}
