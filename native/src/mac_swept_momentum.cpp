#include "mac_swept_momentum.hpp"
#include "mixed_atmospheres/fluid_thermodynamics.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace ma::experimental {
namespace {
constexpr auto none = std::numeric_limits<std::size_t>::max();
using Point = std::array<double, 2>;
[[noreturn]] void precision(const char* why) { throw FluidFatalError(FluidFatalCode::numerical_precision, why); }
MacScaled product(MacScaled a, MacScaled b) {
    return (b.mantissa() < 0 ? a.negated() : a).shared(b.magnitude(), MacScaled::from_double(1));
}
MacScaled sum(MacScaled a, MacScaled b) { MacScaledSum s; s.add(a); s.add(b); return s.value(); }
bool exceeds(MacScaled a, MacScaled b) { MacScaledSum s; s.add(a); s.add(b.negated()); return s.sign() > 0; }
MacScaled represented(const MacScaledSum& s) {
    const auto value = s.value();
    if (s.sign() != 0 && (value.is_zero() || (value.mantissa() < 0 ? -1 : 1) != s.sign()))
        precision("Swept MAC correction lost a nonzero sign");
    MacScaledAccount account;
    for (std::size_t k = 0; k < s.size(); ++k) account.add(s.component(k));
    account.add(value.negated());
    if (!account.within_bound()) precision("Swept MAC correction storage exceeds original operand bound");
    return value;
}
bool resolved(MacScaled before, MacScaled after, MacScaled change) {
    MacScaledSum difference; difference.add(after); difference.add(before.negated()); difference.add(change.negated());
    MacScaledSum allowance; allowance.add(change.magnitude().shifted(-47));
    for (std::size_t k=0;k<difference.size();++k)
        allowance.add(difference.sign()<0 ? difference.component(k) : difference.component(k).negated());
    return allowance.sign()>=0;
}
}

MacNodalConversion convert_mac_nodal_momentum_1d(const MacGrid& g, bool x_component,
    std::span<const double> cell_mass, std::span<const double> volumes,
    const MacMassFluxes& sweeps, std::span<const double> inertia,
    std::span<const MacScaled> original, double theta) {
    if (cell_mass.size()!=g.walls.size() || volumes.size()!=cell_mass.size() || inertia.size()!=original.size() ||
        !std::isfinite(theta) || theta<0 || theta>1)
        throw std::invalid_argument("Invalid one-dimensional nodal conversion input");
    const auto q=mac_field_values(x_component ? sweeps.x : sweeps.y,x_component ? sweeps.full_x : sweeps.full_y);
    if (q.size()!=original.size()) throw std::invalid_argument("Nodal conversion sweep/face coverage differs");
    const auto width=x_component ? g.x_width() : g.width;
    const auto free=[&](std::size_t f) {
        const auto x=f%width,y=f/width;
        if (x_component) return (x>0 || g.periodic_x) && x<g.width &&
            !g.walls[(x+g.width-1)%g.width+y*g.width] && !g.walls[x+y*g.width];
        return (y>0 || g.periodic_y) && y<g.height &&
            !g.walls[x+((y+g.height-1)%g.height)*g.width] && !g.walls[x+y*g.width];
    };
    MacNodalConversion result;result.momentum.assign(original.begin(),original.end());result.work.resize(original.size());
    std::vector<MacScaledSum> corrections(original.size());
    for (std::size_t i=0;i<cell_mass.size();++i) if (!g.walls[i]) {
        const auto x=i%g.width,y=i/g.width;
        const auto left=x_component ? g.x_face(x,y) : g.y_face(x,y);
        const auto right=x_component ? g.x_face((x+1)%g.x_width(),y) : g.y_face(x,(y+1)%g.y_height());
        MacScaledSum delta;delta.add(q[right]);delta.add(q[left].negated());
        MacScaledAccount closure;closure.add(volumes[i]);closure.add(-g.volume_m3());
        for (std::size_t k=0;k<delta.size();++k) closure.add(delta.component(k).negated());
        if (!closure.within_bound()) throw std::invalid_argument("Nodal conversion requires one-dimensional material-volume closure");
        // Fixed half dual cells retain the existing wall treatment. No
        // representation impulse crosses a rigid boundary or a solid.
        if (!free(left) || !free(right) || delta.sign()==0) continue;
        const double ratio=g.volume_m3()/volumes[i];
        if (!std::isfinite(ratio) || ratio<=0) precision("Nodal conversion volume ratio is unrepresentable");
        const auto coefficient=delta.value().shared(1,volumes[i]).multiplied(1+ratio).shifted(-3);
        const auto difference=mac_scaled_difference(original[right].shared(1,inertia[right]),original[left].shared(1,inertia[left]));
        const auto request=product(coefficient.multiplied(cell_mass[i]),difference);
        const auto impulse=request.multiplied(theta);
        corrections[left].add(impulse);corrections[right].add(impulse.negated());
        result.events.push_back({i,left,right,delta.value(),coefficient,request,impulse});
    }
    MacScaledAccount momentum;
    for (std::size_t f=0;f<original.size();++f) {
        const auto change=represented(corrections[f]);
        MacScaledSum updated;updated.add(original[f]);updated.add(change);result.momentum[f]=represented(updated);
        momentum.add(result.momentum[f]);momentum.add(original[f].negated());
        if (change.is_zero()) continue;
        if (!resolved(original[f],result.momentum[f],change)) ++result.unresolved_updates;
        MacEnergySum work;
        work.add(product(original[f].shared(1,inertia[f]),change).negated());
        work.add(MacEnergy::kinetic(inertia[f],change).negated());
        result.work[f]=represented(work);
        MacEnergyAccount energy;
        energy.add(MacEnergy::kinetic(inertia[f],result.momentum[f]));
        energy.add(MacEnergy::kinetic(inertia[f],original[f]).negated());energy.add(result.work[f]);
        if (!energy.within_bound()) precision("Nodal source conversion kinetic/work account failed");
    }
    if (!momentum.within_bound()) precision("Nodal source conversion changed total momentum");
    return result;
}

