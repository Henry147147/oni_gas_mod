#include "mixed_atmospheres/mac_closed_world.hpp"
#include "mixed_atmospheres/fluid_thermodynamics.hpp"
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace ma::experimental {
namespace {
constexpr std::size_t maximum_bytes=1024ULL*1024*1024;
constexpr std::string_view magic="MACCLS01";
constexpr std::uint32_t legacy_format_version=2,viscous_format_version=3;
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
    // Keep every pre-viscosity checkpoint byte unchanged when disabled.
    const bool viscous=settings_.viscosity.shear_pa_s>0 || settings_.viscosity.bulk_pa_s>0;
    out.u32(viscous ? viscous_format_version : legacy_format_version);
    const auto model=identity(registry_,*thermodynamics_); out.count(model.size());
    for (auto value : model) out.byte(value);
    out.number(time_s_); out.u64(revision_);
    out.count(state_.grid.width); out.count(state_.grid.height); out.number(state_.grid.spacing_m); out.number(state_.grid.depth_m);
    out.byte(state_.grid.periodic_x ? 1 : 0); out.byte(state_.grid.periodic_y ? 1 : 0);
    out.number(settings_.maximum_timestep_s); out.number(settings_.maximum_outgoing_volume_fraction);
    out.u32(static_cast<std::uint32_t>(settings_.preconditioner)); out.number(settings_.gravity_m_per_s2);
    out.u32(static_cast<std::uint32_t>(settings_.reconstruction)); out.u32(static_cast<std::uint32_t>(settings_.compression_viscosity));
    out.u32(static_cast<std::uint32_t>(settings_.momentum_reconstruction)); out.byte(settings_.thermal_conduction ? 1 : 0);
    out.u32(static_cast<std::uint32_t>(settings_.shock_heat));
    if (viscous) { out.number(settings_.viscosity.shear_pa_s);out.number(settings_.viscosity.bulk_pa_s); }
    out.count(state_.cells.size()); for (auto wall : state_.grid.walls) out.byte(wall);
    for (const auto& cell : state_.cells) {
        out.number(cell.mixture.energy); for (auto mass : cell.mixture.masses) out.number(mass);
        out.count(cell.passive_quantities.size());
        for (const auto& passive : cell.passive_quantities) { out.u64(passive.identity); out.number(passive.quantity); }
    }
    write_field(out,state_.momentum_x,state_.full_momentum_x); write_field(out,state_.momentum_y,state_.full_momentum_y);
    out.u64(checksum(out.bytes)); return out.bytes;
}

MacClosedWorld MacClosedWorld::from_checkpoint(SpeciesRegistry registry,std::shared_ptr<const FluidThermodynamics> eos,
    std::span<const std::uint8_t> bytes) {
    require_mac_energy_environment(); check(eos!=nullptr,"Missing saved MAC EOS owner");
    check(bytes.size()>=32 && bytes.size()<=maximum_bytes,"Invalid MAC checkpoint size");
    Reader trailer{bytes.last(8)}; check(trailer.u64()==checksum(bytes.first(bytes.size()-8)),"MAC checkpoint checksum differs");
    Reader in{bytes.first(bytes.size()-8)};
    for (auto value : magic) check(in.byte()==static_cast<std::uint8_t>(value),"Unrecognized MAC checkpoint signature");
    const auto version=in.u32();
    check(version==legacy_format_version || version==viscous_format_version,"Unsupported MAC checkpoint version");
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
    if (version==viscous_format_version) {
        settings.viscosity.shear_pa_s=in.number();settings.viscosity.bulk_pa_s=in.number();
        check(settings.viscosity.shear_pa_s>0 || settings.viscosity.bulk_pa_s>0,"Noncanonical viscosity-disabled checkpoint version");
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
        const auto passives=in.u32(); check(passives<=in.remaining()/16,"Saved MAC passive count exceeds its payload");
        cell.passive_quantities.reserve(passives);
        for (std::size_t j=0;j<passives;++j) { const auto id=in.u64(); cell.passive_quantities.push_back({id,in.number()}); }
        state.cells.push_back(std::move(cell));
    }
    const auto x_count=state.grid.x_width()*state.grid.height,y_count=state.grid.y_height()*state.grid.width;
    read_field(in,x_count,state.momentum_x,state.full_momentum_x); read_field(in,y_count,state.momentum_y,state.full_momentum_y);
    check(in.remaining()==0,"Trailing MAC checkpoint data");
    MacClosedWorld result(std::move(registry),std::move(eos),std::move(state),settings);
    result.time_s_=time; result.revision_=revision; return result;
}
}
