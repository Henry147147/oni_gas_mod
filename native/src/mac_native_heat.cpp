#include "mixed_atmospheres/mac_native_heat.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace ma::experimental {
namespace {
bool contact_limit(const ReferenceError& error) {
    return error.code()==ReferenceErrorCode::insufficient_thermal_energy || error.code()==ReferenceErrorCode::temperature_outside_model ||
        error.code()==ReferenceErrorCode::compression_outside_model || error.code()==ReferenceErrorCode::numerical_range;
}
}
MacNativeHeatExchange exchange_mac_native_heat(const MacClosedWorld& source,std::size_t cell,
    MacNativeHeatBody body,double conductance,double seconds) {
    require_mac_energy_environment();
    if (cell>=source.state().cells.size() || source.state().grid.walls[cell] ||
        !std::isfinite(body.temperature_k) || body.temperature_k<1 || body.temperature_k>1e6 ||
        !std::isfinite(body.heat_capacity_kj_per_k) || body.heat_capacity_kj_per_k<0 ||
        !std::isfinite(conductance) || conductance<0 || !std::isfinite(seconds) || seconds<0 || seconds>1)
        throw std::invalid_argument("Invalid native body/fluid heat contact");
    MacNativeHeatExchange result{source,body.temperature_k,{}};
    const auto& mixture=source.state().cells[cell].mixture;
    const auto volume=mixture.mass()>0 ? mac_material_volume(source.state(),cell) : source.state().grid.volume_m3();
    const auto initial=source.thermodynamics().evaluate(mixture,volume);
    const double capacity=body.heat_capacity_kj_per_k*1000;
    if (!std::isfinite(capacity)) throw std::overflow_error("Native body heat capacity exceeds its joule range");
    const double duration_conductance=seconds*conductance;
    if (capacity==0 || duration_conductance==0 || mixture.mass()==0 || body.temperature_k==initial.temperature_k) return result;
    if (!std::isfinite(duration_conductance)) throw std::overflow_error("Native heat interval/conductance exceeds its range");
    const double direction=body.temperature_k>initial.temperature_k ? 1 : -1;
    const auto temperature=[&](double magnitude) {
        auto changed=source.state().cells[cell];
        (void)add_mac_cell_internal_energy(changed,MacEnergy::from_double(direction*magnitude));
        return source.thermodynamics().evaluate(changed.mixture,volume).temperature_k;
    };
    double lower=0,upper=2*std::min(capacity,duration_conductance)*std::abs(body.temperature_k-initial.temperature_k);
    if (!std::isfinite(upper)) throw std::overflow_error("Native heat bracket exceeds its range");
    if (!(initial.has_gas && initial.liquid_volume_m3>0)) {
        // The declared single-phase profiles have constant Cv at fixed cell
        // volume. Solve their contact analytically, avoiding EOS energy-lattice
        // noise inside a root whose thermal reference can be much larger.
        const long double fluid_capacity=initial.equilibrium_heat_capacity_at_cell_volume_j_per_k;
        const long double inverse=1/static_cast<long double>(capacity)+1/fluid_capacity+1/static_cast<long double>(duration_conductance);
        lower=static_cast<double>(std::abs(static_cast<long double>(body.temperature_k)-initial.temperature_k)/inverse);
    } else {
    bool valid_upper=false;
    try {
        const auto fluid=temperature(upper);
        valid_upper=upper/duration_conductance-direction*(body.temperature_k-direction*upper/capacity-fluid)>=0;
    } catch (const ReferenceError& error) {
        if (!contact_limit(error)) throw;
    } catch (const FluidFatalError& error) {
        if (error.code()!=FluidFatalCode::numerical_precision) throw;
    }
    for (std::size_t i=0;i<128;++i) {
        const double middle=lower+(upper-lower)*.5;
        if (middle==lower || middle==upper) break;
        ++result.report.iterations;
        double fluid_temperature{};
        try {fluid_temperature=temperature(middle);}
        catch (const ReferenceError& error) {if (!contact_limit(error)) throw;upper=middle;continue;}
        catch (const FluidFatalError& error) {if (error.code()!=FluidFatalCode::numerical_precision) throw;upper=middle;continue;}
        const double after_body=body.temperature_k-direction*(middle/capacity);
        // This scaled residual remains finite for very stiff contacts.
        const double residual=middle/duration_conductance-direction*(after_body-fluid_temperature);
        if (residual<0) lower=middle;
        else {upper=middle;valid_upper=true;}
    }
    if (!valid_upper) throw std::invalid_argument("Native heat equilibrium lies outside the fluid EOS range");
    }
    result.report.requested_heat_j=direction*lower;
    const double unrounded=body.temperature_k-direction*(lower/capacity);
    float after=static_cast<float>(unrounded);
    double represented=capacity*(static_cast<double>(body.temperature_k)-after);
    while (direction*represented>lower) {
        after=std::nextafter(after,body.temperature_k);
        represented=capacity*(static_cast<double>(body.temperature_k)-after);
    }
    if (after==body.temperature_k || represented==0) {result.report.unresolved=true;return result;}
    const MacCellHeat input{cell,represented,1e6};
    auto changed=source.with_cell_heat(std::span(&input,1),&result.report.primary_energy);
    const double stored=mac_cell_internal_energy_change(source.state().cells[cell],changed.state().cells[cell]).value().project().value;
    if (stored==0) {result.report.unresolved=true;return result;}
    const auto final=source.thermodynamics().evaluate(changed.state().cells[cell].mixture,volume);
    const double ordering=direction*(static_cast<double>(after)-final.temperature_k);
    if (direction*stored<0 || ordering<0) {result.report.unresolved=true;return result;}
    MacEnergyAccount balance;account_mac_cell_internal_energy(balance,source.state().cells[cell]);account_mac_cell_internal_energy(balance,changed.state().cells[cell],-1);
    balance.add(MacEnergy::from_double(capacity).multiplied(static_cast<double>(body.temperature_k)-after));
    if (!balance.within_bound()) throw FluidFatalError(FluidFatalCode::numerical_precision,"Native body/fluid heat failed its closed energy account");
    result.report.body_heat_loss_j=represented;result.report.stored_fluid_heat_j=stored;
    result.report.energy_residual_j=balance.residual().require_representable();
    result.report.energy_bound_j=balance.bound().require_representable();
    const double hot=direction>0 ? after : final.temperature_k;
    const double cold=direction>0 ? final.temperature_k : after;
    result.report.entropy_lower_bound_j_per_k=std::min(std::abs(represented),std::abs(stored))*((hot-cold)/cold/hot);
    result.world=std::move(changed);result.body_temperature_k=after;return result;
}
}