void reconstruct_mac_swept_momentum(const MacGrid& g, bool x_component,
    std::span<const double> volumes, const std::vector<MacDualFlux>& volume_edges,
    const std::vector<MacScaled>& velocity, std::vector<MacScaled>& momentum,
    MacFaceTransportReport& report, double theta, MacMomentumReconstruction reconstruction) {
    const auto width = x_component ? g.x_width() : g.width;
    const auto height = x_component ? g.height : g.y_height();
    const auto count = width * height;
    if (volumes.size() != count || volume_edges.size() != report.fluxes.size() ||
        velocity.size() != count || momentum.size() != count || !std::isfinite(theta) || theta < 0 || theta > 1 ||
        (reconstruction!=MacMomentumReconstruction::swept_physical && reconstruction!=MacMomentumReconstruction::swept_energy_flux &&
         reconstruction!=MacMomentumReconstruction::swept_nodal_1d))
        throw std::invalid_argument("Invalid swept MAC momentum geometry or correction fraction");
    const auto free = [&](std::size_t i) {
        if (i == none) return false;
        const auto x = i % width, y = i / width;
        if (x_component) return (x > 0 || g.periodic_x) && x < g.width &&
            !g.walls[(x + g.width - 1) % g.width + y * g.width] && !g.walls[x + y * g.width];
        return (y > 0 || g.periodic_y) && y < g.height &&
            !g.walls[x + ((y + g.height - 1) % g.height) * g.width] && !g.walls[x + y * g.width];
    };
    const auto neighbor = [&](std::size_t i, std::size_t axis, bool positive) {
        const auto x = i % width, y = i / width;
        const auto at = axis == 0 ? x : y, length = axis == 0 ? width : height;
        const bool periodic = axis == 0 ? g.periodic_x : g.periodic_y;
        if (!periodic && (positive ? at + 1 == length : at == 0)) return none;
        const auto next = positive ? (at + 1) % length : (at + length - 1) % length;
        const auto result = axis == 0 ? next + y * width : x + next * width;
        return free(result) ? result : none;
    };
    const auto axis_of = [&](const MacDualFlux& edge) -> std::size_t {
        return edge.left % width == edge.right % width ? 1U : 0U;
    };
    std::vector<std::array<MacScaledSum, 2>> means(count);
    std::vector<std::array<MacScaledSum, 2>> moments(count);
    std::vector<MacScaledAccount> closure(count);
    std::vector<Point> half(volume_edges.size()), centres(count);
    std::vector<bool> unresolved(count);
    const double base = g.volume_m3();
    for (std::size_t i = 0; i < count; ++i) if (free(i)) {
        closure[i].add(volumes[i]); closure[i].add(-base);
    }
    for (std::size_t k = 0; k < volume_edges.size(); ++k) {
        const auto& edge = volume_edges[k]; const auto& mass = report.fluxes[k];
        if (edge.left != mass.left || edge.right != mass.right || edge.left >= count || edge.right >= count)
            throw std::invalid_argument("Swept MAC volume and mass topology differ");
        const auto q = mac_dual_mass(edge); const auto axis = axis_of(edge);
        means[edge.left][axis].add(q.shifted(-1)); means[edge.right][axis].add(q.shifted(-1));
        closure[edge.left].add(q.negated()); closure[edge.right].add(q);
    }
    for (std::size_t k = 0; k < volume_edges.size(); ++k) {
        const auto& edge = volume_edges[k]; const auto axis = axis_of(edge);
        const auto normal = mac_dual_mass(edge).shared(1, base).shifted(-1).project();
        const auto transverse = sum(means[edge.left][1-axis].value(), means[edge.right][1-axis].value()).shared(1, base).shifted(-2).project();
        if (normal.rounded || transverse.rounded || !std::isfinite(normal.value) || !std::isfinite(transverse.value)) {
            unresolved[edge.left] = unresolved[edge.right] = true; continue;
        }
        half[k][axis] = normal.value; half[k][1-axis] = transverse.value;
        const auto q = mac_dual_mass(edge);
        for (std::size_t direction = 0; direction < 2; ++direction) {
            moments[edge.left][direction].add(q.multiplied((direction == axis ? .5 : 0) + half[k][direction]));
            moments[edge.right][direction].add(q.multiplied((direction == axis ? .5 : 0) - half[k][direction]));
        }
    }
    for (std::size_t i = 0; i < count; ++i) if (free(i)) {
        if (!closure[i].within_bound()) throw std::domain_error("Swept MAC dual source-volume closure failed");
        for (std::size_t axis = 0; axis < 2; ++axis) {
            const auto coordinate = moments[i][axis].value().shared(1, volumes[i]).project();
            if (coordinate.rounded || !std::isfinite(coordinate.value)) unresolved[i] = true;
            else centres[i][axis] = coordinate.value;
        }
    }
    report.reconstruction = reconstruction;
    report.source_centroid_offsets = centres; report.upwind_momentum = momentum;
    report.velocity_slope_x.resize(count); report.velocity_slope_y.resize(count);
    report.upwind_mixing_heat.resize(count); report.upwind_transported_kinetic.resize(count);
    std::vector<MacEnergySum> transported(count), work(count), bounds(count);
    for (std::size_t i = 0; i < count; ++i) {
        report.upwind_mixing_heat[i] = report.energies[i].mixing_heat;
        report.upwind_transported_kinetic[i] = report.energies[i].transported_kinetic;
        transported[i].add(report.energies[i].transported_kinetic);
        work[i].add(report.energies[i].remap_work_energy);
        bounds[i].add(report.energies[i].composed_energy_bound);
        if (!free(i) || unresolved[i]) continue;
        std::vector<std::pair<std::size_t, Point>> stencil;
        for (std::size_t axis = 0; axis < 2; ++axis) for (const int sign : {-1, 1}) {
            const auto next = neighbor(i, axis, sign > 0);
            if (next == none || unresolved[next]) continue;
            Point offset{centres[next][0]-centres[i][0], centres[next][1]-centres[i][1]};
            offset[axis] += sign; stencil.push_back({next, offset});
        }
        for (const int sx : {-1, 1}) for (const int sy : {-1, 1}) {
            const auto horizontal = neighbor(i, 0, sx > 0), vertical = neighbor(i, 1, sy > 0);
            if (horizontal == none || vertical == none) continue;
            const auto next = neighbor(horizontal, 1, sy > 0);
            if (next == none || next != neighbor(vertical, 0, sx > 0) || unresolved[next]) continue;
            stencil.push_back({next, Point{sx+centres[next][0]-centres[i][0], sy+centres[next][1]-centres[i][1]}});
        }
        double xx = 0, xy = 0, yy = 0;
        for (const auto& entry : stencil) { const auto& d=entry.second; xx+=d[0]*d[0]; xy+=d[0]*d[1]; yy+=d[1]*d[1]; }
        const double determinant = xx*yy-xy*xy;
        const bool rank_x=xx>0 && xy==0 && yy==0,rank_y=yy>0 && xy==0 && xx==0;
        if (!rank_x && !rank_y && (!(determinant > 128*std::numeric_limits<double>::epsilon()*xx*yy) || !std::isfinite(determinant))) {
            ++report.unresolved_momentum_corrections; continue;
        }
        MacScaledSum gx, gy;
        for (const auto& [next, d] : stencil) {
            const auto delta = mac_scaled_difference(velocity[next], velocity[i]);
            gx.add(delta.multiplied(rank_x ? d[0]/xx : rank_y ? 0 : (yy*d[0]-xy*d[1])/determinant));
            gy.add(delta.multiplied(rank_y ? d[1]/yy : rank_x ? 0 : (xx*d[1]-xy*d[0])/determinant));
        }
        auto gradx = represented(gx), grady = represented(gy);
        // Multidimensional neighbour bounds limit the physical gradient, not
        // the volume/mass flux. Uniform affine interiors retain theta=1.
        auto lower = velocity[i], upper = velocity[i];
        for (const auto& [next, d] : stencil) {
            static_cast<void>(d);
            if (exceeds(lower, velocity[next])) lower = velocity[next];
            if (exceeds(velocity[next], upper)) upper = velocity[next];
        }
        auto limiter = MacScaled::from_double(1);
        for (const auto& [next, d] : stencil) {
            static_cast<void>(next);
            const auto delta = sum(gradx.multiplied(d[0]), grady.multiplied(d[1]));
            if (delta.is_zero()) continue;
            const auto available = delta.mantissa() > 0 ? mac_scaled_difference(upper, velocity[i]) : mac_scaled_difference(velocity[i], lower);
            if (exceeds(delta.magnitude(), available)) {
                const auto ratio = available.shared(MacScaled::from_double(1), delta.magnitude());
                if (exceeds(limiter, ratio)) limiter = ratio;
            }
        }
        report.velocity_slope_x[i] = product(gradx, limiter);
        report.velocity_slope_y[i] = product(grady, limiter);
    }
    for (std::size_t k = 0; k < report.fluxes.size(); ++k) {
        const auto& edge=report.fluxes[k]; const auto flux=mac_dual_mass(edge); const auto axis=axis_of(edge);
        if (flux.is_zero() || !free(edge.left) || !free(edge.right)) continue;
        const auto donor=flux.mantissa()>0 ? edge.left : edge.right;
        const auto target=flux.mantissa()>0 ? edge.right : edge.left;
        if (unresolved[donor]) { ++report.unresolved_momentum_corrections; continue; }
        Point offset{half[k][0]-centres[donor][0], half[k][1]-centres[donor][1]};
        offset[axis] += flux.mantissa()>0 ? .5 : -.5;
        const auto delta=sum(report.velocity_slope_x[donor].multiplied(offset[0]), report.velocity_slope_y[donor].multiplied(offset[1]));
        const auto difference=mac_scaled_difference(velocity[target], velocity[donor]); MacScaled bounded;
        if (!delta.is_zero() && !difference.is_zero() && (delta.mantissa()<0)==(difference.mantissa()<0))
            bounded=exceeds(delta.magnitude(),difference.magnitude()) ? difference : delta;
        const auto request=product(flux,bounded);
        if (request.is_zero()) continue;
        MacMomentumRemapEvent event; event.dual_edge=k; event.transport_axis=axis;
        event.predictor_delta_velocity=delta; event.bounded_delta_velocity=bounded;
        event.requested_impulse=request; event.impulse=request.multiplied(theta);
        event.transported_work_limiter=MacScaled::from_double(1);
        event.before_left=momentum[edge.left]; event.before_right=momentum[edge.right];
        const auto ul=event.before_left.shared(1,report.new_inertia[edge.left]);
        const auto ur=event.before_right.shared(1,report.new_inertia[edge.right]);
        const auto requested_work=product(sum(ul,ur),event.impulse).shifted(-1);
        if (!requested_work.is_zero()) {
            const auto& available=requested_work.mantissa()>0 ? transported[edge.left] : transported[edge.right];
            auto remainder=available; remainder.add(requested_work.magnitude().negated());
            if (remainder.sign()<0) event.transported_work_limiter=represented(available).shared(
                MacScaled::from_double(1),requested_work.magnitude());
        }
        bool work_admitted=false;
        for (std::size_t attempt=0;attempt<=64;++attempt) {
            if (report.reconstruction!=MacMomentumReconstruction::swept_physical) {
                // Keep the kinematic correction at a stationary donor. Only
                // the carried energy is restricted by its nonnegative budget;
                // signed compatible internal work supplies the difference.
                event.impulse=request.multiplied(theta);
                event.energy_flux=product(requested_work,event.transported_work_limiter);
            } else {
                event.impulse=product(request.multiplied(theta),event.transported_work_limiter);
                event.energy_flux=product(sum(ul,ur),event.impulse).shifted(-1);
            }
            auto left_work=transported[edge.left],right_work=transported[edge.right];
            left_work.add(event.energy_flux.negated()); right_work.add(event.energy_flux);
            if (left_work.sign()>=0 && right_work.sign()>=0) { work_admitted=true; break; }
            if (event.transported_work_limiter.is_zero() || attempt==64) break;
            const double reduced=std::nextafter(event.transported_work_limiter.mantissa(),0.);
            event.transported_work_limiter=reduced<.5 ? MacScaled::from_parts(2*reduced,event.transported_work_limiter.exponent()-1) :
                MacScaled::from_parts(reduced,event.transported_work_limiter.exponent());
        }
        if (!work_admitted) precision("Swept MAC transported work limiter cannot resolve its original budget");
        if (report.reconstruction!=MacMomentumReconstruction::swept_physical &&
            exceeds(MacScaled::from_double(1),event.transported_work_limiter)) {
            // The same compatible law, evaluated per side. Forming rounded
            // (ul-ur)*dJ/2 first and then adding the opposite carried-energy
            // difference can erase dJ^2/(2I) at a stationary face. Keep the
            // side's work operands in one additive expansion instead.
            MacEnergySum left_work,right_work;
            left_work.add(product(ul,event.impulse));
            left_work.add(MacEnergy::kinetic(report.new_inertia[edge.left],event.impulse).negated());
            left_work.add(event.energy_flux.negated());
            right_work.add(product(ur,event.impulse).negated());
            right_work.add(MacEnergy::kinetic(report.new_inertia[edge.right],event.impulse).negated());
            right_work.add(event.energy_flux);
            event.work_change_left=represented(left_work);event.work_change_right=represented(right_work);
        } else {
            const auto linear=product(mac_scaled_difference(ul,ur),event.impulse).shifted(-1);
            event.work_change_left=sum(linear,MacEnergy::kinetic(report.new_inertia[edge.left],event.impulse).negated());
            event.work_change_right=sum(linear,MacEnergy::kinetic(report.new_inertia[edge.right],event.impulse).negated());
        }
        MacScaledSum left, right; left.add(event.before_left); left.add(event.impulse.negated());
        right.add(event.before_right); right.add(event.impulse);
        event.after_left=represented(left); event.after_right=represented(right);
        if (!resolved(event.before_left,event.after_left,event.impulse.negated())) ++report.unresolved_momentum_corrections;
        if (!resolved(event.before_right,event.after_right,event.impulse)) ++report.unresolved_momentum_corrections;
        MacScaledAccount impulse; impulse.add(event.after_left); impulse.add(event.after_right);
        impulse.add(event.before_left.negated()); impulse.add(event.before_right.negated());
        if (!impulse.within_bound()) precision("Swept MAC remap pair momentum failed");
        for (const bool is_left : {true,false}) {
            const auto i=is_left ? edge.left : edge.right;
            MacEnergyAccount account; account.add(MacEnergy::kinetic(report.new_inertia[i],is_left ? event.after_left : event.after_right));
            account.add(MacEnergy::kinetic(report.new_inertia[i],is_left ? event.before_left : event.before_right).negated());
            account.add(is_left ? event.work_change_left : event.work_change_right);
            account.add(is_left ? event.energy_flux : event.energy_flux.negated());
            if (!account.within_bound()) precision("Swept MAC remap pair compatible work failed");
            bounds[i].add(account.bound());
        }
        momentum[edge.left]=event.after_left; momentum[edge.right]=event.after_right;
        transported[edge.left].add(event.energy_flux.negated()); transported[edge.right].add(event.energy_flux);
        work[edge.left].add(event.work_change_left); work[edge.right].add(event.work_change_right);
        if (theta<1 || exceeds(MacScaled::from_double(1),event.transported_work_limiter)) ++report.limited_corrections;
        report.remap_corrections.push_back(std::move(event));
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (transported[i].sign()<0) throw MacRemapCandidateRejected("Swept MAC candidate has negative transported kinetic energy");
        report.energies[i].transported_kinetic=represented(transported[i]);
        report.energies[i].remap_work_energy=represented(work[i]);
        report.energies[i].new_kinetic=MacEnergy::kinetic(report.new_inertia[i],momentum[i]);
        // Include actual final storage operands, so cancellation does not
        // erase the inherited account or create a unit-scale tolerance.
        MacEnergyAccount storage;
        for (std::size_t j=0;j<transported[i].size();++j) storage.add(transported[i].component(j));
        storage.add(report.energies[i].transported_kinetic.negated());
        for (std::size_t j=0;j<work[i].size();++j) storage.add(work[i].component(j));
        storage.add(report.energies[i].remap_work_energy.negated());
        if (!storage.within_bound()) precision("Swept MAC work storage failed");
        bounds[i].add(storage.bound()); report.energies[i].composed_energy_bound=bounds[i].value();
    }
}
}
