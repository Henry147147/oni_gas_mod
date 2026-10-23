// Production substep kernel driver (MAC_KERNEL.md): owner-state conversion,
// the reference substep schedule (or calm stepping), retries, the interval
// audit and atomic publication through MacClosedWorld.
#include "mac_kernel_detail.hpp"
#include "mac_kernel_failure.hpp"
#include "mac_kernel_regions.hpp"
#include <optional>
#include <string>
#include "mac_interval_audit.hpp"
#include "mixed_atmospheres/mac_phase_change.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <type_traits>

namespace ma::experimental {
struct MacKernelAccess {
    static const MacTransportState& state(const MacClosedWorld& w) { return w.state(); }
    static void publish_state(MacClosedWorld& w, MacTransportState next) { w.replace_state(std::move(next)); }
    static const SpeciesRegistry& registry(const MacClosedWorld& w) { return w.registry_; }
    static const FluidThermodynamics& eos(const MacClosedWorld& w) { return *w.thermodynamics_; }
    static const MacClosedSettings& settings(const MacClosedWorld& w) { return w.settings_; }
    static double time(const MacClosedWorld& w) { return w.time_s_; }
    static std::uint64_t revision(const MacClosedWorld& w) { return w.revision_; }
    static void stamp(MacClosedWorld& w, double time, std::uint64_t revision) {
        w.time_s_ = time; w.revision_ = revision; w.settings_.numerical_kernel = MacNumericalKernel::production;
    }
    static void select_production(MacClosedWorld& w) { w.settings_.numerical_kernel = MacNumericalKernel::production; }
    // The retained reference implementation for the same flow interval
    // (range flags): bypass dispatch but retain interval auditing and the
    // caller's numerical-kernel setting. Flow only, like the kernel: gas
    // diffusion half steps belong to the owner's composition around it.
    static MacClosedAdvance reference(const MacClosedWorld& w, double seconds, std::size_t budget) {
        return w.advanced_flow(seconds, budget, MacExecutionPolicy::reference, true);
    }
};
}

