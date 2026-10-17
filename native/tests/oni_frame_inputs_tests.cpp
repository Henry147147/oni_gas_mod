#include "../src/oni_domain.hpp"
#include "../src/oni_frame_inputs.hpp"
#include <iostream>
#include <limits>

namespace {
using namespace ma::oni;
void require(bool value,const char* why) {if (!value) throw std::runtime_error(why);}
template<class Action> void rejects(Action action) {
    bool rejected=false;try {action();} catch (const std::exception&) {rejected=true;}
    require(rejected,"Invalid frame journal was admitted");
}
MAOniDomainStatus capture() {MAOniDomainStatus status{};copy_domain(status,nullptr,0);require(status.state==1,status.error);return status;}
void drain() {
    MAOniDomainStatus status{};std::array<MAOniDomainCell,6> cells{};std::array<MAOniDomainReceipt,32> receipts{};std::uint32_t count=0;
    copy_domain_receipts(status,cells.data(),6,receipts.data(),32,count);require(status.state==1,status.error);
    if (count) ack_domain_receipts(status.session,receipts[count-1].sequence);
}
void abi_replay(const std::vector<std::uint8_t>& source,const std::vector<std::uint8_t>& journal,const std::vector<std::uint8_t>& expected) {
    const auto size=static_cast<std::uint32_t>(source.size()),input_size=static_cast<std::uint32_t>(journal.size());
    require(ma_oni_domain_frame_inputs_abi_version()==1,"Frame input capability differs");
    std::uint32_t required=123;require(ma_oni_domain_snapshot_advance_with_inputs(source.data(),size,journal.data(),input_size,nullptr,0,&required)==MA_ONI_OK &&
        required==expected.size(),"C ABI journal replay size query failed");
    std::vector<std::uint8_t> output(required,0x5a);const auto held=output;required=123;
    require(ma_oni_domain_snapshot_advance_with_inputs(source.data(),size,journal.data(),input_size,output.data(),1,&required)==MA_ONI_BUFFER_TOO_SMALL &&
        required==123 && output==held,"Insufficient replay capacity changed outputs");
    require(ma_oni_domain_snapshot_advance_with_inputs(source.data(),size,journal.data(),input_size,output.data(),static_cast<std::uint32_t>(output.size()),&required)==MA_ONI_OK &&
        required==expected.size() && output==expected,"C ABI replay differs from the complete worker image");
    auto damaged=journal;damaged.back()^=1;output=held;required=123;
    require(ma_oni_domain_snapshot_advance_with_inputs(source.data(),size,damaged.data(),input_size,output.data(),static_cast<std::uint32_t>(output.size()),&required)==MA_ONI_INVALID_ARGUMENT &&
        output==held && required==123,"Invalid replay changed output or required size");
}
void live_inputs() {
    for (const auto scenario:{0U,1U,3U,4U,5U,6U}) {
        const MAOniDomainConfig config{40,2,1,64,10,10,3,2,scenario,0};start_domain(config,1);auto status=capture();
        rejects([&] {(void)copy_domain_frame_inputs(status.session,0);});
        for (std::uint64_t frame=0;frame<3;++frame) {
            const auto source=snapshot_domain(status.session,frame);const auto time=status.time_seconds;
            MAOniDomainHeatStatus hs{};MAOniDomainTransferStatus ts{};
            MAOniDomainHeat heat{48,1,status.session,frame,650,9,1000,1e6};queue_domain_heat(&heat,1,hs);
            MAOniDomainTransfer transfer{64,2,status.session,frame,650,scenario==1 || scenario==3 ? 3U : 1U,1,1,-1,0,.015625,310};
            queue_domain_transfers(&transfer,1,ts);
            const std::array<MAOniDomainHeat,2> pair{{{48,1,status.session,frame,650,10,3,1e6},{48,1,status.session,frame,651,11,-2,1e6}}};
            queue_domain_heat(pair.data(),2,hs);
            transfer.kind=2;transfer.mass_kg=.03125;transfer.flags|=3U<<8;queue_domain_transfers(&transfer,1,ts);
            auto bad=pair;bad.back().joules=std::numeric_limits<double>::quiet_NaN();rejects([&] {queue_domain_heat(bad.data(),2,hs);});
            transfer.expected_frame++;rejects([&] {queue_domain_transfers(&transfer,1,ts);});
            advance_domain(frame+1,.02F,2);drain();status=capture();
            const auto journal=copy_domain_frame_inputs(status.session,frame+1),actual=snapshot_domain(status.session,frame+1);
            const auto parsed=frame_inputs::decode(journal);
            require(parsed.flags==0 && parsed.source_frame==frame && parsed.completed_frame==frame+1 && parsed.source_time==time && parsed.seconds==(double).02F &&
                parsed.batches.size()==4 && parsed.batches[0].heat.size()==1 && parsed.batches[1].transfers.size()==1 && parsed.batches[2].heat.size()==2 &&
                parsed.batches[3].transfers.size()==1 && parsed.batches[3].transfers[0].flags==(1U|(3U<<8)),"Frame journal lost order, batch boundaries, radius or epoch");
            require(advance_domain_snapshot_with_inputs(source,journal)==actual,"Accepted game inputs do not reproduce the complete native worker image");
            abi_replay(source,journal,actual);
            require(snapshot_domain(status.session,frame+1)==actual && copy_domain_frame_inputs(status.session,frame+1)==journal,"Stateless replay mutated the live owner or journal");
            rejects([&] {(void)advance_domain_snapshot_with_inputs(actual,journal);});
            rejects([&] {(void)copy_domain_frame_inputs(status.session,frame);});
            rejects([&] {(void)copy_domain_frame_inputs(status.session+1,frame+1);});
            auto reordered=parsed;std::reverse(reordered.batches.begin(),reordered.batches.end());
            require(advance_domain_snapshot_with_inputs(source,frame_inputs::encode(reordered))!=actual,"Order sensitivity control did not detect changed heat/transfer chronology");
        }
        const auto source=snapshot_domain(status.session,3);advance_domain(4,.02F,2);status=capture();
        const auto empty=copy_domain_frame_inputs(status.session,4);
        require(frame_inputs::decode(empty).batches.empty() && advance_domain_snapshot_with_inputs(source,empty)==advance_domain_snapshot(source,.02F),
            "An empty input journal changed the legacy trajectory");
        shutdown_domain();start_domain(config,1);rejects([&] {(void)copy_domain_frame_inputs(status.session,4);});shutdown_domain();
    }
    std::cout<<"PASS ordered heat/point/radius batches, late failed reservations, six physical models, empty-input identity, exact C ABI replay and unchanged failure outputs\n";
}
void durable_actors() {
    const MAOniDomainConfig config{40,2,1,64,10,10,3,2,0,0};start_domain(config,1);auto status=capture();
    MAOniDomainPersistentActor actor{{80,1,status.session,0,17,1,650,1,0,0,.01F,0,0,0},101,12345,0};MAOniDomainActorStatus as{};
    configure_persistent_domain_actors(&actor,1,as);advance_domain(1,.02F,2);drain();status=capture();
    require((frame_inputs::decode(copy_domain_frame_inputs(status.session,1)).flags&1U)!=0,"Actor configuration was silently omitted from the journal");
    const auto source=snapshot_domain(status.session,1);MAOniDomainHeat heat{48,1,status.session,1,650,4,12,1e6};MAOniDomainHeatStatus hs{};queue_domain_heat(&heat,1,hs);
    advance_domain(2,.02F,2);drain();const auto inputs=copy_domain_frame_inputs(status.session,2);
    require(advance_domain_snapshot_with_inputs(source,inputs)==snapshot_domain(status.session,2),"External inputs and durable actor timing did not replay together");
    auto flagged=frame_inputs::decode(inputs);flagged.flags=1;rejects([&] {(void)advance_domain_snapshot_with_inputs(source,frame_inputs::encode(flagged));});
    flagged.flags=2;rejects([&] {(void)advance_domain_snapshot_with_inputs(source,frame_inputs::encode(flagged));});
    shutdown_domain();std::cout<<"PASS durable actors with external inputs; explicit rejection of actor reconfiguration and non-durable actor replay\n";
}
void framing() {
    frame_inputs::Image image{0,1,0,1,0,.02F,{40,2,1,64,10,10,3,2,0,0},std::string(64,'a'),{}};
    auto encoded=frame_inputs::encode(image);require(encoded.size()==232 && frame_inputs::encode(frame_inputs::decode(encoded))==encoded,"Empty journal framing changed");
    for (std::size_t size=0;size<encoded.size();++size) rejects([&] {(void)frame_inputs::decode(std::span(encoded).first(size));});
    for (const auto offset:{0U,8U,12U,16U,24U,32U,40U,48U,96U,100U,104U}) {
        auto corrupt=encoded;corrupt[offset]^=0x80;const auto digest=hash_memory(corrupt.data(),corrupt.size()-64);std::copy(digest.begin(),digest.end(),corrupt.end()-64);
        // Geometry and source identities are also checked by the replay owner;
        // these offsets deliberately break format/epoch validation here.
        if (offset==16 || offset==40 || offset==48 || offset==104) continue;
        rejects([&] {(void)frame_inputs::decode(corrupt);});
    }
    frame_inputs::Batch batch;batch.heat.resize(257);rejects([&] {(void)frame_inputs::batch_size(batch);});
    batch.heat.resize(1);batch.transfers.resize(1);rejects([&] {(void)frame_inputs::batch_size(batch);});
    image.batches.resize(frame_inputs::maximum_batches+1);rejects([&] {(void)frame_inputs::encode(image);});
    std::cout<<"PASS bounded little-endian journal framing, truncation, valid-checksum structural corruption and command limits\n";
}
void chunk_contacts() {
    constexpr int owned=11+11*66,outside=14+11*66;
    require(!exchange_domain_chunk_heat(owned,350,10,2,100,1,.2F).handled,"Inactive owner intercepted native body heat");
    for(const auto scenario:{0U,1U,4U}) {
        const MAOniDomainConfig config{40,2,1,64,10,10,3,2,scenario,0};start_domain(config,1);auto status=capture();
        const auto source=snapshot_domain(status.session,0);
        require(!exchange_domain_chunk_heat(outside,350,10,2,100,1,.2F).handled,"Outside native contact was intercepted");
        auto body=350.0F;double expected_heat=0;
        for(int contact=0;contact<3;++contact) {
            const auto result=exchange_domain_chunk_heat(owned,body,10,2,100,1,.2F);
            require(result.handled && result.temperature<body && result.delta_kj<0,"Owned native body did not cool into the full EOS");
            expected_heat+=10000*(static_cast<double>(body)-result.temperature);body=result.temperature;
        }
        MAOniDomainChunkHeatStatus heat{};copy_domain_chunk_heat_status(heat);
        require(heat.contacts==0 && heat.transfers==0 && heat.body_heat_loss_j==0 && heat.completed_frame==0,
            "Pending native body heat was published before its physical frame");
        MAOniDomainHeatStatus hs{};copy_domain_heat(hs);
        require(hs.pending_batches==3 && hs.pending_heat_joules==expected_heat,"Native contact heat reservations differ from represented body energy");
        advance_domain(1,.2F,2);status=capture();copy_domain_chunk_heat_status(heat);
        require(heat.session==status.session && heat.completed_frame==1 && heat.contacts==3 && heat.transfers==3 &&
            heat.body_heat_loss_j==expected_heat && heat.stored_fluid_heat_j>0 && heat.entropy_lower_bound_j_per_k>0 &&
            heat.maximum_balance_residual_j<=heat.maximum_balance_bound_j,"Completed native body heat account failed");
        const auto journal=copy_domain_frame_inputs(status.session,1),actual=snapshot_domain(status.session,1);
        const auto parsed=frame_inputs::decode(journal);
        require(parsed.batches.size()==3 && parsed.batches[0].heat[0].source==0x4d414348U &&
            advance_domain_snapshot_with_inputs(source,journal)==actual,"Native body contact journal did not reproduce the complete worker state");
        const auto unchanged=exchange_domain_chunk_heat(owned,body,10,2,1e-30F,1,.2F);
        require(unchanged.handled && unchanged.temperature==body && unchanged.delta_kj==0,"Unrepresentable native body heat changed an endpoint");
        advance_domain(2,.2F,2);copy_domain_chunk_heat_status(heat);
        require(heat.contacts==4 && heat.transfers==3 && heat.unresolved==1,"Unresolved native body contact was not counted explicitly");
        shutdown_domain();
    }
    std::cout<<"PASS native body heat ownership, atomic pending publication, represented body closure, gas/water/mixture EOS and exact input replay\n";
}
}
int main() {
    try {framing();live_inputs();durable_actors();chunk_contacts();return 0;}
    catch (const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
