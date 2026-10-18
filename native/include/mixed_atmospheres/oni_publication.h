#ifndef MIXED_ATMOSPHERES_ONI_PUBLICATION_H
#define MIXED_ATMOSPHERES_ONI_PUBLICATION_H
#include "mixed_atmospheres/oni_domain.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Additive bounded-domain reader ABI. A native GameDataUpdate and the latest
 * physical owner need not name the same frame. Acquire immediately after the
 * native message returns, on that same thread and before its next native
 * publication query. An update predating ownership returns MA_ONI_BUSY.
 * Leases retain immutable copies through future frames, stop and restart.
 * At most 16 leases may be outstanding; release each exactly once. */
#define MA_ONI_DOMAIN_PUBLICATION_ABI_VERSION 1U
typedef struct MAOniDomainPublicationInfo {
    uint32_t struct_size, abi_version, state, reserved;
    uint64_t token, session, completed_frame, native_epoch;
    uint32_t cells, reserved2;
    double physical_time_s;
} MAOniDomainPublicationInfo;
MA_ONI_API uint32_t ma_oni_domain_publication_abi_version(void);
MA_ONI_API MAOniResult ma_oni_domain_publication_acquire(const void* native_update, MAOniDomainPublicationInfo*);
/* All outputs belong to the acquired frame. No current-owner reads occur. */
MA_ONI_API MAOniResult ma_oni_domain_publication_copy(uint64_t token, MAOniDomainStatus*, MAOniDomainCell*, uint32_t capacity,
    MAOniDomainSolidHeatStatus*, MAOniDomainSolidHeatEndpoint*, MAOniDomainSolidCellState*);
/* The borrowed cell pointer remains valid until this token is released.
 * Sampling a retained view allocates nothing and does not touch native data. */
MA_ONI_API MAOniResult ma_oni_domain_publication_cells(uint64_t token, const MAOniDomainCell**, uint32_t* count);
MA_ONI_API MAOniResult ma_oni_domain_publication_release(uint64_t token);

#ifdef __cplusplus
}
#endif
#endif
