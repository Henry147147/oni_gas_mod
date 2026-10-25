#pragma once
namespace ma::experimental::mac_kernel {
// Windows DLL shutdown only, after every kernel call has completed. Keep the
// registry resident; join its workers before entering the loader lock.
void retire_pools() noexcept;
}
