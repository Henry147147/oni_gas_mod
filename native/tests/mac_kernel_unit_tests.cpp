// Production kernel contract controls (native/MAC_KERNEL.md): checkpoint
// continuation, atomic failure, results independent of the thread count,
// fail-closed admission of unported selectors, the production policy's
// equilibrium certificate and the interval audit report.
#include "mac_kernel_fixture.hpp"
#include "../src/mac_kernel_heat_entropy.hpp"
#include <cstdlib>
#include <iomanip>
#include <tuple>

using namespace ma::testing;
using namespace ma::testing::kernel;

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "PASS " : "FAIL ") << what << '\n';
    if (!ok) ++failures;
}
MacClosedWorld owner(const Scene& sc) { const auto& s = setup(); return MacClosedWorld(s.registry, s.eos, sc.state, sc.settings); }
MacClosedWorld production_run(MacClosedWorld w, std::size_t intervals, unsigned threads, double seconds = .1) {
    MacKernelOptions o; o.threads = threads;
    for (std::size_t n = 0; n < intervals; ++n) w = advance_mac_production(w, seconds, 1024, o).world;
    return w;
}
void conduction_entropy_control() {
    using ma::experimental::mac_kernel::heat_path_energy;
    using ma::experimental::mac_kernel::mixed_cell_entropy;
    bool endpoints = true;
    for (const auto &[before, after] : {std::pair{-1e18, 1.}, std::pair{1., -1e18},
                                     std::pair{1e18, 1.}, std::pair{1., 1e18}}) {
        endpoints = endpoints && heat_path_energy(before, after, 0) == before &&
            heat_path_energy(before, after, 1) == after;
    }
    check(endpoints, "conduction diagnostic preserves extreme heat endpoints");
    bool accurate = true;
    for (const auto &[before, after] : {std::pair{1., 1e6}, std::pair{1e6, 1.}}) {
        const auto temperature = [&](double x) { return heat_path_energy(before, after, x); };
        const double measured = mixed_cell_entropy(temperature, after - before, 1e-10L);
        const long double expected = std::log(static_cast<long double>(after) / before);
        accurate = accurate && std::abs(measured - expected) <= 1e-10L;
    }
    check(accurate, "conduction diagnostic resolves narrow heating and cooling layers");
    auto sc = mixed(12, 12);
    const auto& s = setup();
    for (std::size_t i = 0; i < sc.state.cells.size(); ++i) if (!sc.state.grid.walls[i])
        sc.state.cells[i].mixture = s.mixture(1, 100000, i % 12 < 6 ? 330 : 300, .2);
    const auto source = owner(sc);
    MacKernelOptions options;
    const auto measured = advance_mac_production(source, .1, 1024, options);
    const auto report = last_mac_production_report();
    options.collect_conduction_entropy = false;
    const auto omitted = advance_mac_production(source, .1, 1024, options);
    const auto quiet = last_mac_production_report();
    bool same = measured.world.checkpoint() == omitted.world.checkpoint() &&
        report.entropy_change_j_per_k == quiet.entropy_change_j_per_k &&
        report.entropy_allowance_j_per_k == quiet.entropy_allowance_j_per_k &&
        report.energy_residual_j == quiet.energy_residual_j && report.energy_bound_j == quiet.energy_bound_j &&
        report.audits.size() == quiet.audits.size();
    for (std::size_t i = 0; i < report.audits.size() && same; ++i) {
        const auto& a = report.audits[i]; const auto& b = quiet.audits[i];
        same = a.passed && b.passed && a.residual == b.residual && a.bound == b.bound &&
            a.rounding_allowance == b.rounding_allowance;
    }
    check(same && report.physical.conduction_contacts > 0 &&
        report.physical.conductive_entropy_j_per_k > 0 && quiet.physical.conductive_entropy_j_per_k == 0 &&
        !quiet.reference_fallback, "conduction diagnostic toggle preserves fields and interval audits");
}

