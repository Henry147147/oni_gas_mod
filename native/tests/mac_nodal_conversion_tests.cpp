#include "mac_fixture.hpp"
#include <array>
#include <iomanip>

namespace {
using namespace ma;
using namespace ma::experimental;
void check(bool value,const char* why) {if(!value)throw std::runtime_error(why);}
void scaled(MacScaled value) {std::cout<<' '<<value.mantissa()<<' '<<value.exponent();}
void curved(int id,int axis,int curvature,int direction,int exponent) {
    const auto& set=ma::testing::setup();constexpr std::size_t n=10,transverse=2;
    auto source=set.state(axis==0?n:transverse,axis==0?transverse:n,false);auto& g=source.grid;
    g.periodic_x=axis==1;g.periodic_y=axis==0;
    std::array<double,n+1> q{};std::vector<double> volumes(source.cells.size());
    for(std::size_t f=1;f<n;++f){const auto x=static_cast<double>(f-1);q[f]=direction*(x/64+curvature*x*x/512);}
    for(std::size_t i=0;i<source.cells.size();++i) {
        const auto x=axis==0?i%g.width:i/g.width;volumes[i]=1+q[x+1]-q[x];
        std::vector<double> masses(set.registry.size());masses[set.registry.index("nitrogen")]=volumes[i];masses[set.registry.index("helium")]=volumes[i]*1e-28;
        source.cells[i].mixture=set.eos->initialize_at_temperature(masses,300,volumes[i]).mixture;
        source.cells[i].passive_quantities={{17,volumes[i]},{UINT64_C(0x80000000),volumes[i]*1e-30}};
    }
    const auto inertia=mac_face_inertias(set.registry,source,*set.eos,volumes);
    const auto scale=MacScaled::from_parts(.5,exponent+1);
    MacMassFluxes sweeps{std::vector<double>(inertia.x.size()),std::vector<double>(inertia.y.size())};
    std::vector<MacScaled> jx(inertia.x.size()),jy(inertia.y.size());
    for(std::size_t other=0;other<transverse;++other)for(std::size_t f=0;f<=n;++f) {
        const auto face=axis==0?g.x_face(f,other):g.y_face(other,f);
        (axis==0?sweeps.x:sweeps.y)[face]=q[f];
        if(f!=0&&f!=n)(axis==0?jx:jy)[face]=scale.multiplied((axis==0?inertia.x:inertia.y)[face]*(.25+.125*(static_cast<double>(f)+q[f])));
    }
    store_mac_field(source.momentum_x,source.full_momentum_x,jx);store_mac_field(source.momentum_y,source.full_momentum_y,jy);
    const auto held=ma::testing::fingerprint(source);
    const auto plan=reconstruct_mac_swept_parcels(set.registry,*set.eos,source,volumes,sweeps);
    const auto result=advance_mac_parcel_transport(set.registry,source,*set.eos,plan,MacMomentumReconstruction::swept_nodal_1d);
    const auto& r=axis==0?result.report.x:result.report.y;
    const auto final=axis==0?mac_field_values(result.state.momentum_x,result.state.full_momentum_x):mac_field_values(result.state.momentum_y,result.state.full_momentum_y);
    check(ma::testing::fingerprint(source)==held && result.report.physical_correction_fraction==1 && result.report.physical_admission_backtracks==0,
        "Nodal conversion changed source or restricted the admissible affine control");
    for(std::size_t other=0;other<transverse;++other)for(std::size_t f=4;f<=6;++f) {
        const auto face=axis==0?g.x_face(f,other):g.y_face(other,f);
        MacScaledAccount account;account.add(final[face]);account.add(scale.multiplied(r.new_inertia[face]*(.25+.125*static_cast<double>(f))).negated());
        check(account.within_bound(),"Nodal conversion did not repair actual curved point-velocity remap");
    }
    std::cout<<"NCASE "<<id<<' '<<axis<<' '<<curvature<<' '<<direction<<' '<<exponent<<' '
        <<result.report.physical_correction_fraction<<' '<<result.report.physical_admission_backtracks;
    scaled(result.report.energy_residual_wide_j);scaled(result.report.energy_bound_wide_j);std::cout<<'\n';
    for(std::size_t f=0;f<=n;++f)std::cout<<"NSWEEP "<<id<<' '<<f<<' '<<q[f]<<'\n';
    for(int endpoint=0;endpoint<2;++endpoint) {
        const auto& state=endpoint==0?source:result.state;
        for(std::size_t i=0;i<state.cells.size();++i) {
            const auto& c=state.cells[i];
            check(c.mixture.masses[set.registry.index("helium")]>0 && c.passive_quantities.back().quantity>0,"Nodal conversion lost a positive trace");
            std::cout<<"NCELL "<<id<<' '<<endpoint<<' '<<i<<' '<<(endpoint==0?volumes[i]:1)<<' '<<c.mixture.energy<<' '
                <<c.mixture.masses[set.registry.index("nitrogen")]<<' '<<c.mixture.masses[set.registry.index("helium")]<<' '
                <<c.passive_quantities[0].quantity<<' '<<c.passive_quantities[1].quantity<<'\n';
        }
    }
    for(int component=0;component<2;++component) {
        const auto& faces=component==0?result.report.x:result.report.y;
        const auto old=component==0?jx:jy;
        const auto next=component==0?mac_field_values(result.state.momentum_x,result.state.full_momentum_x):mac_field_values(result.state.momentum_y,result.state.full_momentum_y);
        for(std::size_t f=0;f<old.size();++f) {
            std::cout<<"NFACE "<<id<<' '<<component<<' '<<f<<' '<<faces.old_inertia[f]<<' '<<faces.new_inertia[f];
            const auto& e=faces.energies[f];
            for(const auto& value:{old[f],faces.nodal_source_momentum.empty()?old[f]:faces.nodal_source_momentum[f],next[f],
                faces.nodal_source_work.empty()?MacEnergy{}:faces.nodal_source_work[f],e.old_kinetic,e.new_kinetic,
                e.transported_kinetic,e.mixing_heat,e.wall_heat,e.remap_work_energy,faces.full_wall_impulse[f]}) scaled(value);
            std::cout<<'\n';
        }
        for(const auto& e:faces.nodal_conversions) {
            std::cout<<"NCONV "<<id<<' '<<component<<' '<<e.cell<<' '<<e.left<<' '<<e.right;
            for(const auto& value:{e.volume_change,e.coefficient,e.requested_impulse,e.impulse})scaled(value);
            std::cout<<'\n';
        }
    }
    for(std::size_t i=0;i<source.cells.size();++i) {
        std::cout<<"NWORK "<<id<<' '<<i;scaled(result.report.nodal_source_cell_work[i]);scaled(result.report.nodal_advected_work[i]);std::cout<<'\n';
    }
    for(int component=0;component<2;++component) {
        const auto flux=component==0?mac_field_values(result.report.primal_mass_fluxes_kg.x,result.report.primal_mass_fluxes_kg.full_x):
            mac_field_values(result.report.primal_mass_fluxes_kg.y,result.report.primal_mass_fluxes_kg.full_y);
        for(std::size_t f=0;f<flux.size();++f){std::cout<<"NPRIMAL "<<id<<' '<<component<<' '<<f;scaled(flux[f]);std::cout<<'\n';}
    }
}

void continuation() {
    const auto& set=ma::testing::setup();auto state=set.state(16,2,false);state.grid.periodic_y=true;state.momentum_y.assign(32,0);
    for(std::size_t i=0;i<state.cells.size();++i)state.cells[i].mixture=set.mixture(1,100000+10*std::sin(static_cast<double>(i%16)),300);
    MacClosedSettings settings;settings.maximum_timestep_s=.001;settings.reconstruction=MacMaterialReconstruction::swept_first_moment;
    settings.momentum_reconstruction=MacMomentumReconstruction::swept_nodal_1d;settings.compression_viscosity=MacCompressionViscosity::coupled_pressure;
    const MacClosedWorld source(set.registry,set.eos,state,settings);const auto held=source.checkpoint();
    const auto first=source.advanced(.002);const auto image=first.world.checkpoint();
    const auto restored=MacClosedWorld::from_checkpoint(set.registry,set.eos,image);
    check(restored.settings().momentum_reconstruction==MacMomentumReconstruction::swept_nodal_1d && restored.checkpoint()==image &&
        restored.advanced(.002).world.checkpoint()==first.world.advanced(.002).world.checkpoint() && source.checkpoint()==held,
        "Nodal conversion checkpoint changed the selector, continuation or source");
    check(first.report.momentum_correction_events>0 && first.report.rejected_attempts==0,"Nodal owner did not execute corrections on its nominal path");
    std::cout<<"PASS nodal point-velocity owner, persisted selector and exact save/reload continuation\n";
}
void zero_and_rejection() {
    const auto& set=ma::testing::setup();auto state=set.state(8,2,true);const auto& g=state.grid;
    const auto inertia=mac_face_inertias(set.registry,state,*set.eos);
    for(std::size_t y=0;y<2;++y)for(std::size_t x=0;x<8;++x)state.momentum_x[g.x_face(x,y)]=inertia.x[g.x_face(x,y)]*(.1+.01*std::sin(static_cast<double>(x)));
    MacMassFluxes q{std::vector<double>(inertia.x.size(),.125),std::vector<double>(inertia.y.size())};
    const std::vector<double> volumes(state.cells.size(),1);const auto held=ma::testing::fingerprint(state);
    const auto plan=reconstruct_mac_swept_parcels(set.registry,*set.eos,state,volumes,q);
    const auto plain=advance_mac_parcel_transport(set.registry,state,*set.eos,plan,MacMomentumReconstruction::swept_energy_flux);
    const auto converted=advance_mac_parcel_transport(set.registry,state,*set.eos,plan,MacMomentumReconstruction::swept_nodal_1d);
    check(ma::testing::fingerprint(plain.state)==ma::testing::fingerprint(converted.state) && converted.report.x.nodal_conversions.empty(),
        "Zero stretch changed the original conservative remap");
    q.y.assign(q.y.size(),.125);const auto diagonal=reconstruct_mac_swept_parcels(set.registry,*set.eos,state,volumes,q);
    bool rejected=false;
    try{static_cast<void>(advance_mac_parcel_transport(set.registry,state,*set.eos,diagonal,MacMomentumReconstruction::swept_nodal_1d));}
    catch(const FluidFatalError& e){rejected=e.code()==FluidFatalCode::unsupported_topology;}
    check(rejected && ma::testing::fingerprint(state)==held,"Unimplemented two-dimensional nodal conversion was accepted or changed source");
    std::cout<<"PASS zero-stretch identity and atomic rejection of unsupported two-dimensional conversion\n";
}
}
int main() {
    try {
        std::cout<<std::setprecision(17);const auto& set=ma::testing::setup();
        for(const auto& m:set.eos->reference().materials()) std::cout<<"MATERIAL "<<set.registry.index(m.id)<<' '<<static_cast<int>(m.phase)<<' '
            <<m.molar_mass_kg_per_mol<<' '<<m.cv_j_per_kg_k<<' '<<m.energy_offset_j_per_kg<<' '<<m.zero_pressure_density_kg_per_m3<<' '<<m.reference_bulk_modulus_pa<<'\n';
        int id=0;for(int axis=0;axis<2;++axis)for(int curve=0;curve<2;++curve)for(int direction:{-1,1})for(int exponent:{0,-1200})curved(id++,axis,curve,direction,exponent);
        check(id==16,"Nodal conversion fixture coverage differs");std::cout<<"PASS original curved-deformation point-velocity oracle, both axes, signs and full-range scales\n";
        continuation();zero_and_rejection();return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
