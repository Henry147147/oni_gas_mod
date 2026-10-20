#include "mac_fixture.hpp"
#include "mixed_atmospheres/mac_compressive_work.hpp"
#include <iomanip>
using namespace ma::testing;
namespace {
void check(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
struct Material {
    MixtureInventory mixture;
    double volume;
    MacStoredRemainder energy_remainder{},volume_remainder{};
};
std::size_t records=0;
Material record(int family,const Material& source,double delta,double spacing=1,double seconds=.01) {
    const auto& model=*setup().eos;
    const auto r=evaluate_mac_compressive_work(model,source.mixture,source.volume,delta,spacing,seconds,{},
        {},source.energy_remainder,source.volume_remainder,true);
    check(r.primary_volume.has_value(),"Carried constitutive volume omitted its primary state");
    const auto& v=*r.primary_volume;
    check(v.value==r.isentropic.endpoint.volume_m3 && canonical_mac_primary(v.value,v.remainder),"Noncanonical constitutive volume");
    audit_mac_primary_write(v);
    MacEnergySum closure;closure.add(source.volume);closure.add(mac_stored_remainder_value(source.volume_remainder));closure.add(delta);
    closure.add(-v.value);closure.add(mac_stored_remainder_value(v.remainder).negated());
    for(std::size_t k=0;k<v.discarded.size();++k)closure.add(v.discarded.component(k).negated());
    check(closure.sign()==0,"Volume write or discarded terms do not close exactly");
    const auto before=model.evaluate(source.mixture,source.volume);
    const auto id=records++;
    std::cout<<"PVCASE "<<id<<' '<<family<<' '<<delta<<' '
        <<observed_energy(source.volume,source.volume_remainder)<<' '<<observed_energy(v.value,v.remainder)<<' '
        <<observed_energy(source.mixture.energy,source.energy_remainder)<<' '<<observed_energy(r.mixture.energy,r.energy_remainder_j)<<' '
        <<before.temperature_k<<' '<<r.state.temperature_k<<' '<<r.total_work_pressure_pa.project().value<<' '
        <<r.entropy_change_j_per_k.mantissa()<<' '<<r.entropy_change_j_per_k.exponent()<<' '
        <<r.entropy_allowance_j_per_k.mantissa()<<' '<<r.entropy_allowance_j_per_k.exponent()<<' '
        <<r.energy_residual_j.mantissa()<<' '<<r.energy_residual_j.exponent()<<' '
        <<r.energy_bound_j.mantissa()<<' '<<r.energy_bound_j.exponent()<<' '
        <<v.high_rounded<<' '<<(v.discarded.sign()!=0)<<'\n';
    for(std::size_t k=0;k<v.discarded.size();++k)
        std::cout<<"PVERROR "<<id<<' '<<v.discarded.component(k).mantissa()<<' '<<v.discarded.component(k).exponent()<<'\n';
    return {r.mixture,v.value,r.energy_remainder_j,v.remainder};
}
void masses(int family,const Material& source) {
    for(std::size_t i=0;i<source.mixture.masses.size();++i)if(source.mixture.masses[i]!=0)
        std::cout<<"PVMASS "<<family<<' '<<i<<' '<<source.mixture.masses[i]<<'\n';
}
void captured_entropy_case() {
    const auto& s=setup();MixtureInventory mixture(s.registry.size());
    // Unchanged N64 operands from local/mac-vacuum/failure64.stderr.
    mixture.masses[s.registry.index("oxygen")]=4.1667911863948701e-67;
    mixture.masses[s.registry.index("nitrogen")]=1.4773168752384173e-66;
    mixture.masses[s.registry.index("helium")]=1.8939959938805284e-94;
    mixture.energy=-2.0409157045795193e-64;
    bool rejected=false;
    try {(void)evaluate_mac_compressive_work(*s.eos,mixture,.125,1.1426655702845602e-5,.125,8.3565509496142332e-6,{});}
    catch(const FluidFatalError& error) {rejected=std::string(error.what()).find("stored-state entropy")!=std::string::npos;}
    check(rejected,"Legacy volume path changed its captured entropy rejection");
    Material value{mixture,.125,{},{}};masses(0,value);
    for(int i=0;i<64;++i) {
        value=record(0,value,(i%2==0 ? 1 : -1)*1.1426655702845602e-5,.125,8.3565509496142332e-6);
        check(i%2==0 ? !value.volume_remainder.is_zero() : value.volume==.125 && value.volume_remainder.is_zero(),
            "Forward/reverse material volume lost its correction or failed exact return");
    }
    std::cout<<"PASS captured entropy failure, unchanged bound and 64 carried-volume updates\n";
}
void normal_and_flagged() {
    const auto& s=setup();Material source{s.mixture(1,100000,300),1,{},{}};masses(1,source);
    auto state=source;
    for(int i=0;i<12;++i)state=record(1,state,i%2==0 ? -.0125 : .0125);
    const auto small=record(1,source,-1e-200);
    check(small.volume==1 && !small.volume_remainder.is_zero(),"Sub-ULP compression lost its primary volume");
    source.volume_remainder={0,MacEnergy::from_parts(.5,MacExponent(-2000))};
    const auto idle=record(1,source,0);
    check(same_mac_stored_remainder(source.volume_remainder,idle.volume_remainder),"Flagged source volume was flattened");
    const auto changed=record(1,source,.0125);
    check(!changed.volume_remainder.is_zero(),"Flagged source plus represented change lost all volume correction");
    source.volume_remainder={std::ldexp(1.,-54),{}};
    const auto crossed=record(1,source,std::ldexp(1.,-53));
    check(crossed.volume==std::nextafter(1.,2.) && crossed.volume_remainder.projected==-std::ldexp(1.,-54),
        "Incoming volume correction did not move the projected endpoint across its rounding boundary");
    std::cout<<"PASS normal, sub-ULP, flagged retained and audited third-part volume\n";
}
void invalid() {
    const auto& s=setup();const auto source=s.mixture(1,100000,300);std::size_t rejected=0;
    const auto attempt=[&](const MixtureInventory& input,double delta,const MacStoredRemainder& remainder,bool carry) {
        const auto before=input;
        try {(void)evaluate_mac_compressive_work(*s.eos,input,1,delta,1,.01,{}, {},{},remainder,carry);}
        catch(const std::exception&) {++rejected;}
        check(input.masses==before.masses && input.energy==before.energy,"Rejected volume response changed its source");
    };
    attempt(source,0,{std::ldexp(1.,-60),{}},false);
    attempt(source,0,{.25,{}},true);
    attempt(source,0,{0,MacEnergy::from_double(1)},true);
    attempt(source,-2,{},true);
    attempt(s.mixture(1,300000,300,1),0,{},true);
    check(rejected==5,"Malformed or unsupported carried volume was admitted");
    const auto legacy=evaluate_mac_compressive_work(*s.eos,source,1,0,1,.01,{});
    check(!legacy.primary_volume.has_value(),"Legacy path silently selected carried volume");
    std::cout<<"PASS five rejected requests, source immutability and explicit component admission\n";
}
}
int main() {try {
    require_mac_energy_environment();std::cout<<std::setprecision(17);const auto& s=setup();
    for(const auto& d:s.eos->reference().materials())if(d.phase==ReferencePhase::ideal_gas)
        std::cout<<"PVDEF "<<s.registry.index(d.id)<<' '<<d.molar_mass_kg_per_mol<<' '<<d.cv_j_per_kg_k<<' '<<d.energy_offset_j_per_kg<<'\n';
    captured_entropy_case();normal_and_flagged();invalid();
    check(records==80,"Primary-volume record coverage differs");
    std::cout<<"ALL PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
