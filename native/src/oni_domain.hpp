#pragma once
#include "mixed_atmospheres/oni_domain.h"
#include <cstdint>
#include <mutex>

namespace ma::oni {
// Called only from the native detours. No managed/game callbacks are made.
void start_domain(const MAOniDomainConfig&, std::uint64_t start_thread);
void stop_domain() noexcept;
void shutdown_domain() noexcept;
void advance_domain(std::uint64_t ticket, double seconds, std::uint64_t thread) noexcept;
bool suppress_domain_cell(std::uint32_t hook, int cell) noexcept;
void copy_domain(MAOniDomainStatus&, MAOniDomainCell*, std::uint32_t capacity);
bool domain_active() noexcept;
MAOniDomainSpecies domain_species(std::uint32_t index);
void queue_domain_heat(const MAOniDomainHeat*, std::uint32_t count, MAOniDomainHeatStatus&);
void copy_domain_heat(MAOniDomainHeatStatus&);
void queue_domain_transfers(const MAOniDomainTransfer*,std::uint32_t count,MAOniDomainTransferStatus&);
void copy_domain_transfer_status(MAOniDomainTransferStatus&);
void copy_domain_receipts(MAOniDomainStatus&,MAOniDomainCell*,std::uint32_t,MAOniDomainReceipt*,std::uint32_t,std::uint32_t&);
void ack_domain_receipts(std::uint64_t session,std::uint64_t through);
void configure_domain_actor(const MAOniDomainActor&,MAOniDomainActorStatus&);
void copy_domain_actor_status(MAOniDomainActorStatus&);
std::mutex& domain_frame_mutex() noexcept;
}
