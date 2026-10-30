#include "mixed_atmospheres/oni_conduit_contact.h"
#include "mixed_atmospheres/oni_backend.h"
#include "mixed_atmospheres/oni_native_frames.h"
#include "mixed_atmospheres/oni_domain.h"
#include "mixed_atmospheres/oni_publication.h"
#include "mixed_atmospheres/oni_colony.h"
#include "../src/oni_sha256.hpp"
#include "../src/oni_funchook_storage.hpp"
#include "../src/oni_body_heat_trace.hpp"
#include "../src/oni_chunk_heat_trace.hpp"
#include "../src/oni_paired_heat.hpp"
#include "../src/oni_storage_heat_native.hpp"
#include "../src/oni_conduit_heat.hpp"
#include "../src/oni_runtime_trace.hpp"
#include "../src/oni_runtime_clock.hpp"
#include "../src/oni_load_guard.hpp"
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#if defined(__linux__)
#include <unistd.h>
#endif

static void require(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(message);
}
#include "oni_body_heat_trace_controls.hpp"
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
static void native_load_guard_controls() {
    ma::oni::load_guard::Scope scope;
    MAOniLoadGuardConfig config{32,1,8,6,2,1,3,2};
    require(!scope.suppress(23),"ordinary native loading was guarded");
    const auto token=scope.begin(config);
    for(int cell=0;cell<80;++cell) {
        const auto x=cell%10,y=cell/10;const bool inside=x>=3&&x<=5&&y>=2&&y<=3;
        require(scope.suppress(cell)==inside,"native load guard covered a border or missed its exact rectangle");
    }
    require(!scope.suppress(-1),"negative native load cell was guarded");
    try{(void)scope.begin(config);throw std::runtime_error("nested native load guard accepted");}catch(const std::logic_error&){}
    try{(void)scope.end(token+1);throw std::runtime_error("foreign native load token accepted");}catch(const std::logic_error&){}
    require(scope.suppress(23)&&scope.end(token)==7&&!scope.suppress(23),"native load guard token mismatch changed scope or end did not restore forwarding");
    for(const auto kind:{0,1,2,3}) {
        auto invalid=config;if(kind==0)invalid.struct_size=0;if(kind==1)invalid.abi_version=2;
        if(kind==2)invalid.width=7;if(kind==3)invalid.grid_width=invalid.grid_height=0xfffffffeU;
        try{(void)scope.begin(invalid);throw std::runtime_error("invalid native load geometry accepted");}catch(const std::invalid_argument&){}
        require(!scope.suppress(23),"invalid load guard left suppression active");
    }
    const auto second=scope.begin(config);require(second!=token,"native load scope reused a retired token");
    std::vector<std::thread> workers;
    for(unsigned i=0;i<4;++i)workers.emplace_back([&]{for(unsigned n=0;n<10000;++n)(void)scope.suppress(23);});
    for(auto& worker:workers)worker.join();
    require(scope.end(second)==40000&&!scope.suppress(23),"parallel native load phase accounting lost calls or remained active");
    std::uint64_t output=123;
    require(ma_oni_load_guard_version()==1&&ma_oni_load_guard_begin(&config,&output)==MA_ONI_BUSY&&output==123&&
        ma_oni_load_guard_begin(nullptr,&output)==MA_ONI_INVALID_ARGUMENT&&ma_oni_load_guard_end(0,nullptr)==MA_ONI_INVALID_ARGUMENT,
        "native load guard ABI admission or failure output changed");
    std::cout<<"PASS native mixture-load guard: exact padded rectangle, scoped forwarding, foreign/nested/stale token refusal, invalid/overflow geometry and40000 parallel calls\n";
}
static void runtime_clock_controls() {
    using namespace ma::oni::runtime_clock;
    for(const auto windows:{false,true}) {
        std::array<std::byte,168> sim{};sim.fill(std::byte{0x7a});
        const auto put=[&](std::size_t offset,const auto& value){std::memcpy(sim.data()+offset,&value,sizeof(value));};
        put(0,std::int32_t{258});put(4,std::int32_t{386});put(8,std::int32_t{98304});put(12,std::uint32_t{123});
        put(16,std::int32_t{-1});put(20,std::int32_t{1});put(windows?160:144,std::uint16_t{42});
        const auto original=sim;auto desired=read(sim.data(),windows);desired.random=0;desired.tick=65535;desired.iterate=1;desired.displacement=-1;
        auto malformed=desired;malformed.width+=2;malformed.cells+=768;
        try{write(sim.data(),windows,malformed);throw std::runtime_error("foreign clock geometry accepted");}catch(const std::invalid_argument&){}
        require(sim==original,"foreign clock geometry partially changed native state");
        write(sim.data(),windows,desired);const auto changed=read(sim.data(),windows);
        require(std::memcmp(&changed,&desired,sizeof(desired))==0,"native loop field restoration changed values");
        auto expected=original;
        std::memcpy(expected.data()+12,&desired.random,4);std::memcpy(expected.data()+16,&desired.iterate,4);
        std::memcpy(expected.data()+20,&desired.displacement,4);std::memcpy(expected.data()+(windows?160:144),&desired.tick,2);
        require(sim==expected,"native loop restore changed another native field");
        for(const auto kind:{0,1,2,3}) {
            malformed=desired;if(kind==0)malformed.struct_size=0;if(kind==1)malformed.abi_version=2;if(kind==2)malformed.reserved=1;if(kind==3)malformed.iterate=0;
            try{write(sim.data(),windows,malformed);throw std::runtime_error("malformed clock accepted");}catch(const std::invalid_argument&){}
            require(sim==expected,"malformed native loop state partially changed native memory");
        }
        if(windows!=Monitor::windows)continue;
        Monitor monitor;monitor.sample(nullptr);monitor.start();monitor.sample(original.data());
        monitor.restore(desired);require(monitor.pending(),"native loop restore was not deferred");
        sim=original;unsigned forwards=0;
        try{monitor.process(sim.data(),0,[&]{++forwards;throw std::runtime_error("original failure");});}catch(const std::runtime_error&){}
        require(monitor.pending()&&sim==original&&forwards==1,"original failure consumed clock restoration");
        monitor.process(sim.data(),0,[&]{++forwards;require(sim==original,"clock changed before original native message processing");});
        require(!monitor.pending()&&sim==expected&&forwards==2,"zero-time native clock restoration failed");
        monitor.sample(sim.data());const auto captured=monitor.copy();require(std::memcmp(&captured,&desired,sizeof(desired))==0,"completed native clock capture changed state");
        monitor.restore(desired);monitor.reset();sim=original;monitor.process(sim.data(),0,[&]{++forwards;});
        require(sim==original&&!monitor.pending(),"shutdown retained a pending native clock write");
        monitor.start();monitor.sample(sim.data());monitor.restore(desired);monitor.process(sim.data(),.2f,[&]{++forwards;});
        require(sim==original,"premature positive time applied native clock restoration");
        try{(void)monitor.copy();throw std::runtime_error("positive-time restore failure was hidden");}catch(const std::logic_error&){}
        monitor.reset();monitor.start();monitor.sample(sim.data());
        std::atomic<bool> finished{false};
        std::thread worker([&]{while(!finished.load(std::memory_order_acquire)){
            monitor.process(sim.data(),0,[]{});monitor.sample(sim.data());std::this_thread::yield();}});
        try {
            for(unsigned step=0;step<1000;++step) {
                desired.random=0xffffffffU-step;desired.tick=static_cast<std::uint16_t>(65500U+step);
                desired.iterate=(step&1U)?-1:1;desired.displacement=-desired.iterate;
                monitor.restore(desired);const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
                for(;;) {
                    bool complete=false;
                    try{const auto observed=monitor.copy();complete=std::memcmp(&observed,&desired,sizeof(desired))==0;}
                    catch(const std::logic_error&){} // pending or awaiting its completed sample
                    if(complete)break;
                    require(std::chrono::steady_clock::now()<deadline,"native clock worker handoff did not complete");std::this_thread::yield();
                }
            }
        }catch(...){finished.store(true,std::memory_order_release);worker.join();throw;}
        finished.store(true,std::memory_order_release);worker.join();
    }
    require(ma_oni_runtime_clock_version()==1&&ma_oni_runtime_clock_start()==MA_ONI_BUSY&&
        ma_oni_runtime_clock_copy(nullptr)==MA_ONI_INVALID_ARGUMENT&&ma_oni_runtime_clock_restore(nullptr)==MA_ONI_INVALID_ARGUMENT&&
        ma_oni_runtime_clock_pending(nullptr)==MA_ONI_INVALID_ARGUMENT,"native clock ABI admission differs");
    std::cout<<"PASS native loop state: both layouts, exact bounded writes, deferred zero-time restore, original failure retry, shutdown reset, premature-positive refusal and1000 threaded handoffs\n";
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
    for(const auto windows:{false,true}) {
        for(const auto building:{false,true}) {
            std::array<std::byte,0x500> world{};std::array<std::uint32_t,22> records{};
            for(std::size_t i=0;i<records.size();++i)records[i]=0x80000000U+static_cast<std::uint32_t>(i);
            const auto offset=building?(windows?0x4e0U:0x438U):(windows?0x410U:0x398U);
            const auto* first=reinterpret_cast<const std::byte*>(records.data());const auto* last=first+88;
            std::memcpy(world.data()+offset,&first,8);std::memcpy(world.data()+offset+8,&last,8);
            const auto kept_world=world;const auto kept_records=records;
            const auto rows=ma::oni::runtime_trace::actors(world.data(),windows,building);
            require(rows.size()==2&&rows[0][0]==0x80000000U&&rows[0][10]==0x8000000aU&&rows[1][0]==0x8000000bU&&rows[1][10]==0x80000015U,
                "native actor registry trace changed layout or order");
            require(world==kept_world&&records==kept_records,"native actor trace changed its source");
            last=first+87;std::memcpy(world.data()+offset+8,&last,8);
            bool refused=false;try{(void)ma::oni::runtime_trace::actors(world.data(),windows,building);}catch(const std::invalid_argument&){refused=true;}
            require(refused,"native actor trace accepted truncated records");
        }
        std::array<std::byte,168> owner{};std::array<std::array<std::byte,160>,2> arrays{};
        std::array<std::array<std::uint16_t,12>,2> elements{};
        std::array<std::array<std::uint32_t,12>,2> masses{},temperatures{};
        std::array<std::array<std::uint8_t,12>,2> properties{},insulation{};std::array<std::uint8_t,12> timers{};
        const auto set=[](auto& target,std::size_t offset,const auto& value){std::memcpy(target.data()+offset,&value,sizeof(value));};
        set(owner,0,std::int32_t{4});set(owner,4,std::int32_t{3});set(owner,8,std::int32_t{2});
        const std::array<std::size_t,3> offsets=windows?std::array<std::size_t,3>{8,72,40}:std::array<std::size_t,3>{0,48,24};
        for(unsigned b=0;b<2;++b) {
            elements[b][5]=static_cast<std::uint16_t>(71+b);masses[b][5]=b?0x3f800000U:0x80000000U;temperatures[b][5]=0x43960000U+b;
            set(owner,32+8*b,arrays[b].data());
            set(arrays[b],offsets[0],elements[b].data());set(arrays[b],offsets[0]+8,elements[b].data()+12);
            set(arrays[b],offsets[1],masses[b].data());set(arrays[b],offsets[1]+8,masses[b].data()+12);
            set(arrays[b],offsets[2],temperatures[b].data());set(arrays[b],offsets[2]+8,temperatures[b].data()+12);
            const auto property=windows?104U:72U,insulate=windows?136U:96U;
            properties[b][5]=static_cast<std::uint8_t>(0x81U+b);insulation[b][5]=static_cast<std::uint8_t>(0xf0U+b);
            set(arrays[b],property,properties[b].data());set(arrays[b],property+8,properties[b].data()+12);
            set(arrays[b],insulate,insulation[b].data());set(arrays[b],insulate+8,insulation[b].data()+12);
        }
        timers[5]=0xa5;set(owner,windows?112:104,timers.data());
        const auto kept_owner=owner;const auto kept_arrays=arrays;const auto kept_mass=masses;const auto kept_temperatures=temperatures;
        const auto kept_properties=properties,kept_insulation=insulation;const auto kept_timers=timers;
        const auto view=ma::oni::runtime_trace::cell(owner.data(),0,windows);
        require(view.valid&&view.element[0]==71&&view.element[1]==72&&view.mass[0]==0x80000000U&&view.mass[1]==0x3f800000U&&
            view.temperature[0]==0x43960000U&&view.temperature[1]==0x43960001U,"native paired cell trace changed raw fields or layout");
        require(view.context_valid&&view.properties[0]==0x81&&view.properties[1]==0x82&&view.insulation[0]==0xf0&&view.insulation[1]==0xf1&&view.stable_ticks==5,
            "native paired cell trace lost thermal properties or timer bits");
        require(!ma::oni::runtime_trace::cell(owner.data(),2,windows).valid,"native paired cell trace accepted outside public cell");
        require(owner==kept_owner&&arrays==kept_arrays&&masses==kept_mass&&temperatures==kept_temperatures,"native paired cell trace modified its input");
        require(properties==kept_properties&&insulation==kept_insulation&&timers==kept_timers,"native thermal context trace changed its inputs");
        set(arrays[1],(windows?136U:96U)+8,insulation[1].data()+5);
        const auto short_context=ma::oni::runtime_trace::cell(owner.data(),0,windows);
        require(short_context.valid&&!short_context.context_valid,"native paired cell trace accepted short thermal context");
        set(arrays[1],offsets[2]+8,temperatures[1].data()+5);
        require(!ma::oni::runtime_trace::cell(owner.data(),0,windows).valid,"native paired cell trace read beyond a short vector");
    }
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
    require(ma::oni::paired_heat::chunk_record(sim.data(),0x01000000,ma::oni::chunk_heat_trace::linux_registry)==record.data() &&
        !ma::oni::paired_heat::chunk_record(sim.data(),0x02000000,ma::oni::chunk_heat_trace::linux_registry),
        "paired chunk record address lost generation identity");
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
static void paired_heat_observation_controls() {
#if defined(__linux__)
    std::array<std::byte,0x600> sim{};std::array<std::byte,44> record{};
    std::array<std::byte,192> old{},next{};std::array<std::byte,16> accessor{};
    std::array<std::uint16_t,16> elements{};std::array<float,16> masses{},temperatures0{},temperatures1{};
    elements[5]=3;masses[5]=1.25f;temperatures0[5]=temperatures1[5]=300.f;
    std::array<std::int32_t,1> mapping{0};std::array<std::uint8_t,1> generations{1};
    const auto put=[](auto& target,std::size_t offset,const auto& value){std::memcpy(target.data()+offset,&value,sizeof(value));};
    put(sim,0,std::int32_t{4});put(sim,4,std::int32_t{4});put(sim,32,old.data());put(sim,40,next.data());
    put(sim,0x398,record.data());put(sim,0x3a0,record.data()+record.size());
    put(sim,0x3e0,mapping.data());put(sim,0x3e8,mapping.data()+mapping.size());
    put(sim,0x3f8,generations.data());put(sim,0x400,generations.data()+generations.size());
    for(auto* soa:{&old,&next}){put(*soa,0,elements.data());put(*soa,48,masses.data());}
    put(old,24,temperatures0.data());put(next,24,temperatures1.data());
    put(accessor,0,next.data());put(accessor,8,std::int32_t{5});
    put(record,0,std::int32_t{5});put(record,4,std::bit_cast<std::uint32_t>(350.f));put(record,8,std::bit_cast<std::uint32_t>(18.f));
    char path[]="/tmp/ma-paired-heat-test-XXXXXX";const int descriptor=mkstemp(path);
    require(descriptor>=0,"paired heat fixture path");(void)close(descriptor);(void)unlink(path);
    require(setenv("MA_ONI_PAIRED_HEAT_TRACE",path,1)==0,"paired heat fixture opt-in");
    std::fenv_t initial{};require(std::fegetenv(&initial)==0,"paired heat initial environment");const int initial_errno=errno;
    ma::oni::paired_heat::Observer observer;
    for(const auto rounding:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}) {
        require(std::fesetround(rounding)==0,"paired heat rounding control");
        (void)std::feclearexcept(FE_ALL_EXCEPT);(void)std::feraiseexcept(FE_DIVBYZERO);errno=111;
        observer.begin(sim.data(),7,9);
        const auto contact=observer.before(accessor.data(),record.data(),true,1005);
        require(contact.array==2 && contact.handle==0x01000000 && contact.before[1].valid && contact.specific_heat==1005,
            "paired heat lost exact updated-array identity or registered handle");
        temperatures1[5]=301.f;put(record,4,std::bit_cast<std::uint32_t>(349.f));
        observer.after(contact,1.25f);observer.finish(sim.data(),7);
        require(errno==111 && std::fegetround()==rounding && std::fetestexcept(FE_ALL_EXCEPT)==FE_DIVBYZERO,
            "paired heat observer changed caller math or errno");
        temperatures1[5]=300.f;put(record,4,std::bit_cast<std::uint32_t>(350.f));
    }
    (void)std::fesetenv(&initial);errno=initial_errno;
    auto* output=std::fopen(path,"r");require(output!=nullptr,"paired heat output absent");
    std::array<char,8192> text{};const auto bytes=std::fread(text.data(),1,text.size()-1,output);(void)std::fclose(output);(void)unlink(path);
    require(bytes>0 && std::strstr(text.data(),"array_match") && std::strstr(text.data(),"7\t9\t0\t5\t1\t2\t16777216\t") &&
        std::strstr(text.data(),"\t3\t41900000\t41900000\t43af0000\t43ae8000\t3fa00000\t1005"),
        "paired heat trace lost native contact endpoints or containing frame");
    std::cout<<"PASS native paired heat observation: exact array and generation identities, endpoints and preserved math/errno\n";
#endif
}
static void paired_building_invocation_controls() {
    using namespace ma::oni::paired_heat;
    for(const auto layout:{linux_buildings,windows_buildings}) {
        struct Fixture {
            alignas(16) std::array<std::byte,200> object{};
            std::array<std::byte,132> records{};
            std::array<std::int32_t,3> handles{2,0,1};
            std::array<std::uint64_t,3> results{};
            std::array<float,3> cells{300.f,300.f,300.f};
        } whole,split;
        const auto initialize=[&](Fixture& f) {
            auto* self=f.object.data()+layout.prefix;
            relative_write(self,layout.records,f.records.data());relative_write(self,layout.records+8,f.records.data()+f.records.size());
            relative_write(self,layout.handles,f.handles.data());relative_write(self,layout.handles+8,f.handles.data()+f.handles.size());
            for(std::size_t i=0;i<3;++i) {
                relative_write(f.records.data()+44*i,0,i==1?299.f:350.f);
                relative_write(f.records.data()+44*i,4,std::int32_t(i%2));
            }
        };
        initialize(whole);initialize(split);
        const auto original=[&](Fixture& f,void* view) {
            auto* records=relative_read<std::byte*>(view,layout.records);
            const auto* end=relative_read<std::byte*>(view,layout.records+8);
            const auto* handles=relative_read<std::int32_t*>(view,layout.handles);
            // Native resize updates only its own result descriptor. Its
            // physical output storage remains the real object's allocation.
            const std::size_t first=layout.result_descriptor==32?8:0;
            relative_write(view,layout.results+first,f.results.data());
            relative_write(view,layout.results+first+8,f.results.data()+f.results.size());
            relative_write(view,layout.results+first+16,f.results.data()+f.results.size());
            for(std::size_t i=0;records+44*i<end;++i) {
                auto* record=records+44*i;auto body=relative_read<float>(record,0);
                const auto cell_index=relative_read<std::int32_t>(record,4);
                const float delta=(body-f.cells[cell_index])*.125f;
                f.cells[cell_index]+=delta;body-=delta*.25f;relative_write(record,0,body);
                f.results[handles[i]]=(std::uint64_t(std::uint32_t(handles[i]))<<32)|std::bit_cast<std::uint32_t>(body);
            }
        };
        original(whole,whole.object.data()+layout.prefix);
        const auto unchanged=split.object;std::vector<std::int32_t> order;
        const bool handled=each_building(split.object.data()+layout.prefix,layout,
            [&](void* view){original(split,view);},[&](const std::byte*,std::int32_t handle){order.push_back(handle);},
            [&](const std::byte*,std::int32_t handle){require(order.back()==handle,"building invocation changed contact identity");});
        require(handled && order==std::vector<std::int32_t>({2,0,1}) && whole.records==split.records && whole.cells==split.cells && whole.results==split.results,
            "per-building invocation changed original arithmetic, overlap order or handle-indexed publication");
        auto expected=unchanged;
        std::memcpy(expected.data()+layout.prefix+layout.results,split.object.data()+layout.prefix+layout.results,layout.result_descriptor);
        require(expected==split.object,"per-building invocation modified real registry descriptors");
        auto* self=split.object.data()+layout.prefix;
        relative_write(self,layout.handles+8,split.handles.data()+2);
        unsigned calls=0;
        require(!each_building(self,layout,[&](void*){++calls;},[](const std::byte*,std::int32_t){},[](const std::byte*,std::int32_t){}) && !calls,
            "per-building invocation accepted a malformed handle registry");
        std::fenv_t initial_environment{};require(std::fegetenv(&initial_environment)==0,"paired building initial environment");
        const int initial_errno=errno;
        for(const auto rounding:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}) {
            Fixture environment;initialize(environment);unsigned forwarded=0;
            require(std::fesetround(rounding)==0,"paired building rounding control");
            (void)std::feclearexcept(FE_ALL_EXCEPT);errno=123;
            require(each_building(environment.object.data()+layout.prefix,layout,[&](void*) {
                require(std::fegetround()==rounding && errno==(forwarded?77:123) &&
                    std::fetestexcept(FE_ALL_EXCEPT)==(forwarded?FE_INVALID:0),"building observer changed original arithmetic environment");
                ++forwarded;(void)std::feraiseexcept(FE_INVALID);errno=77;
            },[](const std::byte*,std::int32_t){(void)std::fesetround(FE_UPWARD);(void)std::feraiseexcept(FE_DIVBYZERO);errno=88;},
              [](const std::byte*,std::int32_t){(void)std::fesetround(FE_TOWARDZERO);(void)std::feraiseexcept(FE_INEXACT);errno=99;}),
                "building environment view was refused");
            require(forwarded==3 && std::fegetround()==rounding && errno==77 && std::fetestexcept(FE_ALL_EXCEPT)==FE_INVALID,
                "building observer changed native return arithmetic environment");
        }
        (void)std::fesetenv(&initial_environment);errno=initial_errno;
    }
    std::cout<<"PASS paired building invocation: both ABI views, native resize ownership, overlapping contact order and unchanged real registries\n";
}
static void paired_heat_closure_controls() {
    using namespace ma::oni::paired_heat;
    const auto state=[](float mass,float temperature,std::uint16_t element=3) {
        return Cell{element,std::bit_cast<std::uint32_t>(mass),std::bit_cast<std::uint32_t>(temperature),true};
    };
    const auto sum=[](const EnergySum& terms){long double value=0;for(std::size_t i=0;i<terms.size();++i)value+=terms.component(i).require_representable();return value;};
    const auto refused=[](const auto& operation){try {operation();return false;}catch(const std::logic_error&){return true;}};
    for(const double direction:{-1.,1.})for(const double requested:{.0003,.03125,1.,37.125}) {
        const float body_before=direction>0?350.f:250.f,body_capacity=18.125f;
        const auto initial=state(1.2f,300.f);
        // Independent endpoint publication: each native endpoint rounds to
        // binary32 using its own capacity. The owner uses catalogue m*Cp.
        const float cell_capacity=1.2f*1.005f;
        const float cell_after=static_cast<float>(300.+direction*requested/(1000.*cell_capacity));
        const float body_after=static_cast<float>(double(body_before)-direction*requested/(1000.*body_capacity));
        const auto after=state(1.2f,cell_after);constexpr double cp=1005.00011444091796875;
        const double body_work=1000.*double(body_capacity)*(double(body_after)-body_before);
        const double native_work=cell_work(initial,after,cp);
        EnergySum correction;correction.add(-body_work);correction.add(-native_work);
        ClosureLedger ledger;ledger.begin(71);ledger.record(5,initial,initial,after,cp,correction);
        const auto completion=ledger.finish(71,true,[&](std::int32_t index){require(index==5,"paired closure moved cell");return after;});
        long double accounted=static_cast<long double>(body_work)+native_work;
        for(const auto& h:completion.heat) {
            require(h.native_cell==5 && h.mass_bits==initial.mass && h.initial_temperature_bits==initial.temperature && h.final_temperature_bits==after.temperature,
                "paired closure lost exact endpoint operands");accounted+=h.joules;
        }
        require(std::abs(accounted)<=1e-15L*(1+std::abs(body_work)+std::abs(native_work)),"paired chunk endpoint closure failed at binary32 ULP");
        require(refused([&]{(void)ledger.finish(71,true,[&](std::int32_t){return after;});}),"paired closure could be credited twice");
    }
    // Two passive bodies overlap one cell; the second consumes precisely the
    // first body's published endpoint. Mixed-sign contacts retain local work.
    const auto a=state(1.25f,300.f),b=state(.75f,340.f),c=state(2.f,310.f);
    const auto aa=state(1.25f,300.001f),bb=state(.75f,339.999f),aaa=state(1.25f,300.002f),cc=state(2.f,309.999f);
    std::array<BodyCell,2> first{{{5,a,a,aa,true,1005},{6,b,b,bb,true,1005}}};
    std::array<BodyCell,2> second{{{5,a,aa,aaa,true,1005},{7,c,c,cc,true,1005}}};
    constexpr double first_body=.123456789,second_body=-.234567891;
    const auto first_correction=passive_body_corrections(first_body,first),second_correction=passive_body_corrections(second_body,second);
    long double exact_first=first_body,exact_second=second_body;
    ClosureLedger ledger;ledger.begin(72);
    for(std::size_t i=0;i<2;++i){const auto& x=first[i];exact_first+=cell_work(x.before,x.after,x.cp)+sum(first_correction[i]);ledger.record(x.index,x.initial,x.before,x.after,x.cp,first_correction[i]);}
    for(std::size_t i=0;i<2;++i){const auto& x=second[i];exact_second+=cell_work(x.before,x.after,x.cp)+sum(second_correction[i]);ledger.record(x.index,x.initial,x.before,x.after,x.cp,second_correction[i]);}
    require(std::abs(exact_first)<1e-15 && std::abs(exact_second)<1e-15,"passive body exact weighted remainder lost represented work");
    const auto completion=ledger.finish(72,true,[&](std::int32_t index){return index==5?aaa:index==6?bb:cc;});
    long double closed=first_body+second_body+cell_work(a,aaa,1005)+cell_work(b,bb,1005)+cell_work(c,cc,1005);
    for(const auto& h:completion.heat)closed+=h.joules;
    require(std::abs(closed)<1e-15,"overlapping passive body contacts failed closed capture");
    auto foreign=first;foreign[1].owned=false;
    require(refused([&]{(void)passive_body_corrections(first_body,foreign);}),"mixed unowned body assigned whole endpoint to owned cell");
    const std::array<BodyCell,1> no_work{{{5,a,a,a,true,1005}}};
    require(refused([&]{(void)passive_body_corrections(1.,no_work);}),"unrepresented passive contact chose an arbitrary residual sink");
    EnergySum one;one.add(1.);
    for(const unsigned failure:{0,1,2,3,4}) {
        ClosureLedger refused_ledger;refused_ledger.begin(73);refused_ledger.record(5,a,a,aa,1005,one);
        if(failure==0)require(refused([&]{(void)refused_ledger.finish(74,true,[&](std::int32_t){return aa;});}),"foreign ticket accepted paired heat");
        if(failure==1)require(refused([&]{(void)refused_ledger.finish(73,false,[&](std::int32_t){return aa;});}),"reallocated native array accepted paired heat");
        if(failure==2)require(refused([&]{(void)refused_ledger.finish(73,true,[&](std::int32_t){return state(1.25f,300.003f);});}),"overwritten endpoint accepted paired heat");
        if(failure==3)require(refused([&]{refused_ledger.record(5,a,a,aaa,1005,one);}),"reordered contact chain accepted paired heat");
        if(failure==4)require(refused([&]{refused_ledger.record(5,a,aa,state(1.25f,300.002f,4),1005,one);}),"native phase/material change accepted paired heat");
    }
    require(refused([&]{(void)cell_work(a,state(1.5f,300.001f),1005);}),"native mass change accepted paired heat");
    // Foreign native actors retain their original heat credit, but their
    // actual cell writes must bridge the stored-contact survival chain.
    const auto origin=state(1,300),stored=state(1,300.01f),foreign1=state(1,300.005f),foreign2=state(1,300.0025f),body=state(1,300.002f);
    EnergySum stored_counterwork;stored_counterwork.add(10.);stored_counterwork.add(-cell_work(origin,stored,1005));
    EnergySum body_counterwork;body_counterwork.add(-.5);body_counterwork.add(-cell_work(foreign2,body,1005));
    const EnergySum no_counterwork;
    ClosureLedger missing;missing.begin(75);missing.record(9,origin,origin,stored,1005,stored_counterwork);
    require(refused([&]{missing.record(9,origin,foreign2,body,1005,body_counterwork);}),"unobserved native writes were silently accepted");
    ClosureLedger bridged;bridged.begin(75);bridged.record(9,origin,origin,stored,1005,stored_counterwork);
    bridged.record(9,origin,stored,foreign1,1005,no_counterwork);
    bridged.record(9,origin,foreign1,foreign2,1005,no_counterwork);
    bridged.record(9,origin,foreign2,body,1005,body_counterwork);
    const auto bridge=bridged.finish(75,true,[&](std::int32_t){return body;});
    long double owner_heat=cell_work(origin,body,1005);
    for(const auto& h:bridge.heat)owner_heat+=h.joules;
    const long double physical=9.5L+cell_work(stored,foreign1,1005)+cell_work(foreign1,foreign2,1005);
    require(std::abs(owner_heat-physical)<1e-15L,"zero-counterwork observations changed foreign heat or credited an unowned body");
    ClosureLedger changed;changed.begin(76);changed.record(9,origin,origin,stored,1005,stored_counterwork);
    require(refused([&]{changed.unpaired_change(9);}),"unpaired phase/mass change discarded pending counterwork");
    ClosureLedger empty_change;empty_change.begin(77);empty_change.record(9,origin,origin,stored,1005,no_counterwork);empty_change.unpaired_change(9);
    require(empty_change.finish(77,true,[&](std::int32_t){return state(2,310,4);}).heat.empty(),"uncorrected foreign mutation created paired credit");
    ma::oni::storage_heat::Ledger registration;registration.add(0x01000000,300,18);
    require(registration.registered(0x01000000) && !registration.registered(0x02000000) && !registration.registered(0x01000001),"paired heat storage registration lost generation identity");
    std::cout<<"PASS paired native closure: both directions/ULPs, overlapping mixed-sign bodies, exact weighted remainder, generation scope and stale/phase/mass/array/ticket refusal\n";
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
        auto* writable=storage_heat::writable_chunk_at(sim.data(),0x01000000,layout);
        require(writable==record.data() && !storage_heat::writable_chunk_at(sim.data(),0x02000000,layout),"storage transfer platform writer accepted a stale generation");
        const auto replacement_t=std::bit_cast<std::uint32_t>(302.f),replacement_c=std::bit_cast<std::uint32_t>(9.f);
        std::memcpy(writable+4,&replacement_t,4);std::memcpy(writable+8,&replacement_c,4);
        auto expected=body;put(expected,4,replacement_t);put(expected,8,replacement_c);
        require(record==expected && sim==source,"storage transfer platform writer changed fields outside registered T/C");
        mapping[0]=-1;require(!storage_heat::writable_chunk_at(sim.data(),0x01000000,layout),"storage transfer platform writer accepted a retired compacted record");
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
    refused=false;try{(void)monitor.begin(sim.data(),4);}catch(const std::logic_error&){refused=true;}
    require(refused,"unsupported native writer permitted a storage transfer lease");
    require(std::fesetenv(&initial)==0,"storage heat environment restoration");errno=initial_errno;
    monitor.reset();require(monitor.copy(receipts.data(),4)==0,"storage native heat teardown left old receipts");
    std::cout<<"PASS storage native heat boundary: both registry layouts, ordered multi-reset processing, immutable messages and sticky refusal of foreign heat writers\n";
}
static void native_frame_prefix_controls() {
    using ma::oni::storage_heat::SubmissionFence;
    int buffers[2]{};
    SubmissionFence submissions;submissions.reset();submissions.copied(&buffers[0]);
    {
        auto lock=submissions.submission();submissions.queued(3);submissions.posted();submissions.publication(&buffers[0]);submissions.queued();
    }
    const auto captured=submissions.progress();
    require(captured.session==1 && captured.queued==4 && captured.posted==3 && captured.processed==0,
        "native frame capture lost a queued/unposted prefix");
    std::mutex domain;int publication_tail=0;
    std::promise<void> attempting,acquired;auto entered=attempting.get_future();auto prefix_done=acquired.get_future();
    auto completion=std::async(std::launch::async,[&] {
        attempting.set_value();auto submission=submissions.acquire();acquired.set_value();
        std::lock_guard frame(domain);
        require(publication_tail==17,"native completion crossed an unfinished Step publication tail");
        return submissions.progress();
    });
    entered.wait();submissions.processed();submissions.processed();
    const bool waited_prefix=prefix_done.wait_for(std::chrono::milliseconds(10))!=std::future_status::ready;
    // The final ProcessFrame still has UpdateData, EndFrame and GameData-copy
    // work after it. Completion must retain its submission gate through that
    // full domain fence, even though the processed prefix is now complete.
    std::unique_lock tail(domain);submissions.processed();
    const bool waited_publication=prefix_done.wait_for(std::chrono::milliseconds(10))!=std::future_status::ready;
    submissions.copied(&buffers[1]);prefix_done.wait();
    const bool waited_tail=completion.wait_for(std::chrono::milliseconds(10))!=std::future_status::ready;
    std::promise<void> posting;auto later_entered=posting.get_future();
    auto later=std::async(std::launch::async,[&] {
        posting.set_value();auto lock=submissions.submission();submissions.posted();submissions.publication(&buffers[1]);submissions.processed();submissions.copied(&buffers[0]);
    });
    later_entered.wait();const bool excluded_later=later.wait_for(std::chrono::milliseconds(10))!=std::future_status::ready;
    publication_tail=17;tail.unlock();const auto finished=completion.get();later.get();
    require(waited_prefix,"one native batch completion satisfied a multi-interval prefix");
    require(waited_publication,"processed intervals satisfied an unfinished native publication");
    require(waited_tail,"native prefix completion returned before the entire Step tail");
    require(excluded_later,"native completion released submission before its publication fence");
    require(finished.session==captured.session && finished.queued==4 && finished.posted==3 && finished.processed==3 &&
        finished.posted_steps==2 && finished.completed_steps==2,
        "native completion processed, posted or discarded an unposted interval");
    const auto old=submissions.progress();submissions.reset();const auto fresh=submissions.progress();
    require(fresh.session==old.session+1 && fresh.queued==0 && fresh.posted==0 && fresh.processed==0 && fresh.posted_steps==0 && fresh.completed_steps==0,
        "native initialization reused an earlier frame-prefix session");
    // Timeout refuses without changing the prefix or manufacturing progress.
    {auto lock=submissions.submission();submissions.queued();submissions.posted();}
    bool timed_out=false;try{auto lock=submissions.acquire(std::chrono::milliseconds(1));}catch(const std::logic_error&){timed_out=true;}
    const auto retained=submissions.progress();
    require(timed_out && retained.queued==1 && retained.posted==1 && retained.processed==0,
        "native completion timeout changed pending work");
    // A Prepare with no NewGameFrame still posts an entire native Step.
    SubmissionFence empty;empty.copied(&buffers[0]);{auto lock=empty.submission();empty.posted();empty.publication(&buffers[0]);}
    std::promise<void> empty_attempt;auto empty_entered=empty_attempt.get_future();
    auto zero=std::async(std::launch::async,[&]{empty_attempt.set_value();auto lock=empty.acquire();return empty.progress();});
    empty_entered.wait();const bool waited_empty=zero.wait_for(std::chrono::milliseconds(10))!=std::future_status::ready;
    empty.copied(&buffers[1]);const auto zero_done=zero.get();
    require(waited_empty && zero_done.queued==0 && zero_done.posted==0 && zero_done.processed==0 && zero_done.posted_steps==2 && zero_done.completed_steps==2,
        "zero-interval Prepare returned before its complete native Step or advanced an interval");
    // Setup/load messages can also synchronize the native thread. Preserve
    // the actual buffer epoch instead of inferring a Prepare/Step count offset.
    SubmissionFence setup;
    for(unsigned i=0;i<6;++i)setup.copied(&buffers[i%2]);
    setup.publication(&buffers[1]);
    const auto target=setup.progress();
    require(target.completed_steps==6 && target.posted_steps==7,"native publication guessed an offset across setup GameSync calls");
    setup.copied(&buffers[0]);
    {auto lock=setup.acquire();require(setup.progress().completed_steps==7,"completed publication did not accept its actual buffer epoch");}
    int unknown{};setup.publication(&unknown);bool refused=false;
    try{(void)setup.progress();}catch(const std::logic_error&){refused=true;}
    require(refused,"unknown native publication buffer silently satisfied a Step fence");
    std::cout<<"PASS native posted-prefix fence: batched/empty intervals, untouched unposted suffix, complete Step publication, later-submit exclusion and native-session reset\n";
}
static void storage_heat_transfer_controls() {
    using namespace ma::oni;
    using storage_heat::Monitor;
    using storage_heat::SubmissionFence;
    constexpr std::int32_t handle=0x01000000;
    const auto bits=[](float value){return std::bit_cast<std::uint32_t>(value);};
    const auto reject=[](auto&& action){bool refused=false;try{action();}catch(const std::exception&){refused=true;}require(refused,"invalid storage transfer accepted");};
    struct Fixture {
        std::array<std::byte,0x600> sim{},frame{};
        std::array<std::byte,88> records{};
        std::array<std::int32_t,2> mapping{0,1};
        std::array<std::uint8_t,2> generations{1,1};
        static void put(void* target,std::size_t offset,const void* value){std::memcpy(static_cast<std::byte*>(target)+offset,&value,sizeof(value));}
        static void put(void* target,std::size_t offset,std::uint32_t value){std::memcpy(static_cast<std::byte*>(target)+offset,&value,sizeof(value));}
        static void put(void* target,std::size_t offset,std::int32_t value){std::memcpy(static_cast<std::byte*>(target)+offset,&value,sizeof(value));}
        static void put(void* target,std::size_t offset,float value){std::memcpy(static_cast<std::byte*>(target)+offset,&value,sizeof(value));}
        Fixture() {
            constexpr auto l=storage_heat::registry_layout;
            put(sim.data(),l.data,records.data());put(sim.data(),l.data+8,records.data()+records.size());
            put(sim.data(),l.mapping,mapping.data());put(sim.data(),l.mapping+8,mapping.data()+mapping.size());
            put(sim.data(),l.generations,generations.data());put(sim.data(),l.generations+8,generations.data()+generations.size());
            set(0,300,18);set(1,310,7);
        }
        void set(std::size_t index,float t,float c) {
            put(records.data()+44*index,0,std::int32_t{123});put(records.data()+44*index,4,t);put(records.data()+44*index,8,c);
        }
        auto read(std::int32_t h=0x01000000) const {return chunk_heat_trace::chunk(sim.data(),h);}
    } f;
    // A posted interval has not entered ProcessFrame. Merely taking an idle
    // domain mutex would admit a transfer too early. Once it enters, retain
    // the fence through UpdateData and the remainder of Step's publication.
    int buffers[2]{};
    SubmissionFence submissions;submissions.copied(&buffers[0]);std::mutex domain;
    Monitor monitor;monitor.add(handle,300,18);
    {auto lock=submissions.submission();submissions.queued();submissions.posted();submissions.publication(&buffers[0]);}
    std::promise<void> allow_process,process_done,allow_sample,sample_done,allow_tail;
    auto process_allowed=allow_process.get_future();auto sample_allowed=allow_sample.get_future();auto tail_allowed=allow_tail.get_future();
    auto processing=std::async(std::launch::async,[&] {
        process_allowed.wait();std::lock_guard frame(domain);
        f.set(0,299,18);
        monitor.process(f.frame.data(),f.sim.data(),1,[]{});
        submissions.processed();process_done.set_value();sample_allowed.wait();
        f.set(0,298,18);monitor.sample(f.sim.data(),1);sample_done.set_value();tail_allowed.wait();submissions.copied(&buffers[1]);
    });
    std::promise<void> trying;auto attempted=trying.get_future();
    auto transferring=std::async(std::launch::async,[&] {
        trying.set_value();auto posted=submissions.acquire();std::unique_lock frame(domain);
        const auto token=monitor.begin(f.sim.data(),1,std::move(posted),std::move(frame));
        require(token!=0,"completed native work did not admit a storage lease");
        std::array<MAOniStorageHeatReceipt,4> receipts{};
        require(monitor.copy(receipts.data(),4)==2 && receipts[0].capacity_kj_k==18 && receipts[0].before_k==300 && receipts[0].after_k==299 &&
            receipts[1].capacity_kj_k==18 && receipts[1].before_k==299 && receipts[1].after_k==298,
            "storage transfer missed ProcessFrame or late UpdateData work at the old capacity");
        const MAOniStorageHeatEndpoint partial{handle,297,9,0};const auto before_ack=f.records;
        reject([&]{monitor.replace(token,&partial,1);});require(f.records==before_ack,"storage replacement passed unacknowledged old-capacity heat");
        monitor.ack(receipts[1].sequence);
        monitor.replace(token,&partial,1);monitor.end(token,1);
    });
    attempted.wait();const bool waited_entry=transferring.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready;
    allow_process.set_value();process_done.get_future().wait();
    const bool waited_update=transferring.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready;
    allow_sample.set_value();sample_done.get_future().wait();
    const bool waited_tail=transferring.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready;
    allow_tail.set_value();processing.get();transferring.get();
    require(waited_entry,"storage transfer passed a posted frame before ProcessFrame");
    require(waited_update,"storage transfer passed before UpdateData sampling");
    require(waited_tail,"storage transfer passed before the complete Step tail");
    require(f.read().temperature_bits==bits(297) && f.read().capacity_bits==bits(9) && monitor.copy(nullptr,0)==0,
        "partial withdrawal left the registered old capacity or lost receipt acknowledgements");
    // Consecutive callbacks do not queue absolute messages for an endpoint
    // already applied under the lease. Complete withdrawal then immediate
    // readmission must therefore be possible without advancing native time.
    auto token=monitor.begin(f.sim.data(),1);const MAOniStorageHeatEndpoint empty{handle,297,0,0};
    monitor.replace(token,&empty,1);monitor.end(token,1);
    f.set(0,296,0);monitor.sample(f.sim.data(),1);
    require(monitor.copy(nullptr,0)==0,"zero-capacity projection accepted massless physical heat");
    token=monitor.begin(f.sim.data(),1);const MAOniStorageHeatEndpoint readmitted{handle,301,4,0};
    monitor.replace(token,&readmitted,1);monitor.end(token,1);
    f.set(0,302,4);monitor.sample(f.sim.data(),2);
    std::array<MAOniStorageHeatReceipt,8> receipts{};
    require(monitor.copy(receipts.data(),8)==1 && receipts[0].capacity_kj_k==4 && receipts[0].before_k==301 && receipts[0].after_k==302,
        "readmission retained the old capacity endpoint or counted a projection as physical heat");
    monitor.ack(receipts[0].sequence);
    // Reject a late invalid endpoint before writing the otherwise valid first
    // row. A rollback restores projections; real sampled work remains exact.
    monitor.add(handle+1,310,7);token=monitor.begin(f.sim.data(),2);
    const auto before=f.records;
    std::array<MAOniStorageHeatEndpoint,2> invalid{{{handle,303,2,0},{handle+1,304,-1,0}}};
    reject([&]{monitor.replace(token,invalid.data(),2);});require(f.records==before,"invalid storage replacement partially changed native bodies");
    invalid[1]={handle,304,1,0};reject([&]{monitor.replace(token,invalid.data(),2);});
    invalid[1]={0x02000001,304,1,0};reject([&]{monitor.replace(token,invalid.data(),2);});
    require(f.records==before,"duplicate/stale storage replacement changed an earlier endpoint");
    reject([&]{monitor.expect(handle,303,2);});reject([&]{monitor.expect_removal(handle);});reject([&]{monitor.reset();});
    auto wrong_thread=std::async(std::launch::async,[&]{reject([&]{monitor.copy(nullptr,0);});reject([&]{monitor.end(token,1);});});wrong_thread.get();
    reject([&]{monitor.end(token+1,1);});reject([&]{monitor.end(token,2);});
    const MAOniStorageHeatEndpoint rolled_back{handle,303,2,0};monitor.replace(token,&rolled_back,1);
    reject([&]{monitor.replace(token,&rolled_back,1);});monitor.end(token,0);
    require(f.records==before,"aborted transfer did not restore registered projection endpoints");
    reject([&]{monitor.end(token,1);});
    // Unposted declarations cannot be settled by manufacturing a frame. They
    // defer the transfer without sampling, replacing or discarding anything.
    monitor.expect(handle,305,3);f.set(0,301,4);const auto pending=f.records;
    require(monitor.begin(f.sim.data(),3)==0 && f.records==pending && monitor.copy(nullptr,0)==0,
        "pending native declaration was accepted or changed during transfer deferral");
#if defined(_WIN32)
    constexpr std::size_t data_offset=0x310;
#else
    constexpr std::size_t data_offset=0x248;
#endif
    std::array<std::uint32_t,3> data{static_cast<std::uint32_t>(handle),bits(305),bits(3)};
    Fixture::put(f.frame.data(),data_offset,data.data());Fixture::put(f.frame.data(),data_offset+8,data.data()+data.size());
    monitor.process(f.frame.data(),f.sim.data(),3,[&]{f.set(0,305,3);});
    token=monitor.begin(f.sim.data(),3);require(token!=0,"applied pending declaration did not unblock a transfer");
    require(monitor.copy(receipts.data(),8)==1 && receipts[0].capacity_kj_k==4 && receipts[0].before_k==302 && receipts[0].after_k==301,
        "deferred transfer lost real heat before the declared capacity endpoint");
    monitor.end(token,1);
    // A recycled compacted slot is a new handle. Retirement itself blocks a
    // lease, its receipt must be acknowledged, and the old generation can
    // never replace the newly registered body's endpoint.
    Fixture recycled;recycled.set(0,300,0);Monitor reused;reused.add(handle,300,0);reused.expect_removal(handle);
    require(reused.begin(recycled.sim.data(),4)==0,"pending storage retirement admitted a transfer lease");
#if defined(_WIN32)
    constexpr std::size_t removal_offset=0x330;
#else
    constexpr std::size_t removal_offset=0x260;
#endif
    std::array<std::uint32_t,2> removal{static_cast<std::uint32_t>(handle),0};
    Fixture::put(recycled.frame.data(),removal_offset,removal.data());Fixture::put(recycled.frame.data(),removal_offset+8,removal.data()+removal.size());
    reused.process(recycled.frame.data(),recycled.sim.data(),4,[&]{recycled.generations[0]=2;});
    require(reused.copy(receipts.data(),8)==1 && receipts[0].kind==1,"storage transfer retirement omitted its completed native endpoint");
    reused.ack(receipts[0].sequence);recycled.set(0,300,4);reused.add(0x02000000,300,4);
    token=reused.begin(recycled.sim.data(),4);const auto recycled_before=recycled.records;
    reject([&]{reused.replace(token,&empty,1);});require(recycled.records==recycled_before,"retired handle replaced a reused native slot");
    const MAOniStorageHeatEndpoint new_generation{0x02000000,301,2,0};reused.replace(token,&new_generation,1);reused.end(token,1);
    require(recycled.read(0x02000000).capacity_bits==bits(2),"new storage generation failed to replace its own native endpoint");
    // No frame has been posted for this queued interval. The submission gate
    // must return immediately, while keeping a later submitter excluded.
    SubmissionFence queued;queued.copied(&buffers[0]);{auto lock=queued.submission();queued.queued();}
    {auto held=queued.acquire();std::promise<void> started;auto entered=started.get_future();
        auto later=std::async(std::launch::async,[&]{started.set_value();auto lock=queued.submission();queued.posted();queued.publication(&buffers[0]);queued.processed();queued.copied(&buffers[1]);});
        entered.wait();require(later.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready,"storage lease did not exclude a subsequent frame submission");
        held.unlock();later.get();}
    std::cout<<"PASS exact storage transfer leases: posted-before-entry and late UpdateData fences, partial/full withdrawal, same-callback readmission, deferred declarations, atomic batch refusal, thread/token checks and rollback\n";
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
        body_heat_trace_layout_controls();
        runtime_context_controls();
        runtime_clock_controls();
        native_load_guard_controls();
        chunk_heat_trace_controls();
        paired_heat_observation_controls();
        paired_building_invocation_controls();
        paired_heat_closure_controls();
        storage_heat_receipt_controls();
        storage_heat_native_controls();
        native_frame_prefix_controls();
        storage_heat_transfer_controls();
        conduit_heat_completion_controls();
        {
            static_assert(sizeof(MAOniNativeFrameProgress)==56);
            MAOniNativeFrameProgress progress{56,1,9,13,11,7,5,4};const auto unchanged=progress;
            require(ma_oni_native_frames_version()==1 && ma_oni_native_frames_progress(nullptr)==MA_ONI_INVALID_ARGUMENT &&
                ma_oni_native_frames_complete(nullptr,1000)==MA_ONI_INVALID_ARGUMENT &&
                ma_oni_native_frames_complete(&progress,0)==MA_ONI_INVALID_ARGUMENT &&
                ma_oni_native_frames_complete(&progress,60001)==MA_ONI_INVALID_ARGUMENT &&
                ma_oni_native_frames_progress(&progress)==MA_ONI_BUSY && ma_oni_native_frames_complete(&progress,1000)==MA_ONI_BUSY &&
                std::memcmp(&progress,&unchanged,sizeof(progress))==0,"native frame ABI accepted invalid/inactive work or changed output");
            progress.abi_version=2;const auto invalid=progress;
            require(ma_oni_native_frames_progress(&progress)==MA_ONI_INVALID_ARGUMENT &&
                ma_oni_native_frames_complete(&progress,1000)==MA_ONI_INVALID_ARGUMENT &&
                std::memcmp(&progress,&invalid,sizeof(progress))==0,"native frame ABI rejection changed progress");
        }
        require(ma_oni_colony_physical_core_count() >= 1 && ma_oni_colony_physical_core_count() <= 256 &&
                    ma_oni_colony_set_worker_count(30) == MA_ONI_OK &&
                    ma_oni_colony_set_worker_count(257) == MA_ONI_INVALID_ARGUMENT &&
                    ma_oni_colony_set_worker_count(0) == MA_ONI_OK,
                "worker preference ABI or physical-core default differs");
        require(ma_oni_colony_precise_receipt_version()==2,"precise receipt observation capability");
        {
            static_assert(sizeof(MAOniColonyActivityState)==56 && sizeof(MAOniColonyActivityCell)==32);
            MAOniColonyActivityState state{};state.struct_size=sizeof(state);state.abi_version=1;state.session=123;
            const auto original=state;std::uint64_t sequence=456;
            require(ma_oni_colony_activity_version()==1 && ma_oni_colony_copy_activity(nullptr)==MA_ONI_INVALID_ARGUMENT &&
                ma_oni_colony_copy_activity(&state)==MA_ONI_INVALID_ARGUMENT && std::memcmp(&state,&original,sizeof(state))==0,
                "Inactive activity query changed its output");
            require(ma_oni_colony_queue_activity(0,.2,nullptr,1,nullptr,0,&sequence)==MA_ONI_INVALID_ARGUMENT &&
                ma_oni_colony_queue_activity(0,.2,nullptr,0,nullptr,0,&sequence)==MA_ONI_INVALID_ARGUMENT && sequence==456 &&
                ma_oni_colony_claim_activity_frame(0,1,.2)==MA_ONI_INVALID_ARGUMENT &&
                ma_oni_colony_set_activity_tiers(0,2)==MA_ONI_INVALID_ARGUMENT,"Invalid activity input admitted or changed its sequence");
            std::uint64_t frame=99;std::uint32_t count=101,parts=103;
            require(ma_oni_colony_readings(&frame,nullptr,0,&count,nullptr,0,&parts)==MA_ONI_INVALID_ARGUMENT &&
                frame==99 && count==101 && parts==103,"Inactive pinned readings changed count outputs");
            const std::uint32_t cell=1;MAOniColonyStatus status{};status.struct_size=sizeof(status);status.abi_version=1;
            MAOniColonyActivityCell row{1,7,2,3,4};const auto original_row=row;const auto original_status=status;
            require(ma_oni_colony_sample_activity(0,0,&cell,1,&status,&row)==MA_ONI_INVALID_ARGUMENT &&
                ma_oni_colony_sample_activity(0,0,&cell,0,&status,&row)==MA_ONI_INVALID_ARGUMENT &&
                std::memcmp(&status,&original_status,sizeof(status))==0 && std::memcmp(&row,&original_row,sizeof(row))==0,
                "Inactive activity observation changed a partial output");
        }
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
        require(ma_oni_storage_heat_version()==3 && sizeof(MAOniStorageHeatReceipt)==48 && sizeof(MAOniStorageHeatEndpoint)==16,"Native exact storage transfer ABI");
        std::uint64_t inactive_storage_token=1234;
        require(ma_oni_storage_heat_begin(&inactive_storage_token)==MA_ONI_BUSY && inactive_storage_token==1234,"Inactive native storage transfer changed its output token");
        require(ma_oni_storage_heat_begin(nullptr)==MA_ONI_INVALID_ARGUMENT && ma_oni_storage_heat_replace(1,nullptr,1)==MA_ONI_INVALID_ARGUMENT &&
            ma_oni_storage_heat_end(1,1)==MA_ONI_INVALID_ARGUMENT,"Invalid native storage transfer framing accepted");
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
