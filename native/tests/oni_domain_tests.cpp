#include "../src/oni_domain.hpp"
#include "../src/oni_actor_checkpoint.hpp"
#include <algorithm>
#include <cmath>
#include <array>
#include <cfenv>
#include <chrono>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <future>
#include <fstream>
#include <iterator>
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
void atomic_solid_startup() {
    using namespace ma::oni;
    constexpr int fluid=11+11*66,solid=10+11*66;
    MAOniDomainConfig config{40,2,1,64,10,10,3,2,0,0};
    MAOniDomainSolidHeatCell input{48,1,649,123,0,0,1.125F,.793F,2.F,255};
    shutdown_domain();std::vector<MAOniDomainCell> cells(6);
    const auto inactive=capture(cells);
    auto invalid=input;invalid.public_cell=650;
    const std::array batch{input,invalid};
    DomainSolidHeatStartup rejected_start{batch};
    rejected_start.status.session=0x12345678;rejected_start.heat.contacts=0x87654321;
    const auto held_status=rejected_start.status;const auto held_heat=rejected_start.heat;
    bool rejected=false;
    try {start_domain(config,1,{},0,{},&rejected_start);} catch(const std::invalid_argument&) {rejected=true;}
    auto after=capture(cells);
    require(rejected && !domain_active() && !domain_solid_heat_registered(solid) &&
        std::memcmp(&after,&inactive,sizeof(after))==0 &&
        std::memcmp(&rejected_start.status,&held_status,sizeof(held_status))==0 &&
        std::memcmp(&rejected_start.heat,&held_heat,sizeof(held_heat))==0,
        "A late invalid initial solid partially activated the domain or changed startup outputs");
    auto stale=input;stale.session=1;DomainSolidHeatStartup stale_start{std::span(&stale,1)};rejected=false;
    try {start_domain(config,1,{},0,{},&stale_start);} catch(const std::invalid_argument&) {rejected=true;}
    require(rejected && !domain_active(),"Initial solid startup accepted a previous session identity");
    for(const auto scenario:{0U,1U,4U,5U,6U}) {
        config.scenario=scenario;
        DomainSolidHeatStartup startup{std::span(&input,1)};
        auto worker=std::async(std::launch::async,[&] {
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
            while(!domain_active() && std::chrono::steady_clock::now()<deadline) std::this_thread::yield();
            require(domain_active(),"Initial solid activation never reached its worker");
            std::lock_guard fence(domain_frame_mutex());
            require(domain_solid_heat_registered(solid),"The first native frame started without its initial solid boundary");
            const auto contact=exchange_domain_solid_heat(fluid,solid,123,input.mass_kg,350,.2F,255,255,349.5F);
            require(contact.handled && contact.temperature<350,"The first native contact missed its initial solid boundary");
            advance_domain(1,.2F,2);
        });
        start_domain(config,1,{},0,{},&startup);worker.get();
        after=capture(cells);MAOniDomainSolidHeatStatus heat{};copy_domain_solid_heat_status(heat);
        require(after.state==1 && after.completed_frames==1 && heat.contacts==1 && heat.transfers==1 &&
            heat.maximum_balance_residual_j<=heat.maximum_balance_bound_j,
            "Atomic solid startup failed its first completed heat account");
        require(startup.status.state==1 && startup.status.session==after.session && startup.status.completed_frames==0 &&
            startup.heat.state==1 && startup.heat.session==after.session && startup.heat.completed_frame==0 &&
            startup.heat.contacts==0 && startup.heat.transfers==0,
            "An immediate worker advance rewrote or mixed the initial activation outputs");
        auto late=input;late.session=after.session;rejected=false;
        const auto before_rejection=snapshot_domain(after.session,after.completed_frames);
        try {configure_domain_solid_heat(&late,1,heat);} catch(const std::logic_error&) {rejected=true;}
        require(rejected && before_rejection==snapshot_domain(after.session,after.completed_frames),
            "Atomic startup weakened subsequent solid registration revision checks");
        shutdown_domain();
    }
    std::cout<<"PASS initial solid boundaries are atomic with activation, invalid batches leave no owner, first worker contacts exchange heat, source outputs stay frame zero and stale revisions still reject\n";
}
void solid_boundary_heat() {
    using namespace ma::oni;
    static_assert(sizeof(MAOniDomainSolidHeatCell)==48);
    static_assert(sizeof(MAOniDomainSolidHeatEndpoint)==64);
    constexpr int fluid=11+11*66,solid=10+11*66;
    for(const auto scenario:{0U,1U,4U,5U,6U}) {
        MAOniDomainConfig config{40,2,1,64,10,10,3,2,scenario,0};start_domain(config,1);
        std::vector<MAOniDomainCell> cells(6);const auto source=capture(cells);const auto image=snapshot_domain(source.session,0);
        MAOniDomainSolidHeatCell input{48,1,649,123,source.session,0,1.125F,.793F,2.F,255};
        MAOniDomainSolidHeatStatus heat{};configure_domain_solid_heat(&input,1,heat);
        require(domain_solid_heat_registered(solid) && heat.contacts==0,"Native solid registration lost its completed source");
        auto invalid=input;invalid.public_cell=650;std::array<MAOniDomainSolidHeatCell,2> batch{input,invalid};bool rejected=false;
        try {configure_domain_solid_heat(batch.data(),2,heat);} catch(const std::invalid_argument&) {rejected=true;}
        require(rejected && domain_solid_heat_registered(solid) && snapshot_domain(source.session,0)==image,"Late invalid solid partially changed registration or physical state");
        auto contact=exchange_domain_solid_heat(fluid,solid,123,input.mass_kg,350,.2F,255,255,349.5F);
        require(contact.handled && contact.temperature<350,"Registered solid did not cool into the fluid");
        const auto duplicate=exchange_domain_solid_heat(fluid,solid,123,input.mass_kg,contact.temperature,.2F);
        require(duplicate.temperature==contact.temperature,"A repeated native pair exchanged heat twice in one frame");
        copy_domain_solid_heat_status(heat);require(heat.contacts==0 && heat.transfers==0,"Pending solid heat was published before its fluid frame");
        MAOniDomainSolidHeatEndpoint endpoint{};copy_domain_solid_heat_endpoint(endpoint);
        require(endpoint.struct_size==64 && endpoint.abi_version==1 && endpoint.session==source.session &&
            endpoint.completed_frame==0 && endpoint.contact_frame==0,"Pending native solid endpoint escaped its completed frame");
        require(copy_completed_domain_snapshot(source.session,0)==image,"Pending solid heat rewrote the completed snapshot");
        advance_domain(1,.2F,2);const auto completed=capture(cells);copy_domain_solid_heat_status(heat);
        const auto represented=static_cast<double>(input.mass_kg)*input.specific_heat_kj_per_kg_k*1000*(350.-contact.temperature);
        require(completed.state==1 && heat.completed_frame==1 && heat.contacts==1 && heat.transfers==1 &&
            heat.body_heat_loss_j==represented && heat.stored_fluid_heat_j!=0 && heat.maximum_balance_residual_j<=heat.maximum_balance_bound_j,
            "Completed native solid heat lost its represented closed endpoint account");
        copy_domain_solid_heat_endpoint(endpoint);
        require(endpoint.state==1 && endpoint.session==source.session && endpoint.completed_frame==1 && endpoint.contact_frame==1 &&
            endpoint.solid_public_cell==649 && endpoint.fluid_public_cell==650 && endpoint.reserved==0 &&
            endpoint.native_old_temperature_k==349.5F && endpoint.before_temperature_k==350 &&
            endpoint.after_temperature_k==contact.temperature && endpoint.seconds==.2F,"Completed native solid contact operands changed or lost their frame");
        const MAOniDomainSolidCellState native_cell{64,1,1,649,source.session,1,350,contact.temperature,
            123,input.mass_kg,255,0,1};
        MAOniDomainSolidCellState observation{};copy_domain_solid_cell_state(observation);const auto unobserved=observation;
        rejected=false;try {(void)capture_domain_publication(7);} catch(const std::logic_error&) {rejected=true;}
        require(rejected,"A completed native publication admitted a stale solid observation");
        auto stale=native_cell;stale.completed_frame=0;rejected=false;
        try {record_domain_solid_cell_state(stale);} catch(const std::logic_error&) {rejected=true;}
        copy_domain_solid_cell_state(observation);
        require(rejected && std::memcmp(&unobserved,&observation,sizeof(observation))==0,"Stale native solid observation changed completed evidence");
        record_domain_solid_cell_state(native_cell);copy_domain_solid_cell_state(observation);
        require(std::memcmp(&native_cell,&observation,sizeof(observation))==0,"Completed native solid cell facts or source ticket changed");
        const auto published=capture_domain_publication(7);
        require(published && published->status.session==source.session && published->status.completed_frames==1 &&
            published->native_epoch==7 && published->solid_heat.contacts==1 && published->solid_endpoint.contact_frame==1 &&
            std::memcmp(&published->solid_cell,&native_cell,sizeof(native_cell))==0 && published->cells.size()==cells.size() &&
            std::memcmp(published->cells.data(),cells.data(),cells.size()*sizeof(MAOniDomainCell))==0,
            "Completed native publication mixed its owner frame, cells or solid observation");
        published_frames::record(&image,published);published_frames::bind(&image,&input);
        const auto lease=published_frames::acquire(&input);
        require(lease.completed_frame==1 && lease.session==source.session && lease.native_epoch==7,"Captured native publication lease lost its source");
        published_frames::release(lease.token);
        const auto journal=copy_domain_frame_inputs(source.session,1);
        require(advance_domain_snapshot_with_inputs(image,journal)==snapshot_domain(source.session,1),"Solid heat input did not replay the complete fluid image exactly");
        MAOniDomainChunkHeatStatus body{};copy_domain_chunk_heat_status(body);require(body.contacts==0,"Solid heat was counted as native body heat");
        input.expected_frame=1;configure_domain_solid_heat(&input,1,heat);
        const auto insulated=exchange_domain_solid_heat(fluid,solid,123,input.mass_kg,contact.temperature,.2F,255,0);
        require(insulated.temperature==contact.temperature,"A fully insulated native fluid face exchanged heat");
        advance_domain(2,.2F,2);copy_domain_solid_heat_status(heat);
        require(heat.contacts==2 && heat.transfers==1,"Insulated solid contact changed the completed heat account");
        (void)capture(cells);const auto held_cells=cells;
        const auto changed=exchange_domain_solid_heat(fluid,solid,124,input.mass_kg,contact.temperature,.2F);
        require(changed.temperature==contact.temperature && capture(cells).state==3 &&
            std::memcmp(held_cells.data(),cells.data(),cells.size()*sizeof(MAOniDomainCell))==0,
            "Changed native solid material modified a thermal endpoint before rejection");
        shutdown_domain();require(!domain_solid_heat_registered(solid),"Native solid registration survived shutdown");
        rejected=false;try {(void)published_frames::acquire(&input);} catch(const std::logic_error&) {rejected=true;}
        require(rejected && published_frames::statistics().cached_frames==0 && published_frames::statistics().leases==0,
            "A native publication binding or cached source survived simulator shutdown");
    }
    std::cout<<"PASS atomic native solid registration, represented gas/water/wet heat, once-per-frame pairs, insulation, separate completed accounts and exact input replay\n";
}
void persistent_actors() {
    static_assert(sizeof(MAOniDomainPersistentActor)==96);
    using namespace ma::oni;
    std::vector<MAOniDomainCell> cells(6);
    std::array<MAOniDomainReceipt,32> receipts{};
    auto drain=[&] {
        MAOniDomainStatus source{};std::uint32_t count=0;
        copy_domain_receipts(source,cells.data(),6,receipts.data(),32,count);
        require(source.state==1,"Persistent actor worker faulted");
        if (count) ack_domain_receipts(source.session,receipts[count-1].sequence);
        return count;
    };
    for (auto scenario : {0U,1U,3U,4U,5U,6U}) {
        MAOniDomainConfig config{40,2,1,64,10,10,3,2,scenario,0};start_domain(config,1);
        auto source=capture(cells);MAOniDomainActorStatus actor_status{};
        const auto species=scenario ? 3U : 1U;
        std::array<MAOniDomainPersistentActor,3> inputs{{
            {{80,1,source.session,0,10,1,650,species,0,0,.05F,0,0,0},101,12345,0},
            {{80,1,source.session,0,20,2,650,species,0,0,.03125F,.7F,300,10000},101,12345,0},
            {{80,1,source.session,0,30,2,651,species,0,0,.015625F,.7F,300,.01},101,12345,1}
        }};
        configure_persistent_domain_actors(inputs.data(),3,actor_status);
        require(actor_status.live_actors==3,"Persistent actor registration lost a live actor");
        for (std::uint64_t frame=1;frame<=10;++frame) { advance_domain(frame,.2F,2);drain(); }
        source=capture(cells);const auto image=snapshot_domain(source.session,source.completed_frames);
        const auto saved=actor_checkpoint::decode(image);
        require(saved.actors.size()==3 && saved.actors[0].remainder>0 && saved.actors[1].elapsed>0 && saved.actors[2].blocked,
            "Actor image omitted a consumer remainder, emitter timing or blocked state");
        require(advance_domain_snapshot(image,0)==image,"Actor snapshot did not validate and round trip exactly");
        auto bad=inputs;for (auto& value : bad) value.actor.expected_frame=source.completed_frames;
        bad.back().component=0;bool rejected=false;
        try { configure_persistent_domain_actors(bad.data(),3,actor_status); } catch(const std::invalid_argument&) { rejected=true; }
        require(rejected && snapshot_domain(source.session,source.completed_frames)==image,"Late duplicate identity partially changed actor state");
        const auto saved_time=source.time_seconds;
        for (int fault=0;fault<5;++fault) {
            auto malformed=image;
            if (fault==0) malformed[64]=0; // owner=101 becomes zero
            if (fault==1) malformed[64+24]=5; // unsupported species
            if (fault==2) malformed[64+32+7]=0x7f; // very large but finite rate is legal; make it NaN below
            if (fault==2) for (int b=0;b<8;++b) malformed[64+32+b]=static_cast<std::uint8_t>((0x7ff8000000000000ULL>>(8*b))&255);
            if (fault==3) malformed[64+80]=2; // nonboolean blocked state
            if (fault==4) malformed[64+28]=1; // reserved word
            const auto digest=hash_memory(malformed.data(),malformed.size()-64);std::copy(digest.begin(),digest.end(),malformed.end()-64);
            rejected=false;try { validate_domain_checkpoint(config,malformed,saved_time); } catch(const std::invalid_argument&) { rejected=true; }
            require(rejected,"Malformed durable actor state was admitted despite a valid outer checksum");
        }
        shutdown_domain();start_domain(config,1,image,saved_time);source=capture(cells);
        require(pending_domain_actor_restores()==3 && snapshot_domain(source.session,0)==image,"Frame-zero restore lost pending stable actor state");
        // A source object with the wrong prefab must not steal the saved identity.
        auto mismatch=inputs[0];mismatch.actor.session=source.session;mismatch.prefab++;
        rejected=false;try { configure_persistent_domain_actors(&mismatch,1,actor_status); } catch(const std::invalid_argument&) { rejected=true; }
        require(rejected && pending_domain_actor_restores()==3,"Mismatched restored prefab consumed a saved actor");
        auto bootstrap=inputs[0];bootstrap.actor.session=source.session;bootstrap.actor.mass_kg=0;
        configure_persistent_domain_actors(&bootstrap,1,actor_status);
        require(pending_domain_actor_restores()==3 && snapshot_domain(source.session,0)==image,"Inactive spawn bootstrap reset saved timing or rate");
        advance_domain(1,.2F,2);auto faulted=capture(cells);
        require(faulted.state==3 && faulted.completed_frames==0 && faulted.time_seconds==saved_time && domain_active(),
            "A worker advanced or released fluid ownership before stable actors rebound");
        shutdown_domain();start_domain(config,1,image,saved_time);source=capture(cells);
        // Reverse command order and change the relative native handle order.
        // Stable identity, rather than allocator history, must order transfers.
        auto rebound=inputs;std::reverse(rebound.begin(),rebound.end());
        for (std::size_t i=0;i<rebound.size();++i) {
            rebound[i].actor.session=source.session;rebound[i].actor.expected_frame=0;rebound[i].actor.native_handle=static_cast<int>(900-i*100);
        }
        configure_persistent_domain_actors(rebound.data(),3,actor_status);
        require(pending_domain_actor_restores()==0 && snapshot_domain(source.session,0)==image,"Rebinding changed saved actor or physical state");
        configure_persistent_domain_actors(rebound.data(),3,actor_status);
        require(snapshot_domain(source.session,0)==image,"An unchanged configuration refresh reset restored actor timing");
        auto expected=image;
        for (std::uint64_t frame=1;frame<=10;++frame) {
            expected=advance_domain_snapshot(expected,.2F);advance_domain(frame,.2F,2);const auto count=drain();source=capture(cells);
            for (std::uint32_t i=0;i<count;++i)
                require(receipts[i].callback==900 || receipts[i].callback==800 || receipts[i].callback==700,
                    "Restored actor published to a stale native handle");
            require(snapshot_domain(source.session,frame)==expected,"Rebound actors diverged from exact saved continuation");
        }
        require(source.maximum_relative_mass_error<1e-8 && source.maximum_relative_energy_error<1e-6,"Restored actor transfers violated conservation");
        shutdown_domain();
    }
    std::cout<<"PASS durable gas/water actors: identity remapping, exact timing/remainder/blocked state, atomic rejection and conservative continuation\n";
}
void initial_inventory() {
    static_assert(sizeof(MAOniDomainInitialCell)==56);
    MAOniDomainConfig config{40,2,1,64,10,10,3,2,0,0};
    std::array<MAOniDomainInitialCell,6> inputs{};
    for (std::size_t i=0;i<inputs.size();++i) {
        auto& cell=inputs[i];cell.struct_size=56;cell.abi_version=1;
        cell.masses[1]=.8+.02*static_cast<double>(i);
        cell.masses[4]=.15;cell.masses[2]=1e-28;
        cell.temperature_k=280+static_cast<double>(i);
    }
    auto reject=[&](const auto& values) {
        bool failed=false;
        try { ma::oni::start_domain(config,1,{},0,values); }
        catch(const std::invalid_argument&) { failed=true; }
        require(failed && !ma::oni::domain_active(),"Invalid initial inventory partially activated ownership");
    };
    reject(std::span(inputs).first(5));
    for (int fault=0;fault<7;++fault) {
        auto bad=inputs;auto& last=bad.back();
        if (fault==0) last.abi_version=2;
        if (fault==1) last.struct_size=0;
        if (fault==2) last.masses[1]=-1;
        if (fault==3) last.masses[4]=std::numeric_limits<double>::quiet_NaN();
        if (fault==4) last.temperature_k=std::numeric_limits<double>::infinity();
        if (fault==5) last.masses[3]=1;
        if (fault==6) std::fill(std::begin(last.masses),std::end(last.masses),0);
        reject(bad);
    }
    std::vector<MAOniDomainCell> cells(6);
    for (const auto scenario : {0U,1U,3U}) {
        config.scenario=scenario;
        auto seed=inputs;
        if (scenario) for (auto& cell : seed) {
            std::fill(std::begin(cell.masses),std::end(cell.masses),0);
            cell.masses[3]=1100;
        }
        ma::oni::start_domain(config,1,{},0,seed);auto status=capture(cells);
        require(status.completed_frames==0 && status.time_seconds==0,"Initial inventory advanced physical time");
        for (std::size_t i=0;i<cells.size();++i) {
            for (std::size_t k=0;k<5;++k)
                require(cells[i].masses[k]==seed[i].masses[k],"Initial inventory changed or invented a constituent");
            require(std::abs(cells[i].temperature-seed[i].temperature_k)<1e-9 &&
                    cells[i].momentum_x==0 && cells[i].momentum_y==0 && cells[i].resolved_kinetic_energy==0,
                    "Initial inventory changed temperature or invented motion");
        }
        ma::oni::advance_domain(1,.2,2);status=capture(cells);
        require(status.state==1 && status.maximum_relative_mass_error<1e-8 && status.maximum_relative_energy_error<1e-6,
                "Imported inventory failed conservative physical continuation");
        const auto image=ma::oni::checkpoint_domain(status.session,1);
        const auto continued=ma::oni::advance_domain_checkpoint(image,.2);
        ma::oni::shutdown_domain();ma::oni::start_domain(config,1,image,status.time_seconds);
        status=capture(cells);
        require(ma::oni::checkpoint_domain(status.session,0)==image,"Imported inventory restore changed its source");
        ma::oni::advance_domain(1,.2,2);status=capture(cells);
        require(status.state==1 && ma::oni::checkpoint_domain(status.session,1)==continued,
                "Imported inventory failed exact checkpoint continuation");
        ma::oni::shutdown_domain();
    }
    std::cout<<"PASS initial gas/water inventory, late atomic rejection, no invented constituents/motion, and exact reload\n";
}
void liquid_gravity() {
    MAOniDomainConfig config{40,2,1,64,10,10,3,2,3,0};
    std::vector<MAOniDomainCell> cells(6);
    auto unshifted=config;unshifted.scenario=1;
    ma::oni::start_domain(unshifted,1);(void)capture(cells);
    const auto physical_reference=cells;
    ma::oni::shutdown_domain();
    ma::oni::start_domain(config,1);
    auto status=capture(cells);
    for (std::size_t i=0;i<cells.size();++i)
        require(std::abs(cells[i].internal_energy-physical_reference[i].internal_energy)<=
                    32*std::numeric_limits<double>::epsilon()*std::abs(physical_reference[i].internal_energy) &&
                std::abs(cells[i].temperature-physical_reference[i].temperature)<1e-8 &&
                std::abs(cells[i].pressure-physical_reference[i].pressure)<1 && cells[i].masses[3]==1100,
                "Water energy basis omitted reference energy or changed its initial physical pressure/temperature");
    MAOniDomainGravityStatus gravity{};ma::oni::copy_domain_gravity_status(gravity);
    require(gravity.enabled==1 && gravity.gravity_m_per_s2==9.81 && gravity.initial_potential_j==6600*9.81,
            "Filled water gravity did not initialize the cell-centre potential datum");
    long double before=gravity.potential_j,scale{};
    for (const auto& cell : cells) { before+=cell.internal_energy+cell.resolved_kinetic_energy;scale+=std::abs(cell.internal_energy)+cell.resolved_kinetic_energy; }
    MAOniDomainTransferStatus transfers{};
    MAOniDomainTransfer input{64,2,status.session,0,10+11*64,3,1,1,71,0,.05F,300};
    ma::oni::queue_domain_transfers(&input,1,transfers);
    ma::oni::advance_domain(1,.2,2);
    std::array<MAOniDomainReceipt,4> receipts{};std::uint32_t count{};
    ma::oni::copy_domain_receipts(status,cells.data(),6,receipts.data(),4,count);
    require(status.state==1 && count==1 && receipts[0].kind==1,"Filled-water gravity injection failed");
    ma::oni::ack_domain_receipts(status.session,receipts[0].sequence);
    input.expected_frame=1;input.public_cell=10+10*64;input.kind=2;input.callback=72;input.mass_kg=.02F;input.temperature_k=0;
    ma::oni::queue_domain_transfers(&input,1,transfers);
    ma::oni::advance_domain(2,.2,2);
    ma::oni::copy_domain_receipts(status,cells.data(),6,receipts.data(),4,count);
    require(status.state==1 && count==1 && receipts[0].kind==2 && receipts[0].kinetic_energy_j>0,
            "Filled-water gravity extraction did not carry moving fluid energy");
    ma::oni::ack_domain_receipts(status.session,receipts[0].sequence);
    for (std::uint64_t frame=3;frame<=1200;++frame) ma::oni::advance_domain(frame,.2,2);
    status=capture(cells);ma::oni::copy_domain_gravity_status(gravity);ma::oni::copy_domain_transfer_status(transfers);
    if (status.state!=1) throw std::runtime_error(std::string("Filled-water gravity frame: ")+status.error);
    long double after=gravity.potential_j;
    for (const auto& cell : cells) { after+=cell.internal_energy+cell.resolved_kinetic_energy;scale+=std::abs(cell.internal_energy)+cell.resolved_kinetic_energy; }
    const auto boundary=(static_cast<double>(.05F)*1.5-static_cast<double>(.02F)*.5)*9.81;
    const auto residual=after-before-transfers.material_energy_change_j-transfers.kinetic_energy_change_j-boundary;
    const auto bound=128*std::numeric_limits<double>::epsilon()*scale;
    std::cout<<std::setprecision(17)<<"Filled-water gravity accounting: frames="<<status.completed_frames
             <<" residual="<<static_cast<double>(residual)<<" bound="<<static_cast<double>(bound)
             <<" maximum="<<gravity.maximum_energy_residual_j<<" body="<<gravity.body_impulse_y
             <<" pressure head="<<cells[0].pressure-cells[3].pressure<<'\n';
    require(gravity.body_impulse_y<0 && gravity.potential_j!=gravity.initial_potential_j && cells[0].pressure>cells[3].pressure &&
            std::abs(gravity.committed_boundary_potential_j-boundary)<1e-15 && std::abs(residual)<=bound &&
            gravity.maximum_energy_residual_j<=bound,"Filled-water pressure head, body force or U+K+G accounting failed");
    require(std::all_of(cells.begin(),cells.end(),[](const auto& cell) { return cell.liquid_fraction==1 && cell.masses[3]>1000; }),
            "Filled-water gravity changed phase or lost occupied liquid");
    const auto image=ma::oni::checkpoint_domain(status.session,status.completed_frames);
    const auto continued=ma::oni::advance_domain_checkpoint(image,.2);
    auto wrong=config;wrong.scenario=1;bool rejected{};
    try { ma::oni::validate_domain_checkpoint(wrong,image,status.time_seconds); } catch(const std::invalid_argument&) { rejected=true; }
    require(rejected,"Gravity water checkpoint admitted with gravity disabled");
    const auto saved_time=status.time_seconds;ma::oni::shutdown_domain();
    ma::oni::start_domain(config,1,image,saved_time);status=capture(cells);
    require(status.completed_frames==0 && ma::oni::checkpoint_domain(status.session,0)==image,"Water gravity restore changed its physical source");
    ma::oni::advance_domain(1,.2,2);status=capture(cells);
    require(status.state==1 && ma::oni::checkpoint_domain(status.session,1)==continued,"Water gravity reload lost exact physical continuation");
    ma::oni::shutdown_domain();
    std::cout<<"PASS filled-water gravity, pressure head, carried boundary potential, U+K+G residual="<<static_cast<double>(residual)
             <<" bound="<<static_cast<double>(bound)<<" and exact reload\n";
}
void certified_thermal_intervals() {
    using namespace ma::oni;
    shutdown_domain();
    MAOniDomainConfig config{40,2,1,64,10,10,3,2,1,0};
    start_domain(config,1);
    std::vector<MAOniDomainCell> cells(6);
    for(std::uint64_t ticket=1;ticket<=5;++ticket)advance_domain(ticket,.2,2);
    auto state=capture(cells);MAOniDomainThermalStatus thermal{};copy_domain_thermal_status(thermal);
    require(state.substeps==5 && thermal.stages==0 && thermal.exact_equilibrium_intervals==5 && thermal.completed_frame==5,
        "Certified idle frames were counted as physical conduction or lost their clock");
    MAOniDomainHeat request{48,1,state.session,state.completed_frames,10+10*64,17,1000000,1000};
    MAOniDomainHeatStatus heat{};queue_domain_heat(&request,1,heat);advance_domain(6,.2,2);
    state=capture(cells);copy_domain_thermal_status(thermal);
    require(thermal.abi_version==2 && thermal.struct_size==88 && thermal.exact_equilibrium_intervals==5 &&
        thermal.stages==2*(state.substeps-thermal.exact_equilibrium_intervals) && thermal.stages>0 &&
        thermal.contacts>0 && thermal.redistributed_energy_j>0 && thermal.entropy_change_j_per_k>0,
        "Heat after certified idle frames did not retain the exact physical-stage clock");
    const auto checkpoint=snapshot_domain(state.session,state.completed_frames);const auto time=state.time_seconds;
    shutdown_domain();start_domain(config,1,checkpoint,time);copy_domain_thermal_status(thermal);
    require(thermal.exact_equilibrium_intervals==0 && thermal.stages==0,"Restored owner leaked previous-session thermal counters");
    advance_domain(1,.2,2);state=capture(cells);copy_domain_thermal_status(thermal);
    require(thermal.stages==2*(state.substeps-thermal.exact_equilibrium_intervals) && thermal.redistributed_energy_j>0,
        "Restored heated owner lost its physical-stage accounting");
    shutdown_domain();
    std::cout<<"PASS certified idle intervals, later conduction and restored thermal clocks\n";
}
}
int main(int argc,char** argv) {
    try {
        if (argc==3 && std::string(argv[1])=="--read-checkpoint") {
            std::ifstream input(argv[2],std::ios::binary); require(input.good(),"Could not open the cross-platform native-domain image");
            const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(input),{}};
            require(ma::oni::advance_domain_checkpoint(bytes,0)==bytes,"Native-domain checkpoint changed across platforms");
        } else require(argc==1,"Invalid native domain test arguments");
        static_assert(sizeof(MAOniDomainConfig)==40);
        static_assert(sizeof(MAOniDomainStatus)==440);
        static_assert(sizeof(MAOniDomainCell)==96);
        static_assert(sizeof(MAOniDomainHeat)==48 && sizeof(MAOniDomainHeatStatus)==56);
        static_assert(sizeof(MAOniDomainTransfer)==64 && sizeof(MAOniDomainReceipt)==88 && sizeof(MAOniDomainTransferStatus)==104);
        static_assert(sizeof(MAOniDomainActor)==80 && sizeof(MAOniDomainActorStatus)==64);
        atomic_solid_startup();
        solid_boundary_heat();
        persistent_actors();
        initial_inventory();
        certified_thermal_intervals();
        MAOniDomainConfig config{40,2,1,64,10,10,3,2,0,0};
        std::vector<MAOniDomainCell> cells(6);
        ma::oni::start_domain(config,1);
        require(ma::oni::domain_active(),"domain did not activate");
        auto initial=capture(cells);
        const auto initial_image=ma::oni::checkpoint_domain(initial.session,0);
        require(initial_image.size()>12 && initial_image[8]==6 && initial_image[9]==0 && initial_image[10]==0 && initial_image[11]==0,
            "New native owner did not persist the production kernel with midpoint pressure");
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
        const auto liquid_initial=cells;
        MAOniDomainTransfer liquid_in{64,2,final.session,final.completed_frames,10+10*64,3,1,1,21,0,.05F,300};
        auto liquid_out=liquid_in; liquid_out.kind=2; liquid_out.callback=22; liquid_out.mass_kg=.02F; liquid_out.temperature_k=0;
        ma::oni::queue_domain_transfers(&liquid_in,1,transfer_status);
        const auto liquid_pending=transfer_status;
        std::array invalid_liquid{liquid_out,liquid_out}; invalid_liquid[1].mass_kg=200;
        rejected=false;
        try { ma::oni::queue_domain_transfers(invalid_liquid.data(),2,transfer_status); } catch (const std::exception&) { rejected=true; }
        require(rejected && std::memcmp(&liquid_pending,&transfer_status,sizeof(transfer_status))==0,
            "Later void-creating liquid request altered an earlier pending command or output");
        ma::oni::advance_domain(12,.2,2);
        ma::oni::copy_domain_receipts(final,cells.data(),6,receipts.data(),4,receipt_count);
        if (final.state!=1) throw std::runtime_error(std::string("Native liquid injection advance: ")+final.error);
        require(final.state==1 && receipt_count==1 && receipts[0].species==3 && receipts[0].kind==1 && receipts[0].callback==21 &&
            receipts[0].mass_kg==static_cast<double>(.05F) && std::isfinite(receipts[0].internal_energy_j) && receipts[0].internal_energy_j!=0,
            "Native liquid injection lost its publication receipt");
        ma::oni::ack_domain_receipts(final.session,receipts[0].sequence);
        liquid_out.expected_frame=final.completed_frames;
        ma::oni::queue_domain_transfers(&liquid_out,1,transfer_status);
        ma::oni::advance_domain(13,.2,2);
        ma::oni::copy_domain_receipts(final,cells.data(),6,receipts.data(),4,receipt_count);
        ma::oni::copy_domain_transfer_status(transfer_status);
        require(final.state==1 && receipt_count==1 && receipts[0].species==3 && receipts[0].kind==2 && receipts[0].callback==22 &&
            receipts[0].mass_kg==static_cast<double>(.02F) && receipts[0].kinetic_energy_j>0 &&
            transfer_status.accepted_batches==2 && transfer_status.committed_batches==2 && transfer_status.pending_batches==0 &&
            transfer_status.mass_changes_kg[3]==static_cast<double>(.05F)-static_cast<double>(.02F),
            "Native liquid extraction lost carried mechanics or admitted a rejected command");
        long double liquid_mass=0,liquid_energy=0,liquid_scale=0;
        for (std::size_t i=0;i<cells.size();++i) {
            liquid_mass+=static_cast<long double>(cells[i].masses[3])-liquid_initial[i].masses[3];
            liquid_energy+=static_cast<long double>(cells[i].internal_energy)-liquid_initial[i].internal_energy+cells[i].resolved_kinetic_energy;
            liquid_scale+=std::abs(cells[i].internal_energy)+std::abs(liquid_initial[i].internal_energy);
        }
        require(std::abs(liquid_mass-transfer_status.mass_changes_kg[3])<1e-9 &&
            std::abs(liquid_energy-transfer_status.material_energy_change_j-transfer_status.kinetic_energy_change_j)<=
                128*std::numeric_limits<double>::epsilon()*liquid_scale,"Native liquid transfer ledger disagrees with conserved physical state");
        ma::oni::ack_domain_receipts(final.session,receipts[0].sequence);
        MAOniDomainActor water_consumer{80,1,final.session,final.completed_frames,31,1,10+10*64,3,0,0,.1F,0,0,0};
        MAOniDomainActor water_emitter{80,1,final.session,final.completed_frames,32,2,11+10*64,3,0,0,.03125F,.2,300,10000};
        const std::array water_actors{water_consumer,water_emitter};
        ma::oni::configure_domain_actors(water_actors.data(),2,actor_status);
        auto wrong_water_actors=water_actors; wrong_water_actors[0].mass_kg=.5F; wrong_water_actors[1].species=1;
        const auto held_water_actors=actor_status; rejected=false;
        try { ma::oni::configure_domain_actors(wrong_water_actors.data(),2,actor_status); } catch (const std::invalid_argument&) { rejected=true; }
        require(rejected && std::memcmp(&held_water_actors,&actor_status,sizeof(actor_status))==0,
            "Late liquid actor phase mismatch changed an earlier rate or output");
        std::size_t water_samples=0,water_emissions=0,water_blocked=0,water_unblocked=0; double water_consumed=0,water_emitted=0;
        const auto water_initial_account=transfer_status.mass_changes_kg[3];
        for (std::uint64_t ticket=14;ticket<=21;++ticket) {
            if (ticket==16 || ticket==18) {
                water_emitter.expected_frame=final.completed_frames; water_emitter.maximum_gas_density_kg_m3=ticket==16 ? 1000 : 10000;
                ma::oni::configure_domain_actor(water_emitter,actor_status);
            }
            ma::oni::advance_domain(ticket,.2,2);
            ma::oni::copy_domain_receipts(final,cells.data(),6,receipts.data(),4,receipt_count);
            if (final.state!=1) throw std::runtime_error(std::string("Liquid actor advance: ")+final.error);
            for (std::size_t i=0;i<receipt_count;++i) {
                const auto& receipt=receipts[i]; require(receipt.species==3,"Liquid actor changed material identity");
                if (receipt.kind==3) { ++water_samples; water_consumed+=receipt.mass_kg; require(receipt.callback==31 && receipt.mass_kg>0,"Liquid consumer lost its full handle or mass"); }
                else if (receipt.kind==4) { ++water_emissions; water_emitted+=receipt.mass_kg; }
                else if (receipt.kind==5) ++water_blocked;
                else if (receipt.kind==6) ++water_unblocked;
                else throw std::runtime_error("Unexpected liquid actor receipt");
            }
            ma::oni::ack_domain_receipts(final.session,receipts[receipt_count-1].sequence);
        }
        ma::oni::copy_domain_transfer_status(transfer_status);
        require(water_samples==8 && water_emissions==8 && water_blocked==1 && water_unblocked==1 &&
            std::abs(water_consumed-8*static_cast<double>(.1F)*.2)<1e-8 &&
            std::abs(transfer_status.mass_changes_kg[3]-water_initial_account+water_consumed-water_emitted)<1e-15,
            "Liquid actor cadence, rate, density edges or mass account differs");
        water_consumer.expected_frame=water_emitter.expected_frame=final.completed_frames; water_consumer.flags=water_emitter.flags=1;
        const std::array retire_water{water_consumer,water_emitter}; ma::oni::configure_domain_actors(retire_water.data(),2,actor_status);
        ma::oni::advance_domain(22,.2,2); ma::oni::copy_domain_receipts(final,cells.data(),6,receipts.data(),4,receipt_count);
        ma::oni::copy_domain_actor_status(actor_status);
        require(final.state==1 && receipt_count==0 && actor_status.live_actors==0 && actor_status.consumer_samples==8 && actor_status.emitter_samples==8,
            "Retired liquid actors continued to transfer mass or publish samples");
        water_consumer.expected_frame=final.completed_frames; water_consumer.flags=0;
        auto void_consumer=water_consumer; void_consumer.native_handle=33; void_consumer.public_cell+=1; void_consumer.mass_kg=1000;
        const std::array void_actors{water_consumer,void_consumer}; ma::oni::configure_domain_actors(void_actors.data(),2,actor_status);
        const auto before_void_cells=cells; const auto before_void_status=final; const auto before_void_actor_steps=actor_status.actor_steps;
        ma::oni::copy_domain_transfer_status(transfer_status); const auto before_void_account=transfer_status;
        ma::oni::advance_domain(23,.2,2); ma::oni::copy_domain_receipts(final,cells.data(),6,receipts.data(),4,receipt_count);
        ma::oni::copy_domain_actor_status(actor_status); ma::oni::copy_domain_transfer_status(transfer_status);
        require(final.state==3 && final.completed_frames==before_void_status.completed_frames && final.time_seconds==before_void_status.time_seconds &&
            receipt_count==0 && actor_status.actor_steps==before_void_actor_steps &&
            std::memcmp(cells.data(),before_void_cells.data(),cells.size()*sizeof(cells[0]))==0 &&
            transfer_status.mass_changes_kg[3]==before_void_account.mass_changes_kg[3] &&
            transfer_status.material_energy_change_j==before_void_account.material_energy_change_j &&
            ma::oni::suppress_domain_cell(MA_ONI_LIQUID,native_cell(10,10)),
            "Later void-creating liquid actor partially committed state, mechanics, callbacks, clock or exclusions");
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
        rejected=false;
        try { (void)ma::oni::checkpoint_domain(final.session,final.completed_frames); } catch (const std::logic_error&) { rejected=true; }
        require(rejected,"A physical checkpoint omitted unacknowledged game callbacks");
        ma::oni::ack_domain_receipts(final.session,receipts[receipt_count-1].sequence);
        const auto checkpoint=ma::oni::checkpoint_domain(final.session,final.completed_frames);
        const auto checkpoint_time=final.time_seconds; const auto checkpoint_session=final.session;
        require(ma::oni::advance_domain_checkpoint(checkpoint,0)==checkpoint,"Native physical image did not round trip exactly");
        const auto continued=ma::oni::advance_domain_checkpoint(checkpoint,.2);
        ma::oni::advance_domain(2,.2,2); final=capture(cells);
        require(final.state==1 && ma::oni::checkpoint_domain(final.session,final.completed_frames)==continued,
            "Restored native physical image diverged from the actual next worker timestep");
        rejected=false;
        try { (void)ma::oni::checkpoint_domain(final.session,1); } catch (const std::logic_error&) { rejected=true; }
        require(rejected,"Stale checkpoint source epoch was accepted");
        request={48,1,final.session,final.completed_frames,10+10*64,17,10,1000}; ma::oni::queue_domain_heat(&request,1,heat);
        rejected=false;
        try { (void)ma::oni::checkpoint_domain(final.session,final.completed_frames); } catch (const std::logic_error&) { rejected=true; }
        require(rejected,"A checkpoint dropped reserved pending heat");
        ma::oni::shutdown_domain();
        ma::oni::validate_domain_checkpoint(config,checkpoint,checkpoint_time);
        for (int malformed=0;malformed<4;++malformed) {
            auto wrong=config; auto when=checkpoint_time; auto image=checkpoint;
            if (malformed==0) ++wrong.width;
            if (malformed==1) wrong.scenario=1;
            if (malformed==2) when+=1;
            if (malformed==3) image.back()^=1;
            rejected=false;
            try { ma::oni::validate_domain_checkpoint(wrong,image,when); } catch (const std::exception&) { rejected=true; }
            require(rejected && !ma::oni::domain_active(),"Invalid saved geometry, phase, clock or image activated native ownership");
        }
        ma::oni::start_domain(config,1,checkpoint,checkpoint_time); final=capture(cells);
        require(final.session!=checkpoint_session && final.completed_frames==0 && final.time_seconds==checkpoint_time &&
            ma::oni::checkpoint_domain(final.session,0)==checkpoint,"Native restore reset canonical state, revision or physical clock");
        ma::oni::advance_domain(3,.2,2); final=capture(cells);
        require(final.state==1 && final.completed_frames==1 && ma::oni::checkpoint_domain(final.session,1)==continued,
            "Restored native owner did not continue to the exact uninterrupted physical state");
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
        liquid_gravity();
        config.offset_x=63;
        try { ma::oni::start_domain(config,1); throw std::runtime_error("row-wrap geometry accepted"); }
        catch (const std::invalid_argument&) {}
        require(!ma::oni::domain_active(),"rejected geometry activated hooks");
        std::cout << "PASS sealed-domain native scheduling, transport, conservation, compressed EOS, atomic failure and exclusions\n";
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
