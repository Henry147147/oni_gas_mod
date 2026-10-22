#pragma once
// Included in the driver's private namespace after run_interval. Component
// schedules are independent; their committed receipts feed one full-grid audit.
template<class T, class... Members>
void regional_add(T& target, const T& source, Members... member) {
    ((target.*member += source.*member), ...);
}
struct RegionalAudit {
    MacKernelAudit value;
    DD operands, rounding, tiny;
    MacEnergySum transfer_error, transfer_bound;
    void add(const MacKernelAudit& a) {
        value.quantity = a.quantity;
        operands.add(a.stage_operand_magnitude); rounding.add(a.rounding_allowance); tiny.add(a.below_range_allowance);
        regional_add(value,a,&MacKernelAudit::rounded_primary_writes,&MacKernelAudit::rounded_inventory_writes,
            &MacKernelAudit::rounded_energy_writes,&MacKernelAudit::discarded_energy_remainders,&MacKernelAudit::rounded_transfers);
        value.escalated = value.escalated || a.escalated;
        transfer_error.add(a.transfer_roundoff); transfer_bound.add(a.transfer_roundoff_bound);
    }
};
void regional_physical(PhysicalAccounts& target, const MacKernelPhysicalReport& p) {
    target.accepted_substeps += p.accepted_substeps;
    target.conduction_stages += p.conduction_stages; target.conduction_contacts += p.conduction_contacts;
    target.conduction_iterations += p.conduction_iterations; target.unresolved_conduction_updates += p.unresolved_conduction_updates;
    target.viscosity_stages += p.viscosity_stages; target.viscosity_modes += p.viscosity_modes;
    target.unresolved_viscous_heat_updates += p.unresolved_viscous_heat_updates;
    target.unresolved_viscous_momentum_updates += p.unresolved_viscous_momentum_updates;
    target.conductive_redistribution.add(p.conductive_redistribution_j); target.conductive_entropy.add(p.conductive_entropy_j_per_k);
    target.viscous_heat.add(p.viscous_heat); target.viscous_stored_heat.add(p.viscous_stored_heat);
    target.viscous_wall_x.add(p.viscous_wall_impulse_x); target.viscous_wall_y.add(p.viscous_wall_impulse_y);
    target.gravity_internal.add(p.gravity_internal_change_wide_j);
    target.viscous_entropy.add(MacEnergy::from_double(p.viscous_entropy_lower_bound_j_per_k));
}
void regional_report(MacKernelReport& total, const MacKernelReport& p) {
    regional_add(total,p,&MacKernelReport::substeps,&MacKernelReport::rejected_attempts,&MacKernelReport::audit_retries,
        &MacKernelReport::multigrid_solves,&MacKernelReport::multigrid_cycles,&MacKernelReport::multigrid_fallbacks,
        &MacKernelReport::topology_builds,&MacKernelReport::incomplete_cholesky_solves,&MacKernelReport::incomplete_cholesky_fallbacks,
        &MacKernelReport::newton_iterations,&MacKernelReport::linear_iterations,&MacKernelReport::line_search_backtracks,
        &MacKernelReport::eos_evaluations,&MacKernelReport::isentropic_evaluations,&MacKernelReport::reconstruction_backtracks,
        &MacKernelReport::limited_momentum_corrections,&MacKernelReport::compression_events,&MacKernelReport::shock_heat_events,
        &MacKernelReport::conduction_stages,&MacKernelReport::viscosity_modes,&MacKernelReport::rounded_range_inputs,
        &MacKernelReport::rounded_transfers,&MacKernelReport::rounded_primary_writes,&MacKernelReport::rounded_inventory_writes,
        &MacKernelReport::rounded_energy_writes,&MacKernelReport::discarded_energy_remainders,&MacKernelReport::unresolved_updates,
        &MacKernelReport::cell_substeps,&MacKernelReport::interface_conversions,&MacKernelReport::subcell_void_events,
        &MacKernelReport::caloric_resolution_cells,&MacKernelReport::unresolved_conduction_cells);
    total.multigrid_levels = std::max(total.multigrid_levels,p.multigrid_levels);
    total.maximum_linear_components = std::max(total.maximum_linear_components,p.maximum_linear_components);
    total.maximum_volume_closure = std::max(total.maximum_volume_closure,p.maximum_volume_closure);
    total.minimum_timestep_s = std::min(total.minimum_timestep_s,p.minimum_timestep_s);
    total.maximum_timestep_s = std::max(total.maximum_timestep_s,p.maximum_timestep_s);
    total.workspace_bytes = std::max(total.workspace_bytes,p.workspace_bytes);
    total.threads_used = std::max(total.threads_used,p.threads_used);
    regional_add(total.timing,p.timing,&MacKernelStageTiming::viscosity_ms,&MacKernelStageTiming::conduction_ms,
        &MacKernelStageTiming::shock_heat_ms,&MacKernelStageTiming::compression_ms,&MacKernelStageTiming::eos_ms,
        &MacKernelStageTiming::pressure_ms,&MacKernelStageTiming::reconstruction_ms,&MacKernelStageTiming::transport_ms,
        &MacKernelStageTiming::momentum_ms,&MacKernelStageTiming::gravity_ms,&MacKernelStageTiming::audit_ms,
        &MacKernelStageTiming::conversion_ms);
}
void audit_regions(const SpeciesRegistry& registry, const FluidThermodynamics& eos, const MacTransportState& source,
    const MacTransportState& endpoint, const MacClosedSettings& settings, const MacKernelOptions& options,
    const Grid& g, const Model& model, const std::vector<std::uint8_t>& rest,
    std::map<std::string,RegionalAudit>& receipts, MacKernelReport& report) {
    auto& w = workspaces();
    Context c{w.ws,source.grid,model,g,settings,options,Exec{g,pool_for(threads_for(g,options))},{},{}};
    load(c.exec,model,g,source,w.start); load(c.exec,model,g,endpoint,w.current);
    const auto before = totals(c,w.start,-1), after = totals(c,w.current,1);
    for (std::size_t q = 0; q < c.quantities(); ++q) {
        auto& receipt = receipts[quantity_name(c,q)]; auto a = receipt.value; a.quantity = quantity_name(c,q);
        const double external = q == c.q_jx() ? report.wall_impulse_x :
            q == c.q_jy() ? report.wall_impulse_y + report.body_impulse_y : 0;
        const double stage = receipt.operands.value();
        Quantity total = before[q]; total.merge(after[q]); total.add(-external); total.add(stage); total.add(-stage);
        // Count the same endpoint/reduction operations plus each component's
        // already counted below-range arithmetic; no global minimum dt charge.
        const double tiny = receipt.tiny.value() + 0x1p-1070 * static_cast<double>(total.count);
        const double residual = total.sum.value(), scale = total.scale.value(), bound = 128 * eps * scale + tiny;
        const double error = 64 * eps * eps * (static_cast<double>(total.count) + 1) * scale +
            16 * eps * (std::abs(residual) + bound);
        const double margin = bound - std::abs(residual);
        a.residual = residual; a.bound = bound; a.below_range_allowance = tiny;
        a.stage_operand_magnitude = stage; a.rounding_allowance = receipt.rounding.value();
        if (total.ordinary && std::isfinite(scale) && std::isfinite(residual) && total.count < (std::size_t{1} << 26) &&
            (margin > error || margin < -error)) a.passed = margin > error;
        else {
            a.escalated = true; MacEnergyAccount exact;
            exact_replay(c,q,w.start,w.current,external,stage + std::ldexp(tiny,44),exact);
            a.passed = exact.within_bound();
        }
        a.transfer_roundoff = receipt.transfer_error.value(); a.transfer_roundoff_bound = receipt.transfer_bound.value();
        MacEnergySum transfer; transfer.add(a.transfer_roundoff_bound); transfer.add(a.transfer_roundoff.magnitude().negated());
        a.passed = a.passed && transfer.sign() >= 0 && a.rounding_allowance <= a.bound;
        if (!a.passed) precision("Regional interval conservation audit failed");
        if (q == c.q_energy()) { report.energy_residual_j = residual; report.energy_bound_j = bound; }
        report.audits.push_back(std::move(a));
    }
    DD s0,s1; double allowance0 = 0, allowance1 = 0, rest_operand = 0;
    entropy(c,w.start,s0,allowance0,&rest,&rest_operand); entropy(c,w.current,s1,allowance1);
    s1.add(-s0.hi); s1.add(-s0.lo); report.entropy_change_j_per_k = s1.value();
    // Resting endpoints are identical, so count their original operands twice.
    report.entropy_allowance_j_per_k += 256 * eps * rest_operand;
    s1.add(-report.gravity_entropy_bound_j_per_k);
    if (!(s1.value() >= -report.entropy_allowance_j_per_k)) precision("Regional interval entropy audit failed");
    // This is a conservative bound on active regional workspaces plus the
    // main thread's full-grid audit buffers (not a sum over every region).
    report.workspace_bytes = report.workspace_bytes * report.threads_used + retained_bytes(w);
    (void)registry; (void)eos;
}
MacKernelReport run_regional(const SpeciesRegistry& registry, const FluidThermodynamics& eos, const MacTransportState& source,
    const MacClosedSettings& settings, double start_time, double seconds, std::size_t budget, const MacKernelOptions& options,
    MacTransportState& out) {
    require_mac_energy_environment(); options.validate(); require_supported(settings);
    require(std::isfinite(seconds) && seconds > 0 && seconds <= 10 && budget > 0 && budget <= 65536,
        "Invalid regional interval or attempt budget");
    const double endpoint_time = start_time + seconds;
    require(std::isfinite(endpoint_time) && endpoint_time > start_time,"Regional clock cannot advance");
    const auto started = Clock::now(); const Grid g(source.grid);
    require(!g.px && !g.py,"Regional stepping requires nonperiodic boundaries");
    require(source.cells.size() == g.N,"Regional cell shape differs");
    Model model = build_model(registry,eos,source);
    model.recover_mixed_eos_endpoint = settings.recover_mixed_eos_endpoint;
    std::vector<std::size_t> labels; auto parts = closed_regions(source,labels);
    const double geometry_ms = since(started);
    SharedAttempts shared{budget};
    MacKernelReport report; report.minimum_timestep_s = std::numeric_limits<double>::infinity();
    std::vector<std::optional<MacKernelAdvance>> solved(parts.size()); std::vector<std::exception_ptr> errors(parts.size());
    std::vector<std::size_t> small;
    std::vector<double> resting_body(parts.size(),0);
    std::vector<std::uint8_t> rest(g.N,0);
    const auto solve = [&](std::size_t n, MacKernelOptions child_options) {
        try {
            auto crop = crop_region(registry,source,parts[n]);
            // The local input is disposable: run_interval reads it completely
            // before publishing its endpoint, so the crop can be its own output.
            auto k = run_interval(registry,eos,crop,settings,start_time,seconds,budget,child_options,crop,&shared);
            solved[n].emplace(MacKernelAdvance{std::move(crop),std::move(k)});
        } catch (...) { errors[n] = std::current_exception(); }
    };
    for (std::size_t n = 0; n < parts.size(); ++n) {
        if (!parts[n].material) { ++report.empty_regions; continue; }
        if (resting_singleton(registry,source,settings,parts[n])) {
            rest[parts[n].cells.front()] = 1;
            resting_body[n] = -settings.gravity_m_per_s2 * (endpoint_time-start_time) * source.cells[parts[n].cells.front()].mixture.mass();
            continue;
        }
        ++report.independent_regions;
        if (parts[n].cells.size() >= 2048 && parts[n].width * parts[n].height >= 4096) solve(n,options);
        else small.push_back(n);
    }
    report.threads_used = static_cast<unsigned>(std::max<std::size_t>(1,std::min<std::size_t>(options.threads,small.size())));
    std::atomic<std::size_t> next{}; auto child_options = options; child_options.threads = 1;
    if (!small.empty()) pool_for(report.threads_used).run(report.threads_used,[&](std::size_t) {
        for (;;) { const auto n = next.fetch_add(1,std::memory_order_relaxed); if (n >= small.size()) return; solve(small[n],child_options); }
    });
    report.attempts = shared.used.load(std::memory_order_relaxed);
    for (const auto& error : errors) if (error) {
        if (report.attempts == budget) throw std::runtime_error("Production interval exhausted its shared attempt budget");
        try { std::rethrow_exception(error); }
        catch (const RangeFlag&) { failed_flow_attempts = report.attempts; throw RangeFlag{report.attempts}; }
        catch (const FluidFatalError& failed) {
            if (failed.code() == FluidFatalCode::numerical_precision) failed_flow_attempts = report.attempts;
            throw;
        }
    }
    if (options.collect_stage_timing) report.timing.conversion_ms += geometry_ms;
    const auto assemble_start = Clock::now();
    out.occupied_volumes_m3.resize(g.N);
    for (std::size_t i = 0; i < g.N; ++i)
        out.occupied_volumes_m3[i] = source.occupied_volumes_m3.empty() ? (source.cells[i].mixture.mass() > 0 ? g.V : 0) : source.occupied_volumes_m3[i];
    std::map<std::string,RegionalAudit> receipts; PhysicalAccounts physical;
    DD wall_x,wall_y,body_y,entropy_allowance,gravity_entropy,caloric;
    for (std::size_t n = 0; n < parts.size(); ++n) {
        if (!solved[n]) { body_y.add(resting_body[n]); wall_y.add(-resting_body[n]); continue; }
        const auto& k = solved[n]->report;
        regional_report(report,k); regional_physical(physical,k.physical);
        wall_x.add(k.wall_impulse_x); wall_y.add(k.wall_impulse_y); body_y.add(k.body_impulse_y);
        entropy_allowance.add(k.entropy_allowance_j_per_k); gravity_entropy.add(k.gravity_entropy_bound_j_per_k);
        caloric.add(k.caloric_redistribution_j);
        regional_add(report,k,&MacKernelReport::mixing_heat_j,&MacKernelReport::wall_heat_j,&MacKernelReport::compression_heat_j,
            &MacKernelReport::shock_heat_transfer_j,&MacKernelReport::evaporated_kg,&MacKernelReport::condensed_kg,
            &MacKernelReport::subcell_void_entropy_j_per_k);
        for (const auto& a : k.audits) receipts[a.quantity].add(a);
        stitch_region(out,solved[n]->state,parts[n]);
    }
    report.wall_impulse_x = wall_x.value(); report.wall_impulse_y = wall_y.value(); report.body_impulse_y = body_y.value();
    report.entropy_allowance_j_per_k = entropy_allowance.value(); report.gravity_entropy_bound_j_per_k = gravity_entropy.value();
    report.caloric_redistribution_j = caloric.value(); report.physical = physical.report();
    bool partial = false;
    for (std::size_t i = 0; i < g.N; ++i) partial = partial || (out.cells[i].mixture.mass() > 0 && out.occupied_volumes_m3[i] < g.V);
    if (!partial) out.occupied_volumes_m3.clear();
    for (auto* full : {&out.full_momentum_x,&out.full_momentum_y})
        if (std::all_of(full->begin(),full->end(),[](const auto& v) { return v.is_zero(); })) full->clear();
    if (report.minimum_timestep_s == std::numeric_limits<double>::infinity()) report.minimum_timestep_s = 0;
    if (options.collect_stage_timing) report.timing.conversion_ms += since(assemble_start);
    const auto audit_start = Clock::now();
    try { audit_regions(registry,eos,source,out,settings,options,g,model,rest,receipts,report); }
    catch (const EosRefinement&) { throw RangeFlag{report.attempts}; }
    catch (const ArithmeticRefinement&) { throw RangeFlag{report.attempts}; }
    if (options.collect_stage_timing) report.timing.audit_ms += since(audit_start);
    return report;
}
