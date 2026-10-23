#ifndef MIXED_ATMOSPHERES_ONI_COLONY_H
#define MIXED_ATMOSPHERES_ONI_COLONY_H
#include "oni_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Colony owner (docs/colony-owner.md): one MacClosedWorld owns every gas and
 * liquid of one asteroid, with one species per catalogue material. Values the
 * game has arrive at runtime in MAOniColonyMaterial; nothing from the game is
 * compiled in. Every size and version field must match exactly. Calls are
 * serialized internally. A positive native frame hands its timestep to the
 * owner's solver thread and returns; the frame's result publishes when that
 * step completes. */
#define MA_ONI_COLONY_ABI_VERSION 1U
#define MA_ONI_COLONY_MAXIMUM_SIDE 1024U
#define MA_ONI_COLONY_MAXIMUM_CELLS 1048576U
#define MA_ONI_COLONY_PHYSICS_CONDUCTION 1U
/* The saved base-game accuracy-floor profile (docs/base-game-floor.md), with
 * its booked trace sink. New colonies only; a restored colony keeps the
 * profile its save holds. */
#define MA_ONI_COLONY_GAME_FLOOR 2U
/* Explicit new-colony diagnostic kernel; exclusive with GAME_FLOOR. */
#define MA_ONI_COLONY_GAME_KERNEL 4U

typedef struct MAOniColonyMaterial {
    uint32_t struct_size, abi_version;
    int32_t element_hash;
    uint32_t native_index;
    uint32_t phase; /* 1 gas, 2 liquid */
    uint32_t reserved;
    double molar_mass_kg_per_mol, specific_heat_j_per_kg_k, conductivity_w_per_m_k, density_kg_per_m3;
    double low_temperature_k, high_temperature_k;
    int32_t low_target, high_target, low_byproduct, high_byproduct;
    double low_byproduct_fraction, high_byproduct_fraction;
    char id[64]; /* element id, UTF-8, NUL-terminated */
} MAOniColonyMaterial;

typedef struct MAOniColonyConfig {
    uint32_t struct_size, abi_version, flags, vacuum_index;
    uint32_t grid_width, grid_height;       /* the game's public grid */
    uint32_t offset_x, offset_y, width, height; /* the owned asteroid rectangle */
    double gravity_m_per_s2, maximum_timestep_s;
} MAOniColonyConfig;

/* One public cell of the owned rectangle at import, row-major from the bottom. */
#define MA_ONI_COLONY_SOLID 0U
#define MA_ONI_COLONY_FLUID 1U
#define MA_ONI_COLONY_SPACE 2U
/* Germs (docs/colony-owner.md#germs): a disease is ONI's disease index + 1,
 * 0 for none; counts are ONI's germ counts. */
typedef struct MAOniColonyCell {
    uint32_t native_index; /* element index; ignored for solids */
    uint32_t kind;
    float mass_kg, temperature_k;
    uint32_t disease; int32_t disease_count;
} MAOniColonyCell;

typedef struct MAOniColonyStatus {
    uint32_t struct_size, abi_version, state /* 0 off, 1 running, 2 faulted */, species;
    uint64_t session, completed_frames, native_ticket, substeps, rejected_attempts, start_thread, solver_thread;
    uint64_t committed_transfers, committed_heat, receipts_pending;
    uint64_t opened_cells, closed_cells;
    double time_seconds, last_frame_seconds, last_advance_ms, maximum_advance_ms;
    double maximum_relative_mass_error, maximum_relative_energy_error;
    double reservoir_kg, space_sink_kg, injected_kg, extracted_kg;
    /* Native bodies and actors meeting owned cells (docs/colony-owner.md#native-bodies). */
    double native_heat_j, native_mass_in_kg, native_mass_out_kg;
    uint64_t native_changes;
    /* Last frame's wall time by stage: terrain, heat, transfers, flow, space
     * sink, ledger check and publication. */
    double stage_ms[6];
    /* Phase change: liquid evaporated and vapour condensed, and frames whose
     * relaxation was skipped (it would have lowered entropy). */
    double evaporated_kg, condensed_kg;
    uint64_t skipped_relaxations, reserved_count;
    double released_kg; /* from the reservoir back into the asteroid */
    double converted_kg; /* fluid converted to another identity (byproducts leave as debris) */
    double settled_kg;   /* immiscible liquid exchanged by buoyant settling */
    /* Removed by the saved game profile's trace sink (its own ledger term,
     * apart from the space sink): mass and U+K+G. */
    double trace_sink_kg, trace_sink_j;
    char error[256];
} MAOniColonyStatus;

