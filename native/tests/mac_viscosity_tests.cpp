#include "mac_fixture.hpp"
#include "mixed_atmospheres/mac_viscosity.hpp"
#include <iomanip>
#include <numbers>
#include <bit>

namespace {
using namespace ma;using namespace ma::experimental;
void require(bool condition,const char* why) { if (!condition) throw std::runtime_error(why); }
void close(double actual,double expected,double tolerance=2e-13) {
    require(std::isfinite(actual) && std::abs(actual-expected)<=tolerance*std::max(std::abs(actual),std::abs(expected)),
        "Viscous control differs from independent arithmetic");
}
template<class Action> void rejects(Action action) {
    bool rejected=false;try { action(); } catch (const std::exception&) { rejected=true; }
    require(rejected,"Invalid viscous operation was accepted");
}
MacViscousDof dof(std::size_t axis,std::size_t face,double inertia,double gradient,double momentum,bool fixed=false) {
    MacViscousDof value;value.axis=axis;value.face=face;value.inertia=inertia;value.gradient=MacScaled::from_double(gradient);
    value.before=MacScaled::from_double(momentum);value.fixed=fixed;return value;
}
void mode_controls() {
    for (double duration:{1e-8,.7,1e300}) {
        const std::array<MacViscousDof,2> input={dof(0,0,2,-1,4),dof(0,1,3,1,-3)};
        const auto result=relax_mac_viscous_mode(input,MacScaled::from_double(.5),duration);
        const long double inverse=1.L/2+1.L/3;
        const long double relaxation=-std::expm1(-.5L*inverse*duration);
        const long double impulse=-3/inverse*relaxation;
        close(result.impulse.require_representable(),static_cast<double>(impulse));
        close(result.dofs[0].after.require_representable(),static_cast<double>(4+impulse));
        close(result.dofs[1].after.require_representable(),static_cast<double>(-3-impulse));
        close(result.heat.require_representable(),static_cast<double>(.5L*9/inverse*(-std::expm1(-inverse*duration))));
        require(result.wall_impulse_x.is_zero() && result.wall_impulse_y.is_zero(),"Internal viscous mode invented wall impulse");
        const auto zero=relax_mac_viscous_mode(input,{},duration);
        require(zero.heat.is_zero() && zero.dofs[0].after.require_representable()==4,"Zero viscosity changed momentum");
        std::cout<<"VMODE "<<duration<<' '<<result.impulse.require_representable()<<' '
            <<result.heat.require_representable()<<' '<<result.dofs[0].after.require_representable()<<' '
            <<result.dofs[1].after.require_representable()<<'\n';
    }
    const std::array<MacViscousDof,4> rotation={dof(0,0,1,-1,0),dof(0,1,1,1,-2),dof(1,0,1,-1,0),dof(1,1,1,1,2)};
    const auto unchanged=relax_mac_viscous_mode(rotation,MacScaled::from_double(10),1);
    require(unchanged.strain.is_zero() && unchanged.impulse.is_zero() && unchanged.heat.is_zero(),"Rigid rotation was viscously damped");
    const std::array<MacViscousDof,2> wall={dof(0,0,2,-1,0,true),dof(0,1,3,1,6)};
    const auto stopped=relax_mac_viscous_mode(wall,MacScaled::from_double(3),1e300);
    require(stopped.dofs[0].after.is_zero() && stopped.dofs[1].after.is_zero(),"Fixed wall did not stop its normal mode");
    close(stopped.wall_impulse_x.require_representable(),-6);close(stopped.heat.require_representable(),6);
    for (const int exponent:{-80,-1100}) {
        auto tiny=wall;tiny[1].before=MacScaled::from_parts(.5,MacExponent(exponent));
        const auto relaxed=relax_mac_viscous_mode(tiny,MacScaled::from_double(3),.25);
        require(!relaxed.impulse.is_zero() && !relaxed.heat.is_zero(),"Positive below-range viscosity was erased");
        close(relaxed.dofs[1].after.shared(MacScaled::from_double(1),tiny[1].before).require_representable(),std::exp(-.25));
    }
    rejects([&] { (void)relax_mac_viscous_mode(wall,MacScaled::from_double(-1),1); });
    rejects([&] { (void)relax_mac_viscous_mode(wall,MacScaled::from_double(1),0); });
    auto duplicate=wall;duplicate[1].face=0;rejects([&] { (void)relax_mac_viscous_mode(duplicate,MacScaled::from_double(1),1); });
    auto moving_wall=wall;moving_wall[0].before=MacScaled::from_double(1);rejects([&] { (void)relax_mac_viscous_mode(moving_wall,MacScaled::from_double(1),1); });
    std::cout<<"PASS exact viscous modes, rigid rotation, stiff wall, range and invalid controls\n";
}
MacTransportState shear(bool wet=false,bool horizontal=true,bool periodic=true) {
    const auto& setup=testing::setup();auto s=setup.state(4,8,periodic,wet);
    // One fixed mixture so all face inertias are uniform. Positive traces and
    // separately owned passive records remain in every fluid cell.
    for (auto& cell:s.cells) cell.mixture=setup.mixture(1,1e5,300,wet ? .25 : 0);
    const auto inertia=mac_face_inertias(setup.registry,s,*setup.eos);
    if (horizontal) for (std::size_t y=0;y<s.grid.height;++y) for (std::size_t x=0;x<s.grid.x_width();++x) {
        if (!periodic && (x==0 || x==s.grid.width)) continue;
        s.momentum_x[s.grid.x_face(x,y)]=inertia.x[s.grid.x_face(x,y)]*std::cos(2*std::numbers::pi*(static_cast<double>(y)+.5)/8);
    }
    else for (std::size_t y=0;y<s.grid.y_height();++y) for (std::size_t x=0;x<s.grid.width;++x) {
        if (!periodic && (y==0 || y==s.grid.height)) continue;
        s.momentum_y[s.grid.y_face(x,y)]=inertia.y[s.grid.y_face(x,y)]*std::cos(2*std::numbers::pi*(static_cast<double>(x)+.5)/4);
    }
    return s;
}
double kinetic(const MacTransportState& state) {
    const auto& setup=testing::setup();const auto inertia=mac_face_inertias(setup.registry,state,*setup.eos);MacEnergySum sum;
    const auto x=mac_field_values(state.momentum_x,state.full_momentum_x),y=mac_field_values(state.momentum_y,state.full_momentum_y);
    for (std::size_t i=0;i<x.size();++i) sum.add(MacEnergy::kinetic(inertia.x[i],x[i]));
    for (std::size_t i=0;i<y.size();++i) sum.add(MacEnergy::kinetic(inertia.y[i],y[i]));
    return sum.value().require_representable();
}
void identities(const MacTransportState& source,const MacViscousAdvance& result) {
    const auto& setup=testing::setup();MacEnergyAccount balance;
    for (std::size_t cell=0;cell<source.cells.size();++cell) {
        const auto& a=source.cells[cell];const auto& b=result.state.cells[cell];
        require(a.mixture.masses==b.mixture.masses && a.passive_quantities.size()==b.passive_quantities.size(),"Viscosity changed constituent inventory");
        for (std::size_t k=0;k<a.passive_quantities.size();++k)
            require(a.passive_quantities[k].identity==b.passive_quantities[k].identity && a.passive_quantities[k].quantity==b.passive_quantities[k].quantity,"Viscosity changed passive metadata");
        if (!source.grid.walls[cell]) {
            balance.add(b.mixture.energy);balance.add(-a.mixture.energy);
            require(b.mixture.energy>=a.mixture.energy && setup.eos->evaluate(b.mixture,1).temperature_k>=300-1e-10,"Viscous heat cooled a cell");
        }
    }
    balance.add(kinetic(result.state));balance.add(-kinetic(source));
    require(balance.within_bound(),"Independent viscosity stored U+K account failed");
}
void stage_controls() {
    const auto& setup=testing::setup();int id=0;
    for (const bool wet:{false,true}) for (const bool horizontal:{false,true}) for (const bool periodic:{false,true}) {
        const auto source=shear(wet,horizontal,periodic);const auto held=testing::fingerprint(source);
        const auto result=advance_mac_viscosity(setup.registry,*setup.eos,source,.75,{.4,.2});identities(source,result);
        require(kinetic(result.state)<kinetic(source) && result.report.heat.mantissa()>0 && result.report.entropy_lower_bound_j_per_k>0,
            "Viscous stage did not relax motion into stored heat");
        require(testing::fingerprint(source)==held && !result.report.events.empty(),"Viscous stage changed its immutable source");
        std::cout<<"VSTAGE "<<id++<<' '<<wet<<' '<<horizontal<<' '<<periodic<<' '<<kinetic(source)<<' '<<kinetic(result.state)<<' '
            <<result.report.heat.require_representable()<<' '<<result.report.entropy_lower_bound_j_per_k<<'\n';
        const auto zero=advance_mac_viscosity(setup.registry,*setup.eos,source,1,{});
        require(testing::fingerprint(zero.state)==held && zero.report.events.empty(),"Zero viscosity changed physical fields");
    }
    auto still=setup.state(4,3,true,true);const auto held=testing::fingerprint(still);
    require(testing::fingerprint(advance_mac_viscosity(setup.registry,*setup.eos,still,1,{1,1}).state)==held,"At-rest viscosity changed a field");
    auto source=shear();const auto bulk=advance_mac_viscosity(setup.registry,*setup.eos,source,1,{0,1});
    require(testing::fingerprint(source)==testing::fingerprint(bulk.state),"Pure divergence viscosity damped incompressible shear");
    source.momentum_x[0]=std::numeric_limits<double>::quiet_NaN();rejects([&] { (void)advance_mac_viscosity(setup.registry,*setup.eos,source,1,{1,1}); });
    for (const MacViscosity bad: {MacViscosity{-1,0},MacViscosity{0,std::numeric_limits<double>::infinity()}})
        rejects([&] { (void)advance_mac_viscosity(setup.registry,*setup.eos,still,1,bad); });
    auto liquid=setup.state(3,2,false);
    for (auto& cell:liquid.cells) cell.mixture=setup.mixture(1,1e5,300,1);
    const auto liquid_inertia=mac_face_inertias(setup.registry,liquid,*setup.eos);
    liquid.momentum_x[liquid.grid.x_face(1,0)]=liquid_inertia.x[liquid.grid.x_face(1,0)]*.5;
    const auto liquid_after=advance_mac_viscosity(setup.registry,*setup.eos,liquid,.75,{.4,.2});identities(liquid,liquid_after);
    require(liquid_after.report.heat.mantissa()>0 && liquid_after.report.stored_heat.mantissa()>0,"Pure liquid did not store dissipated heat");
    auto obstacle=setup.state(4,3,false,false,true);const auto obstacle_inertia=mac_face_inertias(setup.registry,obstacle,*setup.eos);
    obstacle.momentum_x[obstacle.grid.x_face(2,0)]=obstacle_inertia.x[obstacle.grid.x_face(2,0)]*.5;
    const auto obstacle_after=advance_mac_viscosity(setup.registry,*setup.eos,obstacle,.75,{.4,.2});identities(obstacle,obstacle_after);
    require(obstacle_after.report.heat.mantissa()>0 && obstacle_after.state.cells[5].mixture.energy==0,"Interior wall changed or suppressed all physical viscosity");
    auto hot=setup.state(3,2,false);
    for (auto& cell:hot.cells) cell.mixture=setup.mixture(1,1e5,1e6-1e-5);
    const auto hot_inertia=mac_face_inertias(setup.registry,hot,*setup.eos);
    hot.momentum_x[hot.grid.x_face(1,0)]=hot_inertia.x[hot.grid.x_face(1,0)]*1000;
    const auto hot_held=testing::fingerprint(hot);
    rejects([&] { (void)advance_mac_viscosity(setup.registry,*setup.eos,hot,1,{1e100,0}); });
    require(testing::fingerprint(hot)==hot_held,"Late viscous EOS rejection changed source fields");
    std::cout<<"PASS gas, wet, two axes, closed walls, periodic, zero and immutable invalid viscous stages\n";
}
void refinement() {
    const auto& setup=testing::setup();const auto initial=shear();const auto inertia=mac_face_inertias(setup.registry,initial,*setup.eos);
    const double density=initial.cells[0].mixture.mass();const double viscosity=.25*density;constexpr double duration=.5;
    const double decay_rate=.25*4*std::pow(std::sin(std::numbers::pi/8),2);double previous=0;
    for (const int steps:{4,8,16}) {
        auto state=initial;for (int n=0;n<steps;++n) state=advance_mac_viscosity(setup.registry,*setup.eos,state,duration/steps,{viscosity,0}).state;
        double error=0;
        for (std::size_t face=0;face<state.momentum_x.size();++face)
            error=std::max(error,std::abs(state.momentum_x[face]-initial.momentum_x[face]*std::exp(-decay_rate*duration))/inertia.x[face]);
        for (const auto value:state.momentum_y) error=std::max(error,std::abs(value)/density);
        require(error>0 && (!previous || error<.3*previous),"Viscous split did not refine to the independent discrete shear solution");
        std::cout<<"VREFINE "<<steps<<' '<<error<<'\n';previous=error;
    }
    std::cout<<"PASS independent Fourier shear refinement\n";
}
void owner_controls() {
    const auto& setup=testing::setup();
    for (const bool wet:{false,true}) {
        auto state=setup.state(3,2,false,wet);const auto inertia=mac_face_inertias(setup.registry,state,*setup.eos);
        for (std::size_t y=0;y<2;++y) for (std::size_t x=1;x<3;++x)
            state.momentum_x[state.grid.x_face(x,y)]=inertia.x[state.grid.x_face(x,y)]*(y==0 ? .5 : -.5);
        MacClosedSettings settings;settings.viscosity={.5,.25};settings.thermal_conduction=true;
        const MacClosedWorld source(setup.registry,setup.eos,state,settings);const auto original=source.checkpoint();
        require(original[8]==3,"Viscous physical owner did not select checkpoint version 3");
        const auto restored=MacClosedWorld::from_checkpoint(setup.registry,setup.eos,original);
        require(restored.checkpoint()==original && restored.settings().viscosity.shear_pa_s==.5 && restored.settings().viscosity.bulk_pa_s==.25,
            "Viscous zero-time checkpoint differs");
        const auto advanced=source.advanced(.02),continued=restored.advanced(.02);
        require(advanced.world.checkpoint()==continued.world.checkpoint() && source.checkpoint()==original &&
            advanced.report.viscosity_stages==2*advanced.report.substeps && advanced.report.viscosity_modes>0 &&
            advanced.report.viscous_heat.mantissa()>0 && advanced.report.viscous_entropy_lower_bound_j_per_k>0,
            "Viscous complete owner did not continue exactly with positive worker progress");
        auto disabled=settings;disabled.viscosity={};
        const auto legacy=MacClosedWorld(setup.registry,setup.eos,state,disabled).checkpoint();require(legacy[8]==2,"Disabled viscosity changed legacy checkpoint format");
        require(MacClosedWorld::from_checkpoint(setup.registry,setup.eos,legacy).checkpoint()==legacy,"Legacy checkpoint no longer restores exactly");
        const std::uint32_t identity=static_cast<std::uint32_t>(original[12]) | static_cast<std::uint32_t>(original[13])<<8 |
            static_cast<std::uint32_t>(original[14])<<16 | static_cast<std::uint32_t>(original[15])<<24;
        const auto viscosity_offset=static_cast<std::size_t>(16+identity+16+26+45);
        require(original.size()>viscosity_offset+16,"Viscous settings offset exceeds checkpoint");
        const auto seal=[](std::vector<std::uint8_t>& image) {
            std::uint64_t checksum=UINT64_C(14695981039346656037);
            for (std::size_t k=0;k<image.size()-8;++k) checksum=(checksum^image[k])*UINT64_C(1099511628211);
            for (std::size_t k=0;k<8;++k) image[image.size()-8+k]=static_cast<std::uint8_t>(checksum>>(8*k));
        };
        for (int variant=0;variant<4;++variant) {
            auto bad=original;
            if (variant==0 || variant==1) {
                const auto bits=std::bit_cast<std::uint64_t>(variant==0 ? -1. : std::numeric_limits<double>::infinity());
                for (std::size_t k=0;k<8;++k) bad[viscosity_offset+k]=static_cast<std::uint8_t>(bits>>(8*k));
            }
            if (variant==2) std::fill(bad.begin()+static_cast<std::ptrdiff_t>(viscosity_offset),bad.begin()+static_cast<std::ptrdiff_t>(viscosity_offset+16),0);
            if (variant==3) bad[8]=4;
            seal(bad);rejects([&] { (void)MacClosedWorld::from_checkpoint(setup.registry,setup.eos,bad); });
        }
    }
    std::cout<<"PASS gas and wet complete viscous owner, exact v3 continuation, unchanged v2 and semantic rejections\n";
}
void saved_controls() {
    const auto& setup=testing::setup();
    for (const std::string id:{"nitrogen","oxygen","helium","water"}) {
        const auto& s=setup.registry[setup.registry.index(id)];
        std::cout<<"VMATERIAL "<<id<<' '<<s.cv()<<' '<<s.internal_energy(0)<<' '<<s.molar_mass<<'\n';
    }
    for (int id=0;id<2;++id) {
        auto source=setup.state(2,2,false,id==1);const auto inertia=mac_face_inertias(setup.registry,source,*setup.eos);
        source.momentum_x[source.grid.x_face(1,0)]=inertia.x[source.grid.x_face(1,0)]*.75;
        source.momentum_x[source.grid.x_face(1,1)]=inertia.x[source.grid.x_face(1,1)]*-.5;
        source.momentum_y[source.grid.y_face(0,1)]=inertia.y[source.grid.y_face(0,1)]*.25;
        source.momentum_y[source.grid.y_face(1,1)]=inertia.y[source.grid.y_face(1,1)]*-.125;
        const auto after=advance_mac_viscosity(setup.registry,*setup.eos,source,.75,{.4,.2});identities(source,after);
        std::cout<<"VCASE "<<id<<" 0.75 0.4 0.2 "<<after.report.heat.require_representable()<<' '
            <<after.report.entropy_lower_bound_j_per_k<<' '<<after.report.wall_impulse_x.project().value<<' '
            <<after.report.wall_impulse_y.project().value<<'\n';
        for (int endpoint=0;endpoint<2;++endpoint) {
            const auto& state=endpoint==0 ? source : after.state;
            for (std::size_t i=0;i<state.cells.size();++i) {
                const auto& cell=state.cells[i];const auto reading=setup.eos->evaluate(cell.mixture,1);
                std::cout<<"VCELL "<<id<<' '<<endpoint<<' '<<i<<' '<<cell.mixture.energy<<' '<<reading.temperature_k<<' '
                    <<reading.mechanical_pressure_pa<<' '<<reading.gas_volume_m3<<' '<<reading.liquid_volume_m3<<'\n';
                for (std::size_t k=0;k<cell.mixture.masses.size();++k) if (cell.mixture.masses[k]!=0)
                    std::cout<<"VMASS "<<id<<' '<<endpoint<<' '<<i<<' '<<setup.registry[k].id<<' '<<cell.mixture.masses[k]<<'\n';
                for (const auto& passive:cell.passive_quantities)
                    std::cout<<"VPASSIVE "<<id<<' '<<endpoint<<' '<<i<<' '<<passive.identity<<' '<<passive.quantity<<'\n';
            }
            const std::array<std::vector<MacScaled>,2> momentum={mac_field_values(state.momentum_x,state.full_momentum_x),mac_field_values(state.momentum_y,state.full_momentum_y)};
            for (std::size_t axis=0;axis<2;++axis) for (std::size_t face=0;face<momentum[axis].size();++face)
                std::cout<<"VFACE "<<id<<' '<<endpoint<<' '<<axis<<' '<<face<<' '<<(axis==0 ? inertia.x : inertia.y)[face]<<' '
                    <<momentum[axis][face].mantissa()<<' '<<momentum[axis][face].exponent()<<'\n';
        }
        for (std::size_t e=0;e<after.report.events.size();++e) {
            const auto& event=after.report.events[e];const auto& mode=event.mode;
            std::cout<<"VEVENT "<<id<<' '<<e<<' '<<event.kind<<' '<<event.location<<' '<<event.sweep<<' '<<mode.weight.require_representable()<<' '
                <<mode.strain.project().value<<' '<<mode.inverse_modal_inertia.project().value<<' '<<mode.impulse.project().value<<' '
                <<mode.heat.project().value<<' '<<mode.wall_impulse_x.project().value<<' '<<mode.wall_impulse_y.project().value<<'\n';
            for (std::size_t k=0;k<mode.dofs.size();++k) {
                const auto& d=mode.dofs[k];std::cout<<"VDOF "<<id<<' '<<e<<' '<<k<<' '<<d.axis<<' '<<d.face<<' '<<d.fixed<<' '
                    <<d.inertia<<' '<<d.gradient.project().value<<' '<<d.before.mantissa()<<' '<<d.before.exponent()<<' '
                    <<d.after.mantissa()<<' '<<d.after.exponent()<<'\n';
            }
            for (const auto cell:event.heated_cells) std::cout<<"VHEATED "<<id<<' '<<e<<' '<<cell<<'\n';
        }
    }
    std::cout<<"PASS physical viscosity saved controls complete\n";
}
}
int main() {
    try { std::cout<<std::setprecision(17);mode_controls();stage_controls();refinement();owner_controls();saved_controls();return 0; }
    catch (const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
