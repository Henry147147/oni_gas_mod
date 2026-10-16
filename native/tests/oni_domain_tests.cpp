#include "../src/oni_domain.hpp"
#include <cmath>
#include <cfenv>
#include <chrono>
#include <cstring>
#include <iostream>
#include <future>
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