void conduction_caloric_domain_control() {
    namespace k = ma::experimental::mac_kernel;
    // Ordinary public Stefan-profile water/steam operands from a failed heat
    // trial. The caloric sign is independently reviewed from mass*formation
    // coefficients; it does not depend on the cached rounded offset.
    k::Model model;
    k::Species water; water.registry=0; water.gas=false;
    water.cv=0x1.058p+12; water.offset=-0x1.05215b97fd828p+24;
    water.molar_mass=0x1.27298f9b68fcap-6; water.rho0=0x1.f28p+9;
    water.bulk=0x1.0642acp+31; water.maximum_ratio=2; water.tmin=1; water.tmax=1e6;
    k::Species steam=water; steam.registry=1; steam.gas=true;
    steam.cv=0x1.5e9e8a4effe84p+10; steam.offset=-0x1.aa9bfcd5a22c5p+23;
    steam.rho0=0; steam.bulk=0;
    model.species={water,steam}; model.liquids={0}; model.gases={1};
    k::Fields fields; fields.m={0x1.7bce04662ab54p-25,0x1.2f427650e3cd4p-22};
    const auto stats=k::cell_stats(model,fields,1,0);
    constexpr double energy=-0x1.2e4ecb9c111p+2, volume=0x1.003911233c732p-21;
    const auto outcome = [&](double trial, bool retry, const k::Stats& at) {
        try { (void)k::conduction_endpoint(model,fields,1,0,at,trial,volume,retry); return 0; }
        catch (const std::domain_error&) { return 1; }
        catch (const k::EosRefinement&) { return 2; }
    };
    const int rejected=outcome(energy,true,stats), legacy=outcome(energy,false,stats), uncertain=outcome(stats.off,true,stats);
    const double warm=stats.off+300*stats.Cv;
    const int admitted=outcome(warm,true,stats);
    auto stale=stats; stale.off=warm+1;
    const int protected_cache=outcome(warm,true,stale);
    check(rejected==1 && legacy==2 && uncertain==2 && admitted==0 && protected_cache==2,
        "conduction proves negative caloric trials and retains uncertain EOS refinement");
    std::cout<<std::setprecision(17)<<"CALORIC_TRIAL "<<energy<<' '<<volume<<' '<<stats.off<<' '
        <<stats.Cv<<' '<<rejected<<' '<<legacy<<' '<<uncertain<<' '<<admitted<<'\n';
    for (std::size_t j=0;j<model.K();++j)
        std::cout<<"CALORIC_SPECIES "<<j<<' '<<fields.m[j]<<' '<<model.species[j].offset<<' '<<model.species[j].cv<<'\n';
    std::cout<<"CALORIC_GUARD "<<warm<<' '<<stale.off<<' '<<protected_cache<<'\n';
}

