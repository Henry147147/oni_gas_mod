#pragma once
#include "mixed_atmospheres/oni_storage_heat.h"
#include "oni_chunk_heat_trace.hpp"
#include <algorithm>
#include <deque>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace ma::oni::storage_heat {
// No floating-point operations: the receipt consumer uses exact binary32
// products. Observing a native frame must preserve its arithmetic environment.
inline bool temperature(std::uint32_t bits) noexcept {return bits>0 && bits<0x7f800000U;}
inline bool capacity(std::uint32_t bits) noexcept {return bits<0x7f800000U;}
struct Target {std::uint32_t temperature,capacity;};
struct State {Target baseline;std::deque<Target> expected;std::size_t applying=0;bool retirement_requested=false,removing=false;};
class Ledger {
    std::map<std::int32_t,State> states_;
    std::deque<MAOniStorageHeatReceipt> receipts_;
    std::uint64_t sequence_=0,acknowledged_=0;
    void observe(std::int32_t handle,State& state,chunk_heat_trace::Chunk value,std::uint64_t frame) {
        if(!value.valid || !temperature(value.temperature_bits) || !capacity(value.capacity_bits))
            throw std::logic_error("Exact storage lost a valid registered native chunk");
        if(value.capacity_bits!=state.baseline.capacity)
            throw std::logic_error("Exact storage native chunk capacity changed without its declared data message");
        if(value.temperature_bits!=state.baseline.temperature && value.capacity_bits!=0) {
            if(receipts_.size()>=32768 || sequence_==UINT64_MAX)
                throw std::length_error("Exact storage native heat receipt capacity exhausted");
            receipts_.push_back({48,2,sequence_+1,frame,handle,0,std::bit_cast<float>(value.capacity_bits),
                std::bit_cast<float>(state.baseline.temperature),std::bit_cast<float>(value.temperature_bits),0});
            ++sequence_;
        }
        state.baseline.temperature=value.temperature_bits;
    }
public:
    void add(std::int32_t handle,float published,float registered_capacity) {
        const Target initial{std::bit_cast<std::uint32_t>(published),std::bit_cast<std::uint32_t>(registered_capacity)};
        if((static_cast<std::uint32_t>(handle)&0x00f00000U) || !temperature(initial.temperature) || !capacity(initial.capacity))
            throw std::invalid_argument("Invalid exact storage native heat registration");
        if(states_.size()>=(1U<<20) || !states_.emplace(handle,State{initial,{},0}).second)
            throw std::logic_error("Duplicate or excessive exact storage native heat registration");
    }
    void expect(std::int32_t handle,float requested,float registered_capacity) {
        const Target target{std::bit_cast<std::uint32_t>(requested),std::bit_cast<std::uint32_t>(registered_capacity)};
        auto found=states_.find(handle);
        if(found==states_.end() || (found->second.retirement_requested&&target.capacity!=0) || !temperature(target.temperature) || !capacity(target.capacity))
            throw std::invalid_argument("Invalid exact storage native chunk data declaration");
        if(found->second.expected.size()>=4096)throw std::length_error("Exact storage pending native chunk data capacity exhausted");
        found->second.expected.push_back(target);
    }
    void expect_removal(std::int32_t handle) {
        auto found=states_.find(handle);
        if(found==states_.end() || found->second.retirement_requested)
            throw std::logic_error("Unknown or already retiring exact storage projection");
        const auto& state=found->second;
        const auto terminal=state.expected.empty()?state.baseline:state.expected.back();
        if(terminal.capacity!=0)throw std::logic_error("A nonzero-capacity exact storage projection cannot retire");
        found->second.retirement_requested=true;
    }
    bool empty() const noexcept {return states_.empty();}
    bool registered(std::int32_t handle) const noexcept {return states_.find(handle)!=states_.end();}
    bool ready() const noexcept {
        return std::all_of(states_.begin(),states_.end(),[](const auto& entry) {
            const auto& state=entry.second;
            return state.expected.empty() && !state.applying && !state.retirement_requested && !state.removing;
        });
    }
    Target endpoint(std::int32_t handle) const {
        const auto found=states_.find(handle);
        if(found==states_.end())throw std::invalid_argument("Unknown exact storage transfer handle");
        return found->second.baseline;
    }
    // Used only after the complete batch has been checked under the frame
    // and monitor lease. Map membership cannot change until lease release.
    void replace(std::int32_t handle,Target value) noexcept {states_.find(handle)->second.baseline=value;}
    template<class Read,class Messages> void before(Read read,Messages messages,std::uint64_t frame) {
        for(auto& [handle,state]:states_) {
            observe(handle,state,read(handle),frame);state.applying=0;
            const auto append=[&](std::uint32_t t,std::uint32_t c) {
                if(state.applying>=state.expected.size() || state.expected[state.applying].temperature!=t || state.expected[state.applying].capacity!=c)
                    throw std::logic_error("Undeclared or reordered exact storage native chunk data message: handle "+std::to_string(handle)+
                        ", position "+std::to_string(state.applying)+"/"+std::to_string(state.expected.size())+
                        ", temperature/capacity bits "+std::to_string(t)+"/"+std::to_string(c));
                ++state.applying;
            };
            state.removing=false;
            if constexpr(std::is_void_v<decltype(messages(handle,append))>) messages(handle,append);
            else state.removing=messages(handle,append);
            if(state.removing) {
                if(!state.retirement_requested || state.applying!=state.expected.size())
                    throw std::logic_error("Undeclared or premature exact storage native removal");
                const auto terminal=state.applying?state.expected.back():state.baseline;
                if(terminal.capacity!=0)throw std::logic_error("Exact storage native removal retained heat capacity");
            }
        }
    }
    template<class Read> void after(Read read,std::uint64_t frame) {
        for(auto at=states_.begin();at!=states_.end();) {
            auto& [handle,state]=*at;
            if(state.removing) {
                if(read(handle).valid)throw std::logic_error("Exact storage declared removal did not retire its native handle");
                if(receipts_.size()>=32768 || sequence_==UINT64_MAX)throw std::length_error("Exact storage native retirement receipt capacity exhausted");
                const auto terminal=state.applying?state.expected.back():state.baseline;
                receipts_.push_back({48,2,++sequence_,frame,handle,1,0,std::bit_cast<float>(terminal.temperature),std::bit_cast<float>(terminal.temperature),0});
                at=states_.erase(at);continue;
            }
            if(state.applying) {
                const auto actual=read(handle);const auto target=state.expected[state.applying-1];
                if(!actual.valid || actual.temperature_bits!=target.temperature || actual.capacity_bits!=target.capacity)
                    throw std::logic_error("Exact storage native chunk did not apply its declared data endpoint");
                // The absolute replacement is a projection, not physical heat.
                // Its preceding work was retained before the native write.
                state.baseline=target;
                for(std::size_t i=0;i<state.applying;++i)state.expected.pop_front();
                state.applying=0;
            } else observe(handle,state,read(handle),frame);
            ++at;
        }
    }
    template<class Read> void sample(Read read,std::uint64_t frame) {
        for(auto& [handle,state]:states_)observe(handle,state,read(handle),frame);
    }
    std::uint32_t copy(MAOniStorageHeatReceipt* output,std::uint32_t room) const {
        if(output && room<receipts_.size())throw std::length_error("Exact storage heat output capacity is insufficient");
        if(output)std::copy(receipts_.begin(),receipts_.end(),output);
        return static_cast<std::uint32_t>(receipts_.size());
    }
    void ack(std::uint64_t through) {
        if(through<acknowledged_ || through>sequence_)throw std::invalid_argument("Invalid exact storage heat acknowledgement");
        while(!receipts_.empty() && receipts_.front().sequence<=through)receipts_.pop_front();
        acknowledged_=through;
    }
};
}