namespace ma::experimental::mac_kernel {
namespace {
using Clock = std::chrono::steady_clock;
// A value left the normal binary64 range during the interval: the caller
// reruns the whole interval through the reference implementation.
struct RangeFlag { std::size_t consumed_attempts = 0; };
struct SharedAttempts {
    std::size_t limit;
    std::atomic<std::size_t> used{};
    bool take() {
        auto n = used.load(std::memory_order_relaxed);
        while (n < limit) if (used.compare_exchange_weak(n, n + 1, std::memory_order_relaxed)) return true;
        return false;
    }
};

double since(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

// The time-centred sharp-interface gravity weight is implemented for the
// midpoint pressure stage only.
bool sharp_gravity_moments(const MacClosedSettings& s) {
    return s.reconstruction == MacMaterialReconstruction::interface_plic && s.gravity_m_per_s2 != 0 &&
        s.pressure_integration != MacPressureIntegration::discrete_gradient_midpoint;
}
void require_supported(const MacClosedSettings& s) {
    s.validate();
    require(s.reconstruction == MacMaterialReconstruction::donor_cell || s.reconstruction == MacMaterialReconstruction::limited_linear ||
        s.reconstruction == MacMaterialReconstruction::interface_plic, "Production kernel supports material reconstruction selectors 0, 1 and 3");
    require(s.momentum_reconstruction == MacMomentumReconstruction::donor_cell || s.momentum_reconstruction == MacMomentumReconstruction::limited_linear,
        "Production kernel supports momentum reconstruction selectors 0 and 1");
    require(s.preconditioner != PressurePreconditioner::incomplete_cholesky ||
        s.pressure_integration == MacPressureIntegration::discrete_gradient_midpoint,
        "Production incomplete Cholesky requires midpoint pressure integration");
    require(s.compression_viscosity != MacCompressionViscosity::coupled_pressure,
        "Production kernel does not support coupled compressive pressure (compression selector 3)");
    require(!sharp_gravity_moments(s), "Sharp-interface gravity weights require midpoint pressure integration");
    require(s.compression_viscosity != MacCompressionViscosity::monotone_acoustic_quadratic &&
        s.shock_heat != MacShockHeatRegularization::monotone_pressure_sensed,
        "Production kernel does not support monotone regularization (compression/heat selector 4)");
}
Model build_model(const SpeciesRegistry& registry, const FluidThermodynamics& eos, const MacTransportState& state) {
    eos.validate_registry(registry);
    Model m; m.registry_size = registry.size();
    std::vector<std::uint8_t> present(registry.size(), 0);
    std::set<std::uint64_t> ids;
    for (const auto& cell : state.cells) {
        require(cell.mixture.masses.size() == registry.size(), "Production kernel inventory shape differs");
        if (!cell.mixture.masses.implicit_zero())
            for (std::size_t k = 0; k < registry.size(); ++k) if (cell.mixture.masses[k] != 0) present[k] = 1;
        for (const auto& p : cell.passive_quantities) ids.insert(p.identity);
    }
    const auto& materials = eos.reference().materials();
    for (std::size_t k = 0; k < registry.size(); ++k) if (present[k]) {
        const auto& def = registry[k];
        const auto found = std::find_if(materials.begin(), materials.end(), [&](const auto& x) { return x.id == def.id; });
        require(found != materials.end(), "Production kernel constituent has no EOS profile");
        Species s; s.registry = k; s.profile = static_cast<std::size_t>(found - materials.begin());
        s.gas = found->phase == ReferencePhase::ideal_gas; s.molar_mass = found->molar_mass_kg_per_mol; s.cv = found->cv_j_per_kg_k;
        s.offset = found->energy_offset_j_per_kg; s.rho0 = found->zero_pressure_density_kg_per_m3; s.bulk = found->reference_bulk_modulus_pa;
        s.maximum_ratio = found->maximum_density_ratio; s.tmin = found->minimum_temperature_k; s.tmax = found->maximum_temperature_k;
        s.conductivity = def.conductivity; s.entropy_offset = found->entropy_offset_j_per_kg_k; s.group = s.gas ? 0 : def.liquid_phase_group;
        (s.gas ? m.gases : m.liquids).push_back(m.species.size());
        m.species.push_back(s);
    }
    m.phase_of.assign(m.species.size(), std::numeric_limits<std::size_t>::max());
    for (std::size_t l = 0; l < m.liquids.size(); ++l) {
        std::size_t first = l;
        const auto group = m.species[m.liquids[l]].group;
        if (group != 0) for (std::size_t q = 0; q < l; ++q) if (m.species[m.liquids[q]].group == group) { first = q; break; }
        m.phase_of[m.liquids[l]] = first;
    }
    m.passive_ids.assign(ids.begin(), ids.end());
    return m;
}
std::size_t load(const Exec& exec, const Model& model, const Grid& g, const MacTransportState& state, Fields& f) {
    const auto N = g.N, K = model.K(), P = model.P();
    f.m.assign(K * N, 0); f.U.resize(N); f.U_remainder.resize(N); f.q.assign(P * N, 0);
    exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) { for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
        const auto& cell = state.cells[i];
        if (!cell.mixture.masses.implicit_zero())
            for (std::size_t k = 0; k < K; ++k) f.m[k * N + i] = cell.mixture.masses[model.species[k].registry];
        f.U[i] = cell.mixture.energy; f.U_remainder[i] = cell.energy_remainder_j.projected;
        std::size_t p = 0;
        for (const auto& entry : cell.passive_quantities) {
            while (model.passive_ids[p] != entry.identity) ++p;
            f.q[p * N + i] = entry.quantity;
        }
    } });
    require(state.momentum_x.size() == g.NX && state.momentum_y.size() == g.NY, "Production kernel momentum shape differs");
    f.jx = state.momentum_x; f.jy = state.momentum_y;
    require(state.occupied_volumes_m3.empty() || state.occupied_volumes_m3.size() == N, "Production kernel occupied-volume shape differs");
    f.Vo = state.occupied_volumes_m3;
    std::size_t rounded = 0;
    for (const auto* full : {&state.full_momentum_x, &state.full_momentum_y})
        for (const auto& v : *full) if (v.project().rounded) ++rounded;
    return rounded;
}
void store(const Exec& exec, const Model& model, const Grid& g, const Fields& f, MacTransportState& out, bool canonical_void) {
    const auto N = g.N, K = model.K(), P = model.P();
    exec.rows([&](std::size_t y0, std::size_t y1, std::size_t) { for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
        auto& cell = out.cells[i];
        if (!g.wall[i] || !cell.mixture.masses.implicit_zero()) for (std::size_t k = 0; k < K; ++k) cell.mixture.masses[model.species[k].registry] = f.m[k * N + i];
        cell.mixture.energy = f.U[i]; cell.energy_remainder_j.projected = f.U_remainder[i]; cell.energy_remainder_j.full = {};
        cell.passive_quantities.clear();
        for (std::size_t p = 0; p < P; ++p) {
            const double v = f.q[p * N + i];
            if (v != 0) cell.passive_quantities.push_back({model.passive_ids[p], v});
        }
    } });
    out.momentum_x = f.jx; out.momentum_y = f.jy;
    out.full_momentum_x.clear(); out.full_momentum_y.clear();
    out.occupied_volumes_m3 = f.Vo;
    if (canonical_void && !out.occupied_volumes_m3.empty()) {
        bool partial = false;
        for (std::size_t i = 0; i < N; ++i)
            partial = partial || (out.cells[i].mixture.mass() > 0 && out.occupied_volumes_m3[i] < g.V);
        if (!partial) out.occupied_volumes_m3.clear();
    }
}
// Interval totals per audited quantity, from deterministic per-block
// partials: double-double signed sum, double-double absolute operand sum,
// operand count and the normal-range flag of the shared interval audit
// (mac_interval_audit.hpp).
struct Quantity {
    interval_detail::Sum sum, scale;
    std::size_t count = 0, tiny = 0;
    bool ordinary = true;
    void add(double v) {
        sum.add(v); scale.add(std::abs(v)); ++count;
        // Only overflow-range or nonfinite values leave the ordinary path.
        // Below-range (including subnormal) values round with an absolute
        // error of at most 2^-1075 per operation, counted in the bound.
        if (!std::isfinite(v) || std::abs(v) > 0x1p450) ordinary = false;
        if (v != 0 && std::abs(v) < 0x1p-450) ++tiny;
    }
    void merge(const Quantity& o) {
        sum.add(o.sum.hi); sum.add(o.sum.lo); scale.add(o.scale.hi); scale.add(o.scale.lo);
        count += o.count + 4; tiny += o.tiny; ordinary = ordinary && o.ordinary;
    }
};
using Totals = std::vector<Quantity>;
// Visits every audited operand of one field state: f(quantity, value).
template<class F> void visit_operands(const Context& c, const Fields& f, const std::vector<double>& mass,
    const std::vector<double>& ix, const std::vector<double>& iy, std::size_t y0, std::size_t y1, std::size_t blk, F&& add) {
    const auto& g = c.g; const auto N = g.N, K = c.model.K(), P = c.model.P();
    const double gravity = c.settings.gravity_m_per_s2;
    for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
        for (std::size_t k = 0; k < K; ++k) add(k, f.m[k * N + i]);
        for (std::size_t p = 0; p < P; ++p) add(K + p, f.q[p * N + i]);
        add(c.q_energy(), f.U[i]);
        if (f.U_remainder[i] != 0) add(c.q_energy(), f.U_remainder[i]);
        if (gravity != 0 && mass[i] != 0) add(c.q_energy(), gravity * (static_cast<double>(i / g.W) + .5) * g.h * mass[i]);
    }
    for (std::size_t row = y0; row < y1; ++row) for (std::size_t col = 0; col < g.XW; ++col) {
        const auto fidx = g.xface(col, row); const double j = f.jx[fidx];
        add(c.q_jx(), j); if (j != 0) add(c.q_energy(), .5 * j * (j / ix[fidx]));
    }
    for (std::size_t row = y0; row < g.block_yrow_end(blk); ++row) for (std::size_t col = 0; col < g.W; ++col) {
        const auto fidx = g.yface(col, row); const double j = f.jy[fidx];
        add(c.q_jy(), j); if (j != 0) add(c.q_energy(), .5 * j * (j / iy[fidx]));
    }
}
Totals totals(Context& c, const Fields& f, double sign) {
    const auto Q = c.quantities();
    std::vector<double> mass; cell_masses(c, f, mass);
    std::vector<double> ix, iy; face_inertias(c, mass, ix, iy);
    auto parts = c.exec.partials<Totals>([&](std::size_t y0, std::size_t y1, std::size_t blk) {
        Totals t(Q);
        visit_operands(c, f, mass, ix, iy, y0, y1, blk, [&](std::size_t q, double v) { t[q].add(sign * v); });
        return t;
    });
    Totals t(Q);
    for (const auto& part : parts) for (std::size_t q = 0; q < Q; ++q) t[q].merge(part[q]);
    return t;
}
// Exact replay of one quantity's operands (escalation path).
void exact_replay(Context& c, std::size_t q, const Fields& a, const Fields& b, double external, double stage, MacEnergyAccount& exact) {
    for (const auto* f : {&b, &a}) {
        const double sign = f == &b ? 1. : -1.;
        std::vector<double> mass; cell_masses(c, *f, mass);
        std::vector<double> ix, iy; face_inertias(c, mass, ix, iy);
        for (std::size_t blk = 0; blk < c.g.blocks; ++blk)
            visit_operands(c, *f, mass, ix, iy, c.g.block_row_begin(blk), c.g.block_row_end(blk), blk,
                [&](std::size_t quantity, double v) { if (quantity == q) exact.add(sign * v); });
    }
    exact.add(-external);
    // Per-stage operand magnitudes enter the bound as cancelling pairs.
    exact.add(stage); exact.add(-stage);
}
// Total stored-state entropy (reference absolute-entropy expression) and the
// 128-epsilon allowance term sum |U|/T.
void entropy(Context& c, const Fields& f, DD& total, double& allowance,
    const std::vector<std::uint8_t>* rest = nullptr, double* rest_operand = nullptr) {
    const auto& g = c.g; const auto N = g.N; const auto& model = c.model;
    constexpr double R = experimental::reference_gas_constant;
    struct Part { DD s; double allowance = 0, rest_allowance = 0; };
    auto parts = c.exec.partials<Part>([&](std::size_t y0, std::size_t y1, std::size_t) {
        Part o;
        for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
            if (g.wall[i]) continue;
            const auto st0 = cell_stats(model, f, N, i);
            if (st0.kind == empty) continue;
            State st;
            const double volume = material_volume(f, g.V, i);
            if (!evaluate_state(model, f, N, i, st0, f.U[i], volume, st)) reject("Production interval endpoint left its EOS domain");
            // The headspace nRT/p carries the EOS root's volume tolerance. A
            // gas-dominated mixed cell uses the closure V - Vliquid instead
            // (MAC_HEAT.md, "Entropy measurement").
            double headspace = st.Vg;
            if (st0.kind == mixed_cell) {
                const double liquid = liquid_at(model, f, N, i, st.p).volume;
                if (liquid <= .5 * volume) headspace = volume - liquid;
            }
            for (std::size_t k = 0; k < model.K(); ++k) {
                const double m = f.m[k * N + i]; if (m == 0) continue;
                const auto& sp = model.species[k];
                if (!(st.T >= sp.tmin * (1 - 32 * eps) && st.T <= sp.tmax * (1 + 32 * eps)))
                    reject("Production interval endpoint temperature outside its material range");
                o.s.add(m * sp.cv * std::log(st.T / experimental::entropy_reference_temperature_k));
                // Constant per mass: it matters only where phase change moves mass.
                if (sp.entropy_offset != 0) {
                    o.s.add(m * sp.entropy_offset); o.allowance += std::abs(m * sp.entropy_offset);
                    if (rest && (*rest)[i]) o.rest_allowance += std::abs(m * sp.entropy_offset);
                }
                if (sp.gas) { const double n = m / sp.molar_mass; o.s.add(n * R * (std::log(headspace) - std::log(n))); }
            }
            o.s.add(f.U_remainder[i] / st.T);
            o.allowance += (std::abs(f.U[i]) + std::abs(f.U_remainder[i])) / st.T;
            if (rest && (*rest)[i]) o.rest_allowance += (std::abs(f.U[i]) + std::abs(f.U_remainder[i])) / st.T;
        }
        return o;
    });
    for (const auto& p : parts) {
        total.add(p.s); allowance += p.allowance;
        if (rest_operand) *rest_operand += p.rest_allowance;
    }
}
void expand_unresolved_gas(Context& c, Fields& f, Accounts& acc) {
    if (c.settings.subcell_void != MacSubcellVoidPolicy::expand_unresolved_gas || f.Vo.empty()) return;
    const auto& g = c.g;
    for (std::size_t i = 0; i < g.N; ++i) {
        if (g.wall[i] || !(f.Vo[i] > 0 && f.Vo[i] <= c.settings.gas_expansion_volume_fraction * g.V)) continue;
        const auto stats = cell_stats(c.model, f, g.N, i);
        if (!(stats.nR > 0)) continue;
        State before, after;
        if (!evaluate_state(c.model, f, g.N, i, stats, f.U[i], f.Vo[i], before) ||
            !evaluate_state(c.model, f, g.N, i, stats, f.U[i], g.V, after))
            reject("Unresolved gas expansion left its EOS domain");
        const auto headspace = [&](const State& state, double volume) {
            if (stats.kind != mixed_cell) return state.Vg;
            const double liquid = liquid_at(c.model, f, g.N, i, state.p).volume;
            return liquid <= .5 * volume ? volume - liquid : state.Vg;
        };
        // Same entropy expression as the interval audit, differenced at fixed
        // masses/U. Species offsets cancel. Primary-U remainders are retained
        // and observed with the interval's U_low/T convention. This is physical
        // free expansion: its positive entropy is not a rounding allowance.
        DD increase;
        increase.add(stats.Cv * (std::log(after.T) - std::log(before.T)));
        increase.add(stats.nR * (std::log(headspace(after, g.V)) - std::log(headspace(before, f.Vo[i]))));
        increase.add(f.U_remainder[i] / after.T);
        increase.add(-f.U_remainder[i] / before.T);
        if (!std::isfinite(increase.value()) || increase.value() < 0)
            reject("Unresolved gas expansion did not increase entropy");
        if (acc.subcell_void_events == std::numeric_limits<std::size_t>::max())
            precision("Unresolved gas expansion counter overflow");
        ++acc.subcell_void_events; acc.subcell_void_entropy.add(increase);
        // No conserved field is written. In particular U and its carried
        // remainder, constituent/passive inventories and face J stay intact.
        f.Vo[i] = g.V;
    }
}
void substep(Context& c, Fields& f, double dt, Accounts& acc) {
    const bool timed = c.options.collect_stage_timing;
    const auto run = [&](auto&& stage, double seconds, double& slot) {
        if (!timed) { stage(c, f, seconds, acc); return; }
        const auto t = Clock::now(); stage(c, f, seconds, acc); slot += since(t);
    };
    auto& tm = c.timing;
    // Sharp-interface boiling conducts once per substep before the pressure
    // stage, which converts that heat at each interface. Backward-Euler
    // conduction is first order either way.
    const bool boiling = c.sharp_boiling();
    run(stage_viscosity, .5 * dt, tm.viscosity_ms);
    run(stage_conduction, boiling ? dt : .5 * dt, tm.conduction_ms);
    run(stage_shock_heat, .5 * dt, tm.shock_heat_ms);
    run(stage_compression, .5 * dt, tm.compression_ms);
    { double unused = 0; run(c.settings.pressure_integration != MacPressureIntegration::discrete_gradient_midpoint ?
        stage_pressure_transport_moments : stage_pressure_transport, dt, unused); }
    run(stage_compression, .5 * dt, tm.compression_ms);
    run(stage_shock_heat, .5 * dt, tm.shock_heat_ms);
    if (!boiling) run(stage_conduction, .5 * dt, tm.conduction_ms);
    run(stage_viscosity, .5 * dt, tm.viscosity_ms);
    expand_unresolved_gas(c, f, acc);
}
void merge(Accounts& a, const Accounts& b) {
    a.caloric_cells += b.caloric_cells; a.unresolved_conduction_cells += b.unresolved_conduction_cells;
    for (std::size_t n = 0; n < b.caloric_redistribution.size(); ++n) a.caloric_redistribution.add(b.caloric_redistribution.component(n));
    for (std::size_t q = 0; q < a.operands.size(); ++q) {
        a.operands[q] += b.operands[q]; a.rounding[q] += b.rounding[q];
        a.transfers[q].merge(b.transfers[q]);
        a.primary[q].merge(b.primary[q]);
        a.inventory[q].merge(b.inventory[q]);
    }
    a.wall_x += b.wall_x; a.wall_y += b.wall_y; a.body_y += b.body_y;
    a.mixing_heat += b.mixing_heat; a.wall_heat += b.wall_heat; a.compression_heat += b.compression_heat; a.shock_heat += b.shock_heat;
    a.gravity_entropy.add(b.gravity_entropy); a.gravity_entropy_operands += b.gravity_entropy_operands;
    a.unresolved += b.unresolved;
    for (std::size_t q = 0; q < a.conversion.size(); ++q) a.conversion[q].add(b.conversion[q]);
    a.evaporated += b.evaporated; a.condensed += b.condensed; a.interface_conversions += b.interface_conversions;
    if (b.subcell_void_events > std::numeric_limits<std::size_t>::max() - a.subcell_void_events)
        precision("Unresolved gas expansion counter overflow");
    a.subcell_void_events += b.subcell_void_events; a.subcell_void_entropy.add(b.subcell_void_entropy);
    a.physical.merge(b.physical); a.energy.merge(b.energy);
}
std::string quantity_name(const Context& c, std::size_t q) {
    if (q < c.model.K()) return "mass:" + std::to_string(c.model.species[q].registry);
    if (q < c.model.K() + c.model.P()) return "passive:" + std::to_string(c.model.passive_ids[q - c.model.K()]);
    if (q == c.q_jx()) return "momentum_x";
    if (q == c.q_jy()) return "momentum_y";
    return "energy_u_k_g";
}
// Parallel copy of the field state (fixed row blocks of every column).
void copy_fields(const Exec& exec, const Grid& g, const Model& model, const Fields& from, Fields& to) {
    const auto N = g.N;
    to.m.resize(from.m.size()); to.U.resize(from.U.size()); to.U_remainder.resize(from.U_remainder.size()); to.q.resize(from.q.size());
    to.jx.resize(from.jx.size()); to.jy.resize(from.jy.size()); to.Vo.resize(from.Vo.size());
    exec.rows([&](std::size_t y0, std::size_t y1, std::size_t blk) {
        const auto a = y0 * g.W, b = y1 * g.W;
        for (std::size_t k = 0; k < model.K(); ++k) std::copy(from.m.begin() + static_cast<std::ptrdiff_t>(k * N + a), from.m.begin() + static_cast<std::ptrdiff_t>(k * N + b), to.m.begin() + static_cast<std::ptrdiff_t>(k * N + a));
        for (std::size_t p = 0; p < model.P(); ++p) std::copy(from.q.begin() + static_cast<std::ptrdiff_t>(p * N + a), from.q.begin() + static_cast<std::ptrdiff_t>(p * N + b), to.q.begin() + static_cast<std::ptrdiff_t>(p * N + a));
        std::copy(from.U.begin() + static_cast<std::ptrdiff_t>(a), from.U.begin() + static_cast<std::ptrdiff_t>(b), to.U.begin() + static_cast<std::ptrdiff_t>(a));
        std::copy(from.U_remainder.begin() + static_cast<std::ptrdiff_t>(a), from.U_remainder.begin() + static_cast<std::ptrdiff_t>(b), to.U_remainder.begin() + static_cast<std::ptrdiff_t>(a));
        if (!from.Vo.empty()) std::copy(from.Vo.begin() + static_cast<std::ptrdiff_t>(a), from.Vo.begin() + static_cast<std::ptrdiff_t>(b), to.Vo.begin() + static_cast<std::ptrdiff_t>(a));
        std::copy(from.jx.begin() + static_cast<std::ptrdiff_t>(y0 * g.XW), from.jx.begin() + static_cast<std::ptrdiff_t>(y1 * g.XW), to.jx.begin() + static_cast<std::ptrdiff_t>(y0 * g.XW));
        std::copy(from.jy.begin() + static_cast<std::ptrdiff_t>(y0 * g.W), from.jy.begin() + static_cast<std::ptrdiff_t>(g.block_yrow_end(blk) * g.W), to.jy.begin() + static_cast<std::ptrdiff_t>(y0 * g.W));
    });
}
struct Workspaces { Workspace ws; Fields start, current, trial; };
std::size_t retained_bytes(const Workspaces& w) {
    std::size_t total = 0;
    const auto add = [&](const auto& v) { total += v.capacity() * sizeof(typename std::decay_t<decltype(v)>::value_type); };
    const auto& ws = w.ws; const auto& e = ws.eos;
    if (ws.moments) {
        if (ws.moments->coupled) total += ws.moments->coupled->storage_bytes();
        for (const auto* s : {&ws.moments->accepted, &ws.moments->trial})
            for (const auto* v : {&s->a, &s->b, &s->mean, &s->first, &s->du, &s->ra, &s->rb}) add(*v);
        for (const auto& v : ws.moments->linear) add(v);
    }
    if (ws.pressure_topology) total += ws.pressure_topology->storage_bytes();
    if (ws.pressure_multigrid) total += ws.pressure_multigrid->storage_bytes();
    for (const auto* v : {&e.M, &e.Cv, &e.off, &e.nR, &e.Nl, &e.T, &e.p, &e.Vg, &e.Eel, &e.Ceq, &e.comp, &e.lcomp}) add(*v);
    add(e.kind); add(ws.anchor);
    add(ws.full_qx.values); add(ws.full_qx.faces); add(ws.full_qy.values); add(ws.full_qy.faces);
    for (const auto* v : {&ws.cell_mass, &ws.mass_after, &ws.ix, &ws.iy, &ws.ix1, &ws.iy1, &ws.inv_ix, &ws.inv_iy, &ws.dp0x, &ws.dp0y,
        &ws.wx, &ws.wy, &ws.bdiag, &ws.dV, &ws.dW, &ws.cc, &ws.res, &ws.sx, &ws.sy, &ws.tdV, &ws.tdW, &ws.tcc, &ws.tres, &ws.sol,
        &ws.Vmat, &ws.dU, &ws.tdU, &ws.qx, &ws.qy, &ws.body_work_y, &ws.lr, &ws.lz, &ws.lp, &ws.lq, &ws.lAp, &ws.ldiag, &ws.px, &ws.py,
        &ws.retained, &ws.fx, &ws.fy, &ws.theta, &ws.keep, &ws.jsum, &ws.ktr, &ws.mix, &ws.wheat, &ws.v0, &ws.vprov, &ws.slope0,
        &ws.slope1, &ws.cour0, &ws.cour1, &ws.face_heat_x, &ws.face_heat_y, &ws.dual_en, &ws.dual_et, &ws.dual_j1, &ws.speed,
        &ws.pressure, &ws.sensor0, &ws.sensor1, &ws.closing, &ws.diffusion, &ws.cp, &ws.ceq, &ws.temperature, &ws.heat, &ws.proxy,
        &ws.phase_alpha, &ws.phase_gas, &ws.phase_liquid_energy, &ws.phase_gas_energy, &ws.phase_total, &ws.phase_liquid_fraction,
        &ws.gravity_weight_start, &ws.gravity_weight_end, &ws.gravity_alpha, &ws.gravity_total, &ws.gravity_lower, &ws.gravity_mass,
        &ws.gravity_fraction, &ws.px_energy_remainder, &ws.py_energy_remainder, &ws.retained_energy_remainder,
        &ws.interface_temperature, &ws.interface_heat, &ws.interface_conductance, &ws.interface_slope,
        &ws.interface_latent, &ws.interface_mass, &ws.interface_volume, &ws.Vswept, &ws.filled, &ws.material_x, &ws.void_x,
        &ws.material_y, &ws.void_y, &ws.retained_material, &ws.retained_void}) add(*v);
    add(ws.interface_pair);
    add(ws.dual_has_n); add(ws.dual_has_t); add(ws.flag); add(ws.two_phase); add(ws.phase_liquids); add(ws.phase_heavy); add(ws.gravity_heavy);
    for (const auto* f : {&w.start, &w.current, &w.trial}) { add(f->m); add(f->U); add(f->U_remainder); add(f->q); add(f->jx); add(f->jy); add(f->Vo); }
    return total;
}
Workspaces& workspaces() { static thread_local Workspaces w; return w; }
// Threads beyond one per minimum_blocks_per_thread row blocks would only
// wait; the cap never changes a result.
unsigned threads_for(const Grid& g, const MacKernelOptions& o) {
    return static_cast<unsigned>(std::clamp<std::size_t>(g.blocks / o.minimum_blocks_per_thread, 1, o.threads));
}

