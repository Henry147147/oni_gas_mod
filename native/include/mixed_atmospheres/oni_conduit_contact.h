#ifndef MIXED_ATMOSPHERES_ONI_CONDUIT_CONTACT_H
#define MIXED_ATMOSPHERES_ONI_CONDUIT_CONTACT_H
#include "oni_backend.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Read-only replay of the pinned Linux game's pure conduit contact helpers.
 * No native pipe/body, fluid owner, clock or ledger is edited. Capacities are
 * the actual registered binary32 kJ/K values. Flags bit0 is insulated.
 * Caller must validate the actual post-call carrier result before settlement.
 * Credit represented body work Cbody*(Tbody_before-Tbody_after), retaining
 * exact arithmetic components; Q alone omits body-temperature rounding. */
typedef struct MAOniConduitContactInput {
    uint32_t struct_size,abi_version,flags,reserved;
    float seconds,fluid_temperature_k,fluid_heat_capacity_kj_k,fluid_conductivity;
    float body_temperature_k,body_heat_capacity_kj_k,conduit_conductivity;
} MAOniConduitContactInput;
typedef struct MAOniConduitContactResult {
    uint32_t struct_size,abi_version;
    float native_heat_to_body_kj,carrier_temperature_k,body_temperature_k;
} MAOniConduitContactResult;
MA_ONI_API uint32_t ma_oni_conduit_contact_version(void);
/* At most4096 contacts. Failure preserves every output record. Unsupported
 * platforms return capability0. Requires the inspected native image bound. */
MA_ONI_API MAOniResult ma_oni_conduit_contact_replay(const MAOniConduitContactInput*,uint32_t count,
    MAOniConduitContactResult*,uint32_t capacity);
#ifdef __cplusplus
}
#endif
#endif
