#ifndef MIXED_ATMOSPHERES_ONI_DOMAIN_H
#define MIXED_ATMOSPHERES_ONI_DOMAIN_H
#include "oni_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Development-only sealed domain. This is not whole-colony ownership. The
 * caller must verify an empty, impermeable, adiabatic native border before
 * activation. No native inventory, machine, radiation or disease may enter it.
 * Unsupported operations fault the domain; original fluids never take over.
 * Scenarios 0..6 own the complete rectangle. The additive configured ABI
 * below (scenario 7) adds explicit physics, interior solids and larger sizes. */
#define MA_ONI_DOMAIN_ABI_VERSION 2U
#define MA_ONI_DOMAIN_TRANSFER_ABI_VERSION 2U
#define MA_ONI_DOMAIN_SEALED_TEST 1U
#define MA_ONI_DOMAIN_SPECIES 5U /* nitrogen, oxygen, helium, water, carbon dioxide */
typedef struct MAOniDomainConfig {
    uint32_t struct_size, abi_version, flags, native_grid_width; /* public width; native pitch includes a one-cell halo */
    uint32_t offset_x, offset_y, width, height;
    uint32_t scenario, reserved; /* 0: mixed air, 1: compressed water, 2: mixed air with gravity, 3: compressed water with gravity, 4: occupied mixture without conduction, 5: mixture with conduction, 6: mixture with conduction and physical viscosity, 7: configured (physics and solid mask supplied separately) */
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

/* Additive initial-inventory ABI 1. Row-major occupied cells, at rest, with
 * exact supplied masses and temperature in the declared domain EOS. No
 * synthetic background constituents, vacuum or passive data. Scenarios 4..6
 * explicitly admit occupied gas/liquid mixtures; scenarios 0..3 retain
 * their single-phase admission.
 * Complete validation precedes activation; caller retains the input. */
typedef struct MAOniDomainInitialCell {
    uint32_t struct_size, abi_version;
    double masses[MA_ONI_DOMAIN_SPECIES];
    double temperature_k;
} MAOniDomainInitialCell;
MA_ONI_API uint32_t ma_oni_domain_initial_inventory_abi_version(void);
MA_ONI_API MAOniResult ma_oni_domain_start_from_cells(const MAOniDomainConfig*,
    const MAOniDomainInitialCell*, uint32_t count, MAOniDomainStatus*);

/* Additive configured sealed-domain ABI 1. Scenario 7 is admitted only by
 * these entry points; every legacy entry point keeps rejecting it, and the
 * behaviour and bytes of scenarios 0..6 are unchanged. Physics flags select
 * gravity, fluid conduction, physical viscosity and gas/liquid mixture
 * admission independently. Without MIXTURE, LIQUID selects filled
 * single-liquid (water) admission; otherwise the domain is gas-only. The
 * registry and EOS are those of scenarios 0..2 and 4..6; the 5-species
 * catalogue is unchanged. Disabled values must be exactly +0. Admission
 * bounds, not material laws: 0<g<=100 m/s2 and viscosities 0..1000 Pa s.
 *
 * solid_mask is row-major width*height, 0 fluid or 1 solid, with at least one
 * fluid cell. Solids are impermeable MAC walls with all-zero initial records
 * (masses and temperature). Without PHYSICS_VACUUM every fluid cell is occupied;
 * with it, empty cells and partial liquid surfaces are admitted. Passive data
 * and disease remain unsupported. Point transfers admit only the configured
 * gas/liquid phases, including when the destination is empty.
 * Heat edits, point transfers, radius centres and actors
 * targeting a solid reject the whole batch; rectangle footprints skip solids,
 * including their anchor, exactly as the inspected native loop does. Copies
 * publish solids and vacuum as all-zero cells; ma_oni_domain_configuration_copy returns the authoritative
 * mask. Status scalars sample the first fluid cell in row-major order. Native
 * compatibility projections must not be written into solid cells.
 *
 * Ownership is per cell: owned fluid cells exclude native pressure, liquid,
 * gas, displacement, temperature and chunk-heat work, while solids inside the
 * rectangle keep them (native solid/solid and solid/outside conduction).
 * Post-process, state-transition, load-transition and sublimation entries
 * stay excluded for the whole rectangle: a native melt or falling solid would
 * otherwise create fluid inside the owner. Interior fluid/solid faces are
 * impermeable and adiabatic; solid-heat registration still admits only
 * outside faces that touch an owned fluid cell. The caller must verify every
 * interior solid exactly as it verifies the border: static, stable,
 * non-transitioning and insulating (or accepted as adiabatic), with no
 * objects. Outside fluid cells on the rectangle edge still need the verified
 * impermeable, adiabatic native border.
 *
 * Sizes are 2..256 per side and at most 4096 cells. Checkpoints, durable
 * actor images and completed snapshots carry a checksummed MADCFG01 wrapper
 * with the exact geometry, physics and mask around the unchanged MACCLS01
 * solver image. Restore and validation reject any mismatch. */
#define MA_ONI_DOMAIN_CONFIGURED_ABI_VERSION 1U
#define MA_ONI_DOMAIN_CONFIGURED_SCENARIO 7U
#define MA_ONI_DOMAIN_CONFIGURED_MAXIMUM_SIDE 256U
#define MA_ONI_DOMAIN_CONFIGURED_MAXIMUM_CELLS 4096U
#define MA_ONI_DOMAIN_PHYSICS_GRAVITY 1U
#define MA_ONI_DOMAIN_PHYSICS_CONDUCTION 2U
#define MA_ONI_DOMAIN_PHYSICS_VISCOSITY 4U
#define MA_ONI_DOMAIN_PHYSICS_MIXTURE 8U
#define MA_ONI_DOMAIN_PHYSICS_LIQUID 16U
/* Void phase with sharp interfaces (native/MAC_VACUUM.md): vacuum fluid cells,
 * and liquid cells holding less than a full cell at zero pressure. */
#define MA_ONI_DOMAIN_PHYSICS_VACUUM 32U
/* Audited unresolved gas expansion; requires PHYSICS_VACUUM. Default off. */
#define MA_ONI_DOMAIN_PHYSICS_UNRESOLVED_GAS 64U
typedef struct MAOniDomainPhysics {
    uint32_t struct_size, abi_version, flags, reserved;
    double gravity_m_per_s2, shear_viscosity_pa_s, bulk_viscosity_pa_s;
} MAOniDomainPhysics;
/* Completed-owner accounts since activation (reset on restore, like thermal
 * diagnostics). No state or policy change occurs through this reader. */
typedef struct MAOniDomainSubcellStatus {
    uint32_t struct_size, abi_version, state, policy;
    uint64_t session, completed_frame, events;
    double entropy_increase_j_per_k;
} MAOniDomainSubcellStatus;
MA_ONI_API MAOniResult ma_oni_domain_subcell_status(MAOniDomainSubcellStatus*);
/* Actual material volumes of this completed frame, row-major; solids and vacuum
 * are zero. This reader does not reconstruct geometry from rounded pressure. */
MA_ONI_API MAOniResult ma_oni_domain_copy_occupied_volumes(MAOniDomainStatus*, double*, uint32_t capacity);
MA_ONI_API uint32_t ma_oni_domain_configured_abi_version(void);
/* Complete validation precedes activation; the caller retains every input. */
MA_ONI_API MAOniResult ma_oni_domain_start_configured(const MAOniDomainConfig*, const MAOniDomainPhysics*,
    const uint8_t* solid_mask, uint32_t mask_count, const MAOniDomainInitialCell*, uint32_t cell_count, MAOniDomainStatus*);
MA_ONI_API MAOniResult ma_oni_domain_configured_checkpoint_validate(const MAOniDomainConfig*, const MAOniDomainPhysics*,
    const uint8_t* solid_mask, uint32_t mask_count, const uint8_t* input, uint32_t input_bytes, double expected_time);
MA_ONI_API MAOniResult ma_oni_domain_restore_configured(const MAOniDomainConfig*, const MAOniDomainPhysics*,
    const uint8_t* solid_mask, uint32_t mask_count, const uint8_t* input, uint32_t input_bytes, double expected_time,
    MAOniDomainStatus*);
/* Exact configuration of the active configured session. A null mask with
 * zero capacity queries the count; all failure outputs are unchanged. */
MA_ONI_API MAOniResult ma_oni_domain_configuration_copy(uint64_t session, MAOniDomainConfig*, MAOniDomainPhysics*,
    uint8_t* solid_mask, uint32_t mask_capacity, uint32_t* mask_count);

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

/* Fluid point-source ABI 2. kind 1 injects at rest, kind 2 selectively removes.
 * Gas-only or one matching liquid material by default; scenarios 4..6 explicitly
 * admits occupied gas/liquid mixtures. Liquid parcels
 * carry elastic internal energy at the cell's pre-edit pressure. Void creation
 * reject the complete batch; phase mixing requires scenarios 4..6. No implicit enthalpy/pump work.
 * Receipts are retained until acknowledged after an owned snapshot publication.
 * Quantities are binary32-compatible when flags bit 0 is set; radial capability
 * 1 additionally admits bits 8..15 for a selective-consumption radius. Any remainder
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
/* One atomic completed frame, including geometry, accounts and receipts. */
MA_ONI_API MAOniResult ma_oni_domain_copy_phase_frame(MAOniDomainStatus*, MAOniDomainCell*, double*, uint32_t capacity,
    MAOniDomainSubcellStatus*, MAOniDomainReceipt*, uint32_t receipt_capacity, uint32_t* count);

MA_ONI_API MAOniResult ma_oni_domain_ack_receipts(uint64_t session, uint64_t through_sequence);
/* Additive radial consumption capability 1. Transfer flags bits 8..15 select
 * a face-connected radius for kind 2; bit 0 must also be set. Radius zero
 * retains the original point rule. The sealed owner boundary is impermeable.
 * Allocation is centre-first breadth-first order, on the aggregate binary32
 * callback lattice. Finer residual inventory stays in each physical source.
 * Exactly one kind-2 receipt carries total mass, mass-weighted temperature,
 * parcel U/K/momentum and the original callback; public_cell is its centre.
 * A complete batch is prepared atomically, including overlapping requests. */
MA_ONI_API uint32_t ma_oni_domain_radial_consumption_abi_version(void);
/* Additive rectangular consumption capability 1. Nonzero flags bits 16..23
 * select rectangle height, with bits 8..15 then interpreted as width.
 * public_cell is the lower-left anchor, rows are visited bottom-up and
 * left-to-right, and solids are skipped. The entire footprint must be inside
 * one owner. A zero width produces one zero-mass callback. The aggregate
 * binary32 lattice, atomic batch and one-receipt rules above also apply.
 * No reserved word, existing ABI size or point/radius encoding changes. */
MA_ONI_API uint32_t ma_oni_domain_rectangular_consumption_abi_version(void);

/* Registered point-boundary actors, ABI 1. Native handles are opaque 32-bit
 * values. Native registration remains intact, but its transfer stays inactive.
 * kind 1 consumes one fluid in kg/s; kind 2 emits mass_kg each interval. flags 1
 * removes an existing actor. The emitter density limit retains the game's
 * kg/m3 setting; it is not presented as a pressure in pascals.
 *
 * The ABI density-limit field compares total fluid density for a matching
 * filled-liquid emitter. Void creation and phase mixing remain unsupported.
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
/* Ordered, atomic configuration batch; no command is admitted on late failure. */
MA_ONI_API MAOniResult ma_oni_domain_configure_actors(const MAOniDomainActor*, uint32_t count, MAOniDomainActorStatus*);
MA_ONI_API MAOniResult ma_oni_domain_actor_status(MAOniDomainActorStatus*);

/* Additive durable-actor extension, ABI 1. Identity is an application-owned
 * persistent object ID, prefab fingerprint and same-kind component index.
 * Native handles remain session-local. All actor commands in a batch reserve
 * their identities atomically; duplicate identities are rejected.
 * Restored actors cannot advance until every saved identity is rebound. */
typedef struct MAOniDomainPersistentActor {
    MAOniDomainActor actor;
    uint64_t owner;
    uint32_t prefab, component;
} MAOniDomainPersistentActor;
MA_ONI_API uint32_t ma_oni_domain_actor_checkpoint_abi_version(void);
MA_ONI_API MAOniResult ma_oni_domain_configure_persistent_actors(const MAOniDomainPersistentActor*, uint32_t count, MAOniDomainActorStatus*);
MA_ONI_API uint32_t ma_oni_domain_pending_actor_restores(void);
/* Complete physical/actor image. Refuses pending edits and undelivered
 * receipts. Unbound actors refuse export. Newly allocated native handles are
 * absent from the image. Actor-free images keep the legacy physical format. */
MA_ONI_API MAOniResult ma_oni_domain_snapshot_copy(uint64_t session, uint64_t completed_frame,
    uint8_t* output, uint32_t capacity, uint32_t* required_bytes);
/* Additive completed-frame image capability 1. Copies the immutable full
 * physical/durable-actor image published with the frame journal, even when
 * future inputs or completed callback receipts are pending. Never authorizes
 * saving: snapshot_copy and paired saves retain their drained-state guards.
 * Exact current session/frame required; all failure outputs are unchanged. */
MA_ONI_API uint32_t ma_oni_domain_completed_snapshot_abi_version(void);
MA_ONI_API MAOniResult ma_oni_domain_completed_snapshot_copy(uint64_t session, uint64_t completed_frame,
    uint8_t* output, uint32_t capacity, uint32_t* required_bytes);
MA_ONI_API MAOniResult ma_oni_domain_snapshot_advance(const uint8_t* input, uint32_t input_bytes, double seconds,
    uint8_t* output, uint32_t capacity, uint32_t* required_bytes);

/* Accepted frame inputs, additive ABI 1. Each checksummed journal is bound to
 * the exact source snapshot, geometry, session/frame and physical timestep.
 * It preserves successful heat/transfer batch order, including radius requests;
 * failed reservations are absent. Copy observes only the last completed frame.
 * Stateless replay invokes no game callbacks and changes no live owner. Actor
 * configuration changes and actors without durable identities are marked and
 * explicitly refuse input replay. Maximum journal size is 1 MiB. Output and
 * required_bytes are unchanged on failure, including insufficient capacity. */
MA_ONI_API uint32_t ma_oni_domain_frame_inputs_abi_version(void);
MA_ONI_API MAOniResult ma_oni_domain_frame_inputs_copy(uint64_t session, uint64_t completed_frame,
    uint8_t* output, uint32_t capacity, uint32_t* required_bytes);
MA_ONI_API MAOniResult ma_oni_domain_snapshot_advance_with_inputs(const uint8_t* source, uint32_t source_bytes,
    const uint8_t* journal, uint32_t journal_bytes, uint8_t* output, uint32_t capacity, uint32_t* required_bytes);

/* Internal fluid conduction diagnostics, ABI 2. Scalar energy/entropy values
 * are read-only projections, never authority for a producer transaction.
 * Redistribution includes carried U corrections. Certified equilibrium intervals
 * are reported substeps but execute no physical conduction stages. */
typedef struct MAOniDomainThermalStatus {
    uint32_t struct_size, abi_version, state, enabled;
    uint64_t session, completed_frame, stages, contacts, iterations, unresolved_updates;
    double redistributed_energy_j, entropy_change_j_per_k;
    uint64_t exact_equilibrium_intervals;
} MAOniDomainThermalStatus;
MA_ONI_API uint32_t ma_oni_domain_thermal_abi_version(void);
MA_ONI_API MAOniResult ma_oni_domain_thermal_status(MAOniDomainThermalStatus*);

/* Gas diffusion ABI 2. Configuration commits only at the requested completed
 * frame under the worker fence. Mode 0 off, 1 physical (multiplier 1), 2
 * accelerated (multiplier >=1). Saved physical owners retain these settings;
 * restoring a legacy checkpoint never implicitly enables diffusion.
 * Out-of-range fields count cumulative pair-coefficient evaluations outside
 * the correlations' validated ranges (extrapolated, not rejected) and their
 * extreme face temperature/pressure; zero when there are none. The unreleased
 * 184-byte ABI 1 layout is rejected. */
typedef struct MAOniDomainDiffusionStatus {
    uint32_t struct_size,abi_version,state,mode;
    uint64_t session,completed_frame,substeps,rejected_attempts,faces,unresolved_updates;
    double multiplier,exchanged_mass_kg,elapsed_ms;
    double maximum_mass_residual_kg,maximum_mass_bound_kg,energy_residual_j,energy_bound_j;
    char profile[64];
    uint64_t out_of_range_evaluations;
    double out_of_range_minimum_temperature_k,out_of_range_maximum_temperature_k;
    double out_of_range_minimum_pressure_pa,out_of_range_maximum_pressure_pa;
} MAOniDomainDiffusionStatus;
MA_ONI_API MAOniResult ma_oni_domain_diffusion_configure(uint64_t session,uint64_t completed_frame,uint32_t mode,double multiplier);
MA_ONI_API MAOniResult ma_oni_domain_diffusion_status(MAOniDomainDiffusionStatus*);

/* Additive physical-viscosity diagnostics, ABI 1. Requested modal kinetic
 * dissipation and represented cell-U heating are separate accounts. Unresolved
 * heat/momentum updates are explicit. Entropy is a positive-heating lower
 * bound, not a difference of rounded state entropies. */
typedef struct MAOniDomainViscosityStatus {
    uint32_t struct_size, abi_version, state, enabled;
    uint64_t session, completed_frame, stages, modes, unresolved_heat_updates, unresolved_momentum_updates;
    double shear_pa_s, bulk_pa_s, dissipated_kinetic_j, stored_heat_j, entropy_lower_bound_j_per_k;
    double wall_impulse_x, wall_impulse_y;
} MAOniDomainViscosityStatus;
MA_ONI_API uint32_t ma_oni_domain_viscosity_abi_version(void);
MA_ONI_API MAOniResult ma_oni_domain_viscosity_status(MAOniDomainViscosityStatus*);

/* Completed native body/item contacts, ABI 1. Body heat is measured from the
 * represented binary32 native temperature and its supplied heat capacity.
 * Fluid heat uses the complete EOS. Unresolved contacts change neither owner.
 * These are fluid-boundary accounts, not a complete metabolic/body model. */
typedef struct MAOniDomainChunkHeatStatus {
    uint32_t struct_size, abi_version, state, reserved;
    uint64_t session, completed_frame, contacts, transfers, unresolved, iterations;
    double body_heat_loss_j, stored_fluid_heat_j, maximum_balance_residual_j, maximum_balance_bound_j;
    double entropy_lower_bound_j_per_k, requested_absolute_heat_j;
} MAOniDomainChunkHeatStatus;
MA_ONI_API uint32_t ma_oni_domain_chunk_heat_abi_version(void);
MA_ONI_API MAOniResult ma_oni_domain_chunk_heat_status(MAOniDomainChunkHeatStatus*);

/* Sealed-domain solid boundary admission, ABI 1. Each cell is outside the
 * fluid rectangle and shares a face with it. Native mass and material index
 * must still match at every actual temperature-pair contact. Capacity uses
 * the separate native binary32 mass and specific heat without rounding their
 * product to binary32. Zero/insulating solids need no registered contact.
 * Registration is atomic and requires a drained completed source frame. */
typedef struct MAOniDomainSolidHeatCell {
    uint32_t struct_size, abi_version, public_cell, native_element_index;
    uint64_t session, expected_frame;
    float mass_kg, specific_heat_kj_per_kg_k, conductivity_w_per_m_k;
    uint32_t native_insulation; /* exact native byte, 0..255 */
} MAOniDomainSolidHeatCell;
typedef MAOniDomainChunkHeatStatus MAOniDomainSolidHeatStatus;
/* Last completed native solid contact, additive endpoint-observation ABI 1.
 * The owner frame and contact frame are separate: a later frame may have no
 * contact. Temperatures are the actual native binary32 operands; no diagnostic
 * changes either endpoint or the physical checkpoint. */
typedef struct MAOniDomainSolidHeatEndpoint {
    uint32_t struct_size, abi_version, state, solid_public_cell;
    uint64_t session, completed_frame, contact_frame;
    uint32_t fluid_public_cell, reserved;
    float native_old_temperature_k, before_temperature_k, after_temperature_k, seconds;
} MAOniDomainSolidHeatEndpoint;
MA_ONI_API MAOniResult ma_oni_domain_solid_heat_endpoint(MAOniDomainSolidHeatEndpoint*);
/* Actual native cell arrays after the completed worker update. This separate
 * read-only observation distinguishes native storage from a prepared game
 * publication; it does not alter the endpoint or checkpoint. */
typedef struct MAOniDomainSolidCellState {
    uint32_t struct_size, abi_version, state, solid_public_cell;
    uint64_t session, completed_frame;
    float old_temperature_k, updated_temperature_k;
    uint32_t native_element_index;
    float mass_kg;
    uint32_t native_insulation, reserved;
    uint64_t native_ticket;
} MAOniDomainSolidCellState;
MA_ONI_API MAOniResult ma_oni_domain_solid_cell_state(MAOniDomainSolidCellState*);
MA_ONI_API uint32_t ma_oni_domain_solid_heat_abi_version(void);
MA_ONI_API MAOniResult ma_oni_domain_solid_heat_configure(const MAOniDomainSolidHeatCell*, uint32_t count,
    MAOniDomainSolidHeatStatus*);
MA_ONI_API MAOniResult ma_oni_domain_solid_heat_status(MAOniDomainSolidHeatStatus*);

/* Additive startup ABI 1 for preset scenarios 0..6. Registers 1..128 native
 * solid faces in the same activation transaction as ma_oni_domain_start.
 * Inputs use session=expected_frame=0; the new session is assigned internally.
 * Every fact is checked before activation. Failure preserves both outputs and
 * the previous ownership state. Both success outputs describe frame zero, even
 * when the native worker advances immediately after return. The old start and
 * revision-checked solid_heat_configure entry points retain their meanings. */
MA_ONI_API uint32_t ma_oni_domain_solid_heat_startup_abi_version(void);
MA_ONI_API MAOniResult ma_oni_domain_start_with_solid_heat(const MAOniDomainConfig*,
    const MAOniDomainSolidHeatCell*, uint32_t count, MAOniDomainStatus*, MAOniDomainSolidHeatStatus*);

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

/* Physical checkpoint extension, ABI 1. Export requires an exact completed
 * session/frame with no pending work, actors or undelivered receipts. A null
 * output with zero capacity queries size. All other errors change no output.
 * Stateless advance validates/restores a physical image and returns its next
 * image; seconds=0 validates and reserializes without advancing. Neither call
 * permits an ordinary scalar colony save or replaces the live game owner. */
MA_ONI_API uint32_t ma_oni_domain_checkpoint_abi_version(void);
MA_ONI_API MAOniResult ma_oni_domain_checkpoint_copy(uint64_t session, uint64_t completed_frame,
    uint8_t* output, uint32_t capacity, uint32_t* required_bytes);
MA_ONI_API MAOniResult ma_oni_domain_checkpoint_advance(const uint8_t* input, uint32_t input_bytes, double seconds,
    uint8_t* output, uint32_t capacity, uint32_t* required_bytes);
/* Validate before native Load; restore activates only after the caller has
 * loaded the matching native section and verified the sealed boundary. */
MA_ONI_API MAOniResult ma_oni_domain_checkpoint_validate(const MAOniDomainConfig*, const uint8_t* input, uint32_t input_bytes, double expected_time);
MA_ONI_API MAOniResult ma_oni_domain_restore(const MAOniDomainConfig*, const uint8_t* input, uint32_t input_bytes, double expected_time, MAOniDomainStatus*);

/* Paired capture ABI 1. Request against an exact physical source; the next
 * completed native worker epoch captures both owners (including zero-time
 * synchronization). State 1 is pending, 2 ready. Poll/copy never repeat native
 * saving. No ordinary scalar-save permit is exposed. */
typedef struct MAOniDomainPairStatus {
    uint32_t struct_size, abi_version, state, native_bytes, physical_bytes, reserved;
    uint64_t token, session, completed_frame, native_epoch, capture_thread;
    MAOniDomainConfig configuration;
    double physical_time_seconds;
} MAOniDomainPairStatus;
MA_ONI_API uint32_t ma_oni_domain_pair_abi_version(void);
MA_ONI_API MAOniResult ma_oni_domain_pair_request(uint64_t session, uint64_t completed_frame, int32_t x, int32_t y, MAOniDomainPairStatus*);
/* Same worker fence and token lifecycle, with durable actor state included. */
MA_ONI_API MAOniResult ma_oni_domain_pair_snapshot_request(uint64_t session, uint64_t completed_frame, int32_t x, int32_t y, MAOniDomainPairStatus*);
MA_ONI_API MAOniResult ma_oni_domain_pair_poll(uint64_t token, MAOniDomainPairStatus*);
MA_ONI_API MAOniResult ma_oni_domain_pair_copy(uint64_t token, uint8_t* native_data, uint32_t native_capacity,
    uint8_t* physical_data, uint32_t physical_capacity);
MA_ONI_API MAOniResult ma_oni_domain_pair_release(uint64_t token);

#ifdef __cplusplus
}
#endif
#endif
