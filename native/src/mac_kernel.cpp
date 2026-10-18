// Production substep kernel driver (MAC_KERNEL.md): owner-state conversion,
// the reference substep schedule (or calm stepping), retries, the interval
// audit and atomic publication through MacClosedWorld.
#include "mac_kernel_detail.hpp"
#include "mac_interval_audit.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

namespace ma::experimental {
struct MacKernelAccess {
    static const MacTransportState& state(const MacClosedWorld& w) { return w.state_; }
    static MacTransportState& state(MacClosedWorld& w) { return w.state_; }
    static const SpeciesRegistry& registry(const MacClosedWorld& w) { return w.registry_; }
    static const FluidThermodynamics& eos(const MacClosedWorld& w) { return *w.thermodynamics_; }
    static const MacClosedSettings& settings(const MacClosedWorld& w) { return w.settings_; }
    static double time(const MacClosedWorld& w) { return w.time_s_; }
    static std::uint64_t revision(const MacClosedWorld& w) { return w.revision_; }
    static void stamp(MacClosedWorld& w, double time, std::uint64_t revision) { w.time_s_ = time; w.revision_ = revision; }
    // The retained reference implementation for the same interval (range
    // flags): a copy that selects no fast path, published with the caller's
    // numerical-kernel setting.
    static MacClosedAdvance reference(const MacClosedWorld& w, double seconds, std::size_t budget) {
        auto copy = w; copy.settings_.numerical_kernel = MacNumericalKernel::reference;
        auto result = copy.advanced(seconds, budget, MacExecutionPolicy::reference);
        result.world.settings_.numerical_kernel = w.settings_.numerical_kernel;
        return result;
    }
};
}

