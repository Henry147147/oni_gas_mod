#pragma once
#include "../src/oni_body_heat_trace.hpp"
#include <array>
#include <limits>
#include <string>
#include <type_traits>

namespace ma_oni_body_heat_trace_controls {
namespace trace=ma::oni::body_heat_trace;
struct Sink {
    inline static std::FILE* output=nullptr;
    static bool enabled() noexcept {return output!=nullptr;}
    static std::FILE* file() noexcept {
        // Force observable I/O-side changes: the observer must restore the
        // caller even when its sink changes more than ordinary stdio does.
        (void)std::fesetround(FE_TONEAREST);(void)std::feraiseexcept(FE_OVERFLOW);
#if defined(__linux__) || defined(_WIN32)
        _mm_setcsr(_mm_getcsr()^0x8040U);
#endif
        errno=3456;return output;
    }
};
template<bool Windows> struct Fixture {
    std::array<std::byte,0x600> sim{};
    std::array<std::byte,0x300> frame{};
    std::array<std::byte,88> data{};
    std::array<std::int32_t,3> mapping{1,0,1};
    std::array<std::uint8_t,3> generations{0,1,128};
    std::array<std::uint32_t,8> messages{0x01000001U,0,0x438c0000U,0x43a00000U,
                                      0x02000001U,0x80000000U,0x438c0000U,0x43a00000U};
    // Independent fixture offsets: do not construct synthetic memory with
    // constants taken from the implementation's Layout under test.
    static constexpr std::size_t data_first=Windows?0x4e0:0x438,data_last=Windows?0x4e8:0x440;
    static constexpr std::size_t map_first=Windows?0x540:0x480,map_last=Windows?0x548:0x488;
    static constexpr std::size_t gen_first=Windows?0x560:0x498,gen_last=Windows?0x568:0x4a0;
    static constexpr std::size_t msg_first=Windows?0x250:0x1b8,msg_last=Windows?0x258:0x1c0;
    template<class Array,class T> static void put(Array& target,std::size_t offset,const T& value){
        std::memcpy(target.data()+offset,&value,sizeof value);
    }
    Fixture(){
        put(sim,data_first,data.data());put(sim,data_last,data.data()+data.size());
        put(sim,map_first,mapping.data());put(sim,map_last,mapping.data()+mapping.size());
        put(sim,gen_first,generations.data());put(sim,gen_last,generations.data()+generations.size());
        put(frame,msg_first,messages.data());put(frame,msg_last,messages.data()+messages.size());
        put(data,0,std::uint32_t{0x43960000});put(data,16,std::uint32_t{0x41900000});
        put(data,28,std::int32_t{10});put(data,32,std::int32_t{20});put(data,36,std::int32_t{12});put(data,40,std::int32_t{23});
        put(data,44,std::uint32_t{0x7fa12345});put(data,60,std::uint32_t{0});
        put(data,72,std::int32_t{-4});put(data,76,std::int32_t{2});put(data,80,std::int32_t{-1});put(data,84,std::int32_t{3});
    }
    bool same(const Fixture& held)const{return sim==held.sim && frame==held.frame && data==held.data && mapping==held.mapping && generations==held.generations && messages==held.messages;}
};
template<class Layout,bool Windows> void layout(){
    Fixture<Windows> f;
    const auto observe=[&](std::uint32_t bits){
        const auto held=f;const auto sample=trace::read_body<Layout>(f.sim.data(),std::bit_cast<std::int32_t>(bits));
        require(f.same(held),"body layout reader mutated synthetic native memory");return sample;
    };
    const auto normal=observe(0x01000001U);
    require(normal.valid && normal.area==6 && normal.temperature_bits==0x43960000U && normal.capacity_per_cell_bits==0x41900000U,
        "body layout did not read mapped record temperature/capacity/area");
    const auto zero_generation=observe(0);
    require(zero_generation.valid && zero_generation.area==3 && zero_generation.temperature_bits==0x7fa12345U && zero_generation.capacity_per_cell_bits==0,
        "body layout changed raw bits or rejected generation zero");
    require(observe(0x80000002U).valid && !observe(0x7f000002U).valid && !observe(0x80100002U).valid &&
        !observe(0x01000003U).valid && !trace::read_body<Layout>(nullptr,0).valid,"body layout stale/reserved/index guards differ");
    f.generations[2]=255;require(observe(0xff000002U).valid,"body layout did not retain all generation bits");f.generations[2]=128;
    for(const auto invalid:{-1,2,std::numeric_limits<std::int32_t>::max()}){
        f.mapping[1]=invalid;require(!observe(0x01000001U).valid,"body layout accepted retired or out-of-range mapping");
    }
    f.mapping[1]=0;
    const auto original_sim=f.sim;
    const auto bad_span=[&](std::size_t first,std::size_t last,const auto* pointer,std::size_t bytes){
        Fixture<Windows>::put(f.sim,first,pointer);
        const auto* end=reinterpret_cast<const std::byte*>(reinterpret_cast<std::uintptr_t>(pointer)+bytes);
        Fixture<Windows>::put(f.sim,last,end);
        require(!observe(0x01000001U).valid,"body layout accepted malformed native vector bounds");f.sim=original_sim;
    };
    bad_span(f.gen_first,f.gen_last,f.generations.data(),1);
    bad_span(f.gen_first,f.gen_last,f.generations.data(),(1U<<20)+1U);
    bad_span(f.map_first,f.map_last,f.mapping.data(),3);
    bad_span(f.map_first,f.map_last,f.mapping.data(),((1U<<20)+1U)*4U);
    bad_span(f.data_first,f.data_last,f.data.data(),87);
    bad_span(f.data_first,f.data_last,f.data.data(),((1U<<20)+1U)*44U);
    Fixture<Windows>::put(f.sim,f.map_first,static_cast<const std::int32_t*>(nullptr));
    require(!observe(0x01000001U).valid,"body layout accepted null native vector start");f.sim=original_sim;
    Fixture<Windows>::put(f.sim,f.gen_first,f.generations.data()+1);Fixture<Windows>::put(f.sim,f.gen_last,f.generations.data());
    require(!observe(0x01000001U).valid,"body layout accepted reversed native vector");f.sim=original_sim;
    const auto original_data=f.data;
    for(const auto bounds:std::array<std::array<std::int32_t,4>,5>{{{{10,20,10,23}},{{10,20,12,19}},{{0,0,1048577,1}},{{0,0,1025,1024}},{{std::numeric_limits<std::int32_t>::min(),0,std::numeric_limits<std::int32_t>::max(),1}}}}){
        for(std::size_t i=0;i<bounds.size();++i)Fixture<Windows>::put(f.data,28+i*4,bounds[i]);
        require(!observe(0x01000001U).valid,"body layout accepted malformed or overflowing body bounds");
    }
    Fixture<Windows>::put(f.data,28,std::int32_t{0});Fixture<Windows>::put(f.data,32,std::int32_t{0});
    Fixture<Windows>::put(f.data,36,std::int32_t{1024});Fixture<Windows>::put(f.data,40,std::int32_t{1024});
    require(observe(0x01000001U).area==1048576,"body layout rejected the valid area boundary");f.data=original_data;
    using ObservedFrame=trace::BasicFrame<Layout,Sink>;
    require(!ObservedFrame::enabled(),"body synthetic trace sink began enabled");
    for(const auto rounding:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}){
        Sink::output=std::tmpfile();require(Sink::output!=nullptr && ObservedFrame::enabled(),"body synthetic trace sink unavailable");
        require(std::fesetround(rounding)==0,"body trace rounding control failed");
        (void)std::feclearexcept(FE_ALL_EXCEPT);(void)std::feraiseexcept(FE_DIVBYZERO);
#if defined(__linux__) || defined(_WIN32)
        _mm_setcsr((_mm_getcsr()|0x1f80U)^0x8040U);const auto before_csr=_mm_getcsr();
#endif
        const auto before_flags=std::fetestexcept(FE_ALL_EXCEPT);int return_flags=0;
        require((before_flags&FE_DIVBYZERO)!=0,"body trace exception control failed");errno=1234;
#if defined(__linux__) || defined(_WIN32)
        unsigned return_csr=0;
#endif
        const auto held=f;
        {
            ObservedFrame frame(73,f.frame.data(),f.sim.data());
            require(f.same(held) && errno==1234 && std::fegetround()==rounding && std::fetestexcept(FE_ALL_EXCEPT)==before_flags,
                "body trace construction changed memory, errno or FPU state");
#if defined(__linux__) || defined(_WIN32)
            require(_mm_getcsr()==before_csr,"body trace construction changed MXCSR");
#endif
            const auto raw=observe(0x80000002U);require(raw.temperature_bits==0x7fa12345U,"body reader interpreted raw signalling-NaN bits");
            require(errno==1234 && std::fegetround()==rounding && std::fetestexcept(FE_ALL_EXCEPT)==before_flags,
                "body raw-bit reader changed caller math or errno");
#if defined(__linux__) || defined(_WIN32)
            require(_mm_getcsr()==before_csr,"body raw-bit reader changed MXCSR");
#endif
            Fixture<Windows>::put(f.data,0,std::uint32_t{0x43958000});
            (void)std::feraiseexcept(FE_INVALID);errno=2345;
            return_flags=std::fetestexcept(FE_ALL_EXCEPT);
#if defined(__linux__) || defined(_WIN32)
            return_csr=_mm_getcsr();
#endif
        }
        require((return_flags&FE_INVALID)!=0 && errno==2345 && std::fegetround()==rounding && std::fetestexcept(FE_ALL_EXCEPT)==return_flags,
            "body trace emission changed caller errno or FPU state");
#if defined(__linux__) || defined(_WIN32)
        require(_mm_getcsr()==return_csr,"body trace emission changed caller MXCSR");
#endif
        auto expected=held;Fixture<Windows>::put(expected.data,0,std::uint32_t{0x43958000});
        require(f.same(expected),"body trace emission changed native frame/body memory");
        require(std::fseek(Sink::output,0,SEEK_SET)==0,"body trace synthetic rewind failed");
        std::array<char,512> text{};const auto n=std::fread(text.data(),1,text.size()-1,Sink::output);
        require(std::fclose(Sink::output)==0,"body trace synthetic close failed");Sink::output=nullptr;
        require(n>0 && std::string(text.data())==
            "73\t16777217\t00000000\t438c0000\t43a00000\t43960000\t43958000\t41900000\t6\t3\n"
            "73\t33554433\t80000000\t438c0000\t43a00000\t00000000\t00000000\t00000000\t0\t0\n",
            "body trace omitted zero-energy rows, changed raw message bits or lost paired validity");
        f.data=original_data;
    }
    for(const auto size:{std::size_t{15},std::size_t{4097*16}}){
        const auto held=f;
        Fixture<Windows>::put(f.frame,f.msg_last,reinterpret_cast<const std::byte*>(reinterpret_cast<std::uintptr_t>(f.messages.data())+size));
        const auto malformed=f;Sink::output=std::tmpfile();require(Sink::output!=nullptr,"body malformed-frame sink unavailable");
        {ObservedFrame frame(74,f.frame.data(),f.sim.data());}
        require(f.same(malformed) && std::ftell(Sink::output)==0,"body observer read malformed frame bounds or mutated memory");
        require(std::fclose(Sink::output)==0,"body malformed-frame close failed");Sink::output=nullptr;f=held;
    }
}
}

static void body_heat_trace_layout_controls(){
    ma::oni::body_heat_trace::detail::Preserve preserve;
    ma_oni_body_heat_trace_controls::layout<ma::oni::body_heat_trace::LinuxLayout,false>();
    ma_oni_body_heat_trace_controls::layout<ma::oni::body_heat_trace::WindowsLayout,true>();
    require(!ma::oni::body_heat_trace::read_body<ma::oni::body_heat_trace::UnsupportedLayout>(nullptr,0).valid,"unsupported body layout was read");
#if defined(_WIN32)
    static_assert(std::is_same_v<ma::oni::body_heat_trace::NativeLayout,ma::oni::body_heat_trace::WindowsLayout>);
#elif defined(__linux__)
    static_assert(std::is_same_v<ma::oni::body_heat_trace::NativeLayout,ma::oni::body_heat_trace::LinuxLayout>);
#endif
    std::cout<<"PASS Linux/Windows body trace layouts: mapped/stale/generation/bounds guards, zero-energy rows, readonly memory and preserved FPU/errno/MXCSR\n";
}
