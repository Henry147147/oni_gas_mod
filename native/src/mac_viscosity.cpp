#include "mixed_atmospheres/mac_viscosity.hpp"
#include "mixed_atmospheres/fluid_thermodynamics.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace ma::experimental {
namespace {
const auto one=MacScaled::from_double(1);
void require(bool value,const char* why) { if (!value) throw std::invalid_argument(why); }
[[noreturn]] void precision(const char* why) { throw FluidFatalError(FluidFatalCode::numerical_precision,why); }
MacScaled add(MacScaled a,MacScaled b) { MacScaledSum sum;sum.add(a);sum.add(b);return sum.value(); }
MacScaled multiply(MacScaled a,MacScaled b) {
    const auto value=a.shared(b.magnitude(),one);return b.mantissa()<0 ? value.negated() : value;
}
MacScaled decay(MacScaled value) {
    require(value.mantissa()>=0,"Negative viscous relaxation number");
    if (value.is_zero() || value.exponent()<=MacExponent(-53)) return value;
    if (value.exponent()>=MacExponent(7)) return one;
    return MacScaled::from_double(-std::expm1(-value.require_representable()));
}
bool resolved(MacScaled before,MacScaled after,MacScaled requested) {
    MacScaledSum residual;residual.add(after);residual.add(before.negated());residual.add(requested.negated());
    MacScaledSum allowance;allowance.add(requested.magnitude().shifted(-47));
    for (std::size_t k=0;k<residual.size();++k)
        allowance.add(residual.sign()<0 ? residual.component(k) : residual.component(k).negated());
    return allowance.sign()>=0;
}
std::array<std::vector<bool>,2> fixed_faces(const MacGrid& g) {
    std::array<std::vector<bool>,2> output;
    for (std::size_t axis=0;axis<2;++axis) {
        const bool horizontal=axis==0;const auto width=horizontal ? g.x_width() : g.width;
        const auto height=horizontal ? g.height : g.y_height();output[axis].resize(width*height);
        for (std::size_t y=0;y<height;++y) for (std::size_t x=0;x<width;++x) {
            bool left=false,right=false;
            if (horizontal) {
                if (x>0 || g.periodic_x) left=!g.walls[(x+g.width-1)%g.width+y*g.width];
                if (x<g.width) right=!g.walls[x+y*g.width];
            } else {
                if (y>0 || g.periodic_y) left=!g.walls[x+((y+g.height-1)%g.height)*g.width];
                if (y<g.height) right=!g.walls[x+y*g.width];
            }
            output[axis][x+y*width]=!left || !right;
        }
    }
    return output;
}
struct Stencil { std::size_t kind=0,location=0;MacScaled weight;std::vector<MacViscousDof> dofs;std::vector<std::size_t> cells; };
void term(Stencil& s,std::size_t axis,std::size_t face,MacScaled gradient) {
    const auto found=std::find_if(s.dofs.begin(),s.dofs.end(),[&](const auto& d) { return d.axis==axis && d.face==face; });
    if (found==s.dofs.end()) { MacViscousDof d;d.axis=axis;d.face=face;d.gradient=gradient;s.dofs.push_back(std::move(d)); }
    else found->gradient=add(found->gradient,gradient);
}
}
void MacViscosity::validate() const {
    require(std::isfinite(shear_pa_s) && shear_pa_s>=0 && std::isfinite(bulk_pa_s) && bulk_pa_s>=0,
        "Invalid physical dynamic viscosity");
}
MacViscousMode relax_mac_viscous_mode(std::span<const MacViscousDof> input,MacScaled weight,double seconds) {
    require_mac_energy_environment();require(std::isfinite(seconds) && seconds>0 && input.size()<=4 && weight.mantissa()>=0,
        "Invalid viscous mode duration, stencil or weight");
    MacViscousMode result;result.dofs.assign(input.begin(),input.end());result.weight=weight;
    MacScaledSum strain,inverse;
    for (std::size_t index=0;index<input.size();++index) {
        auto& d=result.dofs[index];d.after=d.before;
        require(d.axis<2 && std::isfinite(d.inertia) && d.inertia>=0 && (!d.fixed || d.before.is_zero()) &&
            (d.inertia>0 || d.before.is_zero()),"Invalid viscous face inertia, axis or fixed momentum");
        for (std::size_t other=0;other<index;++other)
            require(d.axis!=input[other].axis || d.face!=input[other].face,"Duplicate viscous face degree of freedom");
        if (d.fixed || d.gradient.is_zero()) continue;
        require(d.inertia>0,"Movable viscous degree of freedom has no inertia");
        strain.add(multiply(d.gradient,d.before.shared(1,d.inertia)));
        inverse.add(MacEnergy::variance(one.shared(1,d.inertia),d.gradient).shifted(1));
    }
    result.strain=strain.value();result.inverse_modal_inertia=inverse.value();
    if (weight.is_zero() || result.strain.is_zero() || result.inverse_modal_inertia.is_zero()) return result;
    const auto lambda_dt=multiply(weight,result.inverse_modal_inertia).multiplied(seconds);
    result.relaxation=decay(lambda_dt);
    result.impulse=result.strain.shared(result.relaxation,result.inverse_modal_inertia);
    result.heat=MacEnergy::variance(one.shared(one,result.inverse_modal_inertia),result.strain).shared(decay(lambda_dt.shifted(1)),one);
    MacEnergyAccount balance;std::array<MacScaledSum,2> wall;
    for (auto& d:result.dofs) {
        const auto delta=multiply(d.gradient,result.impulse).negated();
        if (d.fixed) wall[d.axis].add(delta.negated());
        else {
            d.after=add(d.before,delta);
            if (!resolved(d.before,d.after,delta)) ++result.unresolved_momentum_updates;
        }
        balance.add(MacEnergy::kinetic(d.inertia,d.after));balance.add(MacEnergy::kinetic(d.inertia,d.before).negated());
    }
    balance.add(result.heat);
    if (!balance.within_bound()) precision("Viscous mode kinetic-to-heat account exceeds its operand bound");
    result.energy_residual=balance.residual();result.energy_bound=balance.bound();
    result.wall_impulse_x=wall[0].value();result.wall_impulse_y=wall[1].value();return result;
}
MacViscousAdvance advance_mac_viscosity(const SpeciesRegistry& registry,const FluidThermodynamics& eos,
    const MacTransportState& source,double seconds,MacViscosity coefficients) {
    require_mac_energy_environment();coefficients.validate();require(std::isfinite(seconds) && seconds>0,"Invalid viscous stage duration");
    if (.5*seconds==0) precision("Viscous half interval cannot be represented");
    const auto inertia=mac_face_inertias(registry,source,eos);const auto& g=source.grid;
    const auto fixed=fixed_faces(g);
    std::array<std::vector<MacScaled>,2> momentum={mac_field_values(source.momentum_x,source.full_momentum_x),mac_field_values(source.momentum_y,source.full_momentum_y)};
    const std::array<const std::vector<double>*,2> masses={&inertia.x,&inertia.y};
    for (std::size_t axis=0;axis<2;++axis) for (std::size_t face=0;face<momentum[axis].size();++face) {
        require(!fixed[axis][face] || momentum[axis][face].is_zero(),"Fixed viscous face has nonzero momentum");
        (void)MacEnergy::kinetic((*masses[axis])[face],momentum[axis][face]);
    }
    MacViscousAdvance result{source,{}};
    if (coefficients.shear_pa_s==0 && coefficients.bulk_pa_s==0) return result;
    const auto gradient=one.shared(1,g.spacing_m);const auto negative=gradient.negated();
    const auto shear_weight=MacScaled::from_double(coefficients.shear_pa_s).multiplied(g.volume_m3());
    MacScaledSum div_weight;div_weight.add(shear_weight.shared(1,3));
    div_weight.add(MacScaled::from_double(coefficients.bulk_pa_s).multiplied(g.volume_m3()));
    std::vector<Stencil> stencils;
    for (std::size_t cell=0;cell<source.cells.size();++cell) if (!g.walls[cell]) {
        const auto x=cell%g.width,y=cell/g.width;
        for (std::size_t kind=0;kind<2;++kind) {
            Stencil s;s.kind=kind;s.location=cell;s.cells={cell};s.weight=kind==0 ? shear_weight : div_weight.value();
            if (s.weight.is_zero()) continue;
            term(s,0,g.x_face(x,y),negative);term(s,0,g.x_face((x+1)%g.x_width(),y),gradient);
            term(s,1,g.y_face(x,y),kind==0 ? gradient : negative);
            term(s,1,g.y_face(x,(y+1)%g.y_height()),kind==0 ? negative : gradient);
            stencils.push_back(std::move(s));
        }
    }
    if (!shear_weight.is_zero()) for (std::size_t y=g.periodic_y ? 0 : 1;y<g.height;++y)
        for (std::size_t x=g.periodic_x ? 0 : 1;x<g.width;++x) {
            const auto left=(x+g.width-1)%g.width,bottom=(y+g.height-1)%g.height;
            std::vector<std::size_t> cells={left+bottom*g.width,x+bottom*g.width,left+y*g.width,x+y*g.width};
            if (std::any_of(cells.begin(),cells.end(),[&](auto cell) { return g.walls[cell]!=0; })) continue;
            Stencil s;s.kind=2;s.location=x+y*g.width;s.weight=shear_weight;s.cells=std::move(cells);
            term(s,0,g.x_face(x,bottom),negative);term(s,0,g.x_face(x,y),gradient);
            term(s,1,g.y_face(left,y),negative);term(s,1,g.y_face(x,y),gradient);
            stencils.push_back(std::move(s));
        }
    MacEnergySum heat,entropy;std::array<MacScaledSum,2> wall;
    for (std::size_t sweep=0;sweep<2;++sweep) for (std::size_t index=0;index<stencils.size();++index) {
        auto s=stencils[sweep==0 ? index : stencils.size()-1-index];
        for (auto& d:s.dofs) { d.inertia=(*masses[d.axis])[d.face];d.fixed=fixed[d.axis][d.face];d.before=momentum[d.axis][d.face]; }
        auto mode=relax_mac_viscous_mode(s.dofs,s.weight,.5*seconds);++result.report.modes;
        if (mode.impulse.is_zero()) continue;
        for (const auto& d:mode.dofs) momentum[d.axis][d.face]=d.after;
        heat.add(mode.heat);wall[0].add(mode.wall_impulse_x);wall[1].add(mode.wall_impulse_y);
        result.report.unresolved_momentum_updates+=mode.unresolved_momentum_updates;
        const auto requested=mode.heat.shared(1,static_cast<double>(s.cells.size()));
        for (const auto cell:s.cells) {
            auto& mixture=result.state.cells[cell].mixture;const double before=mixture.energy;
            mixture.energy=add(MacEnergy::from_double(before),requested).project().value;
            if (!resolved(MacEnergy::from_double(before),MacEnergy::from_double(mixture.energy),requested)) ++result.report.unresolved_heat_updates;
            const auto after=eos.evaluate(mixture,g.volume_m3());
            MacEnergySum stored;stored.add(mixture.energy);stored.add(-before);
            require(stored.sign()>=0,"Viscous heating decreased stored internal energy");
            entropy.add(stored.value().shared(1,after.temperature_k));
        }
        result.report.events.push_back({s.kind,s.location,sweep,std::move(s.cells),std::move(mode)});
    }
    store_mac_field(result.state.momentum_x,result.state.full_momentum_x,momentum[0]);
    store_mac_field(result.state.momentum_y,result.state.full_momentum_y,momentum[1]);
    MacEnergyAccount balance;
    for (std::size_t cell=0;cell<source.cells.size();++cell) if (!g.walls[cell]) {
        balance.add(result.state.cells[cell].mixture.energy);balance.add(-source.cells[cell].mixture.energy);
    }
    const std::array<std::vector<MacScaled>,2> old={mac_field_values(source.momentum_x,source.full_momentum_x),mac_field_values(source.momentum_y,source.full_momentum_y)};
    for (std::size_t axis=0;axis<2;++axis) {
        MacScaledAccount impulse;impulse.add(wall[axis].value().negated());
        for (std::size_t face=0;face<old[axis].size();++face) {
            balance.add(MacEnergy::kinetic((*masses[axis])[face],momentum[axis][face]));
            balance.add(MacEnergy::kinetic((*masses[axis])[face],old[axis][face]).negated());
            impulse.add(momentum[axis][face]);impulse.add(old[axis][face].negated());
        }
        // Each assembled stress has equal/opposite traction operands on this
        // axis, including constrained faces. Their exact sum is zero. Retain
        // those operands in the rounding account: a transverse component can
        // start and finish near zero after substantial cancelling stresses.
        // An account scaled only by its tiny final J would omit the operations
        // that actually produced it. The same 128-epsilon rule still applies.
        for (const auto& event:result.report.events) for (const auto& d:event.mode.dofs)
            if (d.axis==axis) impulse.add(multiply(d.gradient,event.mode.impulse));
        if (!impulse.within_bound()) precision("Viscous global momentum/wall account exceeds its operand bound");
        (axis==0 ? result.report.momentum_residual_x : result.report.momentum_residual_y)=impulse.residual();
        (axis==0 ? result.report.momentum_bound_x : result.report.momentum_bound_y)=impulse.bound();
    }
    if (!balance.within_bound()) precision("Viscous stored U+K account exceeds its operand bound");
    result.report.heat=heat.value();result.report.energy_residual=balance.residual();result.report.energy_bound=balance.bound();
    result.report.wall_impulse_x=wall[0].value();result.report.wall_impulse_y=wall[1].value();
    result.report.entropy_lower_bound_j_per_k=entropy.value().project().value;return result;
}
}
