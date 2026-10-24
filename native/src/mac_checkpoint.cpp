#include "mixed_atmospheres/mac_closed_world.hpp"
#include "mixed_atmospheres/fluid_thermodynamics.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace ma::experimental {
namespace {
constexpr std::size_t maximum_bytes=1024ULL*1024*1024;
constexpr std::string_view magic="MACCLS01";
// Version history: 2 reference kernel; 3 adds viscosity; 4 interval kernel
// (diffusion disabled); 5 diffusion with kernels 0/1; 6 production kernel 2
// with explicit diffusion mode/multiplier even when disabled. Each owner writes
// its lowest representing version; 7 adds two-moment temporal pressure,
// 8 selects its coupled linear implementation. 9 is the void phase: every
// version-8 setting, the vacuum selector and each cell's occupied volume.
// Version 10 adds carried primary-U corrections, including flagged range values.
// Every version-2 through 9 byte stays unchanged when corrections are zero.
// Version 12 adds the explicit subcell-void policy after the phase-change field.
// With exact geometry, every version-2 through 11 byte remains unchanged.
constexpr std::uint32_t legacy_format_version=2,viscous_format_version=3;
constexpr std::uint32_t interval_format_version=4,diffusion_format_version=5,production_format_version=6;
constexpr std::uint32_t moment_format_version=7,coupled_moment_format_version=8,void_format_version=9,primary_format_version=10;
constexpr std::uint32_t phase_format_version=11;
constexpr std::uint32_t subcell_void_format_version=12;
// Version 13 adds the step control after the subcell-void policy; every
// earlier byte is unchanged when the control is doubling.
constexpr std::uint32_t step_control_format_version=13;
// Version 14 stores a nonlegacy gas-expansion fraction after step control.
constexpr std::uint32_t gas_expansion_format_version=14;
// Version 15 retains that complete prefix and requires regional control 2.
constexpr std::uint32_t regional_format_version=15;
// Version 16 appends the game-profile selector and trace-sink threshold.
constexpr std::uint32_t game_profile_format_version=16;
// Version 17 appends the two opt-in game root/work policy flags.
constexpr std::uint32_t game_numerics_format_version=17;
// Version18 saves substep trace removal; earlier settings keep their bytes.
constexpr std::uint32_t substep_trace_format_version=18;
constexpr std::uint32_t fast_phase_format_version=19;
void check(bool value,const char* message) { if (!value) throw std::invalid_argument(message); }
std::uint64_t checksum(std::span<const std::uint8_t> bytes) {
    std::uint64_t result=14695981039346656037ULL;
    for (auto byte : bytes) { result^=byte; result*=1099511628211ULL; }
    return result;
}
struct Writer {
    std::vector<std::uint8_t> bytes;
    void byte(std::uint8_t value) { check(bytes.size()<maximum_bytes,"MAC checkpoint exceeds its size limit"); bytes.push_back(value); }
    void u32(std::uint32_t value) { for (int bit=0;bit<32;bit+=8) byte(static_cast<std::uint8_t>(value>>bit)); }
    void u64(std::uint64_t value) { for (int bit=0;bit<64;bit+=8) byte(static_cast<std::uint8_t>(value>>bit)); }
    void count(std::size_t value) { check(value<=UINT32_MAX,"MAC checkpoint count exceeds its wire range"); u32(static_cast<std::uint32_t>(value)); }
    void number(double value) { check(std::isfinite(value),"Nonfinite MAC checkpoint number"); u64(std::bit_cast<std::uint64_t>(value)); }
    void string(std::string_view value) {
        count(value.size()); check(value.size()<=maximum_bytes-bytes.size(),"MAC checkpoint string exceeds its size limit");
        bytes.insert(bytes.end(),value.begin(),value.end());
    }
    void scaled(MacScaled value) { number(value.mantissa()); string(value.exponent().to_string()); }
};
struct Reader {
    std::span<const std::uint8_t> bytes;
    std::size_t position{};
    std::size_t remaining() const { return bytes.size()-position; }
    std::uint8_t byte() { check(remaining()>0,"Truncated MAC checkpoint"); return bytes[position++]; }
    std::uint32_t u32() { std::uint32_t value=0; for (int bit=0;bit<32;bit+=8) value|=static_cast<std::uint32_t>(byte())<<bit; return value; }
    std::uint64_t u64() { std::uint64_t value=0; for (int bit=0;bit<64;bit+=8) value|=static_cast<std::uint64_t>(byte())<<bit; return value; }
    double number() { const auto value=std::bit_cast<double>(u64()); check(std::isfinite(value),"Nonfinite saved MAC number"); return value; }
    bool flag() { const auto value=byte(); check(value<=1,"Invalid saved MAC boolean"); return value!=0; }
    std::string string(std::size_t maximum) {
        const auto size=u32(); check(size<=maximum && size<=remaining(),"Invalid saved MAC string length");
        const std::string value(reinterpret_cast<const char*>(bytes.data()+position),size); position+=size; return value;
    }
    MacScaled scaled() {
        const auto mantissa=number(); const auto text=string(MacExponent::maximum_decimal_characters);
        const auto exponent=MacExponent::from_decimal(text); check(exponent.to_string()==text,"Noncanonical saved MAC exponent");
        const auto value=MacScaled::from_parts(mantissa,exponent);
        check(std::bit_cast<std::uint64_t>(value.mantissa())==std::bit_cast<std::uint64_t>(mantissa) && value.exponent()==exponent,
            "Noncanonical saved MAC significand/exponent"); return value;
    }
};
// Binary identity avoids platform-dependent decimal formatting and retains
// every transport/caloric/EOS property, constituent order and provenance.
std::vector<std::uint8_t> identity(const SpeciesRegistry& registry,const FluidThermodynamics& eos) {
    eos.validate_registry(registry); Writer out; out.string(liquid_reference_eos_version);
    out.number(reference_gas_constant); out.number(entropy_reference_temperature_k);
    out.count(registry.size());
    for (const auto& species : registry.all()) {
        out.string(species.id); out.u32(static_cast<std::uint32_t>(species.phase));
        for (double value : {species.molar_mass,species.cp,species.density,species.conductivity,species.formation_enthalpy,species.max_mole_fraction}) out.number(value);
        out.count(species.atoms.size()); for (const auto& [atom,count] : species.atoms) { out.string(atom); out.number(count); }
    }
    out.string(eos.reference().profile_version()); out.count(eos.reference().materials().size());
    for (const auto& material : eos.reference().materials()) {
        out.string(material.id); out.u32(static_cast<std::uint32_t>(material.phase));
        for (double value : {material.molar_mass_kg_per_mol,material.cv_j_per_kg_k,material.energy_offset_j_per_kg,
            material.minimum_temperature_k,material.maximum_temperature_k,material.zero_pressure_density_kg_per_m3,
            material.reference_bulk_modulus_pa,material.maximum_density_ratio}) out.number(value);
        out.string(material.provenance);
        // Calibrated saturation pairs only; every uncalibrated identity byte is unchanged.
        if (material.entropy_offset_j_per_kg_k!=0) { out.string("entropy_offset"); out.number(material.entropy_offset_j_per_kg_k); }
    }
    return out.bytes;
}
void write_field(Writer& out,const std::vector<double>& projected,const std::vector<MacScaled>& full) {
    out.count(projected.size()); for (auto value : projected) out.number(value);
    out.count(full.size()); for (auto value : full) out.scaled(value);
}
void read_field(Reader& in,std::size_t count,std::vector<double>& projected,std::vector<MacScaled>& full) {
    check(in.u32()==count && count<=in.remaining()/8,"Saved MAC face coverage differs");
    projected.reserve(count); for (std::size_t i=0;i<count;++i) projected.push_back(in.number());
    const auto size=in.u32(); check((size==0 || size==count) && size<=in.remaining()/13,"Saved full MAC face coverage differs");
    full.reserve(size); for (std::size_t i=0;i<size;++i) full.push_back(in.scaled());
}
}