/* Game-facing projection of one public cell of the latest completed frame. */
#define MA_ONI_COLONY_VIEW_GAS 1U
#define MA_ONI_COLONY_VIEW_LIQUID 2U
#define MA_ONI_COLONY_VIEW_OWNED 4U
typedef struct MAOniColonyCellView {
    uint32_t element_index; /* dominant constituent of the dominant phase, or vacuum */
    uint32_t flags;
    float mass_kg;          /* mass of the dominant phase */
    float temperature_k;
    float pressure_pa;
    float liquid_fraction;  /* liquid volume / cell volume */
    uint32_t disease; int32_t disease_count; /* the cell's most numerous germ */
} MAOniColonyCellView;

/* Transfers commit at the start of the next native frame. Injection adds the
 * element at the cell (or the nearest open cell within two); extraction takes
 * up to the mass from the open cells within the radius, nearest first. */
#define MA_ONI_COLONY_INJECT 1U
#define MA_ONI_COLONY_EXTRACT 2U
/* The whole gas or liquid mixture (native_index ignored): the cells' totals of
 * that phase within the radius, nearest first, each split over its
 * constituents by mass. A receipt per constituent, with the same callback. */
#define MA_ONI_COLONY_EXTRACT_GAS 3U
#define MA_ONI_COLONY_EXTRACT_LIQUID 4U
/* Flag on an extraction kind: the callback is an element consumer's handle;
 * receipts keep the flag. */
#define MA_ONI_COLONY_CONSUMER 0x100U
/* Receipt only: a solid byproduct of a conversion for the game to spawn as
 * debris at public_cell; callback holds the element hash. */
#define MA_ONI_COLONY_BYPRODUCT 5U
/* An injection brings disease_count germs of its disease; an extraction's
 * receipt returns the most numerous germ the removed mass carried. */
typedef struct MAOniColonyTransfer {
    uint32_t public_cell, native_index, kind, radius;
    int32_t callback; uint32_t disease;
    double mass_kg, temperature_k, disease_count;
} MAOniColonyTransfer;
typedef struct MAOniColonyReceipt {
    uint64_t sequence, completed_frame;
    int32_t callback; uint32_t kind, native_index, public_cell;
    float mass_kg, temperature_k; /* binary32 compatibility projection; precise sidecars retain original parcels */
    uint32_t disease; int32_t disease_count;
} MAOniColonyReceipt;
typedef struct MAOniColonyHeat { uint32_t public_cell, reserved; double joules, maximum_temperature_k; } MAOniColonyHeat;
/* Terrain changes commit at the start of the next native frame, before
 * transfers. Opening a solid cell (digging) makes it vacuum and refills it
 * from its gas neighbours at their pressure; closing a fluid cell (building)
 * displaces its contents into its open neighbours. */
#define MA_ONI_COLONY_OPEN 1U
#define MA_ONI_COLONY_CLOSE 2U
/* An airflow tile: the cell passes gas but no liquid (docs/colony-owner.md#airflow-tiles). */
#define MA_ONI_COLONY_BLOCK_LIQUID 3U
#define MA_ONI_COLONY_PASS_LIQUID 4U
/* ONI's element replacement with a fluid: the cell's fluid is removed (booked
 * as extracted) before the placed fluid arrives. */
#define MA_ONI_COLONY_CLEAR 5U
typedef struct MAOniColonyTerrain { uint32_t public_cell, kind; } MAOniColonyTerrain;

MA_ONI_API uint32_t ma_oni_colony_abi_version(void);
MA_ONI_API uint32_t ma_oni_colony_game_kernel_version(void);
MA_ONI_API MAOniResult ma_oni_colony_start(const MAOniColonyConfig*, const MAOniColonyMaterial*, uint32_t material_count,
    const MAOniColonyCell*, uint32_t cell_count, MAOniColonyStatus*);
MA_ONI_API MAOniResult ma_oni_colony_restore(const MAOniColonyConfig*, const MAOniColonyMaterial*, uint32_t material_count,
    const uint8_t* image, uint32_t image_bytes, MAOniColonyStatus*);
MA_ONI_API MAOniResult ma_oni_colony_status(MAOniColonyStatus*);
MA_ONI_API MAOniResult ma_oni_colony_copy(MAOniColonyStatus*, MAOniColonyCellView*, uint32_t capacity);
/* Exact publication cache: status always refreshes as with copy(). When the
 * previous successful copy has this session/frame, leaves views untouched
 * and sets copied=0; otherwise copies the complete views and sets copied=1.
 * A new session invalidates the cache, including a restored equal frame. */
