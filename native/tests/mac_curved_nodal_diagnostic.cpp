#include "mac_fixture.hpp"
#include <array>
#include <iomanip>

// Compare moving-face values (convention 0) with dual-volume means (1).
// A nonzero exit retains the unresolved pressure/remap compatibility gate.
namespace {
using namespace ma;
using namespace ma::experimental;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::size_t run(int id,int axis,int curved,int convention,int direction,MacMomentumReconstruction momentum) {
    const auto& set=ma::testing::setup();constexpr std::size_t n=10,transverse=2;
    constexpr double a=.25,b=.125;
    auto source=set.state(axis==0?n:transverse,axis==0?transverse:n,false);auto& g=source.grid;
    g.periodic_x=axis==1;g.periodic_y=axis==0;
    std::array<double,n+1> q{},mean{};
    for(std::size_t f=1;f<n;++f){const auto x=static_cast<double>(f-1);q[f]=direction*(x/64+curved*x*x/512);}
    for(std::size_t f=1;f<n;++f)mean[f]=.25*(q[f-1]+2*q[f]+q[f+1]);
    MacMassFluxes sweeps{std::vector<double>(g.x_width()*g.height),std::vector<double>(g.width*g.y_height())};
    std::vector<double> volumes(source.cells.size());
    for(std::size_t i=0;i<source.cells.size();++i){
        const auto x=axis==0?i%g.width:i/g.width;volumes[i]=1+q[x+1]-q[x];
        std::vector<double> masses(set.registry.size());masses[set.registry.index("nitrogen")]=volumes[i];
        masses[set.registry.index("helium")]=volumes[i]*1e-28;
        source.cells[i].mixture=set.eos->initialize_at_temperature(masses,300,volumes[i]).mixture;
        source.cells[i].passive_quantities={{17,volumes[i]},{UINT64_C(0x80000000),volumes[i]*1e-30}};
    }
    const auto inertia=mac_face_inertias(set.registry,source,*set.eos,volumes);
    std::vector<MacScaled> jx(inertia.x.size()),jy(inertia.y.size());
    for(std::size_t other=0;other<transverse;++other)for(std::size_t f=0;f<=n;++f){
        const auto face=axis==0?g.x_face(f,other):g.y_face(other,f);
        (axis==0?sweeps.x:sweeps.y)[face]=q[f];
        if(f!=0&&f!=n)(axis==0?jx:jy)[face]=MacScaled::from_double((axis==0?inertia.x:inertia.y)[face]*
            (a+b*(static_cast<double>(f)+(convention?mean[f]:q[f]))));
    }
    store_mac_field(source.momentum_x,source.full_momentum_x,jx);store_mac_field(source.momentum_y,source.full_momentum_y,jy);
    const auto held=ma::testing::fingerprint(source);
    const auto plan=reconstruct_mac_swept_parcels(set.registry,*set.eos,source,volumes,sweeps);
    const auto next=advance_mac_parcel_transport(set.registry,source,*set.eos,plan,momentum);
    check(ma::testing::fingerprint(source)==held,"Curved diagnostic changed its source");
    for(const auto& cell:next.state.cells)
        check(cell.mixture.masses[set.registry.index("helium")]>0 && cell.passive_quantities.back().quantity>0,
            "Curved diagnostic lost a positive trace");
    const auto& report=axis==0?next.report.x:next.report.y;
    const auto actual=axis==0?mac_field_values(next.state.momentum_x,next.state.full_momentum_x):
        mac_field_values(next.state.momentum_y,next.state.full_momentum_y);
    std::size_t mismatches=0;
    std::cout<<"CGEOM "<<id<<' '<<axis<<' '<<curved<<' '<<convention<<' '<<direction<<' '<<a<<' '<<b<<' '
        <<next.report.physical_correction_fraction<<' '<<next.report.physical_admission_backtracks<<' '
        <<next.report.energy_residual_j<<' '<<next.report.energy_bound_j<<'\n';
    for(std::size_t f=0;f<=n;++f)std::cout<<"CSWEEP "<<id<<' '<<f<<' '<<q[f]<<' '<<mean[f]<<'\n';
    for(std::size_t other=0;other<transverse;++other)for(std::size_t f=4;f<=6;++f){
        const auto face=axis==0?g.x_face(f,other):g.y_face(other,f);
        const auto expected=MacScaled::from_double(report.new_inertia[face]*(a+b*static_cast<double>(f)));
        MacScaledAccount match;match.add(actual[face]);match.add(expected.negated());
        if(convention==0 && !match.within_bound())++mismatches;
        std::cout<<"CNODE "<<id<<' '<<other<<' '<<f<<' '<<face<<' '<<report.old_inertia[face]<<' '<<report.new_inertia[face]<<' '
            <<report.source_centroid_offsets[face][static_cast<std::size_t>(axis)]<<' '
            <<actual[face].mantissa()<<' '<<actual[face].exponent()<<' '<<expected.mantissa()<<' '<<expected.exponent()<<' '
            <<match.within_bound()<<'\n';
    }
    return mismatches;
}
}
int main(int argc,char** argv){try{
    check(argc==1 || (argc==2 && std::string_view(argv[1])=="--nodal-conversion"),"Invalid curved-nodal diagnostic arguments");
    const auto momentum=argc==1 ? MacMomentumReconstruction::swept_energy_flux : MacMomentumReconstruction::swept_nodal_1d;
    std::cout<<std::setprecision(17);int id=0;std::size_t mismatches=0;
    for(int axis=0;axis<2;++axis)for(int curved=0;curved<2;++curved)for(int convention=0;convention<2;++convention)
        for(int direction:{-1,1})mismatches+=run(id++,axis,curved,convention,direction,momentum);
    std::cout<<"CCOMPLETE "<<id<<' '<<mismatches<<'\n';
    if(mismatches){std::cerr<<"FAIL point-velocity affine remap under curved deformation\n";return 1;}
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
