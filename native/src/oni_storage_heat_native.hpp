#pragma once
#include "oni_storage_heat.hpp"
#include <array>
#include <atomic>

namespace ma::oni::storage_heat {
class Monitor {
    std::mutex mutex_;
    Ledger ledger_;
    std::array<char,256> failure_{};
    std::atomic<bool> active_{false};
    void check() const {if(failure_[0])throw std::logic_error(failure_.data());}
    void fail(const char* reason) noexcept {if(!failure_[0])std::snprintf(failure_.data(),failure_.size(),"%s",reason);}
    static void messages(const void* frame,std::int32_t handle,const auto& append) {
        namespace d=chunk_heat_trace::detail;
        if(!frame)throw std::logic_error("Missing exact storage native frame");
        // These are the pinned frame-vector boundaries, not game payloads.
#if defined(_WIN32)
        // SimFrameInfo.elementChunkMessages+744; modifies+32/removes+64;
        // energy+872/adjuster+904, MSVC vector pointers at+8 (matching PDB).
        constexpr std::size_t data_offset=0x310,energy_offset=0x370,adjuster_offset=0x390,removal_offset=0x330;
#else
        constexpr std::size_t data_offset=0x248,energy_offset=0x290,adjuster_offset=0x2a8,removal_offset=0x260;
#endif
        const auto scan=[&](std::size_t offset,std::size_t stride,const auto& visit) {
            const auto* first=d::read<const std::byte*>(frame,offset);
            const auto* last=d::read<const std::byte*>(frame,offset+8);
            if(first==last)return;
            const auto count=d::count(first,last,stride,65536);
            if(!count)throw std::logic_error("Malformed exact storage native frame vector");
            for(std::size_t i=0;i<count;++i)if(d::read<std::int32_t>(first+i*stride,0)==handle)visit(first+i*stride);
        };
        const auto refuse=[](const void*) {throw std::logic_error("Unsupported native energy, adjuster or removal message for exact storage");};
        scan(energy_offset,8,refuse);scan(adjuster_offset,16,refuse);scan(removal_offset,8,refuse);
        scan(data_offset,12,[&](const void* row) {append(d::read<std::uint32_t>(row,4),d::read<std::uint32_t>(row,8));});
    }
public:
    static constexpr bool supported() noexcept {
#if defined(__linux__) || defined(_WIN32)
        return true;
#else
        return false;
#endif
    }
    void reset(){std::lock_guard lock(mutex_);ledger_=Ledger{};failure_.fill(0);active_.store(false,std::memory_order_release);}
    void add(std::int32_t handle,float t,float c){std::lock_guard lock(mutex_);check();ledger_.add(handle,t,c);active_.store(true,std::memory_order_release);}
    void expect(std::int32_t handle,float t,float c){std::lock_guard lock(mutex_);check();ledger_.expect(handle,t,c);}
    std::uint32_t copy(MAOniStorageHeatReceipt* output,std::uint32_t capacity){std::lock_guard lock(mutex_);check();return ledger_.copy(output,capacity);}
    void ack(std::uint64_t through){std::lock_guard lock(mutex_);check();ledger_.ack(through);}
    template<class Original> void process(const void* frame,const void* sim,std::uint64_t ordinal,Original original) {
        if(!active_.load(std::memory_order_acquire)){original();return;}
        std::unique_lock lock(mutex_);
        if(ledger_.empty() || failure_[0]) {lock.unlock();original();return;}
        const auto read=[&](std::int32_t handle){return chunk_heat_trace::chunk(sim,handle);};
        {const chunk_heat_trace::detail::Preserve preserve;
        try {ledger_.before(read,[&](std::int32_t handle,const auto& append){messages(frame,handle,append);},ordinal);}
        catch(const std::exception& error){fail(error.what());}
        catch(...){fail("Unknown exact storage native heat observation failure");}}
        original();
        const chunk_heat_trace::detail::Preserve preserve;
        if(!failure_[0])try {ledger_.after(read,ordinal);}
        catch(const std::exception& error){fail(error.what());}
        catch(...){fail("Unknown exact storage native heat observation failure");}
    }
    void sample(const void* sim,std::uint64_t ordinal) {
        if(!active_.load(std::memory_order_acquire))return;
        const chunk_heat_trace::detail::Preserve preserve;
        std::lock_guard lock(mutex_);if(ledger_.empty() || failure_[0])return;
        try {ledger_.sample([&](std::int32_t handle){return chunk_heat_trace::chunk(sim,handle);},ordinal);}
        catch(const std::exception& error){fail(error.what());}
        catch(...){fail("Unknown exact storage native heat observation failure");}
    }
};
}
