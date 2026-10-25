// MinGW Windows only (cmake/OniNativeBackend.cmake): the owner DLL's C++
// allocations use mimalloc instead of msvcrt's heap, which maps every large
// solver buffer afresh. Allocation never changes solver arithmetic.
#include <mimalloc-new-delete.h>
#include <mimalloc.h>

namespace {
// Keep freed pages committed for a second: the solver reuses the same
// buffers every frame, and recommitting them costs page faults.
struct PurgeDelay {
    PurgeDelay() { mi_option_set(mi_option_purge_delay, 1000); }
} purge_delay;
}
