#pragma once
#include "mixed_atmospheres/mac_closed_world.hpp"
#include <memory>
#include <span>

namespace ma::experimental::mac_kernel {
struct Model;
struct Fields;
class Pool;
struct PressureTimeOptions {
    double seconds = 0, gravity = 0, maximum_outgoing_fraction = .45;
    unsigned fine_level = 0;
    std::size_t cell_interval_budget = 0;
};
struct PressureTimeReport {
    std::size_t coarse_cells = 0, fine_cells = 0, cell_intervals = 0;
    std::size_t coarse_newton = 0, fine_newton = 0, outer_linear = 0, fine_linear = 0, cycles = 0;
    std::size_t residual_evaluations = 0;
    std::size_t unresolved_updates = 0;
    MacPrimaryRoundingCounts primary_energy{};
    double maximum_closure = 0;
};
// Development component for normal-range EOS/linear coefficients. Pressure
// only: masses stay fixed; interval conservation/rounding/entropy audits and
// remapping belong to the caller. No owner or checkpoint selects this method.
// All state below is returned atomically after convergence.
struct PressureTimeResult {
    std::vector<double> energy, volume, momentum_x, momentum_y, qx, qy, body_work_y;
    std::vector<double> energy_remainders;
    std::vector<std::pair<std::uint32_t, MacEnergy>> full_qx, full_qy;
    MacEnergy wall_x, wall_y, body_y;
    PressureTimeReport report;
};
class PressureTimeWorkspace {
public:
    PressureTimeWorkspace();
    ~PressureTimeWorkspace();
    PressureTimeWorkspace(const PressureTimeWorkspace&) = delete;
    PressureTimeWorkspace& operator=(const PressureTimeWorkspace&) = delete;
    [[nodiscard]] std::size_t storage_bytes() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    friend PressureTimeResult advance_pressure_time_window(const Model&, const Fields&, const MacGrid&,
        std::span<const std::uint8_t>, const PressureTimeOptions&, Pool&, PressureTimeWorkspace&);
};
[[nodiscard]] PressureTimeResult advance_pressure_time_window(const Model&, const Fields&, const MacGrid&,
    std::span<const std::uint8_t> fine_cells, const PressureTimeOptions&, Pool&, PressureTimeWorkspace&);
}
