#pragma once
#include "mixed_atmospheres/oni_load_guard.h"
#include <cstdint>
#include <limits>
#include <mutex>
#include <stdexcept>

namespace ma::oni::load_guard {
inline void validate(const MAOniLoadGuardConfig& c) {
    const auto pitch=std::uint64_t(c.grid_width)+2,rows=std::uint64_t(c.grid_height)+2;
    if(c.struct_size!=sizeof(c)||c.abi_version!=1||!c.grid_width||!c.grid_height||!c.width||!c.height||
        std::uint64_t(c.offset_x)+c.width>c.grid_width||std::uint64_t(c.offset_y)+c.height>c.grid_height||
        pitch>std::numeric_limits<std::int32_t>::max()||rows>std::numeric_limits<std::int32_t>::max()||
        pitch*rows>std::numeric_limits<std::int32_t>::max())
        throw std::invalid_argument("Invalid native mixture-load guard geometry");
}
class Scope {
    std::mutex mutex_;
    MAOniLoadGuardConfig config_{};
    std::uint64_t next_=0,token_=0,suppressed_=0;
public:
    std::uint64_t begin(const MAOniLoadGuardConfig& config) {
        validate(config);std::lock_guard lock(mutex_);
        if(token_||next_==std::numeric_limits<std::uint64_t>::max())throw std::logic_error("Native mixture-load guard is already active or exhausted");
        config_=config;suppressed_=0;token_=++next_;return token_;
    }
    std::uint64_t end(std::uint64_t token) {
        std::lock_guard lock(mutex_);
        if(!token||token_!=token)throw std::logic_error("Native mixture-load guard token differs");
        token_=0;return suppressed_;
    }
    bool suppress(int cell) {
        std::lock_guard lock(mutex_);if(!token_||cell<0)return false;
        const auto pitch=std::uint64_t(config_.grid_width)+2;
        const auto x=std::uint64_t(cell)%pitch,y=std::uint64_t(cell)/pitch;
        if(x<=config_.offset_x||y<=config_.offset_y||x>std::uint64_t(config_.offset_x)+config_.width||
            y>std::uint64_t(config_.offset_y)+config_.height)return false;
        if(suppressed_!=std::numeric_limits<std::uint64_t>::max())++suppressed_;
        return true;
    }
};
static_assert(sizeof(MAOniLoadGuardConfig)==32);
}