MA_ONI_API MAOniResult ma_oni_colony_copy_changed(uint64_t session, uint64_t completed_frame,
    MAOniColonyStatus*, MAOniColonyCellView*, uint32_t capacity, uint32_t* copied);
/* Constituents of one public cell: native element indices and masses. */
MA_ONI_API MAOniResult ma_oni_colony_composition(uint32_t public_cell, uint32_t* native_indices, double* masses_kg,
    uint32_t capacity, uint32_t* count);
MA_ONI_API MAOniResult ma_oni_colony_queue_transfers(const MAOniColonyTransfer*, uint32_t count);
MA_ONI_API MAOniResult ma_oni_colony_queue_heat(const MAOniColonyHeat*, uint32_t count);
MA_ONI_API MAOniResult ma_oni_colony_queue_terrain(const MAOniColonyTerrain*, uint32_t count);
MA_ONI_API MAOniResult ma_oni_colony_copy_receipts(MAOniColonyReceipt*, uint32_t capacity, uint32_t* count);
MA_ONI_API MAOniResult ma_oni_colony_ack_receipts(uint64_t through_sequence);
/* Optional exact adapter receipts. Enable before any outstanding receipts;
 * start/restore resets this opt-in. Repeating the current mode is harmless.
 * Size-query/copy by legacy sequence; bytes=0 means no sidecar (e.g. a solid
 * byproduct). Ack retires both representations together. Undersized buffers
 * fail without consuming the receipt. Wire format: docs/mixed-pipes.md.
 * No solver settings, checkpoint bytes or legacy receipt layouts change. */
MA_ONI_API MAOniResult ma_oni_colony_enable_precise_receipts(uint32_t enabled);
/* Highest optional receipt wire capability; durable jobs require version2
 * before any queue submission. Other clients retain MARECP01 sidecars. */
MA_ONI_API uint32_t ma_oni_colony_precise_receipt_version(void);
MA_ONI_API MAOniResult ma_oni_colony_copy_precise_receipt(uint64_t sequence, uint8_t* buffer, uint32_t capacity, uint32_t* bytes);
/* Wait for queued/running advances. Call while the game submitter is paused
 * to establish a save boundary; this does not acknowledge any receipts. */
MA_ONI_API MAOniResult ma_oni_colony_wait(void);
/* Durable pipe jobs: tokens are accepted contiguously from 1. Repeating an
 * accepted token never queues another transfer; pending requests must match.
 * callback=-1, disease/count=0; kind is INJECT, EXTRACT_GAS or EXTRACT_LIQUID.
 * Passives are positive, finite and strictly ordered by nonzero identity;
 * only injections supply them. Requires precise receipts enabled.
 * State: 0 future/unknown, 1 queued, 2 committed, 3 all outcomes acknowledged.
 * last_receipt_sequence is nonzero only in state2; settle a reservation only
 * after handling every outcome through that sequence. Native pending jobs
 * survive owner checkpoint v8; transient callback handles are never persisted.
 * Save requires all committed pipe outcomes acknowledged first. */
typedef struct MAOniColonyPassive { uint64_t identity; double quantity; } MAOniColonyPassive;
/* Atomic actor enqueue: descriptors tag transfers with NEW durable pipe jobs.
 * Indices strictly increase; tokens are fresh/contiguous in descriptor order.
 * Passive slices are contiguous/exhaustive; reserved=0. The entire ordered
 * batch is admitted or rejected together. Batches containing jobs reject
 * replay; ordinary-only batches retain ordinary non-idempotent semantics.
 * Existing single-job semantics
 * and owner checkpoint v8 remain unchanged. Requires capability1 before use. */
typedef struct MAOniColonyActorJob {
    uint32_t transfer_index, first_passive, passive_count, reserved;
    uint64_t token;
} MAOniColonyActorJob;
MA_ONI_API uint32_t ma_oni_colony_actor_batch_version(void);
MA_ONI_API MAOniResult ma_oni_colony_queue_actor_batch(const MAOniColonyTransfer*, uint32_t count,
    const MAOniColonyActorJob*, uint32_t job_count, const MAOniColonyPassive*, uint32_t passive_count);
MA_ONI_API MAOniResult ma_oni_colony_queue_pipe_job(uint64_t token, const MAOniColonyTransfer*,
    const MAOniColonyPassive*, uint32_t passive_count, uint32_t* state, uint64_t* last_receipt_sequence);
