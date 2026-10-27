#ifndef MIXED_ATMOSPHERES_ONI_RUNTIME_CLOCK_H
#define MIXED_ATMOSPHERES_ONI_RUNTIME_CLOCK_H
#include "oni_backend.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Retained native service loop state, independent of the fluid owner.
 * Additive development capability; default inactive. Restore is applied on
 * the native worker after original message processing in a zero-time frame.
 * Caller must fence that frame before resuming or using the captured state. */
typedef struct MAOniRuntimeClock {
    uint32_t struct_size,abi_version;
    int32_t width,height,cells;
    uint32_t random;
    int32_t iterate,displacement;
    uint16_t tick,reserved;
} MAOniRuntimeClock;
MA_ONI_API uint32_t ma_oni_runtime_clock_version(void);
MA_ONI_API MAOniResult ma_oni_runtime_clock_start(void);
MA_ONI_API MAOniResult ma_oni_runtime_clock_copy(MAOniRuntimeClock* output);
MA_ONI_API MAOniResult ma_oni_runtime_clock_restore(const MAOniRuntimeClock* input);
MA_ONI_API MAOniResult ma_oni_runtime_clock_pending(uint32_t* pending);
#ifdef __cplusplus
}
#endif
#endif