void regional_control() {
    const auto& s = setup();
    MacTransportState state; state.grid = {64,40,1,1,false,false,std::vector<std::uint8_t>(2560,1)};
    state.cells.assign(2560,{MixtureInventory(s.registry.size()),{}});
    state.occupied_volumes_m3.assign(2560,0);
    const auto cell = [&](std::size_t x, std::size_t y, double pressure, double temperature, double water, double volume = 1.) {
        const auto i = x + 64*y; state.grid.walls[i] = 0;
        state.cells[i].mixture = s.mixture(volume,pressure,temperature,water); state.occupied_volumes_m3[i] = volume;
    };
    for (std::size_t y = 2; y < 10; ++y) for (std::size_t x = 2; x < 10; ++x)
        cell(x,y,x < 6 ? 101000 : 100000,x < 6 ? 320 : 300,0);
    for (std::size_t y = 12; y < 20; ++y) for (std::size_t x = 20; x < 28; ++x)
        cell(x,y,100000,x < 24 ? 310 : 300,.2);
    cell(40,30,100000,300,0); cell(50,30,100000,300,0,1e-5);
    for (std::size_t y = 32; y < 36; ++y) for (std::size_t x = 42; x < 46; ++x) state.grid.walls[x+64*y] = 0;
    state.momentum_x.assign(65*40,0); state.momentum_y.assign(64*41,0); reset_passives(s,state);
    MacClosedSettings settings; settings.numerical_kernel = MacNumericalKernel::production;
    settings.step_control = MacStepControl::regional_predictive; settings.vacuum = MacVacuumRepresentation::void_phase;
    settings.subcell_void = MacSubcellVoidPolicy::expand_unresolved_gas; settings.gas_expansion_volume_fraction = .001;
    settings.thermal_conduction = true; settings.gravity_m_per_s2 = 9.81; settings.maximum_timestep_s = 1./64;
    const MacClosedWorld source(s.registry,s.eos,state,settings);
    const auto before = fingerprint(source.state());
    MacKernelOptions options; options.threads = 1;
    const auto one = advance_mac_kernel(s.registry,*s.eos,source.state(),settings,0,1./32,256,options);
    bool same = true, audited = !one.report.audits.empty();
    for (const auto& audit : one.report.audits) audited = audited && audit.passed;
    for (const auto threads : {4U,12U}) {
        options.threads = threads;
        const auto many = advance_mac_kernel(s.registry,*s.eos,source.state(),settings,0,1./32,256,options);
        same = same && fingerprint(one.state) == fingerprint(many.state) && one.report.attempts == many.report.attempts;
    }
    const auto resting = std::size_t{40+64*30};
    check(same && audited && one.report.independent_regions == 3 && one.report.empty_regions == 1 &&
        one.state.cells[resting].mixture.masses == source.state().cells[resting].mixture.masses &&
        same_mac_cell_internal_energy(one.state.cells[resting],source.state().cells[resting]) &&
        mac_material_volume(one.state,50+64*30) == 1 && fingerprint(source.state()) == before,
        "regional threads, audits, exact rest and configured expansion");
    bool failed = false;
    try { static_cast<void>(advance_mac_kernel(s.registry,*s.eos,source.state(),settings,0,1./32,1,options)); }
    catch (const std::runtime_error&) { failed = true; }
    check(failed && fingerprint(source.state()) == before,"regional shared attempt budget fails atomically");
    options.threads = 4;
    const auto advanced = advance_mac_production(source,1./32,256,options);
    const auto image = advanced.world.checkpoint();
    const auto restored = MacClosedWorld::from_checkpoint(s.registry,s.eos,image);
    const auto cold = advance_mac_production(restored,1./32,256,options);
    const auto continued = advance_mac_production(advanced.world,1./32,256,options);
    const auto u32 = [](const std::vector<std::uint8_t>& data, std::size_t at) {
        std::uint32_t value = 0;
        for (unsigned byte = 0; byte < 4; ++byte) value |= static_cast<std::uint32_t>(data[at+byte]) << (8*byte);
        return value;
    };
    const auto seal = [](std::vector<std::uint8_t>& data) {
        std::uint64_t hash = UINT64_C(14695981039346656037);
        for (std::size_t i = 0; i + 8 < data.size(); ++i) hash = (hash ^ data[i]) * UINT64_C(1099511628211);
        for (unsigned byte = 0; byte < 8; ++byte) data[data.size()-8+byte] = static_cast<std::uint8_t>(hash >> (8*byte));
    };
    const std::size_t law_prefix = u32(image,8)==29 ? 4 : 0;
    const auto control_offset = law_prefix + static_cast<std::size_t>(151) + u32(image,12+law_prefix);
    bool rejected = true;
    for (const auto control : {0U,1U,3U}) {
        auto bad = image;
        for (unsigned byte = 0; byte < 4; ++byte) bad[control_offset+byte] = static_cast<std::uint8_t>(control >> (8*byte));
        seal(bad); bool failed_control = false;
        try { static_cast<void>(MacClosedWorld::from_checkpoint(s.registry,s.eos,bad)); }
        catch (const std::invalid_argument&) { failed_control = true; }
        rejected = rejected && failed_control;
    }
    check(u32(image,8) == 29 && u32(image,12) == 15 &&
        restored.settings().caloric_trial_policy==MacCaloricTrialPolicy::reject_proven_negative &&
        u32(image,control_offset) == 2 && restored.checkpoint() == image &&
        cold.world.checkpoint() == continued.world.checkpoint() && rejected,
        "regional layout15 canonical selector and cold continuation");
    auto shut = source.state(); shut.grid.closed_x.assign(shut.momentum_x.size(),0);
    for (std::size_t y = 2; y < 10; ++y) shut.grid.closed_x[shut.grid.x_face(6,y)] = 1;
    const auto closed = advance_mac_kernel(s.registry,*s.eos,shut,settings,0,1./32,256,options);
    bool conserved = true;
    for (std::size_t k = 0; k < s.registry.size(); ++k) {
        long double start = 0, end = 0;
        for (std::size_t y = 2; y < 10; ++y) for (std::size_t x = 2; x < 6; ++x) {
            start += shut.cells[x+64*y].mixture.masses[k]; end += closed.state.cells[x+64*y].mixture.masses[k];
        }
        conserved = conserved && std::abs(end-start) <= 128*std::numeric_limits<double>::epsilon()*std::abs(start);
    }
    check(conserved,"regional crops preserve closed-face mass isolation");
}
void caloric_resolution_control() {
    namespace k = ma::experimental::mac_kernel;
    const auto& s = setup(); const auto index = s.registry.index("nitrogen"); const auto& definition = s.registry[index];
    const auto& profiles = s.eos->reference().materials();
    const auto profile = std::find_if(profiles.begin(),profiles.end(),[](const auto& p) { return p.id == "nitrogen"; });
    k::Model model; model.registry_size = s.registry.size(); k::Species species;
    species.registry = index; species.gas = true; species.molar_mass = profile->molar_mass_kg_per_mol;
    species.cv = profile->cv_j_per_kg_k; species.offset = profile->energy_offset_j_per_kg;
    species.tmin = profile->minimum_temperature_k; species.tmax = profile->maximum_temperature_k;
    species.conductivity = definition.conductivity; model.species = {species}; model.gases = {0};
    model.phase_of = {std::numeric_limits<std::size_t>::max()};
    MacGrid grid{2,1,1,1,false,false,std::vector<std::uint8_t>(2,0)}; k::Grid geometry(grid); k::Workspace workspace;
    MacClosedSettings settings; settings.numerical_kernel = MacNumericalKernel::production;
    settings.step_control = MacStepControl::regional_predictive; settings.vacuum = MacVacuumRepresentation::void_phase;
    settings.subcell_void = MacSubcellVoidPolicy::expand_unresolved_gas; settings.thermal_conduction = true;
    MacKernelOptions options;
    k::Context context{workspace,grid,model,geometry,settings,options,k::Exec{geometry,k::pool_for(1)},{},{}};
    k::Fields fields; fields.m = {std::ldexp(1.,-200),1}; fields.U = {std::ldexp(1.,-64),species.offset+species.cv*300};
    fields.U_remainder = {0,0}; fields.Vo = {1,1}; fields.jx.assign(3,0); fields.jy.assign(4,0);
    const auto masses = fields.m; MacEnergySum before; for (auto value : fields.U) before.add(MacEnergy::from_double(value));
    k::Accounts accounts; accounts.resize(context.quantities());
    k::stage_conduction(context,fields,1./16,accounts);
    MacEnergySum residual;
    for (auto value : fields.U) residual.add(MacEnergy::from_double(value));
    for (auto value : fields.U_remainder) residual.add(MacEnergy::from_double(value));
    for (std::size_t n = 0; n < before.size(); ++n) residual.add(before.component(n).negated());
    k::State endpoint;
    const bool valid = k::evaluate_state(model,fields,2,0,k::cell_stats(model,fields,2,0),fields.U[0],1,endpoint);
    check(valid && std::abs(endpoint.T-300) < 1e-10 && fields.m == masses && accounts.caloric_cells == 1 &&
        accounts.unresolved_conduction_cells == 1 &&
        std::abs(residual.value().project().value) <= accounts.energy.discarded.allowance(),
        "regional caloric resolution retains mass and counted energy");
    fields.U[0] = fields.m[0] * (species.offset + species.cv * .5);
    fields.U_remainder = {0,0};
    const auto stats = k::cell_stats(model,fields,2,0);
    const double temperature = k::gravity_entropy_temperature(model,fields,2,0,stats,fields.U[0],1,16*k::eps*species.cv);
    bool strict_rejected = false;
    try { (void)k::gravity_entropy_temperature(model,fields,2,0,stats,fields.U[0],1,0); }
    catch (const std::domain_error&) { strict_rejected = true; }
    before = {}; for (auto value : fields.U) before.add(MacEnergy::from_double(value));
    k::Accounts gravity_accounts; gravity_accounts.resize(context.quantities());
    k::stage_conduction(context,fields,1./16,gravity_accounts);
    residual = {};
    for (auto value : fields.U) residual.add(MacEnergy::from_double(value));
    for (auto value : fields.U_remainder) residual.add(MacEnergy::from_double(value));
    for (std::size_t n = 0; n < before.size(); ++n) residual.add(before.component(n).negated());
    const bool repaired = k::evaluate_state(model,fields,2,0,k::cell_stats(model,fields,2,0),fields.U[0],1,endpoint);
    check(std::abs(temperature-.5)<1e-12 && strict_rejected && repaired && endpoint.T>species.tmin &&
        fields.m==masses && gravity_accounts.caloric_cells==1 &&
        std::abs(residual.value().project().value)<=gravity_accounts.energy.discarded.allowance(),
        "regional gravity entropy trace reaches conservative caloric repair");
    auto source = s.state(2,1,false);
    source.cells[0].mixture = s.mixture(1,100000,330,.2);
    for (auto& mass : source.cells[0].mixture.masses) mass *= 1e-32;
    source.cells[0].mixture.energy *= 1e-32;
    for (auto& cell : source.cells) cell.passive_quantities.clear();
    auto wet_model = model;
    const auto water_index=s.registry.index("water");
    const auto water_profile=std::find_if(profiles.begin(),profiles.end(),[](const auto& p) { return p.id=="water"; });
    k::Species water;water.registry=water_index;water.gas=false;
    water.molar_mass=water_profile->molar_mass_kg_per_mol;water.cv=water_profile->cv_j_per_kg_k;
    water.offset=water_profile->energy_offset_j_per_kg;water.rho0=water_profile->zero_pressure_density_kg_per_m3;
    water.bulk=water_profile->reference_bulk_modulus_pa;water.maximum_ratio=water_profile->maximum_density_ratio;
    water.tmin=water_profile->minimum_temperature_k;water.tmax=water_profile->maximum_temperature_k;
    water.conductivity=s.registry[water_index].conductivity;
    wet_model.species.push_back(water);wet_model.liquids={1};wet_model.phase_of.push_back(0);
    std::vector<double> tiny_masses(s.registry.size());tiny_masses[index]=1e-32;tiny_masses[water_index]=1e-32;
    source.cells[0].mixture=s.eos->initialize_at_temperature(tiny_masses,330,1).mixture;
    source.cells[1].mixture=MixtureInventory(s.registry.size());
    source.cells[1].mixture.masses[index]=1;source.cells[1].mixture.energy=species.offset+species.cv*300;
    k::Workspace wet_workspace;
    k::Context wet{wet_workspace,grid,wet_model,geometry,settings,options,k::Exec{geometry,k::pool_for(1)},{},{}};
    k::Fields film; film.m.resize(2*wet_model.K());
    for (std::size_t q=0;q<wet_model.K();++q) for (std::size_t i=0;i<2;++i)
        film.m[q*2+i]=source.cells[i].mixture.masses[wet_model.species[q].registry];
    film.U={source.cells[0].mixture.energy,source.cells[1].mixture.energy};
    film.U_remainder={0,0};film.Vo={1,1};film.jx.assign(3,0);film.jy.assign(4,0);
    const auto old_film=film; k::Accounts wet_accounts;wet_accounts.resize(wet.quantities());
    k::stage_conduction(wet,film,1./16,wet_accounts);
    check(film.m==old_film.m && film.U==old_film.U && wet_accounts.unresolved_conduction_cells==1,
        "regional sub-resolution mixed cell skips ill-conditioned heat graph");
}

