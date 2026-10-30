#ifndef MIXED_ATMOSPHERES_ONI_STORAGE_HEAT_H
#define MIXED_ATMOSPHERES_ONI_STORAGE_HEAT_H
#include "oni_backend.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Exact-storage projections only. Observes represented native body work on
 * both sides of absolute chunk-data messages. ABI3 also supplies a same-thread
 * transfer lease which excludes native frames and replaces registered chunk
 * temperature/capacity projections. It never edits messages or native fluids.
 * Receipts carry binary32 operands, not a rounded heat sum.
 * An unsupported writer, missing handle or full receipt queue refuses further
 * reads. Live checkpointing/rebinding is a separate capability. */
typedef struct MAOniStorageHeatReceipt {
    uint32_t struct_size,abi_version;
    uint64_t sequence,native_frame;
    int32_t handle;
    uint32_t kind; /* ABI2: 0 represented heat, 1 completed zero-capacity retirement */
    float capacity_kj_k,before_k,after_k;
    uint32_t reserved2;
} MAOniStorageHeatReceipt;
MA_ONI_API uint32_t ma_oni_storage_heat_version(void);
MA_ONI_API MAOniResult ma_oni_storage_heat_register(int32_t handle,float published_k,float capacity_kj_k);
MA_ONI_API MAOniResult ma_oni_storage_heat_expect_data(int32_t handle,float requested_k,float capacity_kj_k);
/* Declare cleanup of a projection whose final declared capacity is zero.
 * Tracking ends only when the native removal is observed and verified. */
MA_ONI_API MAOniResult ma_oni_storage_heat_expect_removal(int32_t handle);
/* A null destination/zero capacity queries the retained count. Copies do not
 * consume receipts. Acknowledge only after preparing and committing every
 * affected authoritative store. */
MA_ONI_API MAOniResult ma_oni_storage_heat_copy(MAOniStorageHeatReceipt*,uint32_t capacity,uint32_t* count);
MA_ONI_API MAOniResult ma_oni_storage_heat_ack(uint64_t through);
MA_ONI_API MAOniResult ma_oni_storage_heat_reset(void);
typedef struct MAOniStorageHeatEndpoint {
    int32_t handle;
    float temperature_k,capacity_kj_k;
    uint32_t reserved;
} MAOniStorageHeatEndpoint;
/* ABI3: complete every previously posted native frame, then exclude frame
 * submission and native work until end. Unposted data/removal declarations
 * return BUSY without changing token. Never submits a frame or advances time.
 * Credit the retained receipts while the old authoritative inventory exists.
 * Calls and end must use the thread which successfully began the lease. */
MA_ONI_API MAOniResult ma_oni_storage_heat_begin(uint64_t* token);
/* Validate the entire batch before replacing any endpoint. One batch per
 * lease; zero count is legal. Native handle generation and current T/C must
 * still match the registered baseline. All retained heat must be acknowledged
 * before a nonempty replacement. No queued declaration is generated.
 * Suppress matching managed absolute-data messages while updating UI views. */
MA_ONI_API MAOniResult ma_oni_storage_heat_replace(uint64_t token,const MAOniStorageHeatEndpoint*,uint32_t count);
/* commit=1 retains replacements; commit=0 restores their prior T/C. Receipts
 * and acknowledgements describe real work and are not rolled back. Finish
 * preparing the physical transfer before replace; its commit must not throw. */
MA_ONI_API MAOniResult ma_oni_storage_heat_end(uint64_t token,uint32_t commit);
#ifdef __cplusplus
}
#endif
#endif
