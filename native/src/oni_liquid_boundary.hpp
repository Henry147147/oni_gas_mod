#pragma once
#include "mac_game_bridge.hpp"
#include "mac_game/liquid_boundary.hpp"
#include "mac_kernel_detail.hpp"

namespace ma::oni::colony::detail {
struct LiquidBoundaryAdvance {
    ma::experimental::MacClosedAdvance advance;
    std::size_t retries = 0, added_faces = 0;
};

// Owner law 12. Every failed candidate is discarded in full. Only the masks
// grow across retries; each pressure/work solve starts from the same source.
inline LiquidBoundaryAdvance advance_liquid_boundary(
    const ma::experimental::MacClosedWorld& source, double seconds,
    std::size_t budget, std::span<const std::uint8_t> blocked) {
    using namespace ma::experimental;
    const auto check=[](bool ok,const char* why) { if(!ok)throw std::invalid_argument(why); };
    const auto& state=source.state();const auto& grid=state.grid;
    check(source.settings().numerical_kernel==MacNumericalKernel::game,
        "Liquid boundary law needs the game kernel");
    check(blocked.size()==state.cells.size() &&
        std::all_of(blocked.begin(),blocked.end(),[](auto value){return value<=1;}),
        "Invalid owner liquid boundary marks");
    check(!grid.periodic_x && !grid.periodic_y,"Owner liquid boundaries are nonperiodic");
    const auto has_marks=std::any_of(blocked.begin(),blocked.end(),[](auto value){return value!=0;});
    if(!has_marks) {
        auto open=grid.closed_x.empty() && grid.closed_y.empty()?source:source.with_closed_faces({},{});
        return {open.advanced(seconds,budget,MacExecutionPolicy::production),0,0};
    }
    const mac_kernel::Grid faces(grid);
    // Only cells incident to a marked face need a composition scan. A single
    // sparse membrane must not add a whole-world, all-species traversal.
    std::vector<std::uint8_t> wet(state.cells.size(),2);
    const auto is_wet=[&](std::size_t i) {
        if(wet[i]==2) {
            wet[i]=0;
            for(std::size_t k=0;k<source.registry().size();++k)
                if(source.registry()[k].phase==ma::Phase::liquid && state.cells[i].mixture.masses[k]>0) { wet[i]=1;break; }
        }
        return wet[i]!=0;
    };
    std::vector<std::uint8_t> x(faces.NX),y(faces.NY),incident_x(faces.NX),incident_y(faces.NY);
    std::size_t available=0;
    const auto mark=[&](const auto& left,const auto& right,auto& mask,auto& incident) {
        for(std::size_t f=0;f<mask.size();++f) {
            const auto a=left[f],b=right[f];
            if(a==mac_kernel::none || b==mac_kernel::none || !(blocked[a] || blocked[b]))continue;
            incident[f]=1;mask[f]=is_wet(a) || is_wet(b);
            if(!mask[f])++available;
        }
    };
    mark(faces.xl,faces.xr,x,incident_x);mark(faces.yl,faces.yr,y,incident_y);
    const auto initial_available=available;
    std::size_t retries=0;
    for(;;) {
        auto actual_x=x,actual_y=y;
        if(std::none_of(x.begin(),x.end(),[](auto v){return v!=0;}) &&
           std::none_of(y.begin(),y.end(),[](auto v){return v!=0;})) { actual_x.clear();actual_y.clear(); }
        const auto trial=actual_x==grid.closed_x && actual_y==grid.closed_y?source:
            source.with_closed_faces(std::move(actual_x),std::move(actual_y));
        try {
            auto accepted=MacGameKernelAccess::advance_guarded(trial,seconds,budget,blocked);
            // Closing a moving face returns its K to U. Its signed impulse is
            // external wall work, alongside the accepted pressure-wall load.
            const auto settle=[](const auto& before,const auto& after,MacScaled& full,double& projected) {
                MacEnergySum total;total.add(full);
                for(std::size_t f=0;f<before.size();++f)if(before[f]!=after[f]) {total.add(after[f]);total.add(-before[f]);}
                full=total.value();projected=full.project().value;
            };
            settle(state.momentum_x,trial.state().momentum_x,accepted.report.full_wall_impulse_x,accepted.report.wall_impulse_x);
            settle(state.momentum_y,trial.state().momentum_y,accepted.report.full_wall_impulse_y,accepted.report.wall_impulse_y);
            return {std::move(accepted),retries,initial_available-available};
        } catch(const game_kernel::LiquidBoundaryConflict& conflict) {
            check(!conflict.faces.empty() && conflict.faces.size()<=available,
                "Liquid boundary retry made no progress");
            for(const auto& edge:conflict.faces) {
                auto& mask=edge.x?x:y;const auto& incident=edge.x?incident_x:incident_y;
                const auto& left=edge.x?faces.xl:faces.yl;const auto& right=edge.x?faces.xr:faces.yr;
                check(edge.face<mask.size() && incident[edge.face] && !mask[edge.face],
                    "Liquid boundary conflict is closed or nonincident");
                const auto a=left[edge.face],b=right[edge.face];
                check((edge.donor==a && edge.target==b) || (edge.donor==b && edge.target==a),
                    "Liquid boundary conflict endpoints differ");
                mask[edge.face]=1;--available;
            }
            ++retries;
            check(retries<=initial_available,"Liquid boundary retry exceeded its finite face bound");
        }
    }
}
} // namespace ma::oni::colony::detail