void zero_rhs_component_control() {
    namespace k=ma::experimental::mac_kernel;
    MacGrid grid{2,1,1,1,false,false,std::vector<std::uint8_t>(2)}; k::Grid geometry(grid);
    k::LinearSystem sys; sys.g=&geometry; sys.diag={0x1p-1040,4}; sys.active={1,1};
    sys.wx.assign(geometry.NX,0); sys.wy.assign(geometry.NY,0);
    for (const unsigned threads : {1u,12u}) {
        k::Model model; k::Workspace workspace; MacClosedSettings settings;
        settings.preconditioner=PressurePreconditioner::incomplete_cholesky;
        MacKernelOptions options; options.threads=threads;
        k::Context context{workspace,grid,model,geometry,settings,options,k::Exec{geometry,k::pool_for(threads)},{},{}};
        std::vector<double> x; bool legacy_rejected=false;
        try { (void)k::solve_linear(context,sys,{0,8},x,1e-13,1e-15,100); }
        catch (const std::invalid_argument&) { legacy_rejected=true; }
        settings.game_profile=MacGameProfile::base_game_floor;
        const auto solved=k::solve_linear(context,sys,{0,8},x,1e-13,1e-15,100);
        check(legacy_rejected && solved.converged && solved.iterations==1 && solved.residual==0 &&
            x==std::vector<double>({0,2}),"game zero-RHS subnormal component keeps the analytic root and other component");
        bool bad_rejected=true;
        for (const double bad : {0.,-1.,std::numeric_limits<double>::infinity()}) {
            auto invalid=sys; invalid.diag[0]=bad; bool rejected=false;
            try { (void)k::solve_linear(context,invalid,{0,8},x,1e-13,1e-15,100); }
            catch (const std::invalid_argument&) { rejected=true; }
            bad_rejected=bad_rejected && rejected;
        }
        check(bad_rejected,"game zero-RHS scalar keeps matrix admission");
        auto scalar=sys; scalar.diag={4,4}; const std::vector<double> zero{0,0};
        bool limits_rejected=true;
        for (const auto &[relative,absolute,maximum] : {std::tuple{0.,0.,std::size_t{1}},
             std::tuple{-1.,1e-15,std::size_t{1}},std::tuple{1e-13,1e-15,std::size_t{0}}}) {
            bool rejected=false;
            try { (void)k::solve_linear(context,scalar,zero,x,relative,absolute,maximum); }
            catch (const std::invalid_argument&) { rejected=true; }
            limits_rejected=limits_rejected && rejected;
        }
        check(limits_rejected,"game zero-RHS scalar keeps solve-limit admission");
    }
}
void game_numerics_control() {
    zero_rhs_component_control();
    namespace k = ma::experimental::mac_kernel;
    k::Model model; k::Species liquid;
    liquid.registry=0; liquid.gas=false; liquid.cv=1; liquid.rho0=1; liquid.bulk=0x1p20;
    liquid.maximum_ratio=2; liquid.tmin=1; liquid.tmax=1e9;
    model.species.push_back(liquid); model.liquids.push_back(0);
    k::Species gas; gas.registry=1; gas.gas=true; gas.cv=1;
    gas.molar_mass=ma::experimental::reference_gas_constant; gas.tmin=1; gas.tmax=1e9;
    model.species.push_back(gas); model.gases.push_back(1);
    k::Fields fields; fields.m={1,0x1p-336}; fields.U={256};
    const auto stats=k::cell_stats(model,fields,1,0); k::State endpoint;
    const bool old=k::evaluate_state(model,fields,1,0,stats,256,1,endpoint);
    model.recover_mixed_eos_endpoint=true;
    const bool recovered=k::evaluate_state(model,fields,1,0,stats,256,1,endpoint);
    check(!old && recovered && endpoint.T==256 &&
        std::abs(endpoint.p/0x1p-154-1)<=8*k::eps && std::abs(endpoint.Vg/0x1p-174-1)<=8*k::eps,
        "game mixed EOS recovers the analytic tiny-gas endpoint");
    check(!k::evaluate_state(model,fields,1,0,stats,.5,1,endpoint),
        "game mixed EOS endpoint recovery keeps temperature admission");

    const auto& s=setup(); auto state=s.state(2,32,false);
    state.occupied_volumes_m3.assign(state.cells.size(),0);
    const auto water=s.registry.index("water");
    for (std::size_t i=0;i<state.cells.size();++i) {
        auto& cell=state.cells[i]; cell.mixture=MixtureInventory(s.registry.size()); cell.passive_quantities.clear();
        if ((i/2)%2) { state.grid.walls[i]=1; continue; }
        const double volume=i%2 ? 1 : .98663;
        state.occupied_volumes_m3[i]=volume;
        std::vector<double> masses(s.registry.size()); masses[water]=1000*volume;
        cell.mixture=s.eos->initialize_at_temperature(masses,300,volume).mixture;
    }
    const auto inertia=mac_face_inertias(s.registry,state,*s.eos);
    for (std::size_t y=0;y<32;y+=2) state.momentum_x[state.grid.x_face(1,y)]=-20*inertia.x[state.grid.x_face(1,y)];
    MacClosedSettings settings; settings.maximum_timestep_s=.001;
    settings.reconstruction=MacMaterialReconstruction::interface_plic;
    settings.vacuum=MacVacuumRepresentation::void_phase; settings.numerical_kernel=MacNumericalKernel::production;
    settings.step_control=MacStepControl::regional_predictive; settings.game_profile=MacGameProfile::base_game_floor;
    settings.compression_viscosity=MacCompressionViscosity::none; settings.shock_heat=MacShockHeatRegularization::none;
    const auto saved_options=mac_kernel_default_options(); auto options=saved_options; options.threads=1;
    set_mac_kernel_default_options(options);
    const MacClosedWorld legacy(s.registry,s.eos,state,settings);
    const auto old_step=legacy.advanced(.001,65536);
    settings.compatible_void_pressure_work=true; settings.recover_mixed_eos_endpoint=true;
    const MacClosedWorld source(s.registry,s.eos,state,settings);
    const auto advanced=source.advanced(.001,65536); const auto report=last_mac_production_report();
    check(old_step.report.rejected_attempts>0 && advanced.report.rejected_attempts==0 &&
        advanced.report.substeps==16 && !report.audits.empty() &&
        std::all_of(report.audits.begin(),report.audits.end(),[](const auto& a) { return a.passed; }) && !report.reference_fallback,
        "game void pressure work closes without conservation retries");
    options.threads=12; set_mac_kernel_default_options(options);
    auto restored=MacClosedWorld::from_checkpoint(s.registry,s.eos,source.checkpoint()).advanced(.001,65536);
    check(last_mac_production_report().threads_used==12 && advanced.world.checkpoint()==restored.world.checkpoint() &&
        advanced.report.substeps==restored.report.substeps && advanced.report.rejected_attempts==restored.report.rejected_attempts &&
        source.checkpoint().at(8)==17 && legacy.checkpoint().at(8)==16,
        "game root/work policies continue byte-exactly across cold restore and workers");
    const auto one=advanced.world.advanced(.001,65536);
    const auto cold=MacClosedWorld::from_checkpoint(s.registry,s.eos,restored.world.checkpoint()).advanced(.001,65536);
    check(one.world.checkpoint()==cold.world.checkpoint(),"game void pressure work keeps the next cold continuation");
    set_mac_kernel_default_options(saved_options);
}
}

