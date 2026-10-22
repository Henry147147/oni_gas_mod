#pragma once
#include <cstddef>

namespace ma::experimental::mac_kernel {
// Consumed work on an unsuccessful regional interval. The caller thread reads
// this after the original exception; worker exceptions and diagnostics retain
// their original types. Never used to choose successful-interval arithmetic.
inline thread_local std::size_t failed_flow_attempts = 0;
}
