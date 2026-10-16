#ifndef MIXED_ATMOSPHERES_ONI_DOMAIN_H
#define MIXED_ATMOSPHERES_ONI_DOMAIN_H
#include "oni_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Development-only sealed domain. This is not whole-colony ownership. The
 * caller must verify an empty, impermeable, adiabatic native border before
 * activation. No native inventory, machine, radiation or disease may enter it.
 * Unsupported operations fault the domain; original fluids never take over. */
#define MA_ONI_DOMAIN_ABI_VERSION 2U
#define MA_ONI_DOMAIN_TRANSFER_ABI_VERSION 2U
#define MA_ONI_DOMAIN_SEALED_TEST 1U
#define MA_ONI_DOMAIN_SPECIES 5U /* nitrogen, oxygen, helium, water, carbon dioxide */
typedef struct MAOniDomainConfig {
    uint32_t struct_size, abi_version, flags, native_grid_width; /* public width; native pitch includes a one-cell halo */
    uint32_t offset_x, offset_y, width, height;
    uint32_t scenario, reserved; /* 0: mixed air, 1: compressed water, 2: mixed air with gravity */
} MAOniDomainConfig;

typedef struct MAOniDomainStatus {
    uint32_t struct_size, abi_version, state, cells; /* 0 inactive, 1 running, 2 stopped, 3 faulted */
    uint64_t session, completed_frames, native_ticket, substeps, rejected_attempts;
    uint64_t solver_thread, start_thread;
    double time_seconds, last_timestep_seconds;
    double maximum_relative_mass_error, maximum_relative_energy_error;
    double initial_oxygen_kg, current_oxygen_kg, pressure_pa, liquid_mass_kg;
    uint64_t suppressed_pressure, suppressed_liquid, suppressed_temperature;
    uint64_t suppressed_transition, suppressed_post_process;
    uint64_t suppressed_gas;
    char error[256];
} MAOniDomainStatus;

typedef struct MAOniDomainCell {
    double masses[MA_ONI_DOMAIN_SPECIES];
    double internal_energy, pressure, temperature, liquid_fraction;
    double resolved_kinetic_energy, momentum_x, momentum_y;
} MAOniDomainCell;
typedef struct MAOniDomainSpecies {
    double molar_mass, cv, energy_offset, reference_density;
} MAOniDomainSpecies;

MA_ONI_API uint32_t ma_oni_domain_abi_version(void);
MA_ONI_API MAOniResult ma_oni_domain_start(const MAOniDomainConfig*, MAOniDomainStatus*);
/* Copies a coherent completed frame; insufficient capacity changes no output. */
MA_ONI_API MAOniResult ma_oni_domain_copy(MAOniDomainStatus*, MAOniDomainCell*, uint32_t capacity);
MA_ONI_API MAOniResult ma_oni_domain_stop(MAOniDomainStatus*);
MA_ONI_API uint32_t ma_oni_domain_active(void);
MA_ONI_API MAOniResult ma_oni_domain_species(uint32_t index, MAOniDomainSpecies*);

/* Additive heat-command ABI 1. Enqueue a complete reserved batch against the
 * exact session/frame. No physical state changes until the next native tick;
 * a failed batch queues nothing. Positive/negative heat is an explicit external
 * energy account. Temperature-limited or unrepresentable edits reject rather
 * than silently discard heat. public_cell uses the unpadded GameData grid. */
typedef struct MAOniDomainHeat {
    uint32_t struct_size, abi_version;
    uint64_t session, expected_frame;
    uint32_t public_cell, source;
    double joules, maximum_temperature_k;
} MAOniDomainHeat;
typedef struct MAOniDomainHeatStatus {
    uint32_t struct_size, abi_version, state, pending_batches;
    uint64_t session, accepted_batches, committed_batches;
    double committed_heat_joules, pending_heat_joules; /* projections; internal accounts retain full additive expansions */
} MAOniDomainHeatStatus;
MA_ONI_API uint32_t ma_oni_domain_heat_abi_version(void);
MA_ONI_API MAOniResult ma_oni_domain_queue_heat(const MAOniDomainHeat*, uint32_t count, MAOniDomainHeatStatus*);
MA_ONI_API MAOniResult ma_oni_domain_heat_status(MAOniDomainHeatStatus*);

/* Gas point-source ABI 2. kind 1 injects at rest, kind 2 selectively removes.
 * Receipts are retained until acknowledged after an owned snapshot publication.
 * Quantities are binary32-compatible when flags bit 0 is set; any remainder
 * stays in the physical source. These are not pump/aperture transactions. */
