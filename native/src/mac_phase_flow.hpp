#pragma once
#include "mac_kernel_failure.hpp"
#include "mixed_atmospheres/mac_closed_world.hpp"
#include <algorithm>
#include <cmath>
#include <functional>

namespace ma::experimental {
struct MacPhaseFlow {
    MacClosedWorld world;
    MacTraceSinkReport trace_sink;
    std::size_t substeps = 0, rejected_attempts = 0;
    std::vector<long double> converted, converted_gross;
    double evaporated_kg = 0, condensed_kg = 0;
};

// The owner's ordinary phase relaxation follows its flow interval. An extreme
// rarefaction can reach the EOS temperature floor before that relaxation runs.
// Only after a failed owner interval, split flow around fixed-U phase
// equilibrium in cells within a factor two of their EOS temperature floor.
// Failed work and both children share the original attempt budget. Nothing is
// published or booked by the caller unless the entire interval succeeds.
inline MacPhaseFlow advance_mac_phase_flow(const MacClosedWorld& source, double seconds,
    std::size_t budget, std::span<const MacSaturationPair> pairs) {
    MacPhaseFlow result{source, {}, 0, 0, {}, {}, 0, 0};
    if(source.settings().numerical_kernel==MacNumericalKernel::game) {
        auto next=source.advanced(seconds,budget,MacExecutionPolicy::production);
        result.trace_sink=next.report.trace_sink;result.substeps=next.report.substeps;
        result.rejected_attempts=next.report.rejected_attempts;
        result.evaporated_kg=next.report.evaporated_kg;result.condensed_kg=next.report.condensed_kg;
        result.converted.assign(next.report.game_converted_mass_kg.begin(),next.report.game_converted_mass_kg.end());
        for(auto mass:result.converted)result.converted_gross.push_back(std::abs(mass));
        result.world=std::move(next.world);return result;
    }
    auto remaining = budget;
    const auto totals = [](const MacClosedWorld& w) {
        std::vector<long double> values(w.registry().size());
        for (const auto& cell : w.state().cells)
            for (std::size_t k = 0; k < values.size(); ++k) values[k] += cell.mixture.masses[k];
        return values;
    };
    std::function<MacClosedWorld(const MacClosedWorld&, double)> advance;
    advance = [&](const MacClosedWorld& world, double duration) -> MacClosedWorld {
        const double endpoint = world.time_s() + duration;
        std::size_t failed = 0; std::exception_ptr error;
        try {
            mac_kernel::failed_flow_attempts = 0;
            auto next = world.advanced(duration, remaining, MacExecutionPolicy::production);
            remaining -= next.report.attempts;
            result.trace_sink.merge(next.report.trace_sink);
            result.substeps += next.report.substeps;
            result.rejected_attempts += next.report.rejected_attempts;
            return std::move(next.world);
        } catch (...) { failed = mac_kernel::failed_flow_attempts; error = std::current_exception(); }
        if (pairs.empty() ||
            world.settings().step_control != MacStepControl::regional_predictive ||
            world.settings().diffusion.mode != MacDiffusionMode::disabled || failed == 0 || failed >= remaining ||
            world.time_s() + .5 * duration == world.time_s() || world.time_s() + .5 * duration >= endpoint)
            std::rethrow_exception(error);
        remaining -= failed; result.rejected_attempts += failed;
        auto first = advance(world, .5 * duration);
        std::vector<std::size_t> cells;
        const auto& st = first.state(); const auto& materials = first.thermodynamics().reference().materials();
        for (std::size_t i = 0; i < st.cells.size(); ++i) {
            if (st.grid.walls[i]) continue;
            const auto& mixture = st.cells[i].mixture;
            bool vapour = false;
            for (const auto& pair : pairs) vapour = vapour || mixture.masses[pair.vapour] > 0;
            if (!vapour) continue;
            double floor = 0;
            for (const auto& material : materials)
                if (mixture.masses[first.registry().index(material.id)] > 0)
                    floor = std::max(floor, material.minimum_temperature_k);
            try {
                if (first.thermodynamics().evaluate(mixture, mac_material_volume(st, i)).temperature_k <= 2 * floor)
                    cells.push_back(i);
            } catch (const ReferenceError&) { /* An unreadable trace is not a phase interface. */ }
        }
        if (!cells.empty()) {
            const auto before = totals(first);
            auto relaxed = first.with_phase_relaxation_in_cells(pairs, cells);
            if (relaxed.report.evaporated_kg + relaxed.report.condensed_kg > 0) {
                const auto after = totals(relaxed.world);
                if (result.converted.empty()) { result.converted.resize(before.size()); result.converted_gross.resize(before.size()); }
                for (std::size_t k = 0; k < before.size(); ++k) {
                    result.converted[k] += after[k] - before[k];
                    result.converted_gross[k] += std::abs(after[k] - before[k]);
                }
                result.evaporated_kg += relaxed.report.evaporated_kg;
                result.condensed_kg += relaxed.report.condensed_kg;
                first = std::move(relaxed.world);
            }
        }
        return advance(first, endpoint - first.time_s());
    };
    result.world = advance(source, seconds);
    return result;
}
}
