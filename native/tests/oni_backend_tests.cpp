#include "mixed_atmospheres/oni_conduit_contact.h"
#include "mixed_atmospheres/oni_backend.h"
#include "mixed_atmospheres/oni_domain.h"
#include "mixed_atmospheres/oni_publication.h"
#include "mixed_atmospheres/oni_colony.h"
#include "../src/oni_sha256.hpp"
#include "../src/oni_funchook_storage.hpp"
#include "../src/oni_body_heat_trace.hpp"
#include "../src/oni_chunk_heat_trace.hpp"
#include "../src/oni_storage_heat_native.hpp"
#include "../src/oni_conduit_heat.hpp"
#include "../src/oni_runtime_trace.hpp"
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#if defined(__linux__)
#include <unistd.h>
#endif

static void require(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static void capture_file_controls() {
    const auto directory=std::filesystem::temp_directory_path()/
        ("ma-capture-file-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    require(std::filesystem::create_directory(directory),"fresh capture fixture directory");
    for(const auto binary:{false,true}) {
        const auto path=(directory/(binary?"binary":"text")).string();
        auto* file=ma::oni::capture_file(path.c_str(),binary);
        require(file!=nullptr,"first exclusive capture open failed");
        require(std::fputs("kept",file)>=0 && std::fclose(file)==0,"capture fixture write failed");
        require(ma::oni::capture_file(path.c_str(),binary)==nullptr,"existing capture was reopened for overwrite");
        std::ifstream source(path,std::ios::binary);std::string contents;
        source>>contents;require(contents=="kept","exclusive capture changed existing bytes");
    }
    std::filesystem::remove_all(directory);
}
static void body_heat_trace_controls() {
#if defined(__linux__)
    using ma::oni::body_heat_trace::body;
    std::array<std::byte,0x500> frame{};std::array<std::byte,0x600> sim{};std::array<std::byte,44> record{};
    std::array<std::int32_t,1> mapping{0};std::array<std::uint8_t,1> generations{1};
    const auto put=[](auto& target,std::size_t offset,const auto& value){std::memcpy(target.data()+offset,&value,sizeof(value));};
    put(sim,0x438,record.data());put(sim,0x440,record.data()+record.size());
    put(sim,0x480,mapping.data());put(sim,0x488,mapping.data()+mapping.size());
    put(sim,0x498,generations.data());put(sim,0x4a0,generations.data()+generations.size());
    put(record,0,std::bit_cast<std::uint32_t>(300.f));put(record,16,std::bit_cast<std::uint32_t>(18.f));
    put(record,28,std::int32_t{10});put(record,32,std::int32_t{20});put(record,36,std::int32_t{12});put(record,40,std::int32_t{23});
    const auto original_sim=sim;const auto original_frame=frame;const auto original_record=record;
    const auto sample=body(sim.data(),0x01000000);
    require(sample.valid && sample.area==6 && sample.temperature_bits==std::bit_cast<std::uint32_t>(300.f) &&
        sample.capacity_per_cell_bits==std::bit_cast<std::uint32_t>(18.f),"native registered body capacity/area view");
    require(!body(nullptr,0).valid && !body(sim.data(),0x02000000).valid && !body(sim.data(),0x01000001).valid,
        "native body stale generation or missing mapping was observed");
    mapping[0]=-1;require(!body(sim.data(),0x01000000).valid,"retired native body mapping accepted");mapping[0]=0;
    put(record,36,std::int32_t{9});require(!body(sim.data(),0x01000000).valid,"negative native registered area accepted");record=original_record;
    require(sim==original_sim && frame==original_frame && record==original_record && mapping[0]==0 && generations[0]==1,
        "native body observation modified its source");
    std::array<std::uint32_t,4> message{0x01000000,std::bit_cast<std::uint32_t>(-1.f),std::bit_cast<std::uint32_t>(280.f),std::bit_cast<std::uint32_t>(320.f)};
    put(frame,0x1b8,message.data());put(frame,0x1c0,message.data()+message.size());
    char path[]="/tmp/ma-body-heat-trace-test-XXXXXX";const int descriptor=mkstemp(path);
    require(descriptor>=0,"native body trace fixture path");(void)close(descriptor);(void)unlink(path);
    require(setenv("MA_ONI_BODY_HEAT_TRACE",path,1)==0 && ma::oni::body_heat_trace::Frame::enabled(),"native body trace opt-in");
    std::fenv_t initial{};require(std::fegetenv(&initial)==0,"native body trace initial environment");const int initial_errno=errno;
    for(const auto rounding:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}) {
        require(std::fesetround(rounding)==0,"native body trace rounding control");
        (void)std::feclearexcept(FE_ALL_EXCEPT);(void)std::feraiseexcept(FE_DIVBYZERO);errno=111;
        {
            ma::oni::body_heat_trace::Frame observation(7,frame.data(),sim.data());
            require(errno==111 && std::fegetround()==rounding && std::fetestexcept(FE_ALL_EXCEPT)==FE_DIVBYZERO,
                "native body trace capture changed caller math or errno");
            put(record,0,std::bit_cast<std::uint32_t>(299.f));errno=222;(void)std::feraiseexcept(FE_INVALID);
        }
        require(errno==222 && std::fegetround()==rounding && std::fetestexcept(FE_ALL_EXCEPT)==(FE_DIVBYZERO|FE_INVALID),
            "native body trace emission changed original return math or errno");
    }
    (void)std::fesetenv(&initial);errno=initial_errno;
    auto* output=std::fopen(path,"r");require(output!=nullptr,"native body trace output absent");
    std::array<char,1024> text{};const auto bytes=std::fread(text.data(),1,text.size()-1,output);(void)std::fclose(output);(void)unlink(path);
    require(bytes>0 && std::strstr(text.data(),"capacity_per_cell_kj_k_bits") && std::strstr(text.data(),"41900000\t6\t3"),
        "native body trace did not retain actual capacity, area and valid paired observations");
    std::cout<<"PASS native body heat observation: actual registered area/capacity, stale/retired rejection, readonly source and preserved math/errno\n";
#endif
}
static void runtime_context_controls() {
    std::array<std::byte,168> sim{};
    const auto put=[&](std::size_t offset,const auto& value){std::memcpy(sim.data()+offset,&value,sizeof(value));};
    put(0,std::int32_t{256});put(4,std::int32_t{384});put(8,std::int32_t{98304});
    put(12,std::uint32_t{0xffffffffU});put(16,std::int32_t{-1});put(20,std::int32_t{2});
    put(144,std::uint16_t{65535});put(160,std::uint16_t{3});const auto original=sim;
    for(const auto windows:{false,true}) {
        const auto value=ma::oni::runtime_trace::context(sim.data(),windows);
        require(value.width==256&&value.height==384&&value.cells==98304&&value.random==0xffffffffU&&
            value.iterate==-1&&value.displacement==2&&value.tick==(windows?3:65535),"native runtime context layout differs");
    }
    require(sim==original&&ma::oni::runtime_trace::context(nullptr,false).cells==0,"native context observer changed source or null contract");
}
static void chunk_heat_trace_controls() {
#if defined(__linux__)
    using ma::oni::chunk_heat_trace::chunk;
    std::array<std::byte,0x500> frame{};std::array<std::byte,0x600> sim{};std::array<std::byte,44> record{};
    std::array<std::int32_t,1> mapping{0};std::array<std::uint8_t,1> generations{1};
    const auto put=[](auto& target,std::size_t offset,const auto& value){std::memcpy(target.data()+offset,&value,sizeof(value));};
    put(sim,0x398,record.data());put(sim,0x3a0,record.data()+record.size());
    put(sim,0x3e0,mapping.data());put(sim,0x3e8,mapping.data()+mapping.size());
    put(sim,0x3f8,generations.data());put(sim,0x400,generations.data()+generations.size());
    put(record,0,std::int32_t{123});put(record,4,std::bit_cast<std::uint32_t>(300.f));put(record,8,std::bit_cast<std::uint32_t>(18.f));
    const auto held_sim=sim;const auto held_frame=frame;const auto held_record=record;
    const auto sample=chunk(sim.data(),0x01000000);
    require(sample.valid && sample.cell==123 && sample.temperature_bits==std::bit_cast<std::uint32_t>(300.f) &&
        sample.capacity_bits==std::bit_cast<std::uint32_t>(18.f),"native registered chunk temperature/capacity view");
    require(!chunk(nullptr,0).valid && !chunk(sim.data(),0x02000000).valid && !chunk(sim.data(),0x01000001).valid &&
        !chunk(sim.data(),0x01100000).valid,"native chunk stale generation or missing mapping accepted");
    mapping[0]=-1;require(!chunk(sim.data(),0x01000000).valid,"retired native chunk accepted");
    mapping[0]=1;require(!chunk(sim.data(),0x01000000).valid,"out-of-bounds native chunk accepted");mapping[0]=0;
    require(sim==held_sim && frame==held_frame && record==held_record && mapping[0]==0 && generations[0]==1,
        "native chunk observation modified its source");
    std::array<std::uint32_t,6> messages{0x01000000,std::bit_cast<std::uint32_t>(301.f),std::bit_cast<std::uint32_t>(18.f),
        0x01000000,std::bit_cast<std::uint32_t>(302.f),std::bit_cast<std::uint32_t>(18.f)};
    std::array<std::uint32_t,2> energy{0x01000000,0};
    put(frame,0x248,messages.data());put(frame,0x250,messages.data()+messages.size());
    put(frame,0x290,energy.data());put(frame,0x298,energy.data()+energy.size());
    char path[]="/tmp/ma-chunk-heat-trace-test-XXXXXX";const int descriptor=mkstemp(path);
    require(descriptor>=0,"native chunk trace fixture path");(void)close(descriptor);(void)unlink(path);
    require(setenv("MA_ONI_CHUNK_HEAT_TRACE",path,1)==0 && ma::oni::chunk_heat_trace::Frame::enabled(),"native chunk trace opt-in");
    std::fenv_t initial{};require(std::fegetenv(&initial)==0,"native chunk trace initial environment");const int initial_errno=errno;
    for(const auto rounding:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}) {
        require(std::fesetround(rounding)==0,"native chunk trace rounding control");
        (void)std::feclearexcept(FE_ALL_EXCEPT);(void)std::feraiseexcept(FE_DIVBYZERO);errno=111;
        record=held_record;
        {
            ma::oni::chunk_heat_trace::Frame observation(9,frame.data(),sim.data());
            require(errno==111 && std::fegetround()==rounding && std::fetestexcept(FE_ALL_EXCEPT)==FE_DIVBYZERO,
                "native chunk trace capture changed caller math or errno");
            put(record,4,std::bit_cast<std::uint32_t>(302.f));errno=222;(void)std::feraiseexcept(FE_INVALID);
        }
        require(errno==222 && std::fegetround()==rounding && std::fetestexcept(FE_ALL_EXCEPT)==(FE_DIVBYZERO|FE_INVALID),
            "native chunk trace emission changed return math or errno");
    }
    (void)std::fesetenv(&initial);errno=initial_errno;
    auto* output=std::fopen(path,"r");require(output!=nullptr,"native chunk trace output absent");
    std::array<char,4096> text{};const auto bytes=std::fread(text.data(),1,text.size()-1,output);(void)std::fclose(output);(void)unlink(path);
    require(bytes>0 && std::strstr(text.data(),"requested_capacity_kj_k_bits") &&
        std::strstr(text.data(),"9\t0\t16777216\t43968000\t41900000\t43960000\t41900000\t43970000\t41900000\t123\t123\t3\t0\t0\t1\t0\t0") &&
        std::strstr(text.data(),"9\t1\t16777216\t43970000"),
        "native chunk trace lost ordered requests, paired registered values or intervening-energy warning");
    std::cout<<"PASS native chunk observation: actual T/C, ordered absolute updates, intervening-work flags, stale/retired rejection and preserved math/errno\n";
#endif
}
static void storage_heat_receipt_controls() {
    using ma::oni::storage_heat::Ledger;
    using ma::oni::chunk_heat_trace::Chunk;
    constexpr std::int32_t handle=0x01000000;
    const auto value=[](float t,float c=18.f){return Chunk{std::bit_cast<std::uint32_t>(t),std::bit_cast<std::uint32_t>(c),123,true};};
    const auto reject=[](const auto& action){bool refused=false;try{action();}catch(const std::exception&){refused=true;}require(refused,"invalid storage heat transition accepted");};
    Ledger ledger;ledger.add(handle,300.f,18.f);auto actual=value(299.5f);
    const auto read=[&](std::int32_t requested){require(requested==handle,"receipt read wrong handle");return actual;};
    ledger.sample(read,1);ledger.expect(handle,299.5f,18.f);
    // Published299.5, subsequently cooled299.0, then replaced by the queued
    // stale299.5. Both cooling segments must survive the reset exactly once.
    actual=value(299.f);
    ledger.before(read,[&](std::int32_t,const auto& append){append(std::bit_cast<std::uint32_t>(299.5f),std::bit_cast<std::uint32_t>(18.f));},2);
    actual=value(299.5f);ledger.after(read,2);actual=value(299.25f);ledger.sample(read,2);ledger.sample(read,2);
    std::array<MAOniStorageHeatReceipt,8> receipts{};
    require(ledger.copy(nullptr,0)==3 && ledger.copy(receipts.data(),8)==3,"storage heat lost or duplicated a cooling segment");
    require(receipts[0].before_k==300 && receipts[0].after_k==299.5f && receipts[1].before_k==299.5f && receipts[1].after_k==299 &&
        receipts[2].before_k==299.5f && receipts[2].after_k==299.25f,"storage heat receipt counted a projection reset as physical work");
    for(std::size_t i=0;i<3;++i)require(receipts[i].sequence==i+1 && receipts[i].handle==handle && receipts[i].capacity_kj_k==18 &&
        receipts[i].struct_size==48 && receipts[i].abi_version==2 && receipts[i].kind==0 && receipts[i].reserved2==0,"storage heat receipt framing");
    const auto kept=receipts;reject([&]{ledger.copy(receipts.data(),2);});
    require(std::memcmp(receipts.data(),kept.data(),sizeof(receipts))==0 && ledger.copy(nullptr,0)==3,"short storage heat read consumed data or changed output");
    ledger.ack(1);require(ledger.copy(nullptr,0)==2,"storage heat prefix acknowledgement");
    ledger.ack(1);reject([&]{ledger.ack(0);});reject([&]{ledger.ack(4);});ledger.ack(3);
    // A capacity change partitions the exact work into distinct operands.
    ledger.expect(handle,310.f,7.f);actual=value(299.f);
    ledger.before(read,[&](std::int32_t,const auto& append){append(std::bit_cast<std::uint32_t>(310.f),std::bit_cast<std::uint32_t>(7.f));},3);
    actual=value(310.f,7.f);ledger.after(read,3);actual=value(309.f,7.f);ledger.sample(read,4);
    require(ledger.copy(receipts.data(),8)==2 && receipts[0].capacity_kj_k==18 && receipts[1].capacity_kj_k==7 &&
        receipts[0].after_k==299 && receipts[1].before_k==310,"storage heat used the new capacity for earlier work");
    reject([&]{ledger.add(handle,300.f,1.f);});reject([&]{ledger.expect(handle+1,300.f,1.f);});
    reject([&]{Ledger invalid;invalid.add(handle,-1.f,1.f);});reject([&]{Ledger invalid;invalid.add(handle,300.f,-1.f);});
    reject([&]{Ledger invalid;invalid.add(-1,300.f,1.f);});
    actual=value(308.f,8.f);reject([&]{ledger.sample(read,5);});
    Ledger undeclared;undeclared.add(handle,300.f,18.f);actual=value(300.f);
    reject([&]{undeclared.before(read,[&](std::int32_t,const auto& append){append(std::bit_cast<std::uint32_t>(310.f),std::bit_cast<std::uint32_t>(18.f));},1);});
    Ledger reordered;reordered.add(handle,300.f,18.f);reordered.expect(handle,310.f,18.f);reordered.expect(handle,320.f,18.f);
    reject([&]{reordered.before(read,[&](std::int32_t,const auto& append){append(std::bit_cast<std::uint32_t>(320.f),std::bit_cast<std::uint32_t>(18.f));},1);});
    Ledger exhausted;exhausted.add(handle,300.f,18.f);
    for(std::uint32_t i=0;i<32768;++i){actual=value(i%2?300.f:301.f);exhausted.sample(read,i);}
    actual=value(301.f);reject([&]{exhausted.sample(read,32769);});require(exhausted.copy(nullptr,0)==32768,"receipt exhaustion silently discarded heat");
    // Cleanup is legal only after its declared zero-capacity projection.
    // Retain preceding work and the retirement marker until one explicit ack.
    Ledger retiring;retiring.add(handle,300,18);actual=value(299);
    reject([&]{retiring.expect_removal(handle);});
    retiring.expect(handle,300,0);retiring.expect_removal(handle);
    reject([&]{retiring.expect_removal(handle);});reject([&]{retiring.expect(handle,301,1);});
    // Storage cleanup can send a final absolute zero-capacity update while
    // detaching the item's native thermal body. It carries no physical heat.
    retiring.expect(handle,301,0);
    retiring.before(read,[&](std::int32_t,const auto& append){append(std::bit_cast<std::uint32_t>(300.f),0);append(std::bit_cast<std::uint32_t>(301.f),0);return true;},6);
    actual.valid=false;retiring.after(read,6);
    require(retiring.empty() && retiring.copy(receipts.data(),8)==2 && receipts[0].kind==0 && receipts[0].capacity_kj_k==18 &&
        receipts[0].before_k==300 && receipts[0].after_k==299 && receipts[1].kind==1 && receipts[1].capacity_kj_k==0 &&
        receipts[1].before_k==301 && receipts[1].after_k==301,"retirement lost prior heat or its completion receipt");
    retiring.sample(read,7);require(retiring.copy(nullptr,0)==2,"retirement was repeated or read a stale native handle");
    retiring.ack(receipts[1].sequence);require(retiring.copy(nullptr,0)==0,"retirement receipt acknowledgement failed");
    // Generation bits are unsigned even though the ABI handle is int32.
    constexpr auto high_handle=static_cast<std::int32_t>(0x81000000U);
    Ledger high;high.add(high_handle,300,0);high.expect_removal(high_handle);
    high.before([&](std::int32_t h){require(h==high_handle,"retirement changed signed handle generation");return value(300,0);},
        [](std::int32_t,const auto&){return true;},7);
    high.after([](std::int32_t){return Chunk{};},7);
    require(high.copy(receipts.data(),8)==1 && receipts[0].handle==high_handle && receipts[0].kind==1,"negative generation handle could not retire");
    Ledger premature;premature.add(handle,300,18);premature.expect(handle,300,0);premature.expect_removal(handle);actual=value(300);
    reject([&]{premature.before(read,[](std::int32_t,const auto&){return true;},7);});
    Ledger foreign;foreign.add(handle,300,0);actual=value(300,0);
    reject([&]{foreign.before(read,[](std::int32_t,const auto&){return true;},8);});
    Ledger not_removed;not_removed.add(handle,300,0);not_removed.expect_removal(handle);
    not_removed.before(read,[](std::int32_t,const auto&){return true;},8);reject([&]{not_removed.after(read,8);});
    std::cout<<"PASS exact storage heat receipts: post-publication cooling, queued resets, capacity changes, immutable prefix reads, verified zero-capacity retirement and explicit ordering/capacity refusal\n";
}
static void storage_heat_native_controls() {
    using namespace ma::oni;
    // Exercise both platform registry views on either host, independently of
    // a licensed runtime. Source objects and queued messages stay immutable.
    for(const auto layout:{chunk_heat_trace::linux_registry,chunk_heat_trace::windows_registry}) {
        std::array<std::byte,0x600> sim{};std::array<std::byte,44> record{};
        std::array<std::int32_t,1> mapping{0};std::array<std::uint8_t,1> generations{1};
        const auto put=[](auto& target,std::size_t offset,const auto& value){std::memcpy(target.data()+offset,&value,sizeof(value));};
        put(sim,layout.data,record.data());put(sim,layout.data+8,record.data()+record.size());
        put(sim,layout.mapping,mapping.data());put(sim,layout.mapping+8,mapping.data()+mapping.size());
        put(sim,layout.generations,generations.data());put(sim,layout.generations+8,generations.data()+generations.size());
        put(record,0,std::int32_t{123});put(record,4,std::bit_cast<std::uint32_t>(300.f));put(record,8,std::bit_cast<std::uint32_t>(18.f));
        const auto source=sim;const auto body=record;
        const auto read=chunk_heat_trace::chunk_at(sim.data(),0x01000000,layout);
        require(read.valid && read.temperature_bits==std::bit_cast<std::uint32_t>(300.f) && read.capacity_bits==std::bit_cast<std::uint32_t>(18.f),"storage heat registry platform layout");
        require(!chunk_heat_trace::chunk_at(sim.data(),0x02000000,layout).valid,"storage heat platform view accepted a stale generation");
        require(source==sim && body==record,"storage heat registry platform view changed source");
    }
    std::array<std::byte,0x600> sim{},frame{};std::array<std::byte,44> body{};
    std::array<std::int32_t,1> mapping{0};std::array<std::uint8_t,1> generations{1};
    const auto put=[](auto& target,std::size_t offset,const auto& value){std::memcpy(target.data()+offset,&value,sizeof(value));};
#if defined(_WIN32)
    constexpr auto layout=chunk_heat_trace::windows_registry;constexpr std::size_t data_offset=0x310,energy_offset=0x370;
#else
    constexpr auto layout=chunk_heat_trace::linux_registry;constexpr std::size_t data_offset=0x248,energy_offset=0x290;
#endif
    put(sim,layout.data,body.data());put(sim,layout.data+8,body.data()+body.size());
    put(sim,layout.mapping,mapping.data());put(sim,layout.mapping+8,mapping.data()+mapping.size());
    put(sim,layout.generations,generations.data());put(sim,layout.generations+8,generations.data()+generations.size());
    put(body,0,std::int32_t{123});put(body,4,std::bit_cast<std::uint32_t>(299.f));put(body,8,std::bit_cast<std::uint32_t>(18.f));
    std::array<std::uint32_t,6> data{0x01000000,std::bit_cast<std::uint32_t>(300.f),std::bit_cast<std::uint32_t>(18.f),
        0x01000000,std::bit_cast<std::uint32_t>(302.f),std::bit_cast<std::uint32_t>(7.f)};
    put(frame,data_offset,data.data());put(frame,data_offset+8,data.data()+data.size());
    const auto source=sim;const auto source_frame=frame;const auto source_messages=data;
    storage_heat::Monitor monitor;monitor.add(0x01000000,300,18);monitor.expect(0x01000000,300,18);monitor.expect(0x01000000,302,7);
    std::fenv_t initial{};require(std::fegetenv(&initial)==0,"storage heat initial environment");const int initial_errno=errno;
    require(std::fesetround(FE_DOWNWARD)==0 && std::feclearexcept(FE_ALL_EXCEPT)==0 && std::feraiseexcept(FE_INVALID)==0,"storage heat initial flags");errno=EDOM;
    auto called=false;
    monitor.process(frame.data(),sim.data(),1,[&]{
        require(std::fegetround()==FE_DOWNWARD && std::fetestexcept(FE_ALL_EXCEPT)==FE_INVALID && errno==EDOM,"storage observation changed the original call environment");
        called=true;put(body,4,std::bit_cast<std::uint32_t>(302.f));put(body,8,std::bit_cast<std::uint32_t>(7.f));
        require(std::fesetround(FE_TOWARDZERO)==0 && std::feclearexcept(FE_ALL_EXCEPT)==0 && std::feraiseexcept(FE_DIVBYZERO)==0,"storage heat original flags");errno=ERANGE;
    });
    require(std::fegetround()==FE_TOWARDZERO && std::fetestexcept(FE_ALL_EXCEPT)==FE_DIVBYZERO && errno==ERANGE,"storage observation changed the original call's returned environment");
    std::array<MAOniStorageHeatReceipt,4> receipts{};
    require(called && monitor.copy(receipts.data(),4)==1 && receipts[0].before_k==300 && receipts[0].after_k==299 && receipts[0].capacity_kj_k==18,
        "storage native boundary lost final cooling before several queued replacements");
    put(body,4,std::bit_cast<std::uint32_t>(301.f));monitor.sample(sim.data(),2);
    require(std::fegetround()==FE_TOWARDZERO && std::fetestexcept(FE_ALL_EXCEPT)==FE_DIVBYZERO && errno==ERANGE,"storage sampling changed the native environment");
    require(monitor.copy(receipts.data(),4)==2 && receipts[1].before_k==302 && receipts[1].after_k==301 && receipts[1].capacity_kj_k==7,
        "storage native boundary did not resume heat observation at actual applied state");
    require(source==sim && source_frame==frame && source_messages==data,"storage native boundary mutated registry/messages");
    // A declared empty view can be removed in the same native batch as its
    // final zero-capacity update. No native input is changed by observation.
    monitor.expect(0x01000000,301,0);monitor.expect_removal(0x01000000);
#if defined(_WIN32)
    constexpr std::size_t removal_offset=0x330;
#else
    constexpr std::size_t removal_offset=0x260;
#endif
    std::array<std::uint32_t,2> removal{0x01000000,0};
    std::array<std::uint32_t,3> zero_data{0x01000000,std::bit_cast<std::uint32_t>(301.f),0};
    put(frame,data_offset,zero_data.data());put(frame,data_offset+8,zero_data.data()+zero_data.size());
    put(frame,removal_offset,removal.data());put(frame,removal_offset+8,removal.data()+removal.size());
    const auto before_removal_frame=frame;
    monitor.process(frame.data(),sim.data(),3,[&]{put(body,8,std::uint32_t{0});generations[0]=2;});
    require(frame==before_removal_frame && monitor.copy(receipts.data(),4)==3 && receipts[2].kind==1 &&
        receipts[2].capacity_kj_k==0 && receipts[2].before_k==301 && receipts[2].after_k==301,
        "native removal lost heat history or failed to confirm zero-capacity retirement");
    monitor.sample(sim.data(),4);monitor.ack(receipts[2].sequence);
    require(monitor.copy(nullptr,0)==0,"native retirement reread a removed handle or left unacknowledged receipts");
    put(frame,removal_offset,static_cast<const std::uint32_t*>(nullptr));put(frame,removal_offset+8,static_cast<const std::uint32_t*>(nullptr));
    generations[0]=1;put(body,8,std::bit_cast<std::uint32_t>(7.f));
    put(frame,data_offset,static_cast<const std::uint32_t*>(nullptr));put(frame,data_offset+8,static_cast<const std::uint32_t*>(nullptr));
    monitor.reset();monitor.add(0x01000000,301,7);
    std::array<std::uint32_t,2> energy{0x01000000,0};put(frame,energy_offset,energy.data());put(frame,energy_offset+8,energy.data()+energy.size());
    called=false;monitor.process(frame.data(),sim.data(),3,[&]{called=true;});
    bool refused=false;try{(void)monitor.copy(receipts.data(),4);}catch(const std::logic_error&){refused=true;}
    require(called && refused,"unsupported native writer did not preserve original execution and refuse exact heat");
    require(std::fesetenv(&initial)==0,"storage heat environment restoration");errno=initial_errno;
    monitor.reset();require(monitor.copy(receipts.data(),4)==0,"storage native heat teardown left old receipts");
    std::cout<<"PASS storage native heat boundary: both registry layouts, ordered multi-reset processing, immutable messages and sticky refusal of foreign heat writers\n";
}
static void conduit_heat_completion_controls() {
    using ma::oni::conduit_heat::Monitor;
    const auto bits=[](float value){return std::bit_cast<std::uint32_t>(value);};
    const auto reject=[](auto action){bool refused=false;try{action();}catch(const std::exception&){refused=true;}require(refused,"invalid conduit heat completion was accepted");};
    std::array<std::byte,0x600> frame{};
    const auto put=[](auto& target,std::size_t offset,const auto& value){std::memcpy(target.data()+offset,&value,sizeof(value));};
#if defined(_WIN32)
    constexpr std::size_t offset=0x250;
#else
    constexpr std::size_t offset=0x1b8;
#endif
    constexpr std::int32_t handle=4;
    std::array<std::uint32_t,16> rows{
        99,bits(7),bits(290),bits(350), // unrelated body
        handle,bits(8),bits(290),bits(350), // another source on this body
        handle,bits(.15f),bits(300),bits(350),
        handle,bits(-.25f),bits(280),bits(300)};
    put(frame,offset,rows.data());put(frame,offset+8,rows.data()+rows.size());
    Monitor monitor;monitor.expect(handle,.15f,300,350);monitor.expect(handle,-.25f,280,300);
    require(monitor.pending()==2,"conduit heat declaration was already completed");
    const auto source=frame;const auto messages=rows;int calls=0;
    // A body's final temperature can equal its old temperature after separate
    // cooling and warming. Actual processed messages still complete exactly.
    float body_temperature=300;
    reject([&]{monitor.process(frame.data(),[&]{++calls;throw std::runtime_error("injected original failure");});});
    require(monitor.pending()==2,"conduit heat was acknowledged before its native call completed");
    monitor.process(frame.data(),[&]{++calls;body_temperature=300;});
    require(calls==2&&monitor.pending()==0&&body_temperature==300&&frame==source&&rows==messages,
        "conduit completion inferred temperature or edited/duplicated native work");
    monitor.process(frame.data(),[&]{++calls;});require(calls==3&&monitor.pending()==0,"empty completion observer did not forward once");
    monitor.expect(handle,.15f,300,350);
    auto changed=rows;changed[10]=bits(301);put(frame,offset,changed.data());put(frame,offset+8,changed.data()+changed.size());
    monitor.process(frame.data(),[]{});require(monitor.pending()==1,"different native clamp bounds completed a declared request");
    put(frame,offset,rows.data());put(frame,offset+8,rows.data()+rows.size());
    monitor.process(frame.data(),[]{});require(monitor.pending()==0,"unmatched conduit request was lost");
    monitor.expect(handle,.15f,300,350);monitor.expect(handle,-.25f,280,300);
    std::array<std::uint32_t,8> reversed{rows[12],rows[13],rows[14],rows[15],rows[8],rows[9],rows[10],rows[11]};
    put(frame,offset,reversed.data());put(frame,offset+8,reversed.data()+reversed.size());
    monitor.process(frame.data(),[]{});require(monitor.pending()==1,"reordered native work erased the missing ordered suffix");
    monitor.reset();
    // Reordered/missing requests cannot disappear just because another request
    // for the same body was seen. Repeated identical messages each count once.
    monitor.expect(handle,.15f,300,350);monitor.expect(handle,.15f,300,350);
    put(frame,offset,rows.data()+8);put(frame,offset+8,rows.data()+12);
    monitor.process(frame.data(),[]{});require(monitor.pending()==1,"one native message completed duplicate declarations");
    monitor.process(frame.data(),[]{});require(monitor.pending()==0,"duplicate native completion could not resume");
    constexpr auto high=static_cast<std::int32_t>(0x81000004U);
    std::array<std::uint32_t,4> negative{static_cast<std::uint32_t>(high),bits(-.25f),bits(280),bits(300)};
    monitor.expect(high,-.25f,280,300);put(frame,offset,negative.data());put(frame,offset+8,negative.data()+4);
    monitor.process(frame.data(),[]{});require(monitor.pending()==0,"signed native handle generation failed completion");
    for(const auto value:{0.f,-0.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
        reject([&]{monitor.expect(handle,value,300,350);});
    reject([&]{monitor.expect(-1,1,300,350);});reject([&]{monitor.expect(handle,1,350,300);});
    for(std::size_t i=0;i<32768;++i)monitor.expect(handle,1,300,350);
    reject([&]{monitor.expect(handle,1,300,350);});require(monitor.pending()==32768,"conduit completion overflow discarded requests");
    monitor.reset();monitor.expect(handle,1,300,350);put(frame,offset+8,reinterpret_cast<const std::byte*>(negative.data())+1);
    monitor.process(frame.data(),[&]{++calls;});reject([&]{(void)monitor.pending();});
    monitor.reset();require(monitor.pending()==0,"conduit completion reset left a stale failure");
    // The observer preserves errno and floating-point state around the native
    // call, including changes the original itself intentionally makes.
    const auto old_round=std::fegetround();const auto old_error=errno;
    monitor.expect(high,-.25f,280,300);put(frame,offset,negative.data());put(frame,offset+8,negative.data()+4);
    std::fesetround(FE_UPWARD);errno=E2BIG;
    monitor.process(frame.data(),[&]{require(std::fegetround()==FE_UPWARD&&errno==E2BIG,"completion observer changed original environment");std::fesetround(FE_TOWARDZERO);errno=ERANGE;});
    require(std::fegetround()==FE_TOWARDZERO&&errno==ERANGE&&monitor.pending()==0,"completion observer lost native arithmetic environment");
    std::fesetround(old_round);errno=old_error;
    require(ma_oni_conduit_heat_version()==1&&ma_oni_conduit_heat_pending(nullptr)==MA_ONI_INVALID_ARGUMENT&&
        ma_oni_conduit_heat_expect(handle,1,300,350)==MA_ONI_BUSY&&ma_oni_conduit_heat_reset()==MA_ONI_OK,"conduit completion ABI admission");
    std::cout<<"PASS conduit native heat completion: applied message proof, equal-temperature negative control, immutable forwarding, ordering/multiplicity, platform vectors and preserved native environment\n";
}
int main() {
    try {
        capture_file_controls();
        body_heat_trace_controls();
        runtime_context_controls();
        chunk_heat_trace_controls();
        storage_heat_receipt_controls();
        storage_heat_native_controls();
        conduit_heat_completion_controls();
        require(ma_oni_colony_physical_core_count() >= 1 && ma_oni_colony_physical_core_count() <= 256 &&
                    ma_oni_colony_set_worker_count(30) == MA_ONI_OK &&
                    ma_oni_colony_set_worker_count(257) == MA_ONI_INVALID_ARGUMENT &&
                    ma_oni_colony_set_worker_count(0) == MA_ONI_OK,
                "worker preference ABI or physical-core default differs");
        require(ma_oni_colony_precise_receipt_version()==2,"precise receipt observation capability");
        static_assert(sizeof(MAOniColonyParcelContext)==72);
        MAOniColonyParcelContext parcel_context{};parcel_context.struct_size=sizeof(parcel_context);parcel_context.abi_version=1;
        parcel_context.session=456;const auto original_context=parcel_context;
        require(ma_oni_colony_parcel_context_version()==1 &&
            ma_oni_colony_copy_parcel_context(nullptr)==MA_ONI_INVALID_ARGUMENT &&
            ma_oni_colony_copy_parcel_context(&parcel_context)==MA_ONI_INVALID_ARGUMENT &&
            std::memcmp(&parcel_context,&original_context,sizeof(parcel_context))==0,"invalid/inactive parcel context changed output");
        parcel_context.reserved=1;const auto invalid_context=parcel_context;
        require(ma_oni_colony_copy_parcel_context(&parcel_context)==MA_ONI_INVALID_ARGUMENT &&
            std::memcmp(&parcel_context,&invalid_context,sizeof(parcel_context))==0,"invalid parcel context header changed output");
        require(ma_oni_colony_actor_batch_version()==1 &&
            ma_oni_colony_queue_actor_batch(nullptr,0,nullptr,0,nullptr,0)==MA_ONI_INVALID_ARGUMENT,
            "atomic actor batch capability/empty request");
        std::uint32_t copied_views=123; MAOniColonyStatus cached_status{}; cached_status.struct_size=sizeof(cached_status); cached_status.abi_version=1;
        const auto held_cached_status=cached_status;
        require(ma_oni_colony_copy_changed(0,0,&cached_status,nullptr,0,nullptr)==MA_ONI_INVALID_ARGUMENT &&
            ma_oni_colony_copy_changed(0,0,&cached_status,nullptr,0,&copied_views)==MA_ONI_INVALID_ARGUMENT && copied_views==123 &&
            std::memcmp(&cached_status,&held_cached_status,sizeof(cached_status))==0,"invalid/inactive cached copy changed outputs");
        const std::uint32_t sample_cell=1; std::uint32_t sample_parts=123; MAOniColonyStatus sample_status{}; sample_status.state=99;
        const auto held_sample_status=sample_status; double sample_converted=456;
        static_assert(sizeof(MAOniColonyEnergySample)==104);
        MAOniColonyEnergySample sample_energy{};sample_energy.struct_size=sizeof(sample_energy);sample_energy.abi_version=1;
        sample_energy.session=147;sample_energy.internal_energy_j=149;const auto held_energy=sample_energy;
        require(ma_oni_colony_sample_energy(147,0,nullptr)==MA_ONI_INVALID_ARGUMENT &&
            ma_oni_colony_sample_energy(147,0,&sample_energy)==MA_ONI_INVALID_ARGUMENT &&
            std::memcmp(&sample_energy,&held_energy,sizeof(sample_energy))==0,
            "invalid/inactive retained energy modified output");
        sample_energy.abi_version=2;const auto bad_energy=sample_energy;
        require(ma_oni_colony_sample_energy(147,0,&sample_energy)==MA_ONI_INVALID_ARGUMENT &&
            std::memcmp(&sample_energy,&bad_energy,sizeof(sample_energy))==0,"invalid energy version modified output");
        static_assert(sizeof(MAOniColonyNativeSampleCell)==24);
        MAOniColonyNativeSampleCell native_sample{1,99,42,318};const auto held_native_sample=native_sample;
        require(ma_oni_colony_sample_native_frame(147,0,&sample_cell,1,&sample_status,&native_sample)==MA_ONI_INVALID_ARGUMENT &&
            ma_oni_colony_sample_native_frame(147,0,nullptr,1,&sample_status,&native_sample)==MA_ONI_INVALID_ARGUMENT &&
            ma_oni_colony_sample_native_frame(147,0,&sample_cell,0,&sample_status,&native_sample)==MA_ONI_INVALID_ARGUMENT &&
            std::memcmp(&native_sample,&held_native_sample,sizeof(native_sample))==0 &&
            std::memcmp(&sample_status,&held_sample_status,sizeof(sample_status))==0,
            "invalid/inactive native arena sample modified output");
        static_assert(sizeof(MAOniColonyRegionEnergySample)==88);
        MAOniColonyRegionEnergySample region_energy{};region_energy.struct_size=88;region_energy.abi_version=1;
        region_energy.internal_energy_j=149;const auto held_region=region_energy;
        require(ma_oni_colony_sample_region_energy(147,0,&sample_cell,1,&region_energy)==MA_ONI_INVALID_ARGUMENT &&
            ma_oni_colony_sample_region_energy(147,0,nullptr,1,&region_energy)==MA_ONI_INVALID_ARGUMENT &&
            ma_oni_colony_sample_region_energy(147,0,&sample_cell,1,nullptr)==MA_ONI_INVALID_ARGUMENT &&
            std::memcmp(&region_energy,&held_region,sizeof(region_energy))==0,"invalid/inactive region energy modified output");
        require(ma_oni_colony_sample(nullptr,1,&sample_status,nullptr,nullptr,0,&sample_parts,&sample_converted)==MA_ONI_INVALID_ARGUMENT &&
            ma_oni_colony_sample(&sample_cell,0,&sample_status,nullptr,nullptr,0,&sample_parts,&sample_converted)==MA_ONI_INVALID_ARGUMENT &&
            ma_oni_colony_sample(&sample_cell,1,&sample_status,nullptr,nullptr,0,&sample_parts,&sample_converted)==MA_ONI_INVALID_ARGUMENT &&
            sample_parts==123 && sample_converted==456 && std::memcmp(&sample_status,&held_sample_status,sizeof(sample_status))==0,
            "invalid/inactive arena sample changed outputs");
        std::uint32_t receipt_bytes=99;
        require(ma_oni_colony_enable_precise_receipts(2)==MA_ONI_INVALID_ARGUMENT &&
            ma_oni_colony_enable_precise_receipts(1)==MA_ONI_INVALID_ARGUMENT,"invalid/inactive precise receipt mode admitted");
        require(ma_oni_colony_copy_precise_receipt(1,nullptr,0,nullptr)==MA_ONI_INVALID_ARGUMENT &&
            ma_oni_colony_copy_precise_receipt(1,nullptr,1,&receipt_bytes)==MA_ONI_INVALID_ARGUMENT && receipt_bytes==99,
            "invalid precise receipt buffer changed size output");
        require(ma_oni_colony_copy_precise_receipt(1,nullptr,0,&receipt_bytes)==MA_ONI_OK && receipt_bytes==0 &&
            ma_oni_colony_wait()==MA_ONI_OK,"absent precise receipt or idle wait failed");
        std::uint32_t job_state=123; std::uint64_t last_receipt=456,job_token=789;
        require(ma_oni_colony_queue_pipe_job(1,nullptr,nullptr,0,&job_state,&last_receipt)==MA_ONI_INVALID_ARGUMENT &&
            ma_oni_colony_pipe_job_state(1,nullptr,&last_receipt)==MA_ONI_INVALID_ARGUMENT &&
            ma_oni_colony_pipe_job_state(1,&job_state,&last_receipt)==MA_ONI_INVALID_ARGUMENT && job_state==123 && last_receipt==456,
            "invalid/inactive durable pipe job changed outputs");
        require(ma_oni_colony_receipt_job(1,nullptr)==MA_ONI_INVALID_ARGUMENT &&
            ma_oni_colony_receipt_job(1,&job_token)==MA_ONI_OK && job_token==0,"unknown pipe receipt token");
        std::uint32_t callback_count=77,awaiting_requests=77; std::uint64_t released_requests=88;
        const MAOniColonyCallbackRebind no_rebind{0,0};
        require(ma_oni_colony_callback_rebind_version()==1 &&
            ma_oni_colony_copy_checkpoint_callbacks(nullptr,0,nullptr)==MA_ONI_INVALID_ARGUMENT &&
            ma_oni_colony_copy_checkpoint_callbacks(nullptr,1,&callback_count)==MA_ONI_INVALID_ARGUMENT && callback_count==77 &&
            ma_oni_colony_callback_rebind_state(nullptr,&released_requests)==MA_ONI_INVALID_ARGUMENT &&
            ma_oni_colony_rebind_callbacks(nullptr,1)==MA_ONI_INVALID_ARGUMENT &&
            ma_oni_colony_rebind_callbacks(&no_rebind,1)==MA_ONI_INVALID_ARGUMENT,"invalid/inactive callback rebinding admitted");
        require(ma_oni_colony_callback_rebind_state(&awaiting_requests,&released_requests)==MA_ONI_OK && awaiting_requests==0,
            "inactive owner reports awaiting callback requests");
        const std::uint8_t invalid_parcel=0;
        require(ma_oni_colony_moving_parcel_version()==1 &&
            ma_oni_colony_queue_moving_parcel(1,1,nullptr,1,&job_state,&last_receipt)==MA_ONI_INVALID_ARGUMENT &&
            ma_oni_colony_queue_moving_parcel(1,1,&invalid_parcel,1,&job_state,&last_receipt)==MA_ONI_INVALID_ARGUMENT &&
            job_state==123 && last_receipt==456,"invalid moving request changed outputs");
        receipt_bytes=99;
        require(ma_oni_colony_copy_moving_outcome(1,nullptr,1,&receipt_bytes)==MA_ONI_INVALID_ARGUMENT && receipt_bytes==99 &&
            ma_oni_colony_copy_moving_outcome(1,nullptr,0,nullptr)==MA_ONI_INVALID_ARGUMENT,"invalid moving buffer changed output");
        require(ma_oni_colony_copy_moving_outcome(1,nullptr,0,&receipt_bytes)==MA_ONI_OK && receipt_bytes==0,"absent moving outcome copy failed");
        static_assert(sizeof(MAOniColonyPassiveStamp)==32 && sizeof(MAOniColonyPassiveRow)==24);
        MAOniColonyPassiveStamp passive_stamp{32,1,147,148,149};const auto passive_stamp_before=passive_stamp;
        MAOniColonyPassiveRow passive_row{1,0,2,3};const auto passive_row_before=passive_row;
        const std::uint32_t passive_cell=4;std::uint32_t passive_count=147;
        require(ma_oni_colony_passive_snapshot_version()==1 &&
            ma_oni_colony_read_passives(0,0,nullptr,1,&passive_stamp,&passive_row,1,&passive_count)==MA_ONI_INVALID_ARGUMENT &&
            ma_oni_colony_read_passives(0,0,&passive_cell,1,&passive_stamp,nullptr,1,&passive_count)==MA_ONI_INVALID_ARGUMENT &&
            ma_oni_colony_read_passives(0,0,&passive_cell,1,&passive_stamp,&passive_row,1,&passive_count)==MA_ONI_INVALID_ARGUMENT &&
            passive_count==147 && std::memcmp(&passive_stamp,&passive_stamp_before,sizeof(passive_stamp))==0 &&
            std::memcmp(&passive_row,&passive_row_before,sizeof(passive_row))==0,"invalid/inactive passive observation changed outputs");
        static_assert(sizeof(MAOniConduitContactInput)==44 && sizeof(MAOniConduitContactResult)==20);
        MAOniConduitContactInput contact{44,1,0,0,.2f,300,1.005f,.024f,400,1.94f,8};
        MAOniConduitContactResult contact_result{20,1,147,148,149};const auto unchanged_contact=contact_result;
        require(ma_oni_conduit_contact_replay(nullptr,1,&contact_result,1)==MA_ONI_INVALID_ARGUMENT &&
            ma_oni_conduit_contact_replay(&contact,1,nullptr,1)==MA_ONI_INVALID_ARGUMENT &&
            ma_oni_conduit_contact_replay(&contact,1,&contact_result,0)==MA_ONI_BUFFER_TOO_SMALL &&
            std::memcmp(&contact_result,&unchanged_contact,sizeof(contact_result))==0,"invalid thermal replay modified outputs");
        contact.flags=2;require(ma_oni_conduit_contact_replay(&contact,1,&contact_result,1)==MA_ONI_INVALID_ARGUMENT,"unknown thermal replay flags accepted");
        contact.flags=0;
        require(ma_oni_conduit_contact_version()==1 && ma_oni_conduit_contact_replay(&contact,1,&contact_result,1)==MA_ONI_BUSY &&
            std::memcmp(&contact_result,&unchanged_contact,sizeof(contact_result))==0,"unbound contact replay changed outputs");
        static_assert(sizeof(MAOniDomainPublicationInfo)==64);
        require(ma_oni_domain_publication_abi_version()==1,"native publication ABI version");
        MAOniDomainPublicationInfo publication{};publication.struct_size=sizeof(publication);publication.abi_version=1;publication.token=99;
        const auto original_publication=publication;
        require(ma_oni_domain_publication_acquire(nullptr,&publication)==MA_ONI_INVALID_ARGUMENT &&
            std::memcmp(&publication,&original_publication,sizeof(publication))==0,"null publication changed output");
        require(ma_oni_domain_publication_acquire(reinterpret_cast<void*>(std::uintptr_t{1}),&publication)==MA_ONI_BUSY &&
            std::memcmp(&publication,&original_publication,sizeof(publication))==0,"unbound publication changed output");
        publication.abi_version=99;
        require(ma_oni_domain_publication_acquire(reinterpret_cast<void*>(std::uintptr_t{1}),&publication)==MA_ONI_INVALID_ARGUMENT,
            "unknown native publication ABI admitted");
        require(ma_oni_domain_publication_release(0)==MA_ONI_INVALID_ARGUMENT,"unknown publication lease released");
        const MAOniDomainCell* borrowed=nullptr;std::uint32_t borrowed_count=123;
        require(ma_oni_domain_publication_cells(0,&borrowed,&borrowed_count)==MA_ONI_INVALID_ARGUMENT && borrowed==nullptr && borrowed_count==123,
            "invalid publication view changed output");
#if defined(__linux__)
        const ma::oni::TrampolineStorage storage{0xa41c0,0xe40,4096,0x2a000,0x2a000,0x7a1b9};
        std::array<Elf64_Phdr,2> headers{};
        headers[0].p_type=PT_LOAD;headers[0].p_flags=PF_R|PF_X;
        headers[0].p_vaddr=0x2a000;headers[0].p_offset=0x2a000;
        headers[0].p_memsz=0x7a1b9;headers[0].p_filesz=0x7a1b9;headers[0].p_align=4096;
        headers[1].p_type=PT_LOAD;headers[1].p_flags=PF_R;
        headers[1].p_vaddr=0xa5000;headers[1].p_offset=0xa5000;
        headers[1].p_memsz=0x1000;headers[1].p_filesz=0x1000;headers[1].p_align=4096;
        require(ma::oni::verified_trampoline_segment(headers,storage),"verified RX tail padding rejected");
        auto modified=headers;modified[0].p_flags|=PF_W;
        require(!ma::oni::verified_trampoline_segment(modified,storage),"writable segment accepted for trampolines");
        modified=headers;modified[0].p_filesz-=1;
        require(!ma::oni::verified_trampoline_segment(modified,storage),"BSS accepted for trampolines");
        modified=headers;modified[1].p_vaddr=storage.rva;
        require(!ma::oni::verified_trampoline_segment(modified,storage),"overlapping load segment accepted for trampolines");
        modified=headers;modified[1]=headers[0];
        require(!ma::oni::verified_trampoline_segment(modified,storage),"duplicate trampoline segment accepted");
        for (unsigned field=0;field<6;++field) {
            auto invalid=storage;
            switch (field) {
                case 0: invalid.rva-=16;break;
                case 1: invalid.bytes+=16;break;
                case 2: invalid.page_bytes=8192;break;
                case 3: invalid.segment_rva=UINTPTR_MAX;break;
                case 4: invalid.segment_file_offset+=4096;break;
                default: invalid.segment_memory_bytes=UINTPTR_MAX;break;
            }
            require(!ma::oni::verified_trampoline_segment(headers,invalid),"invalid trampoline bounds accepted");
        }
#endif
        require(ma_oni_abi_version()==4,"ABI version");
        require(sizeof(MAOniObserverConfig)==24 && sizeof(MAOniObserverStatus)==528 && sizeof(MAOniTraceEvent)==32,"ABI layouts");
        require(ma::oni::hash_memory("",0)=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","empty SHA256");
        require(ma::oni::hash_memory("abc",3)=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA256 abc");
        const std::string many(1000000,'a');
        require(ma::oni::hash_memory(many.data(),many.size())=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0","SHA256 multi-block");
        MAOniObserverStatus status{};
        status.struct_size=sizeof(status); status.abi_version=4;
        require(ma_oni_observer_status(&status)==MA_ONI_OK && !status.attached && !status.authoritative_fluid_owner,"inactive observer status");
        const auto before=status;
        require(ma_oni_probe(nullptr,&status)==MA_ONI_INVALID_ARGUMENT && std::memcmp(&before,&status,sizeof(status))==0,"invalid config changed output");
        require(std::strlen(ma_oni_last_error())>0,"bounded error text");
        require(ma_oni_probe(nullptr,nullptr)==MA_ONI_INVALID_ARGUMENT,"null output");
        const auto temporary=std::filesystem::temp_directory_path() /
            ("ma-oni-identity-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        require(std::filesystem::create_directory(temporary),"fresh identity fixture directory");
#if defined(_WIN32)
        const auto fake_image=temporary/"OxygenNotIncluded_Data/Plugins/x86_64/SimDLL.dll";
#else
        const auto fake_image=temporary/"OxygenNotIncluded_Data/Plugins/libSimDLL.so";
#endif
        std::filesystem::create_directories(fake_image.parent_path());
        { std::ofstream file(fake_image,std::ios::binary); file << "Not a licensed ONI native image"; }
        const auto path=temporary.u8string();
        const MAOniObserverConfig invalid_image{24,4,MA_ONI_OBSERVER_ONLY,0,reinterpret_cast<const char*>(path.c_str())};
        require(ma_oni_probe(&invalid_image,&status)==MA_ONI_INCOMPATIBLE && std::memcmp(&before,&status,sizeof(status))==0,
                "unknown image probe changed output or accepted native code");
        require(ma_oni_attach_observer(&invalid_image,&status)==MA_ONI_INCOMPATIBLE && std::memcmp(&before,&status,sizeof(status))==0,
                "unknown image attachment changed output or loaded native code");
        std::filesystem::remove_all(temporary);
        status.abi_version=99;
        require(ma_oni_observer_status(&status)==MA_ONI_INVALID_ARGUMENT,"unknown ABI accepted");
        require(ma_oni_detach_observer()==MA_ONI_OK && ma_oni_detach_observer()==MA_ONI_OK,"idempotent inactive detach");
        std::uint32_t count=99;
        require(ma_oni_copy_trace(nullptr,0,&count)==MA_ONI_OK && count==0,"empty trace");
        require(ma_oni_copy_trace(nullptr,0,nullptr)==MA_ONI_INVALID_ARGUMENT,"null trace count");
        require(ma_oni_domain_abi_version()==2 && ma_oni_domain_active()==0,"sealed-domain extension ABI");
        require(ma_oni_domain_completed_snapshot_abi_version()==1,"Completed frame snapshot capability ABI");
        std::array<std::uint8_t,32> completed{};completed.fill(0x5a);const auto held_completed=completed;
        std::uint32_t completed_bytes=123;
        require(ma_oni_domain_completed_snapshot_copy(1,0,completed.data(),32,&completed_bytes)==MA_ONI_BUSY &&
            completed_bytes==123 && completed==held_completed,"Inactive completed frame copy changed outputs");
        MAOniDomainStatus domain{}; domain.struct_size=sizeof(domain); domain.abi_version=2;
        require(ma_oni_domain_copy(&domain,nullptr,0)==MA_ONI_OK && domain.state==0,"inactive domain status");
        auto obsolete_domain=domain; obsolete_domain.abi_version=1; const auto held_obsolete=obsolete_domain;
        require(ma_oni_domain_copy(&obsolete_domain,nullptr,0)==MA_ONI_INVALID_ARGUMENT &&
            std::memcmp(&obsolete_domain,&held_obsolete,sizeof(obsolete_domain))==0,"obsolete four-species client was accepted or changed output");
        const auto unchanged=domain;
        const MAOniDomainConfig domain_config{40,2,1,64,10,10,3,2,0,0};
        constexpr auto inactive_start_result=MA_ONI_BUSY;
        require(ma_oni_domain_start(&domain_config,&domain)==inactive_start_result && std::memcmp(&domain,&unchanged,sizeof(domain))==0,
                "unattached native domain accepted or changed output");
        require(ma_oni_domain_start(nullptr,&domain)==MA_ONI_INVALID_ARGUMENT && std::memcmp(&domain,&unchanged,sizeof(domain))==0,
                "Invalid domain startup escaped its C boundary or changed output");
        auto invalid_domain=domain; invalid_domain.struct_size=0; const auto held_invalid_domain=invalid_domain;
        require(ma_oni_domain_start(&domain_config,&invalid_domain)==MA_ONI_INVALID_ARGUMENT &&
                std::memcmp(&invalid_domain,&held_invalid_domain,sizeof(invalid_domain))==0,
                "Invalid startup output ABI escaped its C boundary or changed output");
        require(ma_oni_domain_copy(&domain,nullptr,6)==MA_ONI_INVALID_ARGUMENT && std::memcmp(&domain,&unchanged,sizeof(domain))==0,
                "invalid native domain copy changed output");
        MAOniDomainSpecies species{-1,-1,-1,-1}; const auto held_species=species;
        require(ma_oni_domain_species(99,&species)==MA_ONI_INVALID_ARGUMENT && std::memcmp(&species,&held_species,sizeof(species))==0,
                "unknown domain species changed output");
        require(ma_oni_domain_species(MA_ONI_DOMAIN_SPECIES,&species)==MA_ONI_INVALID_ARGUMENT && std::memcmp(&species,&held_species,sizeof(species))==0,
                "one-past-last domain species accepted or changed output");
        require(ma_oni_domain_species(4,&species)==MA_ONI_OK && species.molar_mass>0 && species.cv>0,
                "absent physical carbon-dioxide definition");
        require(ma_oni_domain_heat_abi_version()==1 && sizeof(MAOniDomainHeat)==48 && sizeof(MAOniDomainHeatStatus)==56,"heat extension ABI");
        MAOniDomainHeatStatus heat{56,1,0,0,0,0,0,0,0};
        require(ma_oni_domain_heat_status(&heat)==MA_ONI_OK && heat.state==0,"inactive heat status");
        require(ma_oni_domain_chunk_heat_abi_version()==1 && sizeof(MAOniDomainChunkHeatStatus)==112,"Native body heat ABI");
        MAOniDomainChunkHeatStatus chunk_heat{};chunk_heat.struct_size=112;chunk_heat.abi_version=1;
        require(ma_oni_domain_chunk_heat_status(&chunk_heat)==MA_ONI_OK && chunk_heat.state==0 && chunk_heat.contacts==0,"Inactive native body heat status");
        chunk_heat.abi_version=2;const auto held_chunk=chunk_heat;
        require(ma_oni_domain_chunk_heat_status(&chunk_heat)==MA_ONI_INVALID_ARGUMENT &&
            std::memcmp(&chunk_heat,&held_chunk,sizeof(chunk_heat))==0,"Invalid native body heat ABI changed output");
        require(ma_oni_domain_solid_heat_abi_version()==1 && sizeof(MAOniDomainSolidHeatCell)==48 &&
                sizeof(MAOniDomainSolidHeatStatus)==112,"Native solid heat ABI");
        MAOniDomainSolidHeatStatus solid_heat{};solid_heat.struct_size=112;solid_heat.abi_version=1;
        require(ma_oni_domain_solid_heat_status(&solid_heat)==MA_ONI_OK && solid_heat.state==0 && solid_heat.contacts==0,
                "Inactive native solid heat status");
        const auto held_solid=solid_heat;
        const MAOniDomainSolidHeatCell solid_input{48,1,649,123,1,0,10.F,.793F,2.F,255};
        require(ma_oni_domain_solid_heat_startup_abi_version()==1,"Native solid startup ABI");
        auto initial_solid=solid_input;initial_solid.session=0;
        require(ma_oni_domain_start_with_solid_heat(&domain_config,&initial_solid,1,&domain,&solid_heat)==MA_ONI_BUSY &&
            std::memcmp(&domain,&unchanged,sizeof(domain))==0 && std::memcmp(&solid_heat,&held_solid,sizeof(solid_heat))==0,
            "Unattached solid startup changed either output");
        for(const auto count:{0U,129U})
            require(ma_oni_domain_start_with_solid_heat(&domain_config,&initial_solid,count,&domain,&solid_heat)==MA_ONI_INVALID_ARGUMENT &&
                std::memcmp(&domain,&unchanged,sizeof(domain))==0 && std::memcmp(&solid_heat,&held_solid,sizeof(solid_heat))==0,
                "Invalid solid startup count changed either output");
        require(ma_oni_domain_start_with_solid_heat(&domain_config,nullptr,1,&domain,&solid_heat)==MA_ONI_INVALID_ARGUMENT &&
            ma_oni_domain_start_with_solid_heat(&domain_config,&initial_solid,1,&domain,nullptr)==MA_ONI_INVALID_ARGUMENT &&
            std::memcmp(&domain,&unchanged,sizeof(domain))==0 && std::memcmp(&solid_heat,&held_solid,sizeof(solid_heat))==0,
            "Invalid solid startup pointers changed either output");
        require(ma_oni_domain_solid_heat_configure(&solid_input,1,&solid_heat)==MA_ONI_BUSY &&
                std::memcmp(&solid_heat,&held_solid,sizeof(solid_heat))==0,"Inactive solid registration changed output");
        for(const auto count:{0U,129U}) require(ma_oni_domain_solid_heat_configure(&solid_input,count,&solid_heat)==MA_ONI_INVALID_ARGUMENT &&
                std::memcmp(&solid_heat,&held_solid,sizeof(solid_heat))==0,"Invalid solid registration count changed output");
        require(ma_oni_domain_solid_heat_configure(nullptr,1,&solid_heat)==MA_ONI_INVALID_ARGUMENT &&
                std::memcmp(&solid_heat,&held_solid,sizeof(solid_heat))==0,"Null solid registration changed output");
        solid_heat.abi_version=2;const auto invalid_solid=solid_heat;
        require(ma_oni_domain_solid_heat_status(&solid_heat)==MA_ONI_INVALID_ARGUMENT &&
                ma_oni_domain_solid_heat_configure(&solid_input,1,&solid_heat)==MA_ONI_INVALID_ARGUMENT &&
                std::memcmp(&solid_heat,&invalid_solid,sizeof(solid_heat))==0,"Invalid solid heat ABI changed output");
        const auto held_heat=heat;
        const MAOniDomainHeat request{48,1,0,0,0,0,100,1000};
        require(ma_oni_domain_queue_heat(&request,1,&heat)==MA_ONI_BUSY && std::memcmp(&heat,&held_heat,sizeof(heat))==0,
                "unowned heat accepted or changed output");
        require(ma_oni_domain_transfer_abi_version()==2 && sizeof(MAOniDomainTransfer)==64 &&
                sizeof(MAOniDomainReceipt)==88 && sizeof(MAOniDomainTransferStatus)==104,"transfer extension ABI");
        MAOniDomainTransferStatus transfers{}; transfers.struct_size=104; transfers.abi_version=2;
        require(ma_oni_domain_transfer_status(&transfers)==MA_ONI_OK && transfers.state==0,"inactive transfer status");
        const auto held_transfers=transfers;
        const MAOniDomainTransfer transfer{64,2,0,0,0,1,1,0,-1,0,.05,300};
        require(ma_oni_domain_queue_transfers(&transfer,1,&transfers)==MA_ONI_BUSY &&
                std::memcmp(&transfers,&held_transfers,sizeof(transfers))==0,"unowned gas transfer changed output");
        require(ma_oni_domain_actor_abi_version()==1 && sizeof(MAOniDomainActor)==80 && sizeof(MAOniDomainActorStatus)==64,"actor extension ABI");
        require(ma_oni_domain_thermal_abi_version()==2 && sizeof(MAOniDomainThermalStatus)==88,"conduction extension ABI");
        MAOniDomainThermalStatus thermal{}; thermal.struct_size=88; thermal.abi_version=2;
        require(ma_oni_domain_thermal_status(&thermal)==MA_ONI_OK && thermal.state==0 && thermal.enabled==0,"inactive conduction status");
        const auto held_thermal=thermal;thermal.abi_version=1;
        require(ma_oni_domain_thermal_status(&thermal)==MA_ONI_INVALID_ARGUMENT,"Legacy conduction layout was silently admitted");
        thermal=held_thermal;thermal.struct_size=80;
        require(ma_oni_domain_thermal_status(&thermal)==MA_ONI_INVALID_ARGUMENT,"Truncated conduction layout was silently admitted");
        require(ma_oni_domain_viscosity_abi_version()==1 && sizeof(MAOniDomainViscosityStatus)==120,"physical viscosity extension ABI");
        require(ma_oni_domain_radial_consumption_abi_version()==1,"radial consumption capability ABI");
        MAOniDomainViscosityStatus viscosity{};viscosity.struct_size=120;viscosity.abi_version=1;
        require(ma_oni_domain_viscosity_status(&viscosity)==MA_ONI_OK && viscosity.state==0 && viscosity.enabled==0,"inactive viscosity status");
        viscosity.abi_version=2;const auto held_viscosity=viscosity;
        require(ma_oni_domain_viscosity_status(&viscosity)==MA_ONI_INVALID_ARGUMENT &&
            std::memcmp(&viscosity,&held_viscosity,sizeof(viscosity))==0,"Invalid viscosity ABI changed output");
        require(ma_oni_domain_gravity_abi_version()==1 && sizeof(MAOniDomainGravityStatus)==96,"gravity extension ABI");
        MAOniDomainGravityStatus gravity{}; gravity.struct_size=96; gravity.abi_version=1;
        require(ma_oni_domain_gravity_status(&gravity)==MA_ONI_OK && gravity.state==0 && gravity.enabled==0,"inactive gravity status");
        require(ma_oni_domain_checkpoint_abi_version()==1,"physical checkpoint extension ABI");
        std::array<std::uint8_t,16> checkpoint_buffer{}; checkpoint_buffer.fill(0xa5); const auto held_checkpoint=checkpoint_buffer;
        require(ma_oni_domain_restore(&domain_config,checkpoint_buffer.data(),16,.2,&domain)==MA_ONI_BUSY &&
                std::memcmp(&domain,&unchanged,sizeof(domain))==0 && checkpoint_buffer==held_checkpoint,
                "Unattached restore escaped its C boundary or changed source/output");
        std::uint32_t required_checkpoint=123;
        require(ma_oni_domain_checkpoint_copy(1,1,checkpoint_buffer.data(),16,&required_checkpoint)==MA_ONI_BUSY &&
            required_checkpoint==123 && checkpoint_buffer==held_checkpoint,"Inactive checkpoint changed output");
        require(ma_oni_domain_checkpoint_advance(checkpoint_buffer.data(),16,.2,checkpoint_buffer.data(),16,&required_checkpoint)==MA_ONI_INVALID_ARGUMENT &&
            required_checkpoint==123 && checkpoint_buffer==held_checkpoint,"Invalid checkpoint advance changed output");
        require(ma_oni_domain_actor_checkpoint_abi_version()==1 && sizeof(MAOniDomainPersistentActor)==96 &&
            ma_oni_domain_pending_actor_restores()==0,"Durable actor extension ABI or inactive restore status");
        require(ma_oni_domain_snapshot_copy(1,1,checkpoint_buffer.data(),16,&required_checkpoint)==MA_ONI_BUSY &&
            required_checkpoint==123 && checkpoint_buffer==held_checkpoint,"Inactive complete snapshot changed output");
        require(ma_oni_domain_snapshot_advance(checkpoint_buffer.data(),16,.2,checkpoint_buffer.data(),16,&required_checkpoint)==MA_ONI_INVALID_ARGUMENT &&
            required_checkpoint==123 && checkpoint_buffer==held_checkpoint,"Invalid complete snapshot advance changed output");
        require(ma_oni_domain_pair_abi_version()==1 && sizeof(MAOniDomainPairStatus)==112,"paired checkpoint extension ABI");
        MAOniDomainPairStatus pair{}; pair.struct_size=112; pair.abi_version=1; const auto empty_pair=pair;
        require(ma_oni_domain_pair_request(1,1,0,0,&pair)==MA_ONI_BUSY && std::memcmp(&pair,&empty_pair,sizeof(pair))==0,
            "Inactive pair request changed its output");
        require(ma_oni_domain_pair_snapshot_request(1,1,0,0,&pair)==MA_ONI_BUSY && std::memcmp(&pair,&empty_pair,sizeof(pair))==0,
            "Inactive actor-aware pair request changed its output");
        require(ma_oni_domain_pair_poll(999,&pair)==MA_ONI_INVALID_ARGUMENT && std::memcmp(&pair,&empty_pair,sizeof(pair))==0,
            "Unknown paired token changed poll output");
        require(ma_oni_domain_pair_copy(999,checkpoint_buffer.data(),8,checkpoint_buffer.data()+8,8)==MA_ONI_INVALID_ARGUMENT &&
            checkpoint_buffer==held_checkpoint,"Unknown paired token changed copy outputs");
        MAOniDomainConfig checkpoint_configuration{40,2,1,64,10,10,3,2,0,0};
        require(ma_oni_domain_checkpoint_validate(&checkpoint_configuration,checkpoint_buffer.data(),16,.2)==MA_ONI_INVALID_ARGUMENT,
            "Invalid physical restore image passed independent pre-native validation");
        MAOniDomainActorStatus actors{}; actors.struct_size=64; actors.abi_version=1;
        require(ma_oni_domain_actor_status(&actors)==MA_ONI_OK && actors.state==0 && actors.live_actors==0,"inactive actor status");
        const auto held_actors=actors;
        const MAOniDomainActor actor{80,1,0,0,1,1,0,1,0,0,.05,0,0,0};
        require(ma_oni_domain_configure_actor(&actor,&actors)==MA_ONI_BUSY && std::memcmp(&actors,&held_actors,sizeof(actors))==0,
            "unowned actor configuration accepted or changed output");
        const MAOniDomainPersistentActor persistent_actor{actor,1,12345,0};
        require(ma_oni_domain_configure_persistent_actors(&persistent_actor,1,&actors)==MA_ONI_BUSY &&
            std::memcmp(&actors,&held_actors,sizeof(actors))==0,"Inactive persistent actor changed its output");
        std::cout << "PASS: native observer ABI, SHA256 controls, unknown-image/immutable rejection and inactive lifecycle\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
