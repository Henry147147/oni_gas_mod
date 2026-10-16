#ifndef MIXED_ATMOSPHERES_ONI_BACKEND_H
#define MIXED_ATMOSPHERES_ONI_BACKEND_H
#include <stdint.h>

#if defined(_WIN32) && defined(MA_ONI_EXPORTS)
#define MA_ONI_API __declspec(dllexport)
#elif defined(_WIN32)
#define MA_ONI_API __declspec(dllimport)
#else
#define MA_ONI_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif

/* Forwarding observation only. No API grants fluid authority. All sizes/version
 * fields must match exactly. Error text is bounded and thread-local. Observer
 * lifecycle calls must be serialized by the caller before initialize / after
 * shutdown; they are not a mechanism for suspending running game workers. */
#define MA_ONI_ABI_VERSION 1U
#define MA_ONI_HOOK_COUNT 23U
#define MA_ONI_TRACE_CAPACITY 4096U
#define MA_ONI_OBSERVER_ONLY 1U

typedef enum MAOniResult {
    MA_ONI_OK = 0,
    MA_ONI_INVALID_ARGUMENT = 1,
    MA_ONI_INCOMPATIBLE = 2,
    MA_ONI_BUSY = 3,
    MA_ONI_HOOK_ERROR = 4,
    MA_ONI_BUFFER_TOO_SMALL = 5,
    MA_ONI_INTERNAL_ERROR = 6
} MAOniResult;

typedef enum MAOniHook {
    MA_ONI_INITIALIZE, MA_ONI_SHUTDOWN, MA_ONI_MESSAGE, MA_ONI_MESSAGES,
    MA_ONI_BEGIN_SAVE, MA_ONI_END_SAVE, MA_ONI_MAIN, MA_ONI_PROCESS_FRAME,
    MA_ONI_NEXT_FRAME, MA_ONI_UPDATE_DATA, MA_ONI_PRESSURE, MA_ONI_LIQUID,
    MA_ONI_TEMPERATURE, MA_ONI_STATE_TRANSITION, MA_ONI_LOAD_TRANSITION,
    MA_ONI_SUBLIMATION, MA_ONI_POST_PROCESS, MA_ONI_STEP, MA_ONI_BEGIN_FRAME,
    MA_ONI_END_FRAME, MA_ONI_CONSUMER, MA_ONI_EMITTER, MA_ONI_BUILDING_HEAT
} MAOniHook;

typedef struct MAOniObserverConfig {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t flags; /* Must be MA_ONI_OBSERVER_ONLY. */
    uint32_t reserved;
    const char* game_directory_utf8;
} MAOniObserverConfig;

typedef struct MAOniHookStatistics {
    uint64_t entries;
    uint64_t exits;
} MAOniHookStatistics;

typedef struct MAOniObserverStatus {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t oni_build;
    uint32_t attached;
    uint32_t authoritative_fluid_owner; /* Always zero in this observer. */
    uint32_t recovery_required; /* Keep module resident; process restart needed. */
    uint64_t installed_hook_mask;
    uint64_t active_calls;
    uint64_t positive_timestep_returns;
    uint64_t trace_events_reserved;
    uint64_t trace_events_dropped;
    MAOniHookStatistics hooks[MA_ONI_HOOK_COUNT];
} MAOniObserverStatus;

/* Trace records control-flow entry/exit only, never pointers or game data.
 * next-frame exit carries the exact float return bits in value_bits. Positive
 * returns are observations, not an authoritative colony clock. Traces truncate
 * at capacity rather than overwriting evidence. */
typedef struct MAOniTraceEvent {
    uint64_t sequence;
    uint64_t thread;
    uint32_t hook;
    uint32_t entering;
    uint32_t value_bits;
    uint32_t reserved;
} MAOniTraceEvent;

/* probe hashes files without loading the game module or changing output on
 * error. attach requires null gSim/gSimData in the actual loaded module and
 * verifies every entry fingerprint before preparing/installing any detour.
 * detach refuses a live singleton or executing hooks. After an uninstall
 * failure, keep this library resident and terminate the diagnostic process. */
MA_ONI_API uint32_t ma_oni_abi_version(void);
MA_ONI_API MAOniResult ma_oni_probe(const MAOniObserverConfig*, MAOniObserverStatus*);
MA_ONI_API MAOniResult ma_oni_attach_observer(const MAOniObserverConfig*, MAOniObserverStatus*);
MA_ONI_API MAOniResult ma_oni_observer_status(MAOniObserverStatus*);
MA_ONI_API MAOniResult ma_oni_copy_trace(MAOniTraceEvent*, uint32_t capacity, uint32_t* count);
MA_ONI_API MAOniResult ma_oni_detach_observer(void);
MA_ONI_API const char* ma_oni_last_error(void);

#ifdef __cplusplus
}
#endif
#endif