// Runs one interval; writes the endpoint into out (pre-filled with a copy of
// the source state, so per-cell vectors are reused).
MacKernelReport run_interval(const SpeciesRegistry& registry, const FluidThermodynamics& eos, const MacTransportState& source,
    const MacClosedSettings& settings, double start_time, double seconds, std::size_t budget, const MacKernelOptions& options,
    MacTransportState& out, SharedAttempts* shared = nullptr) {
    require_mac_energy_environment();
    options.validate(); require_supported(settings);
    require(std::isfinite(seconds) && seconds > 0 && seconds <= 10 && budget > 0 && budget <= 65536,
        "Invalid production interval or attempt budget");
    require(settings.gravity_m_per_s2 == 0 || !source.grid.periodic_y, "Constant MAC gravity requires nonperiodic vertical boundaries");
    const auto conversion_start = Clock::now();
    const Grid g(source.grid);
    if (!std::isnormal(g.V) || !std::isnormal(g.A)) throw RangeFlag{0};
    require(source.cells.size() == g.N, "Production kernel cell shape differs");
    Model model = build_model(registry, eos, source);
    model.recover_mixed_eos_endpoint = settings.recover_mixed_eos_endpoint;
    auto& w = workspaces();
    w.ws.caloric_labels.clear(); w.ws.caloric_mask.clear(); w.ws.caloric_regions = 0;
    MacKernelReport report;
    report.threads_used = threads_for(g, options);
    // Outer regional workers never dispatch into the same shared pool.
    std::optional<Pool> local_pool;
    if (shared && report.threads_used == 1) local_pool.emplace(1);
    auto& execution_pool = local_pool ? *local_pool : pool_for(report.threads_used);
    Context c{w.ws, source.grid, model, g, settings, options, Exec{g, execution_pool}, {}, {}};
    if (settings.phase_change == MacPhaseChange::saturation) {
        c.thermodynamics = &eos;
        const auto index = [&](std::size_t registry_index) {
            for (std::size_t k = 0; k < model.K(); ++k) if (model.species[k].registry == registry_index) return k;
            return model.K();
        };
        for (const auto& pair : mac_saturation_pairs(registry, eos)) {
            const auto l = index(pair.liquid), v = index(pair.vapour);
            if (l < model.K() && v < model.K()) c.phase_pairs.push_back({l, v, model.species[l].profile, model.species[v].profile});
        }
        require(c.phase_pairs.size() < 255, "Too many production saturation pairs");
    }
    report.rounded_range_inputs = load(c.exec, model, g, source, w.start);
    require(settings.vacuum == MacVacuumRepresentation::void_phase || w.start.Vo.empty(), "MAC occupied volumes require the void phase");
    for (std::size_t i = 0; i < g.N; ++i) {
        if (g.wall[i]) {
            for (std::size_t k = 0; k < model.K(); ++k) require(w.start.m[k * g.N + i] == 0, "Production kernel solid contains fluid");
            require(w.start.U[i] == 0, "Production kernel solid contains energy");
        }
        if (!w.start.Vo.empty()) {
            const double occupied = w.start.Vo[i]; const auto st = cell_stats(model, w.start, g.N, i);
            require(std::isfinite(occupied) && (g.wall[i] || st.M == 0 ? occupied == 0 : occupied > 0 && occupied <= g.V),
                "Invalid MAC occupied volume");
            // A partly filled liquid-only cell holds its liquid at zero pressure.
            require(st.kind != liquid_cell || occupied == g.V || std::abs(occupied - st.Nl) <= 32 * eps * g.V,
                "A void-phase liquid surface must hold its liquid at zero pressure");
        }
    }
    report.timing.conversion_ms += since(conversion_start);
    const double endpoint = start_time + seconds;
    if (!std::isfinite(endpoint) || endpoint <= start_time) throw FluidFatalError(FluidFatalCode::numerical_precision, "Production clock cannot advance");
    const auto audit_start = Clock::now();
    const auto t0 = totals(c, w.start, -1);
    for (const auto& q : t0) if (!q.ordinary) throw RangeFlag{0};
    DD s0; double allowance0 = 0;
    try { entropy(c, w.start, s0, allowance0); } catch (const EosRefinement&) { throw RangeFlag{0}; }
    report.timing.audit_ms += since(audit_start);
    double maximum_dt = settings.maximum_timestep_s;
    const bool predictive = settings.step_control != MacStepControl::doubling;
    for (std::size_t audit_attempt = 0;; ++audit_attempt) {
        copy_fields(c.exec, g, model, w.start, w.current);
        Accounts acc; acc.resize(c.quantities());
        // Configured coarse geometry also resolves an incoming tiny gas cell
        // before its pressure step can require an unrepresentable timestep.
        // Legacy owners retain their original after-substep expansion only.
        if (settings.gas_expansion_volume_fraction != 16 * eps) {
            const auto expansion_start = Clock::now();
            expand_unresolved_gas(c, w.current, acc);
            if (options.collect_stage_timing) report.timing.eos_ms += since(expansion_start);
        }
        double time = start_time, next_dt = maximum_dt;
        report.minimum_timestep_s = std::numeric_limits<double>::infinity(); report.maximum_timestep_s = 0;
        while (time < endpoint) {
            double dt = std::min(next_dt, endpoint - time);
            bool accepted = false;
            while (!accepted) {
                if (report.attempts == budget || (shared && !shared->take()))
                    throw std::runtime_error("Production interval exhausted its shared attempt budget");
                ++report.attempts;
                if (!(dt > 0) || time + dt <= time) throw FluidFatalError(FluidFatalCode::numerical_precision, "Production retry time cannot be represented");
                copy_fields(c.exec, g, model, w.current, w.trial);
                w.ws.stats_valid = false; // trial masses come from the accepted state
                w.ws.cfl_ratio = 0;
                Accounts stage; stage.resize(c.quantities());
                try {
                    substep(c, w.trial, dt, stage);
                    stage.physical.accepted_substeps = 1;
                    std::swap(w.current, w.trial);
                    merge(acc, stage);
                    time += dt; ++report.substeps; report.cell_substeps += g.N;
                    report.minimum_timestep_s = std::min(report.minimum_timestep_s, dt);
                    report.maximum_timestep_s = std::max(report.maximum_timestep_s, dt);
                    next_dt = std::min(maximum_dt, 2 * dt); accepted = true;
                    // Predictive control: the swept volume grows about linearly
                    // with the step, so aim the next one at 80% of the CFL limit.
                    if (predictive && w.ws.cfl_ratio > 0) next_dt = std::min(next_dt, std::max(.5 * dt, .8 * dt / w.ws.cfl_ratio));
                } catch (const EosRefinement&) {
                    throw RangeFlag{report.attempts};
                } catch (const ArithmeticRefinement&) {
                    throw RangeFlag{report.attempts};
                } catch (const std::domain_error& error) {
                    if (kernel_debug()) std::fprintf(stderr, "reject t=%.17g dt=%.17g: %s\n", time, dt, error.what());
                    ++report.rejected_attempts;
                    // A CFL rejection retries at 80% of the measured limit
                    // (at most half the step); other rejections halve.
                    dt *= predictive && w.ws.cfl_ratio > 1 ? std::max(1. / 64, std::min(.5, .8 / w.ws.cfl_ratio)) : .5;
                }
            }
        }
        // ---- interval audit
        const auto audit_time = Clock::now();
        const auto t1 = totals(c, w.current, 1);
        if (std::any_of(t1.begin(),t1.end(),[](const auto& q) { return !q.ordinary; })) {
            bool evacuated=false;
            if (settings.game_profile==MacGameProfile::base_game_floor && settings.step_control==MacStepControl::regional_predictive) {
                std::vector<double> mass,ix,iy; cell_masses(c,w.current,mass); face_inertias(c,mass,ix,iy);
                for (std::size_t i=0;i<ix.size();++i) evacuated=evacuated || (ix[i]==0 && w.current.jx[i]!=0);
                for (std::size_t i=0;i<iy.size();++i) evacuated=evacuated || (iy[i]==0 && w.current.jy[i]!=0);
            }
            if (!evacuated || audit_attempt>=8 || report.attempts>=budget) throw RangeFlag{report.attempts};
            // A cancelled momentum remainder cannot live on an emptied face.
            // Discard the whole endpoint and retry; all original audits still run.
            report.timing.audit_ms+=since(audit_time);
            ++report.audit_retries; maximum_dt*=.5; continue;
        }
        bool passed = true;
        report.audits.clear();
        for (std::size_t q = 0; q < c.quantities(); ++q) {
            double external = 0;
            if (q < model.K()) external = acc.conversion[q].value();
            if (q == c.q_jx()) external = acc.wall_x;
            if (q == c.q_jy()) external = acc.wall_y + acc.body_y;
            // Shared interval rule (mac_interval_audit.hpp): residual against
            // 128 epsilons of the absolute operands, with an outward error
            // envelope; undecidable comparisons replay exactly. Operands here
            // are both endpoints, external impulses and every stage's
            // operand magnitude (entered as cancelling pairs).
            Quantity total = t0[q]; total.merge(t1[q]);
            total.add(-external); total.add(acc.operands[q]); total.add(-acc.operands[q]);
            constexpr double e = eps;
            // Counted below-range rounding: every operand value may carry up
            // to 2^-1075 of absolute rounding per substep (subnormal results,
            // projected full-range inputs), independent of its magnitude.
            const double tiny_allowance = 0x1p-1070 * static_cast<double>(total.count) * static_cast<double>(report.substeps + 1);
            const double residual = total.sum.value(), scale = total.scale.value(), bound = 128 * e * scale + tiny_allowance;
            const double error = 64 * e * e * (static_cast<double>(total.count) + 1) * scale + 16 * e * (std::abs(residual) + bound);
            const double margin = bound - std::abs(residual);
            MacKernelAudit a; a.quantity = quantity_name(c, q); a.residual = residual; a.bound = bound;
            a.rounding_allowance = acc.rounding[q] + acc.primary[q].allowance() + acc.inventory[q].allowance();
            a.rounded_primary_writes = acc.primary[q].count;
            a.rounded_inventory_writes = acc.inventory[q].count;
            if (q == c.q_energy()) {
                a.rounding_allowance += acc.energy.discarded.allowance();
                a.rounded_energy_writes = acc.energy.high_count;
                a.discarded_energy_remainders = acc.energy.discarded.count;
            }
            a.stage_operand_magnitude = acc.operands[q]; a.below_range_allowance = tiny_allowance;
            const auto& transfers = acc.transfers[q];
            a.rounded_transfers = transfers.count; a.transfer_roundoff = transfers.error.value();
            a.transfer_roundoff_bound = transfers.bound();
            if (total.ordinary && std::isfinite(scale) && std::isfinite(residual) && total.count < (std::size_t{1} << 26) &&
                (margin > error || margin < -error)) a.passed = margin > error;
            else {
                if (kernel_debug()) std::fprintf(stderr, "escalate %s residual %.3e bound %.3e error %.3e ordinary %d count %zu\n", quantity_name(c, q).c_str(), residual, bound, error, total.ordinary ? 1 : 0, total.count);
                a.escalated = true;
                // The absolute allowance enters the exact 2^-45 operand bound as a cancelling pair.
                MacEnergyAccount exact; exact_replay(c, q, w.start, w.current, external, acc.operands[q] + std::ldexp(tiny_allowance, 44), exact);
                a.passed = exact.within_bound();
            }
            a.passed = a.passed && transfers.within_bound() && acc.primary[q].within_bound(bound) &&
                acc.inventory[q].within_bound(bound) && a.rounding_allowance <= bound &&
                (q != c.q_energy() || acc.energy.discarded.within_bound(bound));
            passed = passed && a.passed;
            if (q == c.q_energy()) { report.energy_residual_j = residual; report.energy_bound_j = bound; }
            report.audits.push_back(std::move(a));
        }
        DD s1; double allowance1 = 0; bool endpoint_ok = true;
        try { entropy(c, w.current, s1, allowance1); } catch (const EosRefinement&) { throw RangeFlag{report.attempts}; }
        catch (const std::domain_error& error) {
            // An endpoint outside the EOS domain retries the interval smaller, as a failed audit does.
            if (kernel_debug()) std::fprintf(stderr, "endpoint rejected: %s\n", error.what());
            endpoint_ok = false;
        }
        DD change = s1; change.add(-s0.hi); change.add(-s0.lo);
        report.entropy_change_j_per_k = change.value();
        // Every stage except the gravity reconciliation must produce entropy;
        // that reconciliation is credited by its concavity bound.
        report.gravity_entropy_bound_j_per_k = acc.gravity_entropy.value();
        change.add(-acc.gravity_entropy.hi); change.add(-acc.gravity_entropy.lo);
        report.entropy_allowance_j_per_k = 128 * eps * (allowance0 + allowance1 + acc.gravity_entropy_operands);
        const bool entropy_ok = endpoint_ok && change.value() >= -report.entropy_allowance_j_per_k;
        report.timing.audit_ms += since(audit_time);
        if (passed && entropy_ok) {
            report.mixing_heat_j = acc.mixing_heat; report.wall_heat_j = acc.wall_heat;
            report.compression_heat_j = acc.compression_heat; report.shock_heat_transfer_j = acc.shock_heat;
            report.wall_impulse_x = acc.wall_x; report.wall_impulse_y = acc.wall_y; report.body_impulse_y = acc.body_y;
            report.unresolved_updates = acc.unresolved;
            report.interface_conversions = acc.interface_conversions;
            report.evaporated_kg = acc.evaporated; report.condensed_kg = acc.condensed;
            report.subcell_void_events = acc.subcell_void_events;
            report.subcell_void_entropy_j_per_k = acc.subcell_void_entropy.value();
            report.rounded_transfers = 0;
            report.rounded_primary_writes = 0;
            report.rounded_inventory_writes = 0;
            report.rounded_energy_writes = acc.energy.high_count;
            report.discarded_energy_remainders = acc.energy.discarded.count;
            for (const auto& transfers : acc.transfers) {
                if (transfers.count > std::numeric_limits<std::size_t>::max() - report.rounded_transfers)
                    precision("Production interval parcel rounding counter overflow");
                report.rounded_transfers += transfers.count;
            }
            for (const auto& primary : acc.primary) {
                if (primary.count > std::numeric_limits<std::size_t>::max() - report.rounded_primary_writes)
                    precision("Production primary rounding counter overflow");
                report.rounded_primary_writes += primary.count;
            }
            for (const auto& inventory : acc.inventory) {
                if (inventory.count > std::numeric_limits<std::size_t>::max() - report.rounded_inventory_writes)
                    precision("Production inventory rounding counter overflow");
                report.rounded_inventory_writes += inventory.count;
            }
            report.caloric_resolution_cells = acc.caloric_cells;
            report.unresolved_conduction_cells = acc.unresolved_conduction_cells;
            report.caloric_redistribution_j = acc.caloric_redistribution.value().project().value;
            report.physical = acc.physical.report();
            break;
        }
        // A failed audit is never published: retry the interval smaller.
        if (kernel_debug()) std::fprintf(stderr, "audit passed %d entropy change %.17g gravity bound %.17g allowance %.17g\n", passed ? 1 : 0,
            report.entropy_change_j_per_k, report.gravity_entropy_bound_j_per_k, report.entropy_allowance_j_per_k);
        ++report.audit_retries; maximum_dt *= .5;
        if (report.attempts >= budget || audit_attempt >= 8)
            throw FluidFatalError(FluidFatalCode::numerical_precision, passed ? "Production interval entropy audit failed" : "Production interval conservation audit failed");
    }
    const auto store_time = Clock::now();
    store(c.exec, model, g, w.current, out, settings.subcell_void == MacSubcellVoidPolicy::expand_unresolved_gas);
    report.timing.conversion_ms += since(store_time);
    report.workspace_bytes = retained_bytes(w);
    report.multigrid_solves = c.counters.multigrid_solves; report.multigrid_cycles = c.counters.multigrid_cycles;
    report.multigrid_fallbacks = c.counters.multigrid_fallbacks; report.multigrid_levels = c.counters.multigrid_levels;
    report.topology_builds = c.counters.topology_builds;
    report.incomplete_cholesky_solves = c.counters.incomplete_cholesky_solves;
    report.incomplete_cholesky_fallbacks = c.counters.incomplete_cholesky_fallbacks;
    report.maximum_linear_components = c.counters.maximum_linear_components;
    if (kernel_debug() && report.incomplete_cholesky_solves != 0)
        std::fprintf(stderr, "IC components %zu solves %zu fallbacks %zu\n", report.maximum_linear_components,
            report.incomplete_cholesky_solves, report.incomplete_cholesky_fallbacks);
    report.newton_iterations = c.counters.newton; report.linear_iterations = c.counters.linear;
    report.line_search_backtracks = c.counters.line_search; report.eos_evaluations = c.counters.eos;
    report.isentropic_evaluations = c.counters.isentropic; report.reconstruction_backtracks = c.counters.reconstruction_backtracks;
    report.limited_momentum_corrections = c.counters.limited_momentum; report.compression_events = c.counters.compression_events;
    report.shock_heat_events = c.counters.shock_events; report.conduction_stages = c.counters.conduction_stages;
    report.viscosity_modes = c.counters.viscosity_modes; report.maximum_volume_closure = c.counters.maximum_volume_closure;
    const auto& tm = c.timing; auto& rt = report.timing;
    rt.viscosity_ms += tm.viscosity_ms; rt.conduction_ms += tm.conduction_ms; rt.shock_heat_ms += tm.shock_heat_ms;
    rt.compression_ms += tm.compression_ms; rt.pressure_ms += tm.pressure_ms; rt.eos_ms += tm.eos_ms;
    rt.transport_ms += tm.transport_ms; rt.reconstruction_ms += tm.reconstruction_ms; rt.momentum_ms += tm.momentum_ms;
    return report;
}
#include "mac_kernel_regional_driver.hpp"
MacKernelReport& last_report() { static thread_local MacKernelReport r; return r; }
}
}

