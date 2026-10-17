// Bounded-energy selector controls, retaining the original affine/EOS/checkpoint fixtures.
#include "mac_fixture.hpp"
#include <array>
#include <iomanip>
#include <fstream>

namespace {
using namespace ma;
using namespace ma::experimental;
void check(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
void near(MacScaled actual, MacScaled expected, const char* why) {
    MacScaledAccount account; account.add(actual); account.add(expected.negated());
    check(account.within_bound(), why);
}
void affine(int id, int axis, int control, int direction, int exponent, std::size_t transverse=2) {
    const auto& set=ma::testing::setup(); constexpr std::size_t n=10;
    auto source=set.state(axis==0 ? n : transverse, axis==0 ? transverse : n, false);
    auto& g=source.grid; g.periodic_x=axis==1 && transverse>1; g.periodic_y=axis==0 && transverse>1;
    const double a=control==5 ? direction*-.625 : control==2 ? 1 : control==4 ? -.125 : .25;
    const double b=control==5 ? direction*.125 : control==2 ? 0 : .125;
    std::array<double,n+1> q{};
    for (std::size_t f=1;f<n;++f) q[f]=control==5 ? direction*(static_cast<double>(f)-5)/(64-direction) : direction*(control==3 ? 0 : control==1 ? .125 : static_cast<double>(f-1)/64);
    MacMassFluxes sweeps{std::vector<double>(g.x_width()*g.height),std::vector<double>(g.width*g.y_height())};
    std::vector<double> volumes(source.cells.size());
    for (std::size_t i=0;i<source.cells.size();++i) {
        const auto coordinate=axis==0 ? i%g.width : i/g.width;
        volumes[i]=1+q[coordinate+1]-q[coordinate];
        std::vector<double> masses(set.registry.size());
        masses[set.registry.index("nitrogen")]=volumes[i]; masses[set.registry.index("helium")]=volumes[i]*1e-28;
        source.cells[i].mixture=set.eos->initialize_at_temperature(masses,300,volumes[i]).mixture;
        source.cells[i].passive_quantities={{17,volumes[i]},{UINT64_C(0x80000000),volumes[i]*1e-30}};
    }
    const auto inertia=mac_face_inertias(set.registry,source,*set.eos,volumes);
    std::vector<MacScaled> jx(inertia.x.size()),jy(inertia.y.size());
    const auto scale=MacScaled::from_parts(.5,exponent+1);
    for (std::size_t other=0;other<transverse;++other) for (std::size_t f=0;f<=n;++f) {
        const auto face=axis==0 ? g.x_face(f,other) : g.y_face(other,f);
        (axis==0 ? sweeps.x : sweeps.y)[face]=q[f];
        if (f!=0 && f!=n) (axis==0 ? jx : jy)[face]=scale.multiplied(
            (axis==0 ? inertia.x : inertia.y)[face]*(a+b*(static_cast<double>(f)+q[f])));
        if (control==4 && direction==1 && f!=0 && f!=n)
            check(std::abs(q[f]-(8./65)*(a+b*(static_cast<double>(f)+q[f])))<=128*std::numeric_limits<double>::epsilon()*std::abs(q[f]),
                "Consistent control does not satisfy q=dt*u");
    }
    store_mac_field(source.momentum_x,source.full_momentum_x,jx); store_mac_field(source.momentum_y,source.full_momentum_y,jy);
    const auto held=ma::testing::fingerprint(source);
    const auto plan=reconstruct_mac_swept_parcels(set.registry,*set.eos,source,volumes,sweeps);
    const auto next=advance_mac_parcel_transport(set.registry,source,*set.eos,plan,MacMomentumReconstruction::swept_energy_flux);
    check(ma::testing::fingerprint(source)==held,"Swept remap changed source");
    check(next.report.physical_correction_fraction==1 && next.report.physical_admission_backtracks==0,"Admissible affine candidate was restricted");
    const auto& report=axis==0 ? next.report.x : next.report.y;
    const auto actual=axis==0 ? mac_field_values(next.state.momentum_x,next.state.full_momentum_x) : mac_field_values(next.state.momentum_y,next.state.full_momentum_y);
    std::cout << "SWCASE " << id << ' ' << axis << ' ' << control << ' ' << direction << ' ' << exponent << ' ' << a << ' ' << b << ' ' << transverse << '\n';
    for (std::size_t f=0;f<=n;++f) std::cout << "SWSWEEP " << id << ' ' << f << ' ' << q[f] << '\n';
    for (std::size_t other=0;other<transverse;++other) for (std::size_t f=4;f<=6;++f) {
        const auto face=axis==0 ? g.x_face(f,other) : g.y_face(other,f);
        const auto expected=scale.multiplied(report.new_inertia[face]*(a+b*static_cast<double>(f)));
        if (control==5 && expected.is_zero()) {
            MacScaledSum residual, allowance;
            residual.add(actual[face]);residual.add(expected.negated());
            for (const auto& value:{actual[face],expected,report.upwind_momentum[face]}) allowance.add(value.magnitude().shifted(-45));
            for (const auto& event:report.remap_corrections) {
                const auto& edge=report.fluxes[event.dual_edge];
                if (edge.left==face || edge.right==face) allowance.add(event.impulse.magnitude().shifted(-45));
            }
            const auto sign=residual.sign();
            for(std::size_t k=0;k<residual.size();++k) allowance.add(sign<0 ? residual.component(k) : residual.component(k).negated());
            check(allowance.sign()>=0,"Stagnation affine cancellation exceeds its original operand roundoff");
        } else near(actual[face],expected,"Actual stored swept-remap momentum is not affine exact");
        std::cout << "SWNODE " << id << ' ' << other << ' ' << f << ' ' << face << ' '
            << actual[face].mantissa() << ' ' << actual[face].exponent() << ' ' << expected.mantissa() << ' ' << expected.exponent() << '\n';
    }
    if (control==5) {
        const auto previous=advance_mac_parcel_transport(set.registry,source,*set.eos,plan,MacMomentumReconstruction::swept_physical);
        const auto old=axis==0 ? mac_field_values(previous.state.momentum_x,previous.state.full_momentum_x) : mac_field_values(previous.state.momentum_y,previous.state.full_momentum_y);
        bool defect=false;
        for (std::size_t other=0;other<transverse;++other) for (std::size_t f=4;f<=6;++f) {
            const auto face=axis==0 ? g.x_face(f,other) : g.y_face(other,f);
            const auto expected=scale.multiplied(report.new_inertia[face]*(a+b*static_cast<double>(f)));
            MacScaledAccount account;account.add(old[face]);account.add(expected.negated());defect=defect || !account.within_bound();
            std::cout << "BWBASE " << id << ' ' << other << ' ' << f << ' ' << old[face].mantissa() << ' ' << old[face].exponent() << '\n';
        }
        if (direction>0) check(defect,"Original mode did not expose the expanding stagnation control's affine defect");
    }
    ma::testing::export_transport(id,source,next,plan);
}
void invalid() {
    const auto& set=ma::testing::setup(); auto source=set.state(4,3,false,true,true); const auto held=ma::testing::fingerprint(source);
    MacMassFluxes zero{std::vector<double>(source.momentum_x.size()),std::vector<double>(source.momentum_y.size())};
    bool rejected=false;
    try { static_cast<void>(advance_mac_transport(set.registry,source,zero,MacMomentumReconstruction::swept_energy_flux)); }
    catch (const std::invalid_argument&) { rejected=true; }
    check(rejected && ma::testing::fingerprint(source)==held,"EOS-free physical reconstruction was accepted or changed source");
    auto volumes=std::vector<double>(source.cells.size(),1); volumes[5]=0; volumes[0]=1.125;
    rejected=false;
    try { static_cast<void>(advance_mac_transport(set.registry,source,zero,*set.eos,volumes,MacMomentumReconstruction::swept_energy_flux)); }
    catch (const std::domain_error&) { rejected=true; }
    check(rejected && ma::testing::fingerprint(source)==held,"Bad dual geometry was accepted or changed source");
}
MacClosedWorld owner(bool wet, MacMaterialReconstruction material, MacShockHeatRegularization heat,
    MacCompressionViscosity compression) {
    const auto& set=ma::testing::setup();auto state=set.state(4,3,false,wet,true);
    const auto inertia=mac_face_inertias(set.registry,state,*set.eos);
    state.momentum_x[state.grid.x_face(2,0)]=inertia.x[state.grid.x_face(2,0)]*.1;
    state.momentum_y[state.grid.y_face(3,1)]=inertia.y[state.grid.y_face(3,1)]*-.025;
    MacClosedSettings settings;settings.gravity_m_per_s2=9.81;settings.thermal_conduction=!wet;
    settings.reconstruction=material;settings.momentum_reconstruction=MacMomentumReconstruction::swept_energy_flux;
    settings.shock_heat=heat;settings.compression_viscosity=compression;
    return MacClosedWorld(set.registry,set.eos,std::move(state),settings);
}
void continuation() {
    const auto& set=ma::testing::setup();std::size_t count=0;
    for (const bool wet:{false,true}) for (const auto material:{MacMaterialReconstruction::donor_cell,MacMaterialReconstruction::limited_linear,MacMaterialReconstruction::swept_first_moment})
        for (const auto heat:{MacShockHeatRegularization::none,MacShockHeatRegularization::compression_activated,MacShockHeatRegularization::pressure_sensed_acoustic,MacShockHeatRegularization::pressure_sensed_series})
            for (const auto compression:{MacCompressionViscosity::none,MacCompressionViscosity::quadratic,MacCompressionViscosity::acoustic_quadratic,MacCompressionViscosity::coupled_pressure}) {
                const auto source=owner(wet,material,heat,compression);const auto held=source.checkpoint();
                const auto first=source.advanced(.02);const auto image=first.world.checkpoint();
                const auto restored=MacClosedWorld::from_checkpoint(set.registry,set.eos,image);
                check(restored.checkpoint()==image && source.checkpoint()==held &&
                    restored.settings().momentum_reconstruction==MacMomentumReconstruction::swept_energy_flux &&
                    restored.settings().reconstruction==material,"Swept physical checkpoint changed source or selectors");
                const auto expected=first.world.advanced(.02),resumed=restored.advanced(.02);
                check(expected.world.checkpoint()==resumed.world.checkpoint(),"Swept physical checkpoint continuation differs");
                check(first.report.momentum_correction_events>0 && expected.report.momentum_correction_events>0,
                    "Swept correction events missing from owning interval report");
                for (std::size_t i=0;i<resumed.world.state().cells.size();++i) if (!resumed.world.state().grid.walls[i])
                    check(set.eos->evaluate(resumed.world.state().cells[i].mixture,1).temperature_k>0 &&
                        resumed.world.state().cells[i].mixture.masses[set.registry.index("helium")]>0 &&
                        !resumed.world.state().cells[i].passive_quantities.empty(),"Swept continuation lost EOS, trace or passive state");
                ++count;
            }
    check(count==96 && MacClosedSettings{}.momentum_reconstruction==MacMomentumReconstruction::limited_linear,"Continuation coverage or default changed");
    std::cout << "SWCONTINUATION " << count << '\n';
}
void eos_boundary() {
    const auto& set=ma::testing::setup();auto definitions=set.eos->reference().materials();
    for (auto& definition:definitions) definition.minimum_temperature_k=300;
    FluidThermodynamics model(set.registry,"swept-minimum-temperature-control",std::move(definitions));
    auto source=set.state(8,3,true);const auto& g=source.grid;
    std::vector<double> amounts(set.registry.size());amounts[set.registry.index("nitrogen")]=1;amounts[set.registry.index("helium")]=1e-28;
    for (auto& cell:source.cells) cell.mixture=model.initialize_at_temperature(amounts,300,1).mixture;
    const auto inertia=mac_face_inertias(set.registry,source,model);
    MacMassFluxes flux{std::vector<double>(source.momentum_x.size(),.125),std::vector<double>(source.momentum_y.size())};
    for (std::size_t y=0;y<g.height;++y) for (std::size_t x=0;x<g.width;++x)
        source.momentum_x[g.x_face(x,y)]=inertia.x[g.x_face(x,y)]*(30+25*std::sin(2*3.14159265358979323846*static_cast<double>(x)/8));
    const auto held=ma::testing::fingerprint(source);const std::vector<double> volumes(source.cells.size(),1);
    const auto next=advance_mac_transport(set.registry,source,flux,model,volumes,MacMomentumReconstruction::swept_energy_flux);
    check(ma::testing::fingerprint(source)==held && next.report.physical_admission_backtracks==0 &&
        next.report.physical_correction_fraction==1,"Safe EOS-boundary state changed source or was restricted");
    for (const auto& cell:next.state.cells) check(model.evaluate(cell.mixture,1).temperature_k>=300-1e-10,"EOS lower boundary crossed");
    std::cout << "SWEOSBOUNDARY " << next.report.physical_correction_fraction << ' ' << next.report.physical_admission_backtracks << '\n';
    auto upper_definitions=set.eos->reference().materials();
    for (auto& definition:upper_definitions) definition.maximum_temperature_k=300;
    FluidThermodynamics upper(set.registry,"swept-maximum-temperature-rejection",std::move(upper_definitions));
    bool rejected=false;
    try { static_cast<void>(advance_mac_transport(set.registry,source,flux,upper,volumes,MacMomentumReconstruction::swept_energy_flux)); }
    catch (const std::domain_error&) { rejected=true; }
    check(rejected && ma::testing::fingerprint(source)==held,"Unsafe completed EOS state or unsafe donor fallback accepted");
}
}
int main(int argc,char** argv) { try {
    require_mac_energy_environment(); std::cout << std::setprecision(17);
    const auto& set=ma::testing::setup();
    if (argc==2 && std::string(argv[1])=="--owner") {
        continuation();std::cout << "PASS 96 bounded-energy phase material heat compression continuations\n";
        eos_boundary();std::cout << "PASS actual EOS boundary accepts safe state and rejects unsafe fallback atomically\n";return 0;
    }
    if (argc==3) {
        const auto fixture=owner(true,MacMaterialReconstruction::swept_first_moment,MacShockHeatRegularization::pressure_sensed_acoustic,
            MacCompressionViscosity::coupled_pressure).advanced(.02).world;
        if (std::string(argv[1])=="--write-checkpoint") {
            const auto image=fixture.checkpoint();std::ofstream out(argv[2],std::ios::binary);out.write(reinterpret_cast<const char*>(image.data()),static_cast<std::streamsize>(image.size()));check(out.good(),"Could not write swept checkpoint");
        } else if (std::string(argv[1])=="--read-checkpoint") {
            std::ifstream in(argv[2],std::ios::binary);check(in.good(),"Could not read swept checkpoint");
            const std::vector<std::uint8_t> image((std::istreambuf_iterator<char>(in)),{});
            const auto restored=MacClosedWorld::from_checkpoint(set.registry,set.eos,image);
            check(restored.checkpoint()==image,"Cross-platform swept checkpoint bytes changed");
            const auto twin=MacClosedWorld::from_checkpoint(set.registry,set.eos,image);
            check(restored.advanced(.02).world.checkpoint()==twin.advanced(.02).world.checkpoint(),"Cross-read swept continuation differs");
        } else throw std::invalid_argument("Unknown swept checkpoint operation");
        std::cout << "PASS swept physical cross-platform checkpoint operation\n";return 0;
    }
    check(argc==1,"Invalid swept momentum test arguments");
    for (const auto& m:set.eos->reference().materials()) std::cout << "SWMATERIAL " << set.registry.index(m.id) << ' '
        << (m.phase==ReferencePhase::ideal_gas ? 0 : 1) << ' ' << m.molar_mass_kg_per_mol << ' ' << m.cv_j_per_kg_k << ' '
        << m.energy_offset_j_per_kg << ' ' << m.zero_pressure_density_kg_per_m3 << ' ' << m.reference_bulk_modulus_pa << '\n';
    int id=0;
    for (int axis=0;axis<2;++axis) for (int control=0;control<5;++control) for (const int direction:{-1,1})
        affine(id++,axis,control,direction,0);
    for (int axis=0;axis<2;++axis) affine(id++,axis,0,1,-1200);
    for (int axis=0;axis<2;++axis) affine(id++,axis,0,1,0,1);
    for (int axis=0;axis<2;++axis) for (const int direction:{-1,1}) affine(id++,axis,5,direction,0);
    invalid(); std::cout << "SWCOMPLETE " << id << '\n';
    std::cout << "PASS stagnation affine controls and original-mode defect\n";
    std::cout << "PASS actual stored affine controls, both axes and sweep signs\n";
    std::cout << "PASS kinematically consistent affine control\n";
    std::cout << "PASS below-binary64 full momentum remap\n";
    std::cout << "PASS invalid EOS and geometry reject atomically\n";
    return 0;
} catch(const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; } }
