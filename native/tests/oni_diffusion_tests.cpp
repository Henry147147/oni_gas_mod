#include "../src/oni_domain.hpp"
#include "../src/oni_frame_inputs.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value,const char* why) { if(!value)throw std::runtime_error(why); }
}
int main() {
    try {
        using namespace ma::oni;
        static_assert(sizeof(MAOniDomainDiffusionStatus)==184);
        const MAOniDomainConfig config{40,2,1,100,10,10,3,2,0,0};
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
        check(frame_inputs::decode(inputs).flags==0,"Diffusion settings were misreported as an unsupported actor change");
        check(advance_domain_snapshot_with_inputs(saved,inputs)==copy_completed_domain_snapshot(status.session,2),
            "A mode change did not replay from its configured boundary image");
        const auto saved_time=frame_inputs::decode(inputs).source_time;
        shutdown_domain();start_domain(config,1,saved,saved_time);
        copy_domain_diffusion_status(diffusion);
        check(diffusion.mode==2 && diffusion.multiplier==100,"Saved native diffusion mode was lost on restore");
        copy_domain(status,cells,6);advance_domain(1,.2,2);copy_domain(status,cells,6);
        check(status.state==1,"Restored accelerated diffusion faulted");
        shutdown_domain();std::cout<<"PASS native diffusion ABI, worker, stale-frame fence, accelerated paired restore\n";return 0;
    }catch(const std::exception& e) { ma::oni::shutdown_domain();std::cerr<<e.what()<<'\n';return 1; }
}
