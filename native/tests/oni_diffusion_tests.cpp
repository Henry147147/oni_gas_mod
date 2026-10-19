#include "../src/oni_domain.hpp"
#include "../src/oni_frame_inputs.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace ma::oni;
void check(bool value,const char* why) { if(!value)throw std::runtime_error(why); }
const MAOniDomainConfig room{40,2,1,100,10,10,3,2,0,0};
struct Run { std::uint32_t state;std::uint64_t frames;MAOniDomainDiffusionStatus diffusion; };
// Runs a 3x2 gas room in one diffusion mode; optional per-frame cell heat.
Run run(std::uint32_t mode,const std::vector<MAOniDomainInitialCell>& cells,int frames,double heat_j=0) {
    if(cells.empty())start_domain(room,1);else start_domain(room,1,{},0,cells);
    MAOniDomainStatus status{};MAOniDomainCell out[6]{};copy_domain(status,out,6);
    if(mode)configure_domain_diffusion(status.session,0,mode,mode==2 ? 100 : 1);
    for(int f=0;f<frames;++f) {
        copy_domain(status,out,6);if(status.state!=1)break;
        if(heat_j!=0) {
            MAOniDomainHeat heat{48,1,status.session,status.completed_frames,1010,0,heat_j,1000};MAOniDomainHeatStatus heat_status{};
            queue_domain_heat(&heat,1,heat_status);
        }
        advance_domain(status.native_ticket+1,.2,2);
    }
    copy_domain(status,out,6);Run result{status.state,status.completed_frames,{}};copy_domain_diffusion_status(result.diffusion);
    shutdown_domain();return result;
}
// Cell masses N2, O2, He, H2O, CO2 with an O2 gradient over the six cells.
std::vector<MAOniDomainInitialCell> graded(std::array<double,5> base,double temperature) {
    std::vector<MAOniDomainInitialCell> cells;
    for(int i=0;i<6;++i) {
        MAOniDomainInitialCell c{sizeof(MAOniDomainInitialCell),1,{},temperature};
        for(int k=0;k<5;++k)c.masses[k]=base[static_cast<std::size_t>(k)];
        c.masses[1]*=1+.05*i;cells.push_back(c);
    }
    return cells;
}
// Opposing pressure step (left column) and small O2 gradient: the review's
// accelerated frame-0 entropy fault.
std::vector<MAOniDomainInitialCell> stepped(double bump,double gradient) {
    std::vector<MAOniDomainInitialCell> cells;
    for(int y=0;y<2;++y)for(int x=0;x<3;++x) {
        MAOniDomainInitialCell c{sizeof(MAOniDomainInitialCell),1,{},300};const double rho=1.17*(x==0 ? 1+bump : 1),o=.21+gradient*x;
        c.masses[0]=(1-o)*rho;c.masses[1]=o*rho;cells.push_back(c);
    }
    return cells;
}
void review_controls() {
    std::size_t runs=0;
    for(const double bump:{.01,.05})for(const double gradient:{1e-4,1e-5})for(std::uint32_t mode:{1U,2U}) {
        const auto r=run(mode,stepped(bump,gradient),150);
        check(r.state==1 && r.frames==150 && r.diffusion.exchanged_mass_kg>0,"Opposing-gradient room faulted under diffusion");++runs;
    }
    // States the flow owner admits with diffusion disabled, outside the
    // correlations' validated ranges: extrapolated, counted, never faulted.
    struct Scene { const char* name;std::vector<MAOniDomainInitialCell> cells;int frames;double heat;double temperature,pressure; };
    const std::vector<Scene> scenes{
        {"cooled generated scene",{},10,-1500,297.5,0},
        {"297 K helium trace",graded({.9,.25,1e-9,0,0},297),5,0,297,0},
        {"above 200 kPa",graded({0,3,0,0,.01},300),5,0,0,2e5},
        {"below 10 kPa",graded({0,.1,0,0,.01},300),5,0,0,1e4},
        {"250 K",graded({0,1,0,0,.01},250),5,0,250,0},
        {"480 K",graded({0,1,0,0,.01},480),5,0,480,0}};
    for(const auto& scene:scenes)for(std::uint32_t mode:{0U,1U,2U}) {
        const auto r=run(mode,scene.cells,scene.frames,scene.heat);
        if(r.state!=1 || r.frames!=static_cast<std::uint64_t>(scene.frames))throw std::runtime_error(std::string("Out-of-range scene faulted: ")+scene.name);
        if(mode==0)continue;
        const auto& d=r.diffusion;
        check(d.out_of_range_evaluations>0,"Out-of-range evaluations were not counted");
        if(scene.temperature>300)check(d.out_of_range_maximum_temperature_k>473,"Extreme high temperature not recorded");
        if(scene.temperature>0 && scene.temperature<300)check(d.out_of_range_minimum_temperature_k<scene.temperature+1,"Extreme low temperature not recorded");
        if(scene.pressure>1e5)check(d.out_of_range_maximum_pressure_pa>scene.pressure,"Extreme high pressure not recorded");
        if(scene.pressure>0 && scene.pressure<1e5)check(d.out_of_range_minimum_pressure_pa<scene.pressure,"Extreme low pressure not recorded");
        ++runs;
    }
    std::cout<<"PASS review scenes through the domain ABI: "<<runs<<" diffusion runs (opposing gradients 150 frames; extrapolated ranges counted)\n";
}
}
int main() {
    try {
        static_assert(sizeof(MAOniDomainDiffusionStatus)==224);
        const MAOniDomainConfig config=room;
        start_domain(config,1);
        MAOniDomainStatus status{};MAOniDomainCell cells[6]{};copy_domain(status,cells,6);
        MAOniDomainDiffusionStatus diffusion{};copy_domain_diffusion_status(diffusion);
        check(diffusion.mode==0,"Legacy native start silently enabled diffusion");
        configure_domain_diffusion(status.session,0,1,1);
        copy_domain_diffusion_status(diffusion);check(diffusion.mode==1 && diffusion.multiplier==1,"Physical mode did not commit");
        MAOniDomainHeat heat{48,1,status.session,0,1010,0,1,1000};MAOniDomainHeatStatus heat_status{};
        queue_domain_heat(&heat,1,heat_status);
        bool busy=false;try { configure_domain_diffusion(status.session,0,2,100); }catch(const std::logic_error&) { busy=true; }
        check(busy,"Mode change overwrote a queued boundary transaction");
        advance_domain(1,.2,2);copy_domain(status,cells,6);copy_domain_diffusion_status(diffusion);
        check(status.state==1 && diffusion.completed_frame==1 && diffusion.substeps==4 && diffusion.exchanged_mass_kg>0,"Worker did not execute macro-interval diffusion");
        busy=false;try { configure_domain_diffusion(status.session,0,2,100); }catch(const std::logic_error&) { busy=true; }
        check(busy,"Mode change admitted a stale frame");
        (void)copy_completed_domain_snapshot(status.session,1);
        configure_domain_diffusion(status.session,1,2,100);
        busy=false;try { (void)copy_completed_domain_snapshot(status.session,1); }catch(const std::logic_error&) { busy=true; }
        check(busy,"Mode change exposed an old completed snapshot");
        busy=false;try { (void)copy_domain_frame_inputs(status.session,1); }catch(const std::logic_error&) { busy=true; }
        check(busy,"Mode change exposed a replay journal from the previous physics");
        const auto saved=checkpoint_domain(status.session,1);
        advance_domain(2,.2,2);copy_domain(status,cells,6);
        const auto inputs=copy_domain_frame_inputs(status.session,2);
        check(frame_inputs::decode(inputs).flags==frame_inputs::production_advance,"Diffusion settings were misreported as an unsupported actor change");
        check(advance_domain_snapshot_with_inputs(saved,inputs)==copy_completed_domain_snapshot(status.session,2),
            "A mode change did not replay from its configured boundary image");
        const auto saved_time=frame_inputs::decode(inputs).source_time;
        shutdown_domain();start_domain(config,1,saved,saved_time);
        copy_domain_diffusion_status(diffusion);
        check(diffusion.mode==2 && diffusion.multiplier==100,"Saved native diffusion mode was lost on restore");
        copy_domain(status,cells,6);advance_domain(1,.2,2);copy_domain(status,cells,6);
        check(status.state==1,"Restored accelerated diffusion faulted");
        shutdown_domain();std::cout<<"PASS native diffusion ABI, worker, stale-frame fence, accelerated paired restore\n";
        review_controls();return 0;
    }catch(const std::exception& e) { ma::oni::shutdown_domain();std::cerr<<e.what()<<'\n';return 1; }
}
