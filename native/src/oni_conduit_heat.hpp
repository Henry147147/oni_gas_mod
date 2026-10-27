#pragma once
#include "oni_body_heat_trace.hpp"
#include <array>
#include <bit>
#include <deque>
#include <map>
#include <mutex>
#include <stdexcept>

namespace ma::oni::conduit_heat {
// Completion of the original native body-energy messages, not a temperature
// inference: intervening contacts can legitimately return a body to its former
// temperature. Neither messages nor native physical state are changed here.
struct Request {std::uint32_t energy,minimum,maximum;};
class Monitor {
    std::mutex mutex_;
    std::map<std::int32_t,std::deque<Request>> expected_;
    std::uint32_t pending_=0;
    std::array<char,256> failure_{};
    static void trace(char kind,std::int32_t handle,const Request& request,bool matched=false) noexcept {
        static const auto* path=std::getenv("MA_ONI_CONDUIT_HEAT_TRACE");if(!path||!*path)return;
        const body_heat_trace::detail::Preserve preserve;
        if(auto* output=std::fopen(path,"a")) {
            std::fprintf(output,"%c\t%d\t%08x\t%08x\t%08x\t%u\n",kind,handle,request.energy,request.minimum,request.maximum,matched?1U:0U);
            std::fclose(output);
        }
    }
    void fail(const char* reason) noexcept {if(!failure_[0])std::snprintf(failure_.data(),failure_.size(),"%s",reason);}
    void check() const {if(failure_[0])throw std::logic_error(failure_.data());}
public:
    static constexpr bool supported() noexcept {
#if defined(__linux__) || defined(_WIN32)
        return sizeof(void*)==8;
#else
        return false;
#endif
    }
    void expect(std::int32_t handle,float energy,float minimum,float maximum) {
        const Request request{std::bit_cast<std::uint32_t>(energy),std::bit_cast<std::uint32_t>(minimum),std::bit_cast<std::uint32_t>(maximum)};
        if((static_cast<std::uint32_t>(handle)&0x00f00000U) || (request.energy&0x7fffffffU)>=0x7f800000U ||
            !(request.energy&0x7fffffffU) || !request.minimum || request.minimum>=0x7f800000U || request.maximum>=0x7f800000U || request.minimum>request.maximum)
            throw std::invalid_argument("Invalid conduit body-energy completion declaration");
        std::lock_guard lock(mutex_);check();
        if(pending_>=32768)throw std::length_error("Conduit body-energy completion capacity exhausted");
        expected_[handle].push_back(request);++pending_;
        trace('D',handle,request);
    }
    std::uint32_t pending(){std::lock_guard lock(mutex_);check();return pending_;}
    void reset(){std::lock_guard lock(mutex_);expected_.clear();pending_=0;failure_.fill(0);}
    template<class Original> void process(const void* frame,Original original) {
        std::unique_lock lock(mutex_);
        if(!pending_||failure_[0]){lock.unlock();original();return;}
        std::map<std::int32_t,std::size_t> consumed;
        {
            const body_heat_trace::detail::Preserve preserve;
            try {
                namespace d=body_heat_trace::detail;
                if(!frame)throw std::logic_error("Missing conduit body-energy native frame");
                // Pinned SimFrameInfo.modifyBuildingEnergyMessages. MSVC's
                // vector is at offset584 with its first pointer at+8 (PDB);
                // the inspected Linux vector begins at0x1b8. Rows are16 bytes.
#if defined(_WIN32)
                constexpr std::size_t offset=0x250;
#else
                constexpr std::size_t offset=0x1b8;
#endif
                const auto* first=d::read<const std::byte*>(frame,offset);
                const auto* last=d::read<const std::byte*>(frame,offset+8);
                const auto count=d::count(first,last,16,65536);
                if(first!=last&&!count)throw std::logic_error("Malformed conduit body-energy native frame vector");
                for(std::size_t i=0;i<count;++i) {
                    const auto* row=first+i*16;const auto handle=d::read<std::int32_t>(row,0);
                    const auto found=expected_.find(handle);if(found==expected_.end())continue;
                    auto& used=consumed[handle];if(used==found->second.size())continue;
                    const auto& request=found->second[used];
                    const Request actual{d::read<std::uint32_t>(row,4),d::read<std::uint32_t>(row,8),d::read<std::uint32_t>(row,12)};
                    const bool matches=request.energy==actual.energy&&request.minimum==actual.minimum&&request.maximum==actual.maximum;
                    trace('M',handle,actual,matches);
                    if(matches)++used;
                    // Other building services can queue energy on this same
                    // body. Only the declared ordered subsequence completes.
                }
            }catch(const std::exception& error){fail(error.what());}
            catch(...){fail("Unknown conduit body-energy completion observation failure");}
        }
        original(); // exactly once, with the original arithmetic environment
        {
            const body_heat_trace::detail::Preserve preserve;
            if(!failure_[0])for(const auto& [handle,count]:consumed) {
                auto found=expected_.find(handle);
                for(std::size_t i=0;i<count;++i){found->second.pop_front();--pending_;}
                if(found->second.empty())expected_.erase(found);
            }
            consumed.clear();
        }
    }
};
}
