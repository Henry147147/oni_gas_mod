#include "../src/oni_domain.hpp"
#include <cmath>
#include <array>
#include <cfenv>
#include <chrono>
#include <cstring>
#include <iostream>
#include <future>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>
#include <xmmintrin.h>

namespace {
void require(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
MAOniDomainStatus capture(std::vector<MAOniDomainCell>& cells) {
    MAOniDomainStatus value{}; ma::oni::copy_domain(value,cells.data(),static_cast<std::uint32_t>(cells.size())); return value;
}
}
int main() {
    try {
        static_assert(sizeof(MAOniDomainConfig)==40);
        static_assert(sizeof(MAOniDomainStatus)==440);
        static_assert(sizeof(MAOniDomainCell)==96);
        static_assert(sizeof(MAOniDomainHeat)==48 && sizeof(MAOniDomainHeatStatus)==56);
        static_assert(sizeof(MAOniDomainTransfer)==64 && sizeof(MAOniDomainReceipt)==88 && sizeof(MAOniDomainTransferStatus)==104);
        static_assert(sizeof(MAOniDomainActor)==80 && sizeof(MAOniDomainActorStatus)==64);
        MAOniDomainConfig config{40,2,1,64,10,10,3,2,0,0};
        std::vector<MAOniDomainCell> cells(6);
        ma::oni::start_domain(config,1);
        require(ma::oni::domain_active(),"domain did not activate");
        auto initial=capture(cells);
        require(!ma::oni::suppress_domain_cell(MA_ONI_PRESSURE,-1) && !ma::oni::suppress_domain_cell(MA_ONI_PRESSURE,0),"native exterior was claimed");
        const auto native_cell=[](int x,int y) { return x+1+(y+1)*66; };
        require(ma::oni::suppress_domain_cell(MA_ONI_PRESSURE,native_cell(10,10)),"domain cell not excluded from native pressure");
        require(!ma::oni::suppress_domain_cell(MA_ONI_PRESSURE,native_cell(13,10)),"native border was claimed");
        require(!ma::oni::suppress_domain_cell(MA_ONI_PRESSURE,10+10*64),"public cell index was mistaken for a native index");
        require(ma::oni::suppress_domain_cell(MA_ONI_POST_PROCESS,native_cell(12,11)),"last padded domain cell was missed");
        for (std::uint64_t frame=1;frame<=10;++frame) ma::oni::advance_domain(frame,.2,2);
        auto final=capture(cells);
        require(final.state==1 && final.completed_frames==10 && std::abs(final.time_seconds-2)<1e-14,"native clock did not advance ten frames");
        require(final.maximum_relative_mass_error<1e-8 && final.maximum_relative_energy_error<1e-6,"domain conservation failed");
        require(final.current_oxygen_kg!=initial.initial_oxygen_kg && final.substeps>=200,"custom pressure/transport was bypassed");
        MAOniDomainThermalStatus thermal{}; ma::oni::copy_domain_thermal_status(thermal);
        require(thermal.enabled==1 && thermal.stages==2*final.substeps && thermal.contacts>0 && thermal.redistributed_energy_j>0,
            "Native worker omitted the integrated conductive stages or diagnostics");
        require(cells.front().resolved_kinetic_energy>0 && cells.front().momentum_x!=0,"native readings discarded face kinetics");
        for (const auto& cell : cells) {
            double mass=0; for (const auto amount : cell.masses) mass+=amount;
            const auto bulk=.5*(cell.momentum_x*cell.momentum_x+cell.momentum_y*cell.momentum_y)/mass;
            require(bulk<=cell.resolved_kinetic_energy*(1+1e-14),"resolved face energy is below its cell bulk projection");
        }
        const auto old_cells=cells; const auto before_heat=final;
        MAOniDomainHeatStatus heat{};
        MAOniDomainHeat request{48,1,final.session,final.completed_frames,10+10*64,17,1000,1000};
        ma::oni::queue_domain_heat(&request,1,heat);
        request.joules=-250; ma::oni::queue_domain_heat(&request,1,heat);
        require(heat.pending_batches==2 && heat.accepted_batches==2 && heat.committed_batches==0 && heat.pending_heat_joules==750,
            "queued heat account differs");
        const auto held_heat=heat;
        std::array<MAOniDomainHeat,2> invalid{request,request}; invalid[1].joules=-1e30;
        bool rejected=false;
        try { ma::oni::queue_domain_heat(invalid.data(),2,heat); } catch (const std::exception&) { rejected=true; }
        require(rejected && std::memcmp(&held_heat,&heat,sizeof(heat))==0,"rejected heat batch changed output");
        request.expected_frame-=1; rejected=false;
        try { ma::oni::queue_domain_heat(&request,1,heat); } catch (const std::logic_error&) { rejected=true; }
        require(rejected,"stale heat reservation accepted");
        final=capture(cells);
        require(final.completed_frames==10 && final.time_seconds==before_heat.time_seconds && std::memcmp(cells.data(),old_cells.data(),cells.size()*sizeof(cells[0]))==0,
            "heat enqueue advanced time or physical state");
        ma::oni::advance_domain(11,.2,2); final=capture(cells); ma::oni::copy_domain_heat(heat);
        require(final.state==1 && final.completed_frames==11 && std::abs(final.time_seconds-2.2)<1e-13 && heat.pending_batches==0 &&
            heat.committed_batches==2 && heat.committed_heat_joules==750 && heat.pending_heat_joules==0,
            "native tick did not atomically commit its heat account and physical clock");
        long double old_total=0,next_total=0,scale=750;
        for (std::size_t i=0;i<cells.size();++i) {
            old_total+=old_cells[i].internal_energy+old_cells[i].resolved_kinetic_energy;
            next_total+=cells[i].internal_energy+cells[i].resolved_kinetic_energy;
            scale+=std::abs(old_cells[i].internal_energy)+std::abs(cells[i].internal_energy);
        }
        require(std::abs(next_total-old_total-750) <= 128*std::numeric_limits<double>::epsilon()*scale,
            "external heat disappeared from U plus canonical kinetic energy");
        const std::array<MAOniDomainTransfer,2> transfers{{
            {64,2,final.session,final.completed_frames,10+10*64,1,1,1,10,0,.05F,300},
            {64,2,final.session,final.completed_frames,10+10*64,1,2,1,11,0,.02F,0}}};
        MAOniDomainTransferStatus transfer_status{}; ma::oni::queue_domain_transfers(transfers.data(),2,transfer_status);
        require(transfer_status.pending_batches==1 && transfer_status.committed_batches==0,"transfer committed before a native tick");
        std::array<MAOniDomainReceipt,4> receipts{}; std::uint32_t receipt_count=99;
        ma::oni::copy_domain_receipts(final,cells.data(),6,receipts.data(),4,receipt_count);
        require(receipt_count==0 && final.completed_frames==11,"uncommitted receipt escaped its native frame");
        auto invalid_transfers=transfers; invalid_transfers[1].public_cell=0; const auto held_transfer_status=transfer_status;
        rejected=false;
        try { ma::oni::queue_domain_transfers(invalid_transfers.data(),2,transfer_status); } catch (const std::invalid_argument&) { rejected=true; }
        require(rejected && std::memcmp(&held_transfer_status,&transfer_status,sizeof(transfer_status))==0,"invalid gas batch partially queued");
        ma::oni::advance_domain(12,.2,2);
        ma::oni::copy_domain_receipts(final,cells.data(),6,receipts.data(),4,receipt_count);
        require(final.state==1 && final.completed_frames==12 && receipt_count==2 && receipts[0].sequence==1 && receipts[1].sequence==2 &&
            receipts[0].completed_frame==12 && receipts[1].completed_frame==12 && receipts[0].callback==10 && receipts[1].callback==11 &&
            receipts[0].mass_kg==static_cast<double>(.05F) && receipts[1].mass_kg==static_cast<double>(.02F),"gas receipts differ from completed physical state");
        ma::oni::copy_domain_transfer_status(transfer_status);
        require(transfer_status.committed_batches==1 && transfer_status.pending_batches==0 && transfer_status.unacknowledged_receipts==2 &&
            transfer_status.mass_changes_kg[1]==static_cast<double>(.05F)-static_cast<double>(.02F),"committed gas account differs");
        ma::oni::ack_domain_receipts(final.session,1); ma::oni::ack_domain_receipts(final.session,1);
        ma::oni::copy_domain_receipts(final,cells.data(),6,receipts.data(),4,receipt_count);
        require(receipt_count==1 && receipts[0].sequence==2,"receipt acknowledgment lost or duplicated delivery");
        ma::oni::ack_domain_receipts(final.session,2);
        MAOniDomainActorStatus actor_status{};
        MAOniDomainActor consumer{80,1,final.session,final.completed_frames,static_cast<std::int32_t>(0x81000001U),1,10+10*64,1,0,0,.05F,0,0,0};
        MAOniDomainActor emitter{80,1,final.session,final.completed_frames,0x12000002,2,11+10*64,1,0,0,.01F,.2,300,10000};
        const std::array<MAOniDomainActor,2> actor_batch{consumer,emitter};
        ma::oni::configure_domain_actors(actor_batch.data(),2,actor_status);
        const auto held_actors=actor_status; auto invalid_actor=consumer; invalid_actor.species=3; rejected=false;
        try { ma::oni::configure_domain_actor(invalid_actor,actor_status); } catch (const std::invalid_argument&) { rejected=true; }
        require(rejected && std::memcmp(&held_actors,&actor_status,sizeof(actor_status))==0,"invalid actor changed its registration/output");
        auto bad_actor_batch=actor_batch; bad_actor_batch[0].mass_kg=.1F; bad_actor_batch[1].species=3;
        rejected=false;
        try { ma::oni::configure_domain_actors(bad_actor_batch.data(),2,actor_status); } catch (const std::invalid_argument&) { rejected=true; }
        require(rejected && std::memcmp(&held_actors,&actor_status,sizeof(actor_status))==0,
            "Late actor batch failure admitted an earlier rate or changed output");
        bad_actor_batch=actor_batch; bad_actor_batch[0].flags=bad_actor_batch[1].flags=1; bad_actor_batch[1].native_handle=999;
        rejected=false;
        try { ma::oni::configure_domain_actors(bad_actor_batch.data(),2,actor_status); } catch (const std::invalid_argument&) { rejected=true; }
        require(rejected && std::memcmp(&held_actors,&actor_status,sizeof(actor_status))==0,
            "Late batch failure retired an earlier actor");
        bad_actor_batch=actor_batch; --bad_actor_batch[1].expected_frame;
        rejected=false;
        try { ma::oni::configure_domain_actors(bad_actor_batch.data(),2,actor_status); } catch (const std::logic_error&) { rejected=true; }
        require(rejected && std::memcmp(&held_actors,&actor_status,sizeof(actor_status))==0,
            "Stale later batch command changed the actor registry");
        std::size_t blocked_edges=0,unblocked_edges=0; double consumed_total=0,emitted_total=0;
        const auto initial_actor_mass=transfer_status.mass_changes_kg[1];
        for (std::uint64_t frame=13;frame<=19;++frame) {
            if (frame==15 || frame==17) {
                emitter.expected_frame=frame-1; emitter.maximum_gas_density_kg_m3=frame==15 ? .1 : 10000;
                ma::oni::configure_domain_actor(emitter,actor_status);
            }
            if (frame==19) {
                consumer.expected_frame=frame-1; consumer.mass_kg=.025F;
                auto ordered=std::array<MAOniDomainActor,2>{consumer,consumer}; ordered[0].mass_kg=.04F;
                ma::oni::configure_domain_actors(ordered.data(),2,actor_status);
            }
            ma::oni::advance_domain(frame,.2,2);
            ma::oni::copy_domain_receipts(final,cells.data(),6,receipts.data(),4,receipt_count);
            require(final.state==1 && final.completed_frames==frame,"registered actor frame failed");
            std::size_t consumer_count=0,emitter_count=0;
            for (std::size_t i=0;i<receipt_count;++i) {
                const auto& receipt=receipts[i];
                require(receipt.completed_frame==frame,"actor receipt escaped its physical frame");
                if (receipt.kind==3) {
                    ++consumer_count; consumed_total+=receipt.mass_kg;
                    require(receipt.callback==consumer.native_handle && receipt.mass_kg>0 && receipt.kinetic_energy_j>0,
                        "consumer receipt lost full handle, mass or carried kinetic energy");
                } else if (receipt.kind==4) {
                    ++emitter_count; emitted_total+=receipt.mass_kg;
                    require(receipt.callback==emitter.native_handle && receipt.mass_kg==((frame==14 || frame==18 || frame==19) ? static_cast<double>(.01F) : 0),
                        "emitter interval/reset/blocked cadence differs");
                } else if (receipt.kind==5) ++blocked_edges;
                else if (receipt.kind==6) ++unblocked_edges;
                else throw std::runtime_error("unexpected actor receipt kind");
            }
            require(consumer_count==1 && emitter_count==1,"registered actor sample missing or duplicated");
            ma::oni::ack_domain_receipts(final.session,receipts[receipt_count-1].sequence);
        }
        ma::oni::copy_domain_transfer_status(transfer_status);
        require(blocked_edges==1 && unblocked_edges==1 && std::abs(transfer_status.mass_changes_kg[1]-initial_actor_mass+consumed_total-emitted_total)<1e-15,
            "registered actor callbacks or physical mass account differ");
        require(std::abs(consumed_total-(6*static_cast<double>(.05F)+static_cast<double>(.025F))*.2)<1e-9,"actor rate or modified rate differs");
        consumer.expected_frame=emitter.expected_frame=final.completed_frames; consumer.flags=emitter.flags=1;
        ma::oni::configure_domain_actor(consumer,actor_status); ma::oni::configure_domain_actor(emitter,actor_status);
        ma::oni::advance_domain(20,.2,2);
        ma::oni::copy_domain_receipts(final,cells.data(),6,receipts.data(),4,receipt_count); ma::oni::copy_domain_actor_status(actor_status);
        require(final.state==1 && receipt_count==0 && actor_status.live_actors==0 && actor_status.actor_steps==14 &&
            actor_status.consumer_samples==7 && actor_status.emitter_samples==7,"actor removal produced further transfers or samples");
        std::unique_lock frame_lock(ma::oni::domain_frame_mutex());
        std::promise<void> started;
        std::promise<MAOniDomainStatus> copied; auto ready=copied.get_future();
        std::thread reader([&] {
            started.set_value();
            try { std::vector<MAOniDomainCell> values(6); copied.set_value(capture(values)); }
            catch (...) { copied.set_exception(std::current_exception()); }
        });
        started.get_future().wait();
        const bool blocked=ready.wait_for(std::chrono::milliseconds(20))==std::future_status::timeout;
        frame_lock.unlock(); reader.join();
        require(blocked && ready.get().completed_frames==final.completed_frames,"reader escaped the native publication fence");
        const auto before=final;
        try { MAOniDomainStatus output=final; ma::oni::copy_domain(output,cells.data(),1); throw std::runtime_error("undersized copy accepted"); }
        catch (const std::length_error&) {}
        ma::oni::advance_domain(10,.2,2); final=capture(cells);
        require(final.state==3 && final.completed_frames==before.completed_frames && final.time_seconds==before.time_seconds,"duplicate native ticket was not atomic");
        require(ma::oni::suppress_domain_cell(MA_ONI_PRESSURE,native_cell(10,10)),"fault released the native cell filter");
        ma::oni::shutdown_domain(); require(!ma::oni::domain_active(),"shutdown retained native exclusions");
        config.scenario=1;
        std::fenv_t original_environment{}; require(std::fegetenv(&original_environment)==0,"floating-point environment capture");
        const auto original_sse=_mm_getcsr();
        require(std::fesetround(FE_DOWNWARD)==0,"caller rounding setup");
        _mm_setcsr(_mm_getcsr() | 0x8040U); const auto caller_sse=_mm_getcsr();
        ma::oni::start_domain(config,1);
        require(_mm_getcsr()==caller_sse && std::fegetround()==FE_DOWNWARD,"start changed caller arithmetic");
        ma::oni::advance_domain(11,.2,2);
        require(_mm_getcsr()==caller_sse && std::fegetround()==FE_DOWNWARD,"native worker arithmetic leaked into game code");
        require(std::fesetenv(&original_environment)==0,"floating-point environment restoration"); _mm_setcsr(original_sse);
        final=capture(cells);
        require(final.state==1 && final.liquid_mass_kg==1100 && std::abs(final.pressure_pa-2.2e8)<1,"compressed liquid EOS was not used");
        require(std::abs(cells.front().temperature-300)<1e-8,"elastic energy was treated as heat");
        ma::oni::shutdown_domain();
        config.scenario=0; ma::oni::start_domain(config,1); final=capture(cells);
        consumer={80,1,final.session,0,1,1,10+10*64,1,0,0,.05F,0,0,0};
        emitter={80,1,final.session,0,2,2,11+10*64,1,0,0,.01F,1e-6,300,10000};
        ma::oni::configure_domain_actor(consumer,actor_status); ma::oni::configure_domain_actor(emitter,actor_status);
        ma::oni::advance_domain(1,.2,2);
        ma::oni::copy_domain_receipts(final,cells.data(),6,receipts.data(),4,receipt_count);
        require(final.state==1 && receipt_count==2,"actor failure fixture did not complete its control frame");
        ma::oni::ack_domain_receipts(final.session,receipts[receipt_count-1].sequence);
        const auto before_actor_failure=cells; const auto before_actor_frame=final;
        ma::oni::copy_domain_transfer_status(transfer_status); const auto before_actor_account=transfer_status;
        ma::oni::copy_domain_actor_status(actor_status); const auto before_actor_steps=actor_status.actor_steps;
        ma::oni::advance_domain(2,.2,2);
        ma::oni::copy_domain_receipts(final,cells.data(),6,receipts.data(),4,receipt_count);
        ma::oni::copy_domain_transfer_status(transfer_status); ma::oni::copy_domain_actor_status(actor_status);
        require(final.state==3 && final.completed_frames==before_actor_frame.completed_frames && final.time_seconds==before_actor_frame.time_seconds &&
            receipt_count==0 && actor_status.actor_steps==before_actor_steps &&
            std::memcmp(cells.data(),before_actor_failure.data(),cells.size()*sizeof(cells[0]))==0 &&
            transfer_status.mass_changes_kg[1]==before_actor_account.mass_changes_kg[1] &&
            transfer_status.material_energy_change_j==before_actor_account.material_energy_change_j,
            "late emitter failure published an earlier consumer, callback, clock or account");
        ma::oni::shutdown_domain();
        config.scenario=0; ma::oni::start_domain(config,1); final=capture(cells);
        const auto carbon_initial=cells;
        const std::array<MAOniDomainTransfer,2> carbon{{
            {64,2,final.session,0,10+10*64,4,1,1,10,0,.05F,310},
            {64,2,final.session,0,10+10*64,4,2,1,11,0,.02F,0}}};
        ma::oni::queue_domain_transfers(carbon.data(),2,transfer_status);
        auto obsolete_carbon=carbon; obsolete_carbon[1].abi_version=1;
        const auto held_carbon_account=transfer_status; rejected=false;
        try { ma::oni::queue_domain_transfers(obsolete_carbon.data(),2,transfer_status); } catch (const std::invalid_argument&) { rejected=true; }
        require(rejected && std::memcmp(&held_carbon_account,&transfer_status,sizeof(transfer_status))==0,
            "obsolete transfer ABI partially queued carbon dioxide");
        ma::oni::advance_domain(1,.2,2);
        ma::oni::copy_domain_receipts(final,cells.data(),6,receipts.data(),4,receipt_count);
        ma::oni::copy_domain_transfer_status(transfer_status);
        long double carbon_mass=0,carbon_energy_before=0,carbon_energy_after=0;
        std::size_t carbon_cells=0;
        for (std::size_t i=0;i<cells.size();++i) {
            carbon_mass+=cells[i].masses[4]; carbon_cells+=cells[i].masses[4]>0;
            carbon_energy_before+=carbon_initial[i].internal_energy+carbon_initial[i].resolved_kinetic_energy;
            carbon_energy_after+=cells[i].internal_energy+cells[i].resolved_kinetic_energy;
        }
        require(final.state==1 && receipt_count==2 && receipts[0].species==4 && receipts[1].species==4 &&
            transfer_status.abi_version==2 && transfer_status.mass_changes_kg[4]==static_cast<double>(.05F)-.02F &&
            std::abs(carbon_mass-transfer_status.mass_changes_kg[4])<1e-16 && carbon_cells>1 &&
            std::abs(carbon_energy_after-carbon_energy_before-transfer_status.material_energy_change_j-transfer_status.kinetic_energy_change_j)<1e-6,
            "physical carbon-dioxide transfer, transport, complete mass publication or energy account failed");
        ma::oni::shutdown_domain();
        config.scenario=2; ma::oni::start_domain(config,1); final=capture(cells);
        MAOniDomainGravityStatus gravity{}; ma::oni::copy_domain_gravity_status(gravity);
        static_assert(sizeof(MAOniDomainGravityStatus)==96);
        require(gravity.enabled==1 && gravity.gravity_m_per_s2==9.81 && gravity.potential_j>0 &&
            gravity.initial_potential_j==gravity.potential_j && gravity.body_impulse_y==0,"Gravity activation did not retain its physical datum");
        const auto initial_gravity=gravity; const auto gravity_cells=cells;
        long double initial_total=gravity.potential_j;
        for (const auto& cell : cells) initial_total+=cell.internal_energy+cell.resolved_kinetic_energy;
        const std::array<MAOniDomainTransfer,2> elevated{{
            {64,2,final.session,0,10+11*64,1,1,1,10,0,.05F,310},
            {64,2,final.session,0,10+11*64,1,2,1,11,0,.02F,0}}};
        ma::oni::queue_domain_transfers(elevated.data(),2,transfer_status);
        ma::oni::copy_domain_gravity_status(gravity);
        const auto boundary_potential=(static_cast<double>(.05F)-.02F)*9.81*1.5;
        require(std::abs(gravity.pending_boundary_potential_j-boundary_potential)<1e-15 && gravity.committed_boundary_potential_j==0 &&
            gravity.potential_j==initial_gravity.potential_j && gravity.completed_frame==0,"Gravity reservation published energy or used the wrong elevation");
        auto bad_elevated=elevated; bad_elevated[1].public_cell=0; const auto before_bad_gravity=gravity;
        rejected=false; try { ma::oni::queue_domain_transfers(bad_elevated.data(),2,transfer_status); } catch (const std::invalid_argument&) { rejected=true; }
        ma::oni::copy_domain_gravity_status(gravity);
        require(rejected && std::memcmp(&gravity,&before_bad_gravity,sizeof(gravity))==0,"Rejected transfer changed a pending potential account");
        for (std::uint64_t frame=1;frame<=10;++frame) ma::oni::advance_domain(frame,.2,2);
        final=capture(cells); ma::oni::copy_domain_gravity_status(gravity); ma::oni::copy_domain_transfer_status(transfer_status);
        long double final_total=gravity.potential_j;
        for (const auto& cell : cells) final_total+=cell.internal_energy+cell.resolved_kinetic_energy;
        require(final.state==1 && gravity.completed_frame==10 && gravity.session==final.session && gravity.body_impulse_y<0 &&
            gravity.potential_j!=initial_gravity.potential_j && cells[0].momentum_y!=gravity_cells[0].momentum_y &&
            gravity.pending_boundary_potential_j==0 && std::abs(gravity.committed_boundary_potential_j-boundary_potential)<1e-15 &&
            std::abs(final_total-initial_total-transfer_status.material_energy_change_j-transfer_status.kinetic_energy_change_j-boundary_potential)<1e-6 &&
            gravity.maximum_energy_residual_j<1e-6,"Gravity force, carried boundary potential or U+K+G account failed");
        // Registered actors cross different elevations, so equal emitted and
        // consumed masses still require a nonzero boundary-potential account.
        ma::oni::copy_domain_receipts(final,cells.data(),6,receipts.data(),4,receipt_count);
        ma::oni::ack_domain_receipts(final.session,receipts[receipt_count-1].sequence);
        consumer={80,1,final.session,10,1,1,10+10*64,1,0,0,.05F,0,0,0};
        emitter={80,1,final.session,10,2,2,10+11*64,1,0,0,.01F,.2,300,10000};
        ma::oni::configure_domain_actor(consumer,actor_status); ma::oni::configure_domain_actor(emitter,actor_status);
        double actor_potential=boundary_potential;
        for (std::uint64_t frame=11;frame<=20;++frame) {
            ma::oni::advance_domain(frame,.2,2);
            ma::oni::copy_domain_receipts(final,cells.data(),6,receipts.data(),4,receipt_count);
            for (std::uint32_t i=0;i<receipt_count;++i) {
                const auto& receipt=receipts[i]; const auto height=receipt.public_cell/64-10+.5;
                if (receipt.kind==3) actor_potential-=receipt.mass_kg*9.81*height;
                else if (receipt.kind==4) actor_potential+=receipt.mass_kg*9.81*height;
            }
            if (receipt_count) ma::oni::ack_domain_receipts(final.session,receipts[receipt_count-1].sequence);
        }
        ma::oni::copy_domain_gravity_status(gravity); ma::oni::copy_domain_transfer_status(transfer_status);
        final_total=gravity.potential_j;
        for (const auto& cell : cells) final_total+=cell.internal_energy+cell.resolved_kinetic_energy;
        require(final.state==1 && std::abs(gravity.committed_boundary_potential_j-actor_potential)<1e-14 &&
            std::abs(final_total-initial_total-transfer_status.material_energy_change_j-transfer_status.kinetic_energy_change_j-actor_potential)<1e-6,
            "Registered actors at different elevations lost boundary potential energy");
        const auto held_gravity=gravity;
        ma::oni::advance_domain(20,.2,2); ma::oni::copy_domain_gravity_status(gravity);
        require(gravity.state==3 && gravity.completed_frame==held_gravity.completed_frame && gravity.potential_j==held_gravity.potential_j &&
            gravity.body_impulse_y==held_gravity.body_impulse_y && gravity.committed_boundary_potential_j==held_gravity.committed_boundary_potential_j,
            "Failed gravity frame published a potential, force or boundary account");
        ma::oni::shutdown_domain();
        config.offset_x=63;
        try { ma::oni::start_domain(config,1); throw std::runtime_error("row-wrap geometry accepted"); }
        catch (const std::invalid_argument&) {}
        require(!ma::oni::domain_active(),"rejected geometry activated hooks");
        std::cout << "PASS sealed-domain native scheduling, transport, conservation, compressed EOS, atomic failure and exclusions\n";
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