typedef struct MAOniDomainTransfer {
    uint32_t struct_size, abi_version;
    uint64_t session, expected_frame;
    uint32_t public_cell, species, kind, flags;
    int32_t callback;
    uint32_t reserved;
    double mass_kg, temperature_k;
} MAOniDomainTransfer;
typedef struct MAOniDomainReceipt {
    uint64_t sequence, session, completed_frame;
    uint32_t public_cell, species;
    int32_t callback;
    uint32_t kind;
    double mass_kg, temperature_k, internal_energy_j, kinetic_energy_j, momentum_x, momentum_y;
} MAOniDomainReceipt;
typedef struct MAOniDomainTransferStatus {
    uint32_t struct_size, abi_version, state, pending_batches;
    uint64_t session, accepted_batches, committed_batches, unacknowledged_receipts;
    double mass_changes_kg[MA_ONI_DOMAIN_SPECIES];
    double material_energy_change_j, kinetic_energy_change_j;
} MAOniDomainTransferStatus;
MA_ONI_API uint32_t ma_oni_domain_transfer_abi_version(void);
MA_ONI_API MAOniResult ma_oni_domain_queue_transfers(const MAOniDomainTransfer*, uint32_t count, MAOniDomainTransferStatus*);
MA_ONI_API MAOniResult ma_oni_domain_transfer_status(MAOniDomainTransferStatus*);
/* Both physical cells and receipts refer to the same completed native frame. */
MA_ONI_API MAOniResult ma_oni_domain_copy_receipts(MAOniDomainStatus*, MAOniDomainCell*, uint32_t cell_capacity,
    MAOniDomainReceipt*, uint32_t receipt_capacity, uint32_t* receipt_count);
MA_ONI_API MAOniResult ma_oni_domain_ack_receipts(uint64_t session, uint64_t through_sequence);

/* Registered point-boundary actors, ABI 1. Native handles are opaque 32-bit
 * values. Native registration remains intact, but its transfer stays inactive.
 * kind 1 consumes one gas in kg/s; kind 2 emits mass_kg each interval. flags 1
 * removes an existing actor. The emitter density limit retains the game's
 * kg/m3 setting; it is not presented as a pressure in pascals.
 * Receipt kinds 3/4 are consumer/emitter samples, 5/6 blocked/unblocked edges. */
typedef struct MAOniDomainActor {
    uint32_t struct_size, abi_version;
    uint64_t session, expected_frame;
    int32_t native_handle;
    uint32_t kind, public_cell, species, flags, reserved;
    double mass_kg, interval_s, temperature_k, maximum_gas_density_kg_m3;
} MAOniDomainActor;
typedef struct MAOniDomainActorStatus {
    uint32_t struct_size, abi_version, state, live_actors;
    uint64_t session, completed_frame, accepted_configurations, actor_steps, consumer_samples, emitter_samples;
} MAOniDomainActorStatus;
MA_ONI_API uint32_t ma_oni_domain_actor_abi_version(void);
MA_ONI_API MAOniResult ma_oni_domain_configure_actor(const MAOniDomainActor*, MAOniDomainActorStatus*);
MA_ONI_API MAOniResult ma_oni_domain_actor_status(MAOniDomainActorStatus*);

/* Internal fluid conduction diagnostics, ABI 1. Scalar energy/entropy values
 * are read-only projections, never authority for a producer transaction. */
typedef struct MAOniDomainThermalStatus {
    uint32_t struct_size, abi_version, state, enabled;
    uint64_t session, completed_frame, stages, contacts, iterations, unresolved_updates;
    double redistributed_energy_j, entropy_change_j_per_k;
} MAOniDomainThermalStatus;
MA_ONI_API uint32_t ma_oni_domain_thermal_abi_version(void);
MA_ONI_API MAOniResult ma_oni_domain_thermal_status(MAOniDomainThermalStatus*);

/* Gravity diagnostics, ABI 1. Potential is measured from the bottom of the
 * owned rectangle, at cell centres. Boundary transfers carry the same datum.
 * All values describe the completed frame; pending potential is reserved only. */
typedef struct MAOniDomainGravityStatus {
    uint32_t struct_size, abi_version, state, enabled;
    uint64_t session, completed_frame;
    double gravity_m_per_s2, initial_potential_j, potential_j;
    double committed_boundary_potential_j, pending_boundary_potential_j;
    double body_impulse_y, internal_work_j, maximum_energy_residual_j;
} MAOniDomainGravityStatus;
MA_ONI_API uint32_t ma_oni_domain_gravity_abi_version(void);
MA_ONI_API MAOniResult ma_oni_domain_gravity_status(MAOniDomainGravityStatus*);

#ifdef __cplusplus
}
#endif
#endif
