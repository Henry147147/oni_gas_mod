#ifndef MIXED_ATMOSPHERES_ONI_STORAGE_HEAT_H
#define MIXED_ATMOSPHERES_ONI_STORAGE_HEAT_H
#include "oni_backend.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Exact-storage projections only. Observes represented native body work on
 * both sides of absolute chunk-data messages; it never edits a native body,
 * message or fluid. Receipts carry binary32 operands, not a rounded heat sum.
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
#ifdef __cplusplus
}
#endif
#endif
