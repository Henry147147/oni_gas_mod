#pragma once
#include "mixed_atmospheres/oni_runtime_clock.h"
#include "oni_runtime_trace.hpp"
#include <array>
#include <atomic>
#include <mutex>
#include <stdexcept>

namespace ma::oni::runtime_clock {
inline void validate(const MAOniRuntimeClock& state) {
    if(state.struct_size!=sizeof(state)||state.abi_version!=1||state.reserved!=0||state.width<3||state.height<3||
        state.cells<=0||std::int64_t(state.width-2)*(state.height-2)!=state.cells||
        (state.iterate!=-1&&state.iterate!=1)||(state.displacement!=-1&&state.displacement!=1))
        throw std::invalid_argument("Invalid native runtime clock state");
}
inline MAOniRuntimeClock read(const void* sim,bool windows) {
    if(!sim)throw std::invalid_argument("Missing native runtime clock owner");
    const auto c=runtime_trace::context(sim,windows);
    MAOniRuntimeClock result{sizeof(MAOniRuntimeClock),1,c.width,c.height,c.cells,c.random,c.iterate,c.displacement,c.tick,0};
    validate(result);return result;
}
inline void write(void* sim,bool windows,const MAOniRuntimeClock& desired) {
    validate(desired);const auto current=read(sim,windows);
    if(current.width!=desired.width||current.height!=desired.height||current.cells!=desired.cells)
        throw std::invalid_argument("Saved native clock geometry differs from its live owner");
    auto* bytes=static_cast<std::byte*>(sim);
    std::memcpy(bytes+12,&desired.random,4);std::memcpy(bytes+16,&desired.iterate,4);
    std::memcpy(bytes+20,&desired.displacement,4);std::memcpy(bytes+(windows?160:144),&desired.tick,2);
}
class Monitor {
    std::atomic<bool> active_{false};
    std::mutex mutex_;
    MAOniRuntimeClock latest_{},restore_{};
    bool available_=false,pending_=false;
    std::array<char,256> failure_{};
    void check() const {if(failure_[0])throw std::logic_error(failure_.data());}
    void fail(const char* reason) noexcept {if(!failure_[0])std::snprintf(failure_.data(),failure_.size(),"%s",reason);}
public:
#if defined(_WIN32)
    static constexpr bool windows=true;
#else
    static constexpr bool windows=false;
#endif
    static constexpr bool supported() noexcept {
#if defined(__x86_64__) || defined(_M_X64)
        return sizeof(void*)==8;
#else
        return false;
#endif
    }
    void start() {std::lock_guard lock(mutex_);check();active_.store(true,std::memory_order_release);}
    void reset() {
        std::lock_guard lock(mutex_);active_.store(false,std::memory_order_release);
        latest_={};restore_={};available_=pending_=false;failure_.fill(0);
    }
    void sample(const void* sim) noexcept {
        if(!active_.load(std::memory_order_acquire))return;
        body_heat_trace::detail::Preserve preserve;std::lock_guard lock(mutex_);
        if(!active_.load(std::memory_order_relaxed))return;
        try{check();latest_=read(sim,windows);available_=true;}
        catch(const std::exception& error){fail(error.what());}catch(...){fail("Unknown native runtime clock observation failure");}
    }
    MAOniRuntimeClock copy() {
        std::lock_guard lock(mutex_);check();
        if(!active_.load(std::memory_order_relaxed)||!available_||pending_)throw std::logic_error("Native runtime clock has no completed capture");
        return latest_;
    }
    void restore(const MAOniRuntimeClock& state) {
        validate(state);std::lock_guard lock(mutex_);check();
        if(!active_.load(std::memory_order_relaxed)||!available_||pending_)throw std::logic_error("Native runtime clock restore requires a fresh completed capture");
        if(latest_.width!=state.width||latest_.height!=state.height||latest_.cells!=state.cells)
            throw std::invalid_argument("Saved native clock geometry differs from its live owner");
        restore_=state;pending_=true;
    }
    bool pending() {std::lock_guard lock(mutex_);check();return pending_;}
    template<class Next> void process(void* sim,float seconds,Next&& next) {
        next(); // Never consume a request if original processing throws.
        if(!active_.load(std::memory_order_acquire))return;
        body_heat_trace::detail::Preserve preserve;std::lock_guard lock(mutex_);
        if(!pending_||!active_.load(std::memory_order_relaxed))return;
        try {
            check();if((std::bit_cast<std::uint32_t>(seconds)&0x7fffffffU)!=0)
                throw std::logic_error("Native runtime clock restore reached positive time before its zero-time barrier");
            write(sim,windows,restore_);pending_=false;available_=false;
        }catch(const std::exception& error){fail(error.what());}catch(...){fail("Unknown native runtime clock restore failure");}
    }
};
static_assert(sizeof(MAOniRuntimeClock)==36);
}