namespace ma::experimental {
bool mac_kernel_supports(const MacClosedSettings& s) {
    if (s.step_control == MacStepControl::regional_predictive &&
        (s.phase_change != MacPhaseChange::none || s.vacuum != MacVacuumRepresentation::void_phase ||
         s.pressure_integration != MacPressureIntegration::discrete_gradient_midpoint)) return false;
    return (s.preconditioner != PressurePreconditioner::incomplete_cholesky ||
        s.pressure_integration == MacPressureIntegration::discrete_gradient_midpoint) &&
        (s.reconstruction == MacMaterialReconstruction::donor_cell || s.reconstruction == MacMaterialReconstruction::limited_linear ||
        s.reconstruction == MacMaterialReconstruction::interface_plic) &&
        (s.momentum_reconstruction == MacMomentumReconstruction::donor_cell || s.momentum_reconstruction == MacMomentumReconstruction::limited_linear) &&
        s.compression_viscosity != MacCompressionViscosity::coupled_pressure && !mac_kernel::sharp_gravity_moments(s) &&
        s.compression_viscosity != MacCompressionViscosity::monotone_acoustic_quadratic &&
        s.shock_heat != MacShockHeatRegularization::monotone_pressure_sensed;
}
bool mac_kernel_normal_range(const MacTransportState& state) {
    for (const auto& cell : state.cells) if (!cell.energy_remainder_j.full.is_zero()) return false;
    // Below-range values (and full-range canonical momentum, whose binary64
    // projection rounds below 2^-1074) are admitted and counted; only
    // nonfinite or overflow-range values need the reference.
    const auto normal = [](double v) { return std::isfinite(v) && std::abs(v) <= 0x1p450; };
    for (const auto& cell : state.cells) {
        if (!normal(cell.mixture.energy)) return false;
        if (!cell.mixture.masses.implicit_zero())
            for (const double m : cell.mixture.masses) if (!normal(m)) return false;
        for (const auto& p : cell.passive_quantities) if (!normal(p.quantity)) return false;
    }
    for (const double j : state.momentum_x) if (!normal(j)) return false;
    for (const double j : state.momentum_y) if (!normal(j)) return false;
    return true;
}
MacKernelAdvance advance_mac_kernel(const SpeciesRegistry& registry, const FluidThermodynamics& eos, const MacTransportState& state,
    const MacClosedSettings& settings, double start_time, double seconds, std::size_t budget, const MacKernelOptions& options) {
    mac_kernel::failed_flow_attempts = 0;
    mac_kernel::require(settings.diffusion.mode == MacDiffusionMode::disabled,
        "The production kernel advances flow only; MacExecutionPolicy::production composes gas diffusion");
    MacKernelAdvance result{state, {}};
    if (settings.step_control == MacStepControl::regional_predictive)
        result.report = mac_kernel::run_regional(registry,eos,state,settings,start_time,seconds,budget,options,result.state);
    else result.report = mac_kernel::run_interval(registry, eos, state, settings, start_time, seconds, budget, options, result.state);
    return result;
}
const MacKernelReport& last_mac_production_report() { return mac_kernel::last_report(); }
MacClosedAdvance advance_mac_production(const MacClosedWorld& world, double seconds, std::size_t budget, const MacKernelOptions& options) {
    mac_kernel::require(MacKernelAccess::settings(world).diffusion.mode == MacDiffusionMode::disabled,
        "advance_mac_production advances flow only; MacExecutionPolicy::production composes gas diffusion");
    return advance_mac_production_flow(world, seconds, budget, options);
}
MacClosedAdvance advance_mac_production_flow(const MacClosedWorld& world, double seconds, std::size_t budget, const MacKernelOptions& options) {
    mac_kernel::failed_flow_attempts = 0;
    mac_kernel::last_report() = {};
    const auto revision = MacKernelAccess::revision(world);
    if (revision == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("MAC closed revision overflow");
    mac_kernel::require_supported(MacKernelAccess::settings(world));
    const auto fallback = [&](std::size_t consumed) {
        const bool regional = world.settings().step_control == MacStepControl::regional_predictive;
        if (MacKernelAccess::settings(world).subcell_void != MacSubcellVoidPolicy::exact_geometry || regional) {
            // Name the trigger (the reference path cannot take this owner).
            const auto& st = MacKernelAccess::state(world);
            std::string why = "an EOS range flag during the interval";
            const auto normal = [](double v) { return std::isfinite(v) && std::abs(v) <= 0x1p450; };
            for (std::size_t i = 0; i < st.cells.size() && consumed == 0; ++i) {
                const auto& cell = st.cells[i];
                if (!cell.energy_remainder_j.full.is_zero()) { why = "a full-range energy remainder in cell " + std::to_string(i); break; }
                bool bad = !normal(cell.mixture.energy);
                for (const double m : cell.mixture.masses) bad = bad || !normal(m);
                if (bad) { why = "an out-of-range energy or mass in cell " + std::to_string(i); break; }
            }
            if (consumed > 0) why += " after " + std::to_string(consumed) + " attempts";
            throw std::invalid_argument(std::string(regional ? "Regional stepping" : "Unresolved gas expansion") +
                " cannot continue through reference fallback (" + why + ")");
        }
        mac_kernel::require(MacKernelAccess::settings(world).pressure_integration == MacPressureIntegration::discrete_gradient_midpoint,
            "Two-moment pressure integration cannot continue through legacy reference fallback");
        const auto& grid = MacKernelAccess::state(world).grid;
        mac_kernel::require(grid.closed_x.empty() && grid.closed_y.empty(), "Closed faces cannot continue through reference fallback");
        // Flagged range: the complete interval through the reference, with
        // the attempts already consumed charged to the shared budget.
        if (consumed >= budget) throw std::runtime_error("Production interval exhausted its shared attempt budget");
        auto selected = world;
        MacKernelAccess::select_production(selected);
        auto r = MacKernelAccess::reference(selected, seconds, budget - consumed);
        r.report.interval_reference_fallbacks = 1;
        r.report.attempts += consumed; r.report.rejected_attempts += consumed;
        mac_kernel::last_report() = {}; mac_kernel::last_report().reference_fallback = true;
        return r;
    };
    if (!mac_kernel_normal_range(MacKernelAccess::state(world))) return fallback(0);
    MacClosedAdvance result{world, {}};
    MacTransportState endpoint = world.state();
    const auto start = MacKernelAccess::time(world);
    MacKernelReport k;
    try {
        if (world.settings().step_control == MacStepControl::regional_predictive)
            k = mac_kernel::run_regional(world.registry(),world.thermodynamics(),world.state(),world.settings(),start,seconds,budget,options,endpoint);
        else k = mac_kernel::run_interval(MacKernelAccess::registry(world), MacKernelAccess::eos(world), MacKernelAccess::state(world),
            MacKernelAccess::settings(world), start, seconds, budget, options, endpoint);
    } catch (const mac_kernel::RangeFlag& flag) { return fallback(flag.consumed_attempts); }
    MacKernelAccess::publish_state(result.world, std::move(endpoint));
    MacKernelAccess::stamp(result.world, start + seconds, revision + 1);
    auto& r = result.report;
    r.interval_kernel_intervals = 1;
    r.primary_energy.merge({k.rounded_energy_writes,k.discarded_energy_remainders});
    r.interval_audit_retries = k.audit_retries; r.interval_unresolved_updates = k.unresolved_updates;
    r.evaporated_kg += k.evaporated_kg; r.condensed_kg += k.condensed_kg;
    r.subcell_void_events = k.subcell_void_events; r.subcell_void_entropy_j_per_k = k.subcell_void_entropy_j_per_k;
    if (k.interface_conversions > 0) ++r.phase_change_stages;
    for (const auto count : {k.rounded_range_inputs, k.rounded_transfers, k.rounded_primary_writes, k.rounded_inventory_writes, k.rounded_energy_writes, k.discarded_energy_remainders}) {
        if (count > std::numeric_limits<std::size_t>::max() - r.interval_rounded_updates)
            mac_kernel::precision("Production owner rounding counter overflow");
        r.interval_rounded_updates += count;
    }
    for (const auto& audit : k.audits) r.interval_audit_exact_fallbacks += audit.escalated ? 1U : 0U;
    r.substeps = k.physical.accepted_substeps; r.attempts = k.attempts;
    r.rejected_attempts = k.rejected_attempts + (k.substeps - k.physical.accepted_substeps);
    r.interval_multigrid_solves = k.multigrid_solves; r.interval_multigrid_cycles = k.multigrid_cycles;
    r.interval_multigrid_fallbacks = k.multigrid_fallbacks; r.interval_multigrid_levels = k.multigrid_levels;
    r.interval_topology_builds = k.topology_builds;
    r.pressure_iterations = k.linear_iterations; r.nonlinear_iterations = k.newton_iterations;
    r.unresolved_heat_updates = k.unresolved_updates;
    r.compression_events = k.compression_events; r.shock_heat_events = k.shock_heat_events;
    const auto& physical = k.physical;
    r.conduction_stages = physical.conduction_stages; r.conduction_contacts = physical.conduction_contacts;
    r.conduction_iterations = physical.conduction_iterations;
    r.unresolved_conduction_updates = physical.unresolved_conduction_updates;
    r.conductive_redistribution_j = physical.conductive_redistribution_j;
    r.conductive_entropy_j_per_k = physical.conductive_entropy_j_per_k;
    r.viscosity_stages = physical.viscosity_stages; r.viscosity_modes = physical.viscosity_modes;
    r.unresolved_viscous_heat_updates = physical.unresolved_viscous_heat_updates;
    r.unresolved_viscous_momentum_updates = physical.unresolved_viscous_momentum_updates;
    r.viscous_heat = physical.viscous_heat; r.viscous_stored_heat = physical.viscous_stored_heat;
    r.viscous_wall_impulse_x = physical.viscous_wall_impulse_x; r.viscous_wall_impulse_y = physical.viscous_wall_impulse_y;
    r.viscous_entropy_lower_bound_j_per_k = physical.viscous_entropy_lower_bound_j_per_k;
    r.gravity_internal_change_wide_j = physical.gravity_internal_change_wide_j;
    const auto gravity_projection = physical.gravity_internal_change_wide_j.project();
    r.gravity_internal_change_j = gravity_projection.value;
    r.rounded_gravity_projections += gravity_projection.rounded ? 1U : 0U;
    r.energy_residual_j = k.energy_residual_j; r.energy_bound_j = k.energy_bound_j;
    r.energy_residual_wide_j = MacEnergy::from_double(k.energy_residual_j); r.energy_bound_wide_j = MacEnergy::from_double(k.energy_bound_j);
    r.mixing_heat_j = k.mixing_heat_j; r.wall_heat_j = k.wall_heat_j;
    r.mixing_heat_wide_j = MacEnergy::from_double(k.mixing_heat_j); r.wall_heat_wide_j = MacEnergy::from_double(k.wall_heat_j);
    r.compression_heat_j = k.compression_heat_j; r.compression_heat_wide_j = MacEnergy::from_double(k.compression_heat_j);
    r.absolute_shock_heat_transfer_j = k.shock_heat_transfer_j; r.absolute_shock_heat_transfer_wide_j = MacEnergy::from_double(k.shock_heat_transfer_j);
    r.wall_impulse_x = k.wall_impulse_x; r.wall_impulse_y = k.wall_impulse_y;
    r.full_wall_impulse_x = MacScaled::from_double(k.wall_impulse_x); r.full_wall_impulse_y = MacScaled::from_double(k.wall_impulse_y);
    r.body_impulse_y = k.body_impulse_y; r.full_body_impulse_y = MacScaled::from_double(k.body_impulse_y);
    r.maximum_geometric_residual_m3 = k.maximum_volume_closure * world.state().grid.volume_m3();
    r.entropy_change_j_per_k = k.entropy_change_j_per_k; r.entropy_roundoff_j_per_k = k.entropy_allowance_j_per_k;
    r.gravity_entropy_bound_j_per_k = k.gravity_entropy_bound_j_per_k;
    mac_kernel::last_report() = std::move(k);
    return result;
}
}