namespace ma::experimental::mac_kernel {
namespace {
using Clock = std::chrono::steady_clock;
// A value left the normal binary64 range during the interval: the caller
// reruns the whole interval through the reference implementation.
struct RangeFlag { std::size_t consumed_attempts = 0; };
double since(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

void require_supported(const MacClosedSettings& s) {
    s.validate();
    require(s.reconstruction == MacMaterialReconstruction::donor_cell || s.reconstruction == MacMaterialReconstruction::limited_linear,
        "Production kernel supports material reconstruction selectors 0 and 1");
    require(s.momentum_reconstruction == MacMomentumReconstruction::donor_cell || s.momentum_reconstruction == MacMomentumReconstruction::limited_linear,
        "Production kernel supports momentum reconstruction selectors 0 and 1");
    require(s.compression_viscosity != MacCompressionViscosity::coupled_pressure,
        "Production kernel does not support coupled compressive pressure (compression selector 3)");
}
Model build_model(const SpeciesRegistry& registry, const FluidThermodynamics& eos, const MacTransportState& state) {
    eos.validate_registry(registry);
    Model m; m.registry_size = registry.size();
    std::vector<std::uint8_t> present(registry.size(), 0);
    std::set<std::uint64_t> ids;
    for (const auto& cell : state.cells) {
        require(cell.mixture.masses.size() == registry.size(), "Production kernel inventory shape differs");
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
        s.conductivity = def.conductivity;
        (s.gas ? m.gases : m.liquids).push_back(m.species.size());
        m.species.push_back(s);
    }
    m.passive_ids.assign(ids.begin(), ids.end());
    return m;
}
std::size_t load(const Model& model, const Grid& g, const MacTransportState& state, Fields& f) {
    const auto N = g.N, K = model.K(), P = model.P();
    f.m.assign(K * N, 0); f.U.assign(N, 0); f.q.assign(P * N, 0);
    for (std::size_t i = 0; i < N; ++i) {
        const auto& cell = state.cells[i];
        for (std::size_t k = 0; k < K; ++k) f.m[k * N + i] = cell.mixture.masses[model.species[k].registry];
        f.U[i] = cell.mixture.energy;
        std::size_t p = 0;
        for (const auto& entry : cell.passive_quantities) {
            while (model.passive_ids[p] != entry.identity) ++p;
            f.q[p * N + i] = entry.quantity;
        }
    }
    require(state.momentum_x.size() == g.NX && state.momentum_y.size() == g.NY, "Production kernel momentum shape differs");
    f.jx = state.momentum_x; f.jy = state.momentum_y;
    std::size_t rounded = 0;
    for (const auto* full : {&state.full_momentum_x, &state.full_momentum_y})
        for (const auto& v : *full) if (v.project().rounded) ++rounded;
    return rounded;
}
void store(const Model& model, const Grid& g, const Fields& f, MacTransportState& out) {
    const auto N = g.N, K = model.K(), P = model.P();
    for (std::size_t i = 0; i < N; ++i) {
        auto& cell = out.cells[i];
        for (std::size_t k = 0; k < K; ++k) cell.mixture.masses[model.species[k].registry] = f.m[k * N + i];
        cell.mixture.energy = f.U[i];
        cell.passive_quantities.clear();
        for (std::size_t p = 0; p < P; ++p) {
            const double v = f.q[p * N + i];
            if (v != 0) cell.passive_quantities.push_back({model.passive_ids[p], v});
        }
    }
    out.momentum_x = f.jx; out.momentum_y = f.jy;
    out.full_momentum_x.clear(); out.full_momentum_y.clear();
}
// Interval totals per audited quantity, from deterministic per-block
// partials: double-double signed sum, double-double absolute operand sum,
// operand count and the normal-range flag of the shared interval audit
// (mac_interval_audit.hpp).
struct Quantity {
    interval_detail::Sum sum, scale;
    std::size_t count = 0;
    bool ordinary = true;
    void add(double v) {
        sum.add(v); scale.add(std::abs(v)); ++count;
        if (!std::isfinite(v) || (v != 0 && (std::abs(v) < 0x1p-450 || std::abs(v) > 0x1p450))) ordinary = false;
    }
    void merge(const Quantity& o) {
        sum.add(o.sum.hi); sum.add(o.sum.lo); scale.add(o.scale.hi); scale.add(o.scale.lo);
        count += o.count + 4; ordinary = ordinary && o.ordinary;
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
void entropy(Context& c, const Fields& f, DD& total, double& allowance) {
    const auto& g = c.g; const auto N = g.N; const auto& model = c.model;
    constexpr double R = experimental::reference_gas_constant;
    struct Part { DD s; double allowance = 0; };
    auto parts = c.exec.partials<Part>([&](std::size_t y0, std::size_t y1, std::size_t) {
        Part o;
        for (std::size_t i = y0 * g.W; i < y1 * g.W; ++i) {
            if (g.wall[i]) continue;
            const auto st0 = cell_stats(model, f, N, i);
            if (st0.kind == empty) continue;
            State st;
            if (!evaluate_state(model, f, N, i, st0, f.U[i], g.V, st)) reject("Production interval endpoint left its EOS domain");
            for (std::size_t k = 0; k < model.K(); ++k) {
                const double m = f.m[k * N + i]; if (m == 0) continue;
                const auto& sp = model.species[k];
                if (!(st.T >= sp.tmin * (1 - 32 * eps) && st.T <= sp.tmax * (1 + 32 * eps)))
                    reject("Production interval endpoint temperature outside its material range");
                o.s.add(m * sp.cv * std::log(st.T / experimental::entropy_reference_temperature_k));
                if (sp.gas) { const double n = m / sp.molar_mass; o.s.add(n * R * (std::log(st.Vg) - std::log(n))); }
            }
            o.allowance += std::abs(f.U[i]) / st.T;
        }
        return o;
    });
    for (const auto& p : parts) { total.add(p.s); allowance += p.allowance; }
}
void substep(Context& c, Fields& f, double dt, Accounts& acc) {
    const bool timed = c.options.collect_stage_timing;
    const auto run = [&](auto&& stage, double seconds, double& slot) {
        if (!timed) { stage(c, f, seconds, acc); return; }
        const auto t = Clock::now(); stage(c, f, seconds, acc); slot += since(t);
    };
    auto& tm = c.timing;
    run(stage_viscosity, .5 * dt, tm.viscosity_ms);
    run(stage_conduction, .5 * dt, tm.conduction_ms);
    run(stage_shock_heat, .5 * dt, tm.shock_heat_ms);
    run(stage_compression, .5 * dt, tm.compression_ms);
    { double unused = 0; run(stage_pressure_transport, dt, unused); }
    run(stage_compression, .5 * dt, tm.compression_ms);
    run(stage_shock_heat, .5 * dt, tm.shock_heat_ms);
    run(stage_conduction, .5 * dt, tm.conduction_ms);
    run(stage_viscosity, .5 * dt, tm.viscosity_ms);
}
void merge(Accounts& a, const Accounts& b) {
    for (std::size_t q = 0; q < a.operands.size(); ++q) { a.operands[q] += b.operands[q]; a.rounding[q] += b.rounding[q]; }
    a.wall_x += b.wall_x; a.wall_y += b.wall_y; a.body_y += b.body_y;
    a.mixing_heat += b.mixing_heat; a.wall_heat += b.wall_heat; a.compression_heat += b.compression_heat; a.shock_heat += b.shock_heat;
    a.unresolved += b.unresolved;
}
std::string quantity_name(const Context& c, std::size_t q) {
    if (q < c.model.K()) return "mass:" + std::to_string(c.model.species[q].registry);
    if (q < c.model.K() + c.model.P()) return "passive:" + std::to_string(c.model.passive_ids[q - c.model.K()]);
    if (q == c.q_jx()) return "momentum_x";
    if (q == c.q_jy()) return "momentum_y";
    return "energy_u_k_g";
}
struct Workspaces { Workspace ws; Fields start, current, trial; };
Workspaces& workspaces() { static thread_local Workspaces w; return w; }

// Runs one interval; writes the endpoint into out (pre-filled with a copy of
// the source state, so per-cell vectors are reused).
MacKernelReport run_interval(const SpeciesRegistry& registry, const FluidThermodynamics& eos, const MacTransportState& source,
    const MacClosedSettings& settings, double start_time, double seconds, std::size_t budget, const MacKernelOptions& options,
    MacTransportState& out) {
    require_mac_energy_environment();
    options.validate(); require_supported(settings);
    require(std::isfinite(seconds) && seconds > 0 && seconds <= 10 && budget > 0 && budget <= 65536,
        "Invalid production interval or attempt budget");
    require(settings.gravity_m_per_s2 == 0 || !source.grid.periodic_y, "Constant MAC gravity requires nonperiodic vertical boundaries");
    const auto conversion_start = Clock::now();
    const Grid g(source.grid);
    require(source.cells.size() == g.N, "Production kernel cell shape differs");
    const Model model = build_model(registry, eos, source);
    auto& w = workspaces();
    MacKernelReport report;
    report.rounded_range_inputs = load(model, g, source, w.start);
    Context c{w.ws, model, g, settings, options, Exec{g, pool_for(options.threads)}, {}, {}};
    for (std::size_t i = 0; i < g.N; ++i) {
        if (g.wall[i]) {
            for (std::size_t k = 0; k < model.K(); ++k) require(w.start.m[k * g.N + i] == 0, "Production kernel solid contains fluid");
            require(w.start.U[i] == 0, "Production kernel solid contains energy");
        }
    }
    report.timing.conversion_ms += since(conversion_start);
    const double endpoint = start_time + seconds;
    if (!std::isfinite(endpoint) || endpoint <= start_time) throw FluidFatalError(FluidFatalCode::numerical_precision, "Production clock cannot advance");
    const auto audit_start = Clock::now();
    const auto t0 = totals(c, w.start, -1);
    for (const auto& q : t0) if (!q.ordinary) throw RangeFlag{0};
    DD s0; double allowance0 = 0; entropy(c, w.start, s0, allowance0);
    report.timing.audit_ms += since(audit_start);
    double maximum_dt = settings.maximum_timestep_s;
    for (std::size_t audit_attempt = 0;; ++audit_attempt) {
        w.current = w.start;
        Accounts acc; acc.resize(c.quantities());
        double time = start_time, next_dt = maximum_dt;
        report.minimum_timestep_s = std::numeric_limits<double>::infinity(); report.maximum_timestep_s = 0;
        while (time < endpoint) {
            double dt = std::min(next_dt, endpoint - time);
            bool accepted = false;
            while (!accepted) {
                if (report.attempts == budget) throw std::runtime_error("Production interval exhausted its shared attempt budget");
                ++report.attempts;
                if (!(dt > 0) || time + dt <= time) throw FluidFatalError(FluidFatalCode::numerical_precision, "Production retry time cannot be represented");
                w.trial = w.current;
                w.ws.stats_valid = false; // trial masses come from the accepted state
                Accounts stage; stage.resize(c.quantities());
                try {
                    substep(c, w.trial, dt, stage);
                    std::swap(w.current, w.trial);
                    merge(acc, stage);
                    time += dt; ++report.substeps; report.cell_substeps += g.N;
                    report.minimum_timestep_s = std::min(report.minimum_timestep_s, dt);
                    report.maximum_timestep_s = std::max(report.maximum_timestep_s, dt);
                    next_dt = std::min(maximum_dt, 2 * dt); accepted = true;
                } catch (const std::domain_error& error) {
                    if (std::getenv("MA_KERNEL_DEBUG")) std::fprintf(stderr, "reject t=%.17g dt=%.17g: %s\n", time, dt, error.what());
                    ++report.rejected_attempts; dt *= .5;
                }
            }
        }
        // ---- interval audit
        const auto audit_time = Clock::now();
        const auto t1 = totals(c, w.current, 1);
        for (const auto& q : t1) if (!q.ordinary) throw RangeFlag{report.attempts};
        bool passed = true;
        report.audits.clear();
        for (std::size_t q = 0; q < c.quantities(); ++q) {
            double external = 0;
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
            const double residual = total.sum.value(), scale = total.scale.value(), bound = 128 * e * scale;
            const double error = 64 * e * e * (static_cast<double>(total.count) + 1) * scale + 16 * e * (std::abs(residual) + bound);
            const double margin = bound - std::abs(residual);
            MacKernelAudit a; a.quantity = quantity_name(c, q); a.residual = residual; a.bound = bound; a.rounding_allowance = acc.rounding[q];
            if (total.ordinary && std::isfinite(scale) && std::isfinite(residual) && total.count < (std::size_t{1} << 26) &&
                (margin > error || margin < -error)) a.passed = margin > error;
            else {
                a.escalated = true;
                MacEnergyAccount exact; exact_replay(c, q, w.start, w.current, external, acc.operands[q], exact);
                a.passed = exact.within_bound();
            }
            passed = passed && a.passed;
            if (q == c.q_energy()) { report.energy_residual_j = residual; report.energy_bound_j = bound; }
            report.audits.push_back(std::move(a));
        }
        DD s1; double allowance1 = 0; entropy(c, w.current, s1, allowance1);
        DD change = s1; change.add(-s0.hi); change.add(-s0.lo);
        report.entropy_change_j_per_k = change.value();
        report.entropy_allowance_j_per_k = 128 * eps * (allowance0 + allowance1);
        const bool entropy_ok = report.entropy_change_j_per_k >= -report.entropy_allowance_j_per_k;
        report.timing.audit_ms += since(audit_time);
        if (passed && entropy_ok) {
            report.mixing_heat_j = acc.mixing_heat; report.wall_heat_j = acc.wall_heat;
            report.compression_heat_j = acc.compression_heat; report.shock_heat_transfer_j = acc.shock_heat;
            report.wall_impulse_x = acc.wall_x; report.wall_impulse_y = acc.wall_y; report.body_impulse_y = acc.body_y;
            report.unresolved_updates = acc.unresolved;
            break;
        }
        // A failed audit is never published: retry the interval smaller.
        ++report.audit_retries; maximum_dt *= .5;
        if (report.attempts >= budget || audit_attempt >= 8)
            throw FluidFatalError(FluidFatalCode::numerical_precision, passed ? "Production interval entropy audit failed" : "Production interval conservation audit failed");
    }
    const auto store_time = Clock::now();
    store(model, g, w.current, out);
    report.timing.conversion_ms += since(store_time);
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
std::mutex& defaults_mutex() { static std::mutex m; return m; }
MacKernelOptions& defaults() { static MacKernelOptions o; return o; }
MacKernelReport& last_report() { static thread_local MacKernelReport r; return r; }
}
}

namespace ma::experimental {
void MacKernelOptions::validate() const {
    mac_kernel::require(threads >= 1 && threads <= 256, "Production kernel thread count must be 1..256");
    mac_kernel::require(preconditioner == MacKernelPreconditioner::jacobi || preconditioner == MacKernelPreconditioner::multigrid,
        "Invalid production preconditioner");
}
bool mac_kernel_supports(const MacClosedSettings& s) {
    return (s.reconstruction == MacMaterialReconstruction::donor_cell || s.reconstruction == MacMaterialReconstruction::limited_linear) &&
        (s.momentum_reconstruction == MacMomentumReconstruction::donor_cell || s.momentum_reconstruction == MacMomentumReconstruction::limited_linear) &&
        s.compression_viscosity != MacCompressionViscosity::coupled_pressure;
}
bool mac_kernel_normal_range(const MacTransportState& state) {
    if (!state.full_momentum_x.empty() || !state.full_momentum_y.empty()) return false;
    const auto normal = [](double v) { return v == 0 || (std::isfinite(v) && std::abs(v) >= 0x1p-450 && std::abs(v) <= 0x1p450); };
    for (const auto& cell : state.cells) {
        if (!normal(cell.mixture.energy)) return false;
        for (const double m : cell.mixture.masses) if (!normal(m)) return false;
        for (const auto& p : cell.passive_quantities) if (!normal(p.quantity)) return false;
    }
    for (const double j : state.momentum_x) if (!normal(j)) return false;
    for (const double j : state.momentum_y) if (!normal(j)) return false;
    return true;
}
MacKernelOptions mac_kernel_default_options() { std::lock_guard lock(mac_kernel::defaults_mutex()); return mac_kernel::defaults(); }
void set_mac_kernel_default_options(const MacKernelOptions& options) {
    options.validate(); std::lock_guard lock(mac_kernel::defaults_mutex()); mac_kernel::defaults() = options;
}
MacKernelAdvance advance_mac_kernel(const SpeciesRegistry& registry, const FluidThermodynamics& eos, const MacTransportState& state,
    const MacClosedSettings& settings, double start_time, double seconds, std::size_t budget, const MacKernelOptions& options) {
    MacKernelAdvance result{state, {}};
    result.report = mac_kernel::run_interval(registry, eos, state, settings, start_time, seconds, budget, options, result.state);
    return result;
}
const MacKernelReport& last_mac_production_report() { return mac_kernel::last_report(); }
MacClosedAdvance advance_mac_production(const MacClosedWorld& world, double seconds, std::size_t budget, const MacKernelOptions& options) {
    mac_kernel::last_report() = {};
    const auto revision = MacKernelAccess::revision(world);
    if (revision == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("MAC closed revision overflow");
    mac_kernel::require_supported(MacKernelAccess::settings(world));
    const auto fallback = [&](std::size_t consumed) {
        // Flagged range: the complete interval through the reference, with
        // the attempts already consumed charged to the shared budget.
        if (consumed >= budget) throw std::runtime_error("Production interval exhausted its shared attempt budget");
        auto r = MacKernelAccess::reference(world, seconds, budget - consumed);
        r.report.attempts += consumed; r.report.rejected_attempts += consumed;
        mac_kernel::last_report() = {}; mac_kernel::last_report().reference_fallback = true;
        return r;
    };
    if (!mac_kernel_normal_range(MacKernelAccess::state(world))) return fallback(0);
    MacClosedAdvance result{world, {}};
    const auto start = MacKernelAccess::time(world);
    MacKernelReport k;
    try {
        k = mac_kernel::run_interval(MacKernelAccess::registry(world), MacKernelAccess::eos(world), MacKernelAccess::state(world),
            MacKernelAccess::settings(world), start, seconds, budget, options, MacKernelAccess::state(result.world));
    } catch (const mac_kernel::RangeFlag& flag) { return fallback(flag.consumed_attempts); }
    MacKernelAccess::stamp(result.world, start + seconds, revision + 1);
    auto& r = result.report;
    r.substeps = k.substeps; r.attempts = k.attempts; r.rejected_attempts = k.rejected_attempts;
    r.pressure_iterations = k.linear_iterations; r.nonlinear_iterations = k.newton_iterations;
    r.unresolved_heat_updates = k.unresolved_updates;
    r.compression_events = k.compression_events; r.shock_heat_events = k.shock_heat_events;
    r.conduction_stages = k.conduction_stages; r.viscosity_modes = k.viscosity_modes;
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
    mac_kernel::last_report() = std::move(k);
    return result;
}
}