std::vector<std::uint8_t> MacClosedWorld::checkpoint() const {
    require_mac_energy_environment(); Writer out;
    for (auto value : magic) out.byte(static_cast<std::uint8_t>(value));
    if(settings_.numerical_kernel==MacNumericalKernel::game) {
        settings_.validate();
        const bool surface=settings_.game_profile==MacGameProfile::game_kernel_free_surface;
        const bool positive=surface || settings_.game_profile==MacGameProfile::game_kernel_positive_liquid;
        out.u32(surface?22:positive?21:20);const auto model=identity(registry_,*thermodynamics_);out.count(model.size());for(auto b:model)out.byte(b);
        out.number(time_s_);out.u64(revision_);const auto& st=state();const auto& g=st.grid;
        out.count(g.width);out.count(g.height);out.number(g.spacing_m);out.number(g.depth_m);
        out.number(settings_.maximum_timestep_s);out.number(settings_.gravity_m_per_s2);out.byte(settings_.thermal_conduction?1:0);out.number(settings_.trace_sink_threshold_kg);
        const auto& cfg=settings_.game;out.number(cfg.liquid_stiffness_ratio);out.u32(cfg.flow_parts);out.number(cfg.pressure_relative_tolerance);
        out.number(cfg.gas_heat_mixing);out.byte(cfg.sharpen_liquid_interfaces?1:0);out.number(cfg.interface_detail_fraction);out.number(cfg.phase_relaxation_time_s);
        if(positive){out.u32(static_cast<std::uint32_t>(cfg.liquid_law));out.u32(cfg.pressure_maximum_iterations);out.u32(cfg.heat_maximum_iterations);}
        out.count(cfg.saturation_pairs.size());for(const auto& p:cfg.saturation_pairs){out.count(p.liquid);out.count(p.vapour);out.number(p.critical_temperature_k);}
        out.count(st.cells.size());for(auto wall:g.walls)out.byte(wall);
        for(const auto& mask:{g.closed_x,g.closed_y}){out.count(mask.size());for(auto value:mask)out.byte(value);}
        for(const auto& cell:st.cells){check(cell.energy_remainder_j.is_zero(),"Game checkpoint cannot omit a primary correction");out.number(cell.mixture.energy);
            std::size_t present=0;for(auto mass:cell.mixture.masses)if(mass>0)++present;out.count(present);
            for(std::size_t key=0;key<cell.mixture.masses.size();++key)if(cell.mixture.masses[key]>0){out.count(key);out.number(cell.mixture.masses[key]);}
            out.count(cell.passive_quantities.size());for(const auto& q:cell.passive_quantities){out.u64(q.identity);out.number(q.quantity);}
        }
        check(st.full_momentum_x.empty()&&st.full_momentum_y.empty(),"Game checkpoint cannot omit full-range momentum");
        for(const auto* field:{&st.momentum_x,&st.momentum_y}){out.count(field->size());for(auto value:*field)out.number(value);}
        out.u64(checksum(out.bytes));return out.bytes;
    }
    // Keep every pre-viscosity checkpoint byte unchanged when disabled.
    const bool viscous=settings_.viscosity.shear_pa_s>0 || settings_.viscosity.bulk_pa_s>0;
    const bool production=settings_.numerical_kernel==MacNumericalKernel::production;
    const bool diffusive=settings_.diffusion.mode!=MacDiffusionMode::disabled;
    const bool coupled=settings_.pressure_integration==MacPressureIntegration::two_moment_coupled;
    const bool moments=settings_.pressure_integration!=MacPressureIntegration::discrete_gradient_midpoint;
    const bool voids=settings_.vacuum==MacVacuumRepresentation::void_phase;
    const bool carried=std::any_of(state().cells.begin(),state().cells.end(),
        [](const auto& cell) { return !cell.energy_remainder_j.is_zero(); });
    // Version 11 (phase change) has version 10's layout plus the phase-change
    // selector, and always carries the primary-U sidecar.
    const bool subcell=settings_.subcell_void!=MacSubcellVoidPolicy::exact_geometry;
    const bool stepped=settings_.step_control!=MacStepControl::doubling;
    const bool regional=settings_.step_control==MacStepControl::regional_predictive;
    const bool configured_expansion=settings_.gas_expansion_volume_fraction!=16 * std::numeric_limits<double>::epsilon();
    const bool game_profile=settings_.game_profile!=MacGameProfile::legacy;
    const bool fast_phase=settings_.fast_phase_trial_eos;
    const bool substep_trace=settings_.trace_sink_per_substep || fast_phase;
    const bool game_numerics=settings_.compatible_void_pressure_work || settings_.recover_mixed_eos_endpoint || substep_trace;
    const bool phase=settings_.phase_change!=MacPhaseChange::none, primary=carried || phase || subcell || stepped;
    out.u32(fast_phase ? fast_phase_format_version : substep_trace ? substep_trace_format_version : game_numerics ? game_numerics_format_version : game_profile ? game_profile_format_version : regional ? regional_format_version : configured_expansion ? gas_expansion_format_version : stepped ? step_control_format_version : subcell ? subcell_void_format_version : phase ? phase_format_version : primary ? primary_format_version : voids ? void_format_version : coupled ? coupled_moment_format_version : moments ? moment_format_version : production ? production_format_version : diffusive ? diffusion_format_version : viscous ? viscous_format_version : legacy_format_version);
    const auto model=identity(registry_,*thermodynamics_); out.count(model.size());
    for (auto value : model) out.byte(value);
    out.number(time_s_); out.u64(revision_);
    out.count(state().grid.width); out.count(state().grid.height); out.number(state().grid.spacing_m); out.number(state().grid.depth_m);
    out.byte(state().grid.periodic_x ? 1 : 0); out.byte(state().grid.periodic_y ? 1 : 0);
    out.number(settings_.maximum_timestep_s); out.number(settings_.maximum_outgoing_volume_fraction);
    out.u32(static_cast<std::uint32_t>(settings_.preconditioner)); out.number(settings_.gravity_m_per_s2);
    out.u32(static_cast<std::uint32_t>(settings_.reconstruction)); out.u32(static_cast<std::uint32_t>(settings_.compression_viscosity));
    out.u32(static_cast<std::uint32_t>(settings_.momentum_reconstruction)); out.byte(settings_.thermal_conduction ? 1 : 0);
    out.u32(static_cast<std::uint32_t>(settings_.shock_heat));
    if (viscous || diffusive || production || voids || primary) { out.number(settings_.viscosity.shear_pa_s);out.number(settings_.viscosity.bulk_pa_s); }
    // Versions 5/6 store the kernel selector; retired version 4 is read-only rejection.
    if (diffusive || production || voids || primary) out.u32(static_cast<std::uint32_t>(settings_.numerical_kernel));
    if (diffusive || production || voids || primary) {
        out.u32(static_cast<std::uint32_t>(settings_.diffusion.mode));out.number(settings_.diffusion.multiplier);
    }
    if (moments || voids || primary) out.u32(static_cast<std::uint32_t>(settings_.pressure_integration));
    if (voids || primary) out.u32(static_cast<std::uint32_t>(settings_.vacuum));
    if (phase || subcell || stepped) out.u32(static_cast<std::uint32_t>(settings_.phase_change));
    if (subcell || stepped) out.u32(static_cast<std::uint32_t>(settings_.subcell_void));
    if (stepped || configured_expansion) out.u32(static_cast<std::uint32_t>(settings_.step_control));
    if (configured_expansion || regional) out.number(settings_.gas_expansion_volume_fraction);
    if (game_profile) { out.u32(static_cast<std::uint32_t>(settings_.game_profile)); out.number(settings_.trace_sink_threshold_kg); }
    if (game_numerics) { out.byte(settings_.compatible_void_pressure_work ? 1 : 0); out.byte(settings_.recover_mixed_eos_endpoint ? 1 : 0); }
    if (substep_trace) out.byte(settings_.trace_sink_per_substep ? 1 : 0);
    if (fast_phase) out.byte(1);
    if(diffusive) {
        // Discrete flux formulation. Diffusion images were never released; the
        // earlier version-4 diffusion layouts no longer parse and reject.
        out.u32(mac_diffusion_formulation);
        out.string(transport_->identity());out.count(transport_->pairs().size());
        for(const auto& p:transport_->pairs()) {
            out.string(p.first);out.string(p.second);
            for(double v:{p.a,p.b,p.c,p.minimum_temperature_k,p.maximum_temperature_k,p.minimum_pressure_pa,p.maximum_pressure_pa})out.number(v);
            out.byte(p.estimated ? 1:0);out.string(p.source);
        }
    }
    out.count(state().cells.size()); for (auto wall : state().grid.walls) out.byte(wall);
    for (const auto& cell : state().cells) {
        out.number(cell.mixture.energy); for (auto mass : cell.mixture.masses) out.number(mass);
        out.count(cell.passive_quantities.size());
        for (const auto& passive : cell.passive_quantities) { out.u64(passive.identity); out.number(passive.quantity); }
    }
    write_field(out,state().momentum_x,state().full_momentum_x); write_field(out,state().momentum_y,state().full_momentum_y);
    if (voids || primary) { out.count(state().occupied_volumes_m3.size()); for (auto value : state().occupied_volumes_m3) out.number(value); }
    if (primary) {
        out.count(state().cells.size());
        for (const auto& cell : state().cells) {
            out.number(cell.energy_remainder_j.projected);
            out.byte(cell.energy_remainder_j.full.is_zero() ? 0 : 1);
            if (!cell.energy_remainder_j.full.is_zero()) out.scaled(cell.energy_remainder_j.full);
        }
    }
    out.u64(checksum(out.bytes)); return out.bytes;
}

