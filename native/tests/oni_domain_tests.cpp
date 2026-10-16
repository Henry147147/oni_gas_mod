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
        static_assert(sizeof(MAOniDomainCell)==88);
        static_assert(sizeof(MAOniDomainHeat)==48 && sizeof(MAOniDomainHeatStatus)==56);
        static_assert(sizeof(MAOniDomainTransfer)==64 && sizeof(MAOniDomainReceipt)==88 && sizeof(MAOniDomainTransferStatus)==96);
        MAOniDomainConfig config{40,1,1,64,10,10,3,2,0,0};
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
            {64,1,final.session,final.completed_frames,10+10*64,1,1,1,10,0,.05F,300},
            {64,1,final.session,final.completed_frames,10+10*64,1,2,1,11,0,.02F,0}}};
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
        config.offset_x=63;
        try { ma::oni::start_domain(config,1); throw std::runtime_error("row-wrap geometry accepted"); }
        catch (const std::invalid_argument&) {}
        require(!ma::oni::domain_active(),"rejected geometry activated hooks");
        std::cout << "PASS sealed-domain native scheduling, transport, conservation, compressed EOS, atomic failure and exclusions\n";
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
