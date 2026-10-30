#ifndef MIXED_ATMOSPHERES_ONI_NATIVE_FRAMES_H
#define MIXED_ATMOSPHERES_ONI_NATIVE_FRAMES_H
#include "oni_backend.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Native submission progress, separate from observer hook counts. One queued
 * interval may contain several active-region rows, and one native Step may
 * process several intervals. A session changes whenever native initialization
 * resets the prefix. These counters do not describe MacClosedWorld frames. */
typedef struct MAOniNativeFrameProgress {
    uint32_t struct_size,abi_version;
    uint64_t session,queued,posted,processed,posted_steps,completed_steps;
} MAOniNativeFrameProgress;
MA_ONI_API uint32_t ma_oni_native_frames_version(void);
/* Capture counters only; does not wait, submit, publish or advance time. */
MA_ONI_API MAOniResult ma_oni_native_frames_progress(MAOniNativeFrameProgress*);
/* Step targets follow the actual published GameData buffer's completed copy
 * sequence. This includes initial/setup Steps and zero-interval publications,
 * without assuming a fixed offset between Prepare messages and native Steps.
 * Complete the posted interval and Step prefixes at entry, then acquire the native domain fence
 * through the complete Step tail (including GameData copies). Holds message
 * submission while waiting; later unposted messages remain untouched. No
 * callbacks are dispatched and no frame or transfer lease is manufactured.
 * Forbidden inside a native call or storage transfer lease. Output is changed
 * only on success. timeout_ms must be in [1,60000]. */
MA_ONI_API MAOniResult ma_oni_native_frames_complete(MAOniNativeFrameProgress*,uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
#endif
