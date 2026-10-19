#pragma once
#include <algorithm>
#include <cstddef>

namespace ma::experimental::interval_detail {
// Each block owns its outputs. Neither shared flux scattering nor unordered
// reductions belong here. Exceptions are delivered after all workers finish,
// choosing the lowest failing block regardless of scheduling.
using BlockFunction = void (*)(const void*, std::size_t);
void execute_blocks(std::size_t count, BlockFunction, const void*, std::size_t parallel_threshold = 32);
[[nodiscard]] std::size_t block_worker_count();
// Active callers retain their own executor until their job returns. Native
// shutdown invokes this after stopping the simulator, before DLL teardown.
void retire_block_executor() noexcept;
template<class Function>
void independent_blocks(std::size_t count, const Function& function, std::size_t parallel_threshold = 32) {
    execute_blocks(count, [](const void* context, std::size_t block) {
        (*static_cast<const Function*>(context))(block);
    }, &function, parallel_threshold);
}
template<class Function>
void independent_rows(std::size_t count, const Function& function) {
    independent_blocks(count / 256 + static_cast<std::size_t>(count % 256 != 0), [&](std::size_t block) {
        const auto begin = block * 256, end = begin + std::min(std::size_t{256}, count - begin);
        for (auto row = begin; row < end; ++row) function(row);
    }, 128);
}
}
