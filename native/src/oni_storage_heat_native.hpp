#pragma once
#include "oni_storage_heat.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <thread>
#include <vector>

namespace ma::oni::storage_heat {
// All message submissions pass this gate. NewGameFrame queues one global
// interval (its active-region rows are not separate frames); PrepareGameData
// posts the queued prefix. ProcessFrame completion alone is insufficient:
// acquire the domain fence afterward to include UpdateData and Step's tail.
class SubmissionFence {
    std::recursive_mutex submission_;
    std::mutex progress_;
    std::condition_variable progressed_;
    std::uint64_t session_=0,queued_=0,posted_=0,processed_=0,posted_steps_=0,completed_steps_=0;
    struct Publication {const void* game=nullptr;std::uint64_t sequence=0;};
    std::array<Publication,2> publications_{};
    std::atomic<bool> failed_{false};
public:
    struct Progress {std::uint64_t session,queued,posted,processed,posted_steps,completed_steps;};
    auto submission() {return std::unique_lock<std::recursive_mutex>(submission_);}
    void reset() {
        auto gate=submission();std::lock_guard lock(progress_);
        queued_=posted_=processed_=posted_steps_=completed_steps_=0;publications_={};failed_=session_==UINT64_MAX;
        if(!failed_.load(std::memory_order_relaxed))++session_;
    }
    void queued(std::uint32_t count=1) noexcept {
        if(UINT64_MAX-queued_<count)failed_=true;else queued_+=count;
    }
    void posted() noexcept {posted_=queued_;}
    void processed() noexcept {
        {std::lock_guard lock(progress_);if(processed_==UINT64_MAX)failed_=true;else ++processed_;}
        progressed_.notify_all();
    }
    void copied(const void* game) noexcept {
        {std::lock_guard lock(progress_);
        if(!game || completed_steps_==UINT64_MAX)failed_=true;
        else {
            auto* slot=&publications_[0];
            if(publications_[1].game==game || (slot->game!=game && publications_[1].sequence<slot->sequence))slot=&publications_[1];
            *slot={game,++completed_steps_};
        }}
        progressed_.notify_all();
    }
    // PrepareGameDataUpdate reads the stable front buffer after GameSync has
    // swapped it. The just-woken Step writes the other buffer, so its actual
    // completion is exactly the front buffer's copy sequence plus one. This
    // includes zero-interval Steps and startup/load GameSyncs without guessing
    // a fixed offset between Prepare messages and native Step invocations.
    void publication(const void* game) noexcept {
        std::lock_guard lock(progress_);
        const auto* slot=publications_[0].game==game?&publications_[0]:publications_[1].game==game?&publications_[1]:nullptr;
        if(!game || !slot || slot->sequence==UINT64_MAX)failed_=true;
        else posted_steps_=std::max(posted_steps_,slot->sequence+1);
    }
    void refuse() noexcept {failed_.store(true,std::memory_order_release);progressed_.notify_all();}
    Progress progress() {
        auto gate=submission();std::lock_guard lock(progress_);
        if(failed_.load(std::memory_order_relaxed))throw std::logic_error("Native frame publication progress is unavailable or exhausted");
        return {session_,queued_,posted_,processed_,posted_steps_,completed_steps_};
    }
    auto acquire(std::chrono::milliseconds timeout=std::chrono::seconds(10)) {
        auto gate=submission();std::unique_lock lock(progress_);
        const auto target=posted_,steps=posted_steps_;
        if(!progressed_.wait_for(lock,timeout,[&]{return failed_.load(std::memory_order_relaxed) || (processed_>=target && completed_steps_>=steps);}))
            throw std::logic_error("Timed out completing posted exact storage native frames");
        if(failed_.load(std::memory_order_relaxed))throw std::logic_error("Exact storage native frame submission counter exhausted");
        return gate;
    }
};
// Both registry views validate generations, mapping and vector bounds before
// returning a record. No pointer survives the domain-frame lease; compaction,
// removal, reallocation and native shutdown are excluded for its duration.
inline std::byte* writable_chunk_at(void* sim,std::int32_t handle,chunk_heat_trace::RegistryLayout layout) noexcept {
    if(!chunk_heat_trace::chunk_at(sim,handle,layout).valid)return nullptr;
    namespace d=chunk_heat_trace::detail;
    const auto index=static_cast<std::uint32_t>(handle)&0x00ffffffU;
    const auto* mapping=d::read<const std::int32_t*>(sim,layout.mapping);
    auto* records=d::read<std::byte*>(sim,layout.data);
    return records+static_cast<std::size_t>(mapping[index])*44;
}
inline constexpr auto registry_layout=
#if defined(_WIN32)
    chunk_heat_trace::windows_registry;
#else
    chunk_heat_trace::linux_registry;
#endif
class Monitor {
    std::recursive_mutex mutex_;
    Ledger ledger_;
    std::array<char,256> failure_{};
    std::atomic<bool> active_{false};
    struct Replacement {std::int32_t handle;std::byte* record;Target before,after;};
    std::vector<Replacement> replacements_;
    std::unique_lock<std::recursive_mutex> lease_lock_,submission_lock_;
    std::unique_lock<std::mutex> frame_lock_;
    void* leased_sim_=nullptr;
    std::uint64_t token_=0,sequence_=0;
    bool replaced_=false;
    std::atomic<std::uint64_t> lease_owner_{0};
    static std::uint64_t thread() noexcept {
        static std::atomic<std::uint64_t> sequence{0};
        thread_local const auto identity=sequence.fetch_add(1,std::memory_order_relaxed)+1;
        return identity;
    }
    void check_thread() const {
        const auto owner=lease_owner_.load(std::memory_order_acquire);
        if(owner && owner!=thread())throw std::invalid_argument("Exact storage transfer lease belongs to another thread");
    }
    void check_token(std::uint64_t token) const {
        check_thread();if(!token || token!=token_)throw std::invalid_argument("Invalid exact storage transfer lease token");
    }
    void no_lease() const {
        if(lease_owner_.load(std::memory_order_acquire))throw std::logic_error("Queued exact storage mutation is forbidden during its transfer lease");
    }
    static void write(const Replacement& value,bool commit) noexcept {
        const auto endpoint=commit?value.after:value.before;
        std::memcpy(value.record+4,&endpoint.temperature,4);std::memcpy(value.record+8,&endpoint.capacity,4);
    }
    void check() const {if(failure_[0])throw std::logic_error(failure_.data());}
    void fail(const char* reason) noexcept {if(!failure_[0])std::snprintf(failure_.data(),failure_.size(),"%s",reason);}
    static bool messages(const void* frame,std::int32_t handle,const auto& append) {
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
        scan(energy_offset,8,refuse);scan(adjuster_offset,16,refuse);
        unsigned removals=0;scan(removal_offset,8,[&](const void*){++removals;});
        if(removals>1)throw std::logic_error("Repeated exact storage native removal");
        scan(data_offset,12,[&](const void* row) {append(d::read<std::uint32_t>(row,4),d::read<std::uint32_t>(row,8));});
        return removals!=0;
    }
public:
    void available() {check_thread();std::lock_guard lock(mutex_);check();no_lease();}
    bool registered(std::int32_t handle) {std::lock_guard lock(mutex_);return !failure_[0] && ledger_.registered(handle);}
    static constexpr bool supported() noexcept {
#if defined(__linux__) || defined(_WIN32)
        return true;
#else
        return false;
#endif
    }
    void reset(){check_thread();std::lock_guard lock(mutex_);no_lease();ledger_=Ledger{};failure_.fill(0);active_.store(false,std::memory_order_release);}
    void add(std::int32_t handle,float t,float c){check_thread();std::lock_guard lock(mutex_);check();no_lease();ledger_.add(handle,t,c);active_.store(true,std::memory_order_release);}
    void expect(std::int32_t handle,float t,float c){check_thread();std::lock_guard lock(mutex_);check();no_lease();ledger_.expect(handle,t,c);}
    void expect_removal(std::int32_t handle){check_thread();std::lock_guard lock(mutex_);check();no_lease();ledger_.expect_removal(handle);}
    std::uint32_t copy(MAOniStorageHeatReceipt* output,std::uint32_t capacity){check_thread();std::lock_guard lock(mutex_);check();return ledger_.copy(output,capacity);}
    void ack(std::uint64_t through){check_thread();std::lock_guard lock(mutex_);check();ledger_.ack(through);}
    std::uint64_t begin(void* sim,std::uint64_t ordinal,
        std::unique_lock<std::recursive_mutex> submission={},std::unique_lock<std::mutex> frame={}) {
        check_thread();std::unique_lock lock(mutex_);check();no_lease();
        if(!ledger_.ready())return 0;
        if(!sim || sequence_==UINT64_MAX)throw std::logic_error("Exact storage transfer has no live native owner or token capacity");
        const chunk_heat_trace::detail::Preserve preserve;
        try {ledger_.sample([&](std::int32_t handle){return chunk_heat_trace::chunk(sim,handle);},ordinal);}
        catch(const std::exception& error){fail(error.what());throw;}
        leased_sim_=sim;token_=++sequence_;replaced_=false;replacements_.clear();
        submission_lock_=std::move(submission);frame_lock_=std::move(frame);lease_lock_=std::move(lock);
        lease_owner_.store(thread(),std::memory_order_release);return token_;
    }
    void replace(std::uint64_t token,const MAOniStorageHeatEndpoint* values,std::uint32_t count) {
        check_thread();std::lock_guard lock(mutex_);check_token(token);check();
        if(replaced_ || (count && !values) || count>(1U<<20))throw std::invalid_argument("Invalid exact storage replacement batch");
        if(count && ledger_.copy(nullptr,0))throw std::logic_error("Exact storage replacement requires acknowledgement of all preceding native heat");
        const chunk_heat_trace::detail::Preserve preserve;
        std::vector<Replacement> prepared;prepared.reserve(count);
        for(std::uint32_t i=0;i<count;++i) {
            const auto& value=values[i];const Target target{std::bit_cast<std::uint32_t>(value.temperature_k),std::bit_cast<std::uint32_t>(value.capacity_kj_k)};
            if(value.reserved || !temperature(target.temperature) || !capacity(target.capacity))
                throw std::invalid_argument("Invalid exact storage replacement temperature/capacity");
            const auto baseline=ledger_.endpoint(value.handle);
            auto* record=writable_chunk_at(leased_sim_,value.handle,registry_layout);
            if(!record || chunk_heat_trace::detail::read<std::uint32_t>(record,4)!=baseline.temperature ||
                chunk_heat_trace::detail::read<std::uint32_t>(record,8)!=baseline.capacity)
                throw std::logic_error("Exact storage replacement lost its registered native endpoint");
            prepared.push_back({value.handle,record,baseline,target});
        }
        std::sort(prepared.begin(),prepared.end(),[](const auto& a,const auto& b){return std::less<std::byte*>{}(a.record,b.record);});
        for(std::size_t i=1;i<prepared.size();++i)if(prepared[i-1].record==prepared[i].record)
            throw std::invalid_argument("Repeated or aliased exact storage replacement handle");
        replacements_=std::move(prepared);
        // Everything which can fail or allocate precedes the first write.
        for(const auto& value:replacements_){write(value,true);ledger_.replace(value.handle,value.after);}
        replaced_=true;
    }
    void end(std::uint64_t token,std::uint32_t commit) {
        check_thread();std::lock_guard lock(mutex_);check_token(token);
        if(commit>1)throw std::invalid_argument("Invalid exact storage transfer commit flag");
        if(!commit)for(const auto& value:replacements_){write(value,false);ledger_.replace(value.handle,value.before);}
        replacements_.clear();leased_sim_=nullptr;token_=0;replaced_=false;
        lease_owner_.store(0,std::memory_order_release);
        lease_lock_.unlock();if(frame_lock_.owns_lock())frame_lock_.unlock();if(submission_lock_.owns_lock())submission_lock_.unlock();
    }
    template<class Original> void process(const void* frame,const void* sim,std::uint64_t ordinal,Original original) {
        if(!active_.load(std::memory_order_acquire)){original();return;}
        std::unique_lock lock(mutex_);
        if(ledger_.empty() || failure_[0]) {lock.unlock();original();return;}
        const auto read=[&](std::int32_t handle){return chunk_heat_trace::chunk(sim,handle);};
        {const chunk_heat_trace::detail::Preserve preserve;
        try {ledger_.before(read,[&](std::int32_t handle,const auto& append){return messages(frame,handle,append);},ordinal);}
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