MA_ONI_API MAOniResult ma_oni_colony_pipe_job_state(uint64_t token, uint32_t* state, uint64_t* last_receipt_sequence);
/* Zero for a non-pipe or acknowledged receipt. */
MA_ONI_API MAOniResult ma_oni_colony_receipt_job(uint64_t sequence, uint64_t* token);
/* Size query with a null buffer; the image describes the latest completed frame. */
MA_ONI_API MAOniResult ma_oni_colony_checkpoint(uint8_t* buffer, uint32_t capacity, uint32_t* bytes);
MA_ONI_API MAOniResult ma_oni_colony_stop(MAOniColonyStatus*);
MA_ONI_API uint32_t ma_oni_colony_active(void);
/* Presentation of the latest completed frame (docs/colony-owner.md#presentation).
 * Species in registry order, and one reading per open owned cell holding
 * material with its constituents. Size queries take null buffers. */
typedef struct MAOniColonySpecies {
    uint32_t native_index, phase; /* 1 gas, 2 liquid */
    double molar_mass_kg_per_mol, cv_j_per_kg_k, density_kg_per_m3, energy_offset_j_per_kg;
} MAOniColonySpecies;
typedef struct MAOniColonyReading {
    uint32_t public_cell, first_constituent, constituents, reserved;
    double temperature_k, mechanical_pressure_pa, gas_pressure_pa, internal_energy_j;
    double occupied_volume_m3, liquid_volume_m3;
} MAOniColonyReading;
typedef struct MAOniColonyConstituent {
    uint32_t species, reserved;
    double mass_kg, partial_volume_m3; /* gas: thermodynamic partial volume; liquid: actual volume */
} MAOniColonyConstituent;
MA_ONI_API MAOniResult ma_oni_colony_species(MAOniColonySpecies*, uint32_t capacity, uint32_t* count);
MA_ONI_API MAOniResult ma_oni_colony_readings(uint64_t* completed_frame, MAOniColonyReading*, uint32_t capacity, uint32_t* count,
    MAOniColonyConstituent*, uint32_t constituent_capacity, uint32_t* constituent_count);
/* Fixed-arena diagnostics: all requested compositions and their projected
 * binary32 temperatures from ONE immutable completed world. Unlike the
 * presentation table, includes cells whose EOS projection was refused.
 * flags:1 owned/open,2 refused projection (temperature keeps its old view).
 * Each output row has one entry per requested cell, in request order. Parts
 * name native element indices directly. Max4096 distinct requested cells.
 * Query with null rows/parts and capacity0; count gives required parts.
 * booked_mass is the full owner's expected fluid+reservoir mass from all
 * ledger terms (including identity conversions, not only public queued-input
 * counters). It is comparable to the sampled mass only for a complete arena.
 * An undersized copy writes count only; other outputs stay unchanged. */
typedef struct MAOniColonySampleCell {
    uint32_t public_cell, first_constituent, constituents, flags;
    double temperature_k;
} MAOniColonySampleCell;
typedef struct MAOniColonySamplePart { uint32_t native_index, reserved; double mass_kg; } MAOniColonySamplePart;
MA_ONI_API MAOniResult ma_oni_colony_sample(const uint32_t* cells, uint32_t count, MAOniColonyStatus*,
    MAOniColonySampleCell* rows, MAOniColonySamplePart* parts, uint32_t capacity, uint32_t* part_count, double* booked_mass_kg);
/* Optional fixed-arena history:0 disables;1..64 retains that many immutable
 * completed frames. Requires an owner of at most4096 cells; never persisted.
 * Enable before the measured window. Exact-frame lookup fails if unavailable;
 * UINT64_MAX selects latest. Same buffer rules as sample(). booked_heat is
 * cumulative accepted EXTERNAL heat in that frame's ledger, including body
 * exchange, not a live counter of heat still queued. Solids retain flags0. */
MA_ONI_API MAOniResult ma_oni_colony_sample_history(uint32_t retained_frames);
MA_ONI_API MAOniResult ma_oni_colony_sample_frame(uint64_t completed_frame,
    const uint32_t* cells, uint32_t count, MAOniColonyStatus*, MAOniColonySampleCell*, MAOniColonySamplePart*,
    uint32_t capacity, uint32_t* part_count, double* booked_mass_kg, double* booked_heat_j);
/* A catalogue liquid's reference density (kg/m^3) at a pressure, from the
 * same material records as start: the mass of one full cell. Needs no owner. */
MA_ONI_API MAOniResult ma_oni_colony_liquid_density(const MAOniColonyMaterial*, uint32_t material_count, uint32_t native_index,
    double pressure_pa, double* density_kg_per_m3);
/* 1 while the solver thread is advancing or has a frame waiting. */
MA_ONI_API uint32_t ma_oni_colony_busy(void);

#ifdef __cplusplus
}
#endif
#endif
