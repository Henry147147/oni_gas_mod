#ifndef MIXED_ATMOSPHERES_ONI_LOAD_GUARD_H
#define MIXED_ATMOSPHERES_ONI_LOAD_GUARD_H
#include "oni_backend.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Scoped mixture-load guard: preserve saved native cells instead of applying
 * vanilla load-time phase transitions. No ownership or pointers are retained. */
typedef struct MAOniLoadGuardConfig {
    uint32_t struct_size,abi_version,grid_width,grid_height,offset_x,offset_y,width,height;
} MAOniLoadGuardConfig;
MA_ONI_API uint32_t ma_oni_load_guard_version(void);
MA_ONI_API MAOniResult ma_oni_load_guard_begin(const MAOniLoadGuardConfig*,uint64_t* token);
MA_ONI_API MAOniResult ma_oni_load_guard_end(uint64_t token,uint64_t* suppressed);
#ifdef __cplusplus
}
#endif
#endif