int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string(argv[1]) == "--game-numerics-only") {
            game_numerics_control();
            return failures == 0 ? 0 : 1;
        }
        if (argc > 1 && std::string(argv[1]) == "--regional-only") {
            regional_control(); caloric_resolution_control();
            return failures == 0 ? 0 : 1;
        }
        if (argc > 1 && std::string(argv[1]) == "--conduction-entropy-only") {
            conduction_entropy_control();
            return failures == 0 ? 0 : 1;
        }
        if (argc > 1 && std::string(argv[1]) == "--conduction-caloric-only") {
            conduction_caloric_domain_control();
            return failures == 0 ? 0 : 1;
        }
        std::vector<unsigned> thread_counts{1, 2, 4, 8, 12};
        if (argc > 1 && std::string(argv[1]) == "--quick") thread_counts = {1, 3};
        const auto& s = setup();
        // Checkpoint continuation: an interrupted run continues byte for byte.
        for (auto sc : {wave(16, 16), room(24, 16), liquid(8, 8), mixed(12, 12), vacuum(16, 8)}) {
            if (sc.name == "empty_cells") sc.settings.maximum_timestep_s = 1. / 4096;
            const double seconds = sc.name == "empty_cells" ? .02 : .1;
            const auto uninterrupted = production_run(owner(sc), 2, 1, seconds);
            const auto first = production_run(owner(sc), 1, 1, seconds);
            const auto restored = MacClosedWorld::from_checkpoint(s.registry, s.eos, first.checkpoint());
            const auto resumed = production_run(restored, 1, 1, seconds);
            check(uninterrupted.checkpoint() == resumed.checkpoint() && resumed.revision() == 3,
                "checkpoint continuation " + sc.name);
        }
        // Results never depend on the thread count (fixed blocks, block-ordered
        // reductions, line-ordered sweeps). The two larger grids have nine row
        // blocks each, so up to nine threads run blocks concurrently.
        for (auto sc : {wave(40, 37), room(33, 21), hydrostatic(9, 31), conduction(27, 18), viscous_wave(19, 23), wave(96, 90), room(128, 72)}) {
            std::vector<std::uint8_t> first;
            bool same = true;
            for (const auto t : thread_counts) {
                const auto bytes = production_run(owner(sc), 2, t).checkpoint();
                if (first.empty()) first = bytes; else same = same && bytes == first;
            }
            check(same, "thread-count independence " + sc.name);
        }
        // Atomic failure: an exhausted attempt budget publishes nothing.
        {
            auto sc = vacuum(16, 8); // the first 0.01 s attempts reject (volume CFL)
            const auto source = owner(sc); const auto before = source.checkpoint();
            bool threw = false;
            try { static_cast<void>(advance_mac_production(source, .1, 2, {})); } catch (const std::runtime_error&) { threw = true; }
            check(threw && source.checkpoint() == before, "budget exhaustion is atomic");
        }
        // Fail-closed admission of unported selectors.
        {
            auto sc = wave(8, 8); sc.settings.reconstruction = MacMaterialReconstruction::swept_first_moment;
            bool rejected = false;
            try { static_cast<void>(advance_mac_production(owner(sc), .1, 64, {})); } catch (const std::invalid_argument&) { rejected = true; }
            check(rejected, "swept reconstruction rejects");
            sc = wave(8, 8); sc.settings.compression_viscosity = MacCompressionViscosity::coupled_pressure; rejected = false;
            try { static_cast<void>(advance_mac_production(owner(sc), .1, 64, {})); } catch (const std::invalid_argument&) { rejected = true; }
            check(rejected, "coupled compressive pressure rejects");
        }
        // Production policy: exact equilibrium certificate first.
        {
            const MacClosedWorld rest(s.registry, s.eos, s.state(8, 8, true));
            const auto r = rest.advanced(.1, 64, MacExecutionPolicy::production);
            check(r.report.exact_equilibrium_intervals == 1 && r.world.revision() == rest.revision() + 1, "production policy certifies exact equilibrium");
            const auto moving = owner(wave(8, 8)).advanced(.1, 64, MacExecutionPolicy::production);
            check(moving.report.exact_equilibrium_intervals == 0 && moving.report.substeps > 0, "production policy advances a wave");
        }
        // Gas diffusion: the flow-only entry rejects a diffusion-enabled owner;
        // the production policy wraps the kernel in the owner's diffusion half
        // steps and agrees with the reference composition.
        {
            auto sc = room(24, 16);
            const auto source = owner(sc).with_diffusion(MacDiffusionSettings{});
            bool rejected = false;
            try { static_cast<void>(advance_mac_production(source, .1, 1024, {})); } catch (const std::invalid_argument&) { rejected = true; }
            const auto ref = source.advanced(.1, 1024, MacExecutionPolicy::reference);
            const auto prod = source.advanced(.1, 1024, MacExecutionPolicy::production);
            const auto m = compare(ref.world, prod.world, sc.disturbance_pa);
            std::cout << "DIFFUSION substeps=" << prod.report.diffusion.substeps << " dT_K=" << m.temperature_k
                << " dp_over_disturbance=" << m.pressure_over_disturbance << " dO2pp_rel=" << m.oxygen_relative
                << " dconstituent_rel=" << m.constituent_relative << '\n';
            check(rejected && prod.report.diffusion.substeps > 0 && ref.report.diffusion.substeps == prod.report.diffusion.substeps &&
                m.temperature_k <= .1 && m.pressure_over_disturbance <= .01 && m.oxygen_relative <= .01 && m.constituent_relative <= .01,
                "production policy composes gas diffusion");
        }
        // Persisted production selection admits the production physical
        // defaults and below-range traces; flagged large values refine.
        {
            auto sc = wave(8, 8);
            const auto off = owner(sc).advanced(.1);
            sc.settings.numerical_kernel = MacNumericalKernel::production;
            const auto on = owner(sc).advanced(.1);
            auto trace = sc; for (auto& cell : trace.state.cells) cell.mixture.masses[s.registry.index("helium")] = 1e-310;
            auto huge = sc; huge.state.cells[0].passive_quantities.back().quantity = 1e200;
            const auto subnormal = owner(trace).advanced(.1);
            const auto flagged = owner(huge).advanced(.1);
            const auto m = compare(off.world, on.world, sc.disturbance_pa);
            check(on.report.interval_kernel_intervals == 1 && m.pressure_over_disturbance < 1e-6 && m.temperature_k < 1e-6 &&
                on.world.settings().numerical_kernel == MacNumericalKernel::production, "persisted production selects production physics");
            check(subnormal.report.interval_kernel_intervals == 1 && flagged.report.interval_reference_fallbacks == 1 &&
                flagged.report.interval_kernel_intervals == 0, "below-range admitted, overflow-range keeps the reference fallback");
        }
        // Roundoff-level transverse momentum decays far below binary64's
        // normal range on a tall grid; it rounds (counted) without a
        // reference fallback, a limiter fault or an audit failure.
        {
            auto w = owner(wave(32, 512));
            bool fallback = false;
            for (int n = 0; n < 3; ++n) { w = advance_mac_production(w, .1, 1024, {}).world; fallback = fallback || last_mac_production_report().reference_fallback; }
            check(!fallback, "below-range transverse momentum stays on the production path");
        }
        // Interval audit report: every quantity audited and passed.
        {
            MacKernelOptions o; static_cast<void>(advance_mac_production(owner(room(16, 12)), .1, 1024, o));
            const auto& k = last_mac_production_report();
            bool all = !k.audits.empty();
            for (const auto& a : k.audits) {
                all = all && a.passed && std::abs(a.residual) <= a.bound;
                std::cout << "AUDIT " << a.quantity << " residual=" << a.residual << " bound=" << a.bound << (a.escalated ? " exact" : "") << '\n';
            }
            check(all && k.entropy_change_j_per_k >= -k.entropy_allowance_j_per_k, "interval audit covers every quantity");
        }
        conduction_entropy_control(); conduction_caloric_domain_control();
        regional_control(); caloric_resolution_control(); game_numerics_control();
        std::cout << (failures == 0 ? "ALL PASS" : "SOME FAILED") << '\n';
        return failures == 0 ? 0 : 1;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
