#include "mixed_atmospheres/occupied_staggered_hybrid.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>

namespace {
using namespace ma;
using namespace ma::experimental;
constexpr double eps=std::numeric_limits<double>::epsilon();
void check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
struct Fixture {
    SpeciesRegistry registry=SpeciesRegistry::standard();
    std::vector<double> fractions;
    std::shared_ptr<const FluidThermodynamics> eos;
    double temperature=0,sound=0;
    Fixture():fractions(registry.size()) {
        fractions[registry.index("oxygen")]=.22;fractions[registry.index("nitrogen")]=.78;fractions[registry.index("helium")]=1e-28;
        std::vector<ReferenceMaterial> materials;double coefficient=0,capacity=0;
        for(const auto* id:{"oxygen","nitrogen","helium"}) {
            const auto index=registry.index(id);const auto& species=registry[index];ReferenceMaterial material;
            material.id=id;material.phase=ReferencePhase::ideal_gas;material.molar_mass_kg_per_mol=species.molar_mass;
            material.cv_j_per_kg_k=species.cv();material.energy_offset_j_per_kg=species.internal_energy(0);
            material.minimum_temperature_k=1e-20;material.maximum_temperature_k=1e9;
            material.provenance="Explicit constant-property ideal-gas rarefaction validation profile";materials.push_back(std::move(material));
            coefficient+=fractions[index]*gas_constant/species.molar_mass;capacity+=fractions[index]*species.cv();
        }
        temperature=100000/coefficient;sound=std::sqrt((1+coefficient/capacity)*100000);
        eos=std::make_shared<const FluidThermodynamics>(registry,"ideal-gas-vacuum-validation-v1",std::move(materials));
    }
    StaggeredInputCell cell(double width) const {
        auto masses=fractions;for(auto& mass:masses) mass*=width;
        return {eos->initialize_at_temperature(masses,temperature,width).mixture,0};
    }
    StaggeredInitialization uniform(std::size_t count,double dx) const {
        StaggeredInitialization input;input.thermodynamics=eos;input.retain_local_geometry=true;input.left=StaggeredBoundary::rigid_wall;
        for(std::size_t i=0;i<count;++i) input.cells.push_back(cell(dx));
        for(std::size_t i=0;i<=count;++i) {input.positions_m.push_back(static_cast<double>(i)*dx);input.velocities_m_per_s.push_back(0);}
        return input;
    }
};
std::string fingerprint(const OccupiedStaggeredState& state) {
    std::ostringstream out;out<<std::hexfloat<<state.retains_local_geometry()<<state.time_seconds()<<state.origin_high_m()<<state.origin_low_m();
    for(const auto& node:state.nodes()) out<<node.relative_position_m<<node.relative_position_low_m<<node.inertia_kg<<node.momentum_kg_m_per_s<<node.fixed;
    for(const auto& cell:state.cells()) {
        out<<cell.occupied_volume_m3<<cell.mixture.energy<<cell.bulk_momentum_kg_m_per_s<<cell.mechanical_variance_j;
        for(const auto mass:cell.mixture.masses) out<<mass;
        out<<cell.thermodynamics.temperature_k<<cell.thermodynamics.mechanical_pressure_pa;
    }
    return out.str();
}
template<class Action> void unsupported(Action action) {
    bool caught=false;try {action();} catch(const FluidFatalError& error) {caught=error.code()==FluidFatalCode::unsupported_configuration;}
    check(caught,"Scalar geometry consumer accepted retained coordinate state");
}
void short_volume(const Fixture& fixture) {
    auto state=initialize_occupied_staggered(fixture.uniform(128,1.0/64));const auto old=state;
    const auto report=advance_occupied_staggered(state,.025*(1.0/64)/fixture.sound);
    check(state.retains_local_geometry() && !old.cells().empty(),"Retained geometry policy was lost");
    std::size_t low_count=0;double scalar_error=0;
    for(const auto& node:state.nodes()) if(node.relative_position_low_m!=0) ++low_count;
    check(low_count>0,"Local coordinate update retained no low components");
    for(std::size_t i=0;i<state.cells().size();++i) {
        const auto& left=state.nodes()[i];const auto& right=state.nodes()[i+1];const auto volume=state.cells()[i].occupied_volume_m3;
        scalar_error=std::max(scalar_error,std::abs((right.relative_position_m-left.relative_position_m)-volume)/volume);
        std::cout<<std::setprecision(17)<<"GEOMETRY cell="<<i<<" left_hi="<<left.relative_position_m<<" left_lo="<<left.relative_position_low_m<<
            " right_hi="<<right.relative_position_m<<" right_lo="<<right.relative_position_low_m<<" volume="<<volume<<'\n';
    }
    check(scalar_error>32*eps,"Control did not exercise the observed scalar-coordinate loss");
    check(report.absolute_geometry_volume_residual_m3<32*eps*2,"Retained geometry changed the declared volume bound");
    const auto before=old.totals(),after=state.totals();
    check(std::abs((after.internal_energy_j-before.internal_energy_j)+after.nodal_kinetic_energy_j-before.nodal_kinetic_energy_j)<
        2e-10*100000*2,"Retained coordinates broke physical energy balance");
    std::cout<<"CONTROL low_nodes="<<low_count<<" scalar_only_volume_error_relative="<<scalar_error<<
        " geometry_absolute="<<report.absolute_geometry_volume_residual_m3<<'\n';
    const auto held=fingerprint(state);bool rejected=false;
    try {static_cast<void>(advance_occupied_staggered(state,1e200));} catch(const FluidFatalError&) {rejected=true;}
    check(rejected && fingerprint(state)==held,"Rejected retained-geometry step changed primary parts or readings");
}
void rebasing_and_consumers(const Fixture& fixture) {
    auto input=fixture.uniform(0,1);input.positions_m={-1e100,0,1e-100};input.velocities_m_per_s={0,0,0};
    input.cells={fixture.cell(1e100),fixture.cell(1e-100)};
    const auto state=initialize_occupied_staggered(input);const auto held=fingerprint(state);
    check(state.nodes()[1].relative_position_m==state.nodes()[2].relative_position_m &&
        state.nodes()[1].relative_position_low_m!=state.nodes()[2].relative_position_low_m,"Rebasing did not preserve the short material interval");
    check(std::abs(state.cells()[1].occupied_volume_m3-1e-100)<=32*eps*1e-100,"Rebased cell lost local volume");
    StaggeredCartesianGrid grid;unsupported([&] {static_cast<void>(plan_staggered_cartesian_rezone(state,grid));});
    unsupported([&] {static_cast<void>(rezone_occupied_staggered(state,{}));});
    unsupported([&] {static_cast<void>(bind_material_endcaps(state));});
    check(fingerprint(state)==held,"Unsupported scalar consumer mutated retained coordinates");
    std::cout<<std::setprecision(17)<<"REBASE left_hi="<<state.nodes()[1].relative_position_m<<" left_lo="<<state.nodes()[1].relative_position_low_m<<
        " right_hi="<<state.nodes()[2].relative_position_m<<" right_lo="<<state.nodes()[2].relative_position_low_m<<
        " volume="<<state.cells()[1].occupied_volume_m3<<'\n';
}
void gravity(const Fixture& fixture) {
    auto input=fixture.uniform(8,.25);input.right=StaggeredBoundary::rigid_wall;input.gravity_m_per_s2=9.81;input.frame_origin_m=std::ldexp(1.0,100);
    auto state=initialize_occupied_staggered(input);const auto before=state;
    long double reported_potential=0,potential_scale=0;
    for(int step=0;step<16;++step) {
        const auto report=advance_occupied_staggered(state,std::ldexp(1.0,-12));
        reported_potential+=report.represented_potential_energy_change_j;
        potential_scale+=std::abs(report.represented_potential_energy_change_j);
    }
    const auto initial=before.totals(),final=state.totals();long double potential=0;
    for(std::size_t i=0;i<state.cells().size();++i) {
        const auto delta=[&](std::size_t node) {return (static_cast<long double>(state.nodes()[node].relative_position_m)-before.nodes()[node].relative_position_m)+
            (static_cast<long double>(state.nodes()[node].relative_position_low_m)-before.nodes()[node].relative_position_low_m);};
        potential+=state.cells()[i].mixture.mass()*9.81L*.5L*(delta(i)+delta(i+1));
    }
    for(std::size_t j=0;j<state.nodes().size();++j)
        std::cout<<std::setprecision(17)<<"GRAVITY_NODE node="<<j<<" old_hi="<<before.nodes()[j].relative_position_m<<
            " old_lo="<<before.nodes()[j].relative_position_low_m<<" next_hi="<<state.nodes()[j].relative_position_m<<
            " next_lo="<<state.nodes()[j].relative_position_low_m<<" mass_right="<<(j<state.cells().size()?state.cells()[j].mixture.mass():0)<<'\n';
    const long double balance=(static_cast<long double>(final.internal_energy_j)-initial.internal_energy_j)+
        (static_cast<long double>(final.nodal_kinetic_energy_j)-initial.nodal_kinetic_energy_j)+potential;
    check(std::abs(balance)<2e-10L*200000,"Retained geometry lost gravity energy");
    check(std::abs(potential-reported_potential)<=128*eps*potential_scale,"Incremental gravity work differs from complete retained geometry");
    for(const auto node:{std::size_t(0),state.nodes().size()-1})
        check(state.nodes()[node].relative_position_m==before.nodes()[node].relative_position_m &&
            state.nodes()[node].relative_position_low_m==before.nodes()[node].relative_position_low_m,"Rigid retained wall moved");
    check(state.origin_high_m()==before.origin_high_m() && state.origin_low_m()==before.origin_low_m(),"Fixed-wall global origin moved");
    std::cout<<std::setprecision(17)<<"GRAVITY balance="<<static_cast<double>(balance)<<
        " potential_delta="<<static_cast<double>(potential)<<" reported_potential_delta="<<static_cast<double>(reported_potential)<<
        " g="<<state.gravity_m_per_s2()<<" origin_old_hi="<<before.origin_high_m()<<" origin_old_lo="<<before.origin_low_m()<<
        " origin_next_hi="<<state.origin_high_m()<<" origin_next_lo="<<state.origin_low_m()<<'\n';
}
}
int main() {
    try {
        const Fixture fixture;short_volume(fixture);std::cout<<"PASS retained local-volume parts and failed-step rollback\n";
        rebasing_and_consumers(fixture);std::cout<<"PASS rebasing and explicit scalar-consumer rejection\n";
        gravity(fixture);std::cout<<"PASS retained local gravity and rigid-wall coordinates\n";return 0;
    } catch(const std::exception& error) {std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