MacClosedWorld MacClosedWorld::from_checkpoint(SpeciesRegistry registry,std::shared_ptr<const FluidThermodynamics> eos,
    std::span<const std::uint8_t> bytes,std::shared_ptr<const GasTransportProfile> transport) {
    require_mac_energy_environment(); check(eos!=nullptr,"Missing saved MAC EOS owner");
    check(bytes.size()>=32 && bytes.size()<=maximum_bytes,"Invalid MAC checkpoint size");
    Reader trailer{bytes.last(8)}; check(trailer.u64()==checksum(bytes.first(bytes.size()-8)),"MAC checkpoint checksum differs");
    Reader in{bytes.first(bytes.size()-8)};
    for (auto value : magic) check(in.byte()==static_cast<std::uint8_t>(value),"Unrecognized MAC checkpoint signature");
    const auto version=in.u32();
    if(version==20 || version==21 || version==22) {
        const auto model=identity(registry,*eos);check(in.u32()==model.size()&&model.size()<=in.remaining(),"Saved game model size differs");for(auto value:model)check(in.byte()==value,"Saved game registry/EOS identity differs");
        const double time=in.number();const auto revision=in.u64();check(time>=0&&revision>0,"Invalid saved game clock");
        MacTransportState st;st.grid.width=in.u32();st.grid.height=in.u32();st.grid.spacing_m=in.number();st.grid.depth_m=in.number();
        MacClosedSettings cfg;cfg.numerical_kernel=MacNumericalKernel::game;cfg.game_profile=MacGameProfile::game_kernel;cfg.vacuum=MacVacuumRepresentation::void_phase;cfg.thermal_conduction=true;
        cfg.maximum_timestep_s=in.number();cfg.gravity_m_per_s2=in.number();cfg.thermal_conduction=in.flag();cfg.trace_sink_threshold_kg=in.number();
        cfg.game.liquid_stiffness_ratio=in.number();cfg.game.flow_parts=in.u32();cfg.game.pressure_relative_tolerance=in.number();cfg.game.gas_heat_mixing=in.number();cfg.game.sharpen_liquid_interfaces=in.flag();cfg.game.interface_detail_fraction=in.number();cfg.game.phase_relaxation_time_s=in.number();
        if(version>=21){cfg.game_profile=version==22?MacGameProfile::game_kernel_free_surface:MacGameProfile::game_kernel_positive_liquid;cfg.game.liquid_law=static_cast<MacGameLiquidLaw>(in.u32());cfg.game.pressure_maximum_iterations=in.u32();cfg.game.heat_maximum_iterations=in.u32();}
        const auto pairs=in.u32();check(pairs<=65536&&pairs<=in.remaining()/16,"Invalid saved game pairs");for(std::size_t n=0;n<pairs;++n){MacSaturationPair pair;pair.liquid=in.u32();pair.vapour=in.u32();pair.critical_temperature_k=in.number();cfg.game.saturation_pairs.push_back(pair);}cfg.validate();
        const auto cells=in.u32();check(st.grid.width>0&&st.grid.height>0&&static_cast<std::uint64_t>(st.grid.width)*st.grid.height==cells&&cells<=in.remaining()/17,"Invalid saved game geometry");
        st.grid.walls.reserve(cells);for(std::size_t i=0;i<cells;++i)st.grid.walls.push_back(in.flag()?1:0);
        const auto nx=st.grid.x_width()*st.grid.height,ny=st.grid.width*st.grid.y_height();
        for(auto* mask:{&st.grid.closed_x,&st.grid.closed_y}){const auto count=in.u32();const auto expected=mask==&st.grid.closed_x?nx:ny;check((count==0||count==expected)&&count<=in.remaining(),"Invalid saved game face masks");for(std::size_t f=0;f<count;++f)mask->push_back(in.flag()?1:0);}
        st.cells.resize(cells);for(auto& cell:st.cells){cell.mixture=MixtureInventory(registry.size());cell.mixture.energy=in.number();const auto count=in.u32();check(count<=registry.size()&&count<=in.remaining()/12,"Invalid saved sparse game cell");std::size_t previous=0;
            for(std::size_t n=0;n<count;++n){const auto key=in.u32();const auto mass=in.number();check(key<registry.size()&&mass>0&&(n==0||key>previous),"Noncanonical saved game constituent");cell.mixture.masses[key]=mass;previous=key;}
            const auto passive=in.u32();check(passive<=in.remaining()/16,"Invalid saved game passives");std::uint64_t last=0;for(std::size_t n=0;n<passive;++n){const auto id=in.u64();const double q=in.number();check(q>0&&(n==0||id>last),"Noncanonical saved game passive");cell.passive_quantities.push_back({id,q});last=id;}
        }
        for(auto* field:{&st.momentum_x,&st.momentum_y}){const auto count=in.u32();check(count==(field==&st.momentum_x?nx:ny)&&count<=in.remaining()/8,"Invalid saved game momentum coverage");field->reserve(count);for(std::size_t f=0;f<count;++f)field->push_back(in.number());}
        check(in.remaining()==0,"Trailing saved game data");MacClosedWorld result(std::move(registry),std::move(eos),std::move(st),cfg,std::move(transport));result.time_s_=time;result.revision_=revision;return result;
    }

    const bool moment_version=version==moment_format_version || version==coupled_moment_format_version;
    const bool void_version=version==void_format_version;
    const bool phase_version=version==phase_format_version;
    const bool fast_phase_version=version==fast_phase_format_version;
    const bool substep_trace_version=version==substep_trace_format_version || fast_phase_version;
    const bool game_numerics_version=version==game_numerics_format_version || substep_trace_version;
    const bool game_profile_version=version==game_profile_format_version || game_numerics_version;
    const bool regional_version=version==regional_format_version || game_profile_version;
    const bool configured_expansion=version==gas_expansion_format_version || regional_version;
    const bool stepped_version=version==step_control_format_version || configured_expansion;
    const bool subcell_version=version==subcell_void_format_version || stepped_version;
    const bool primary_version=version==primary_format_version || phase_version || subcell_version;
    const bool complete_settings=void_version || primary_version;
    check(version==legacy_format_version || version==viscous_format_version || version==interval_format_version ||
        version==diffusion_format_version || version==production_format_version || moment_version || complete_settings,"Unsupported MAC checkpoint version");
    const auto model=identity(registry,*eos);
    check(in.u32()==model.size() && model.size()<=in.remaining(),"Saved MAC model size differs");
    for (auto value : model) check(in.byte()==value,"Saved MAC registry/EOS identity differs");
    const auto time=in.number(); const auto revision=in.u64(); check(time>=0 && revision>0,"Invalid saved MAC clock/revision");
    MacTransportState state; state.grid.width=in.u32(); state.grid.height=in.u32();
    state.grid.spacing_m=in.number(); state.grid.depth_m=in.number(); state.grid.periodic_x=in.flag(); state.grid.periodic_y=in.flag();
    MacClosedSettings settings; settings.maximum_timestep_s=in.number(); settings.maximum_outgoing_volume_fraction=in.number();
    settings.preconditioner=static_cast<PressurePreconditioner>(in.u32()); settings.gravity_m_per_s2=in.number();
    settings.reconstruction=static_cast<MacMaterialReconstruction>(in.u32()); settings.compression_viscosity=static_cast<MacCompressionViscosity>(in.u32());
    settings.momentum_reconstruction=static_cast<MacMomentumReconstruction>(in.u32()); settings.thermal_conduction=in.flag();
    settings.shock_heat=static_cast<MacShockHeatRegularization>(in.u32());
    if (version==viscous_format_version || version==interval_format_version || version==diffusion_format_version || version==production_format_version || moment_version || complete_settings) {
        settings.viscosity.shear_pa_s=in.number();settings.viscosity.bulk_pa_s=in.number();
        if (version==viscous_format_version)
            check(settings.viscosity.shear_pa_s>0 || settings.viscosity.bulk_pa_s>0,"Noncanonical viscosity-disabled checkpoint version");
    }
    check(version!=interval_format_version,"Saved MAC interval kernel 1 is retired; refusing a changed continuation");
    if(version==diffusion_format_version || version==production_format_version || moment_version || complete_settings) {
        settings.numerical_kernel=static_cast<MacNumericalKernel>(in.u32());
        if (complete_settings)
            check(settings.numerical_kernel==MacNumericalKernel::reference || settings.numerical_kernel==MacNumericalKernel::production,
                "Unsupported saved MAC numerical kernel");
        else if (version==production_format_version || moment_version)
            check(settings.numerical_kernel==MacNumericalKernel::production,"Noncanonical saved MAC production kernel");
        else {
            check(settings.numerical_kernel!=MacNumericalKernel::retired_interval_audited,
                "Saved MAC interval kernel 1 is retired; refusing a changed continuation");
            check(settings.numerical_kernel==MacNumericalKernel::reference,"Unsupported saved MAC numerical kernel");
        }
        settings.diffusion.mode=static_cast<MacDiffusionMode>(in.u32());settings.diffusion.multiplier=in.number();
        check(version==production_format_version || moment_version || complete_settings || settings.diffusion.mode!=MacDiffusionMode::disabled,"Noncanonical diffusion-disabled checkpoint version");
    }
    if (moment_version) {
        settings.pressure_integration=static_cast<MacPressureIntegration>(in.u32());
        check(settings.pressure_integration==(version==moment_format_version ? MacPressureIntegration::two_moment :
            MacPressureIntegration::two_moment_coupled),"Noncanonical saved MAC pressure integration");
    }
    if (complete_settings) {
        settings.pressure_integration=static_cast<MacPressureIntegration>(in.u32());
        settings.vacuum=static_cast<MacVacuumRepresentation>(in.u32());
        check(!void_version || settings.vacuum==MacVacuumRepresentation::void_phase,"Noncanonical saved MAC void-phase version");
    }
    if (phase_version || subcell_version) {
        settings.phase_change=static_cast<MacPhaseChange>(in.u32());
        check(!phase_version || settings.phase_change==MacPhaseChange::saturation,"Noncanonical saved MAC phase-change version");
    }
    if (subcell_version) {
        settings.subcell_void=static_cast<MacSubcellVoidPolicy>(in.u32());
        check(settings.subcell_void==MacSubcellVoidPolicy::expand_unresolved_gas ||
            (stepped_version && settings.subcell_void==MacSubcellVoidPolicy::exact_geometry),"Noncanonical saved MAC subcell-void version");
    }
    if (stepped_version) {
        settings.step_control=static_cast<MacStepControl>(in.u32());
        check(regional_version ? settings.step_control==MacStepControl::regional_predictive :
            (settings.step_control==MacStepControl::predictive ||
             (configured_expansion && settings.step_control==MacStepControl::doubling)),"Noncanonical saved MAC step-control version");
    }
    if (configured_expansion) {
        settings.gas_expansion_volume_fraction=in.number();
        check(regional_version || (settings.subcell_void==MacSubcellVoidPolicy::expand_unresolved_gas &&
            settings.gas_expansion_volume_fraction!=16 * std::numeric_limits<double>::epsilon()),
            "Noncanonical saved MAC gas-expansion version");
    }
    if (game_profile_version) {
        settings.game_profile=static_cast<MacGameProfile>(in.u32());
        settings.trace_sink_threshold_kg=in.number();
        check(settings.game_profile==MacGameProfile::base_game_floor,"Noncanonical saved MAC game profile");
    }
    if (game_numerics_version) {
        settings.compatible_void_pressure_work=in.flag(); settings.recover_mixed_eos_endpoint=in.flag();
        check(substep_trace_version || settings.compatible_void_pressure_work || settings.recover_mixed_eos_endpoint,"Noncanonical saved MAC game numerical policies");
    }
    if (substep_trace_version) {
        settings.trace_sink_per_substep=in.flag();
        check(settings.trace_sink_per_substep || fast_phase_version,"Noncanonical saved MAC substep trace policy");
    }
    if (fast_phase_version) {
        settings.fast_phase_trial_eos=in.flag();
        check(settings.fast_phase_trial_eos,"Noncanonical saved MAC fast phase trial policy");
    }
    if (settings.diffusion.mode!=MacDiffusionMode::disabled) {
        check(in.u32()==mac_diffusion_formulation,"Unsupported saved gas diffusion formulation");
        check(transport!=nullptr && in.string(256)==transport->identity(),"Saved transport profile identity differs");
        check(in.u32()==transport->pairs().size(),"Saved transport pair count differs");
        for(const auto& p:transport->pairs()) {
            check(in.string(256)==p.first && in.string(256)==p.second,"Saved transport pair differs");
            for(double v:{p.a,p.b,p.c,p.minimum_temperature_k,p.maximum_temperature_k,p.minimum_pressure_pa,p.maximum_pressure_pa})check(in.number()==v,"Saved transport coefficient differs");
            check(in.flag()==p.estimated && in.string(4096)==p.source,"Saved transport provenance differs");
        }
    }
    settings.validate();
    const auto count=in.u32();
    check(state.grid.width>0 && state.grid.height>0 && static_cast<std::uint64_t>(state.grid.width)*state.grid.height==count &&
        count<=in.remaining()/(13+8*registry.size()),"Saved MAC geometry disagrees with its payload");
    state.grid.walls.reserve(count); for (std::size_t i=0;i<count;++i) state.grid.walls.push_back(in.flag() ? 1 : 0);
    state.cells.reserve(count);
    for (std::size_t i=0;i<count;++i) {
        MacMaterialCell cell{MixtureInventory(registry.size()),{}}; cell.mixture.energy=in.number();
        for (auto& mass : cell.mixture.masses) mass=in.number();
        cell.mixture.masses.compact_checkpoint_zeros();
        const auto passives=in.u32(); check(passives<=in.remaining()/16,"Saved MAC passive count exceeds its payload");
        cell.passive_quantities.reserve(passives);
        for (std::size_t j=0;j<passives;++j) { const auto id=in.u64(); cell.passive_quantities.push_back({id,in.number()}); }
        state.cells.push_back(std::move(cell));
    }
    const auto x_count=state.grid.x_width()*state.grid.height,y_count=state.grid.y_height()*state.grid.width;
    read_field(in,x_count,state.momentum_x,state.full_momentum_x); read_field(in,y_count,state.momentum_y,state.full_momentum_y);
    if (complete_settings) {
        // Canonical: no occupied volumes while every occupied cell is filled.
        const auto size=in.u32(); check((size==0 || size==count) && size<=in.remaining()/8,"Saved MAC occupied-volume coverage differs");
        state.occupied_volumes_m3.reserve(size); for (std::size_t i=0;i<size;++i) state.occupied_volumes_m3.push_back(in.number());
    }
    if (primary_version) {
        check(in.u32()==count && count<=in.remaining()/9,"Saved MAC primary-U remainder coverage differs");
        bool nonzero=false;
        for (auto& cell : state.cells) {
            auto& remainder=cell.energy_remainder_j;
            remainder.projected=in.number();
            if (in.flag()) {
                remainder.full=in.scaled();
                check(!remainder.full.is_zero(),"Redundant full saved MAC primary-U remainder");
                (void)mac_stored_remainder_value(remainder);
            } else check(remainder.projected!=0 || std::bit_cast<std::uint64_t>(remainder.projected)==0,
                "Noncanonical zero saved MAC primary-U remainder");
            check(canonical_mac_primary(cell.mixture.energy,remainder),"Noncanonical saved MAC primary-U remainder");
            nonzero=nonzero || !remainder.is_zero();
        }
        check(nonzero || phase_version || subcell_version,"Noncanonical primary-U remainder-disabled checkpoint version");
    }
    check(in.remaining()==0,"Trailing MAC checkpoint data");
    const bool saved_occupied=!state.occupied_volumes_m3.empty();
    MacClosedWorld result(std::move(registry),std::move(eos),std::move(state),settings,std::move(transport));
    check(saved_occupied==!result.state().occupied_volumes_m3.empty(),"Noncanonical saved MAC occupied volumes");
    result.time_s_=time; result.revision_=revision; return result;
}
}
