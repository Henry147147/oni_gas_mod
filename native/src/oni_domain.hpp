#pragma once
#include "mixed_atmospheres/oni_domain.h"
#include "oni_publication.hpp"
#include <cstdint>
#include <mutex>
#include <span>
#include <vector>

namespace ma::oni {
// Initial boundary facts are validated with the candidate and published before
// enabling its worker. Results describe frame zero even if a worker immediately
// advances after the activation fence is released.
struct DomainSolidHeatStartup {
    std::span<const MAOniDomainSolidHeatCell> cells;
    MAOniDomainStatus status{};
    MAOniDomainSolidHeatStatus heat{};
};
// Called only from the native detours. No managed/game callbacks are made.
void start_domain(const MAOniDomainConfig&, std::uint64_t start_thread,std::span<const std::uint8_t> checkpoint={},double saved_time=0,
    std::span<const MAOniDomainInitialCell> initial_cells={},DomainSolidHeatStartup* solid_heat=nullptr);
void validate_domain_checkpoint(const MAOniDomainConfig&,std::span<const std::uint8_t>,double saved_time);
// Configured ABI 1 (scenario 7). Exactly one of initial_cells or checkpoint is
// supplied; legacy entry points above keep rejecting scenario 7.
void start_configured_domain(const MAOniDomainConfig&,const MAOniDomainPhysics&,std::span<const std::uint8_t> solid_mask,
    std::uint64_t start_thread,std::span<const std::uint8_t> checkpoint={},double saved_time=0,
    std::span<const MAOniDomainInitialCell> initial_cells={});
void validate_configured_domain_checkpoint(const MAOniDomainConfig&,const MAOniDomainPhysics&,std::span<const std::uint8_t> solid_mask,
    std::span<const std::uint8_t>,double saved_time);
void copy_domain_configuration(std::uint64_t session,MAOniDomainConfig&,MAOniDomainPhysics&,std::vector<std::uint8_t>& solid_mask);
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
void configure_domain_actors(const MAOniDomainActor*,std::uint32_t,MAOniDomainActorStatus&);
void configure_persistent_domain_actors(const MAOniDomainPersistentActor*,std::uint32_t,MAOniDomainActorStatus&);
std::uint32_t pending_domain_actor_restores();
void copy_domain_actor_status(MAOniDomainActorStatus&);
void copy_domain_thermal_status(MAOniDomainThermalStatus&);
void configure_domain_diffusion(std::uint64_t,std::uint64_t,std::uint32_t,double);
void copy_domain_diffusion_status(MAOniDomainDiffusionStatus&);
void copy_domain_viscosity_status(MAOniDomainViscosityStatus&);
void copy_domain_subcell_status(MAOniDomainSubcellStatus&);
struct DomainChunkHeat { bool handled{};float temperature{},delta_kj{}; };
DomainChunkHeat exchange_domain_chunk_heat(int native_cell,float temperature,float capacity_kj,float conductivity,
    float transfer_scale,float contact_scale,float seconds) noexcept;
void copy_domain_chunk_heat_status(MAOniDomainChunkHeatStatus&);
void configure_domain_solid_heat(const MAOniDomainSolidHeatCell*,std::uint32_t count,MAOniDomainSolidHeatStatus&);
void copy_domain_solid_heat_status(MAOniDomainSolidHeatStatus&);
void copy_domain_solid_heat_endpoint(MAOniDomainSolidHeatEndpoint&);
void copy_domain_solid_cell_state(MAOniDomainSolidCellState&);
void record_domain_solid_cell_state(const MAOniDomainSolidCellState&);
bool domain_solid_heat_registered(int native_cell) noexcept;
void fault_domain_solid_heat(const char*) noexcept;
// Called at the completed native GameData copy; never serializes a checkpoint.
std::optional<published_frames::Frame> capture_domain_publication(std::uint64_t native_epoch);
// The native detour supplies the current native cell facts. A changed material
// or mass faults the owner before either thermal endpoint is modified.
DomainChunkHeat exchange_domain_solid_heat(int fluid_native_cell,int solid_native_cell,std::uint16_t element,
    float mass,float temperature,float seconds,std::uint8_t solid_insulation=255,std::uint8_t fluid_insulation=255,float native_old_temperature=0) noexcept;
std::vector<std::uint8_t> copy_completed_domain_snapshot(std::uint64_t session,std::uint64_t completed_frame);
void copy_domain_gravity_status(MAOniDomainGravityStatus&);
std::vector<std::uint8_t> checkpoint_domain(std::uint64_t session,std::uint64_t frame);
// Caller must hold domain_frame_mutex; native and physical capture share it.
std::vector<std::uint8_t> checkpoint_domain_under_fence(std::uint64_t session,std::uint64_t frame,MAOniDomainConfig&,MAOniDomainStatus&);
void domain_checkpoint_source(std::uint64_t session,std::uint64_t frame,MAOniDomainConfig&,MAOniDomainStatus&,bool include_actors=false);
MAOniDomainStatus domain_status_under_fence();
std::vector<std::uint8_t> advance_domain_checkpoint(std::span<const std::uint8_t>,double seconds);
std::vector<std::uint8_t> snapshot_domain(std::uint64_t session,std::uint64_t frame);
std::vector<std::uint8_t> snapshot_domain_under_fence(std::uint64_t session,std::uint64_t frame,MAOniDomainConfig&,MAOniDomainStatus&);
std::vector<std::uint8_t> advance_domain_snapshot(std::span<const std::uint8_t>,double seconds);
std::vector<std::uint8_t> copy_domain_frame_inputs(std::uint64_t session,std::uint64_t completed_frame);
// Execution diagnostics for deferred captures. At most the completed owner
// and its immediate source are pinned, independently of reader activity.
struct DomainAuditStatistics {
    std::size_t retained_owners{}, cached_snapshot_bytes{}, cached_journal_bytes{};
    std::uint64_t full_serializations{}, journal_serializations{};
};
DomainAuditStatistics domain_audit_statistics();
std::vector<std::uint8_t> advance_domain_snapshot_with_inputs(std::span<const std::uint8_t> source,std::span<const std::uint8_t> journal);
std::mutex& domain_frame_mutex() noexcept;
}
