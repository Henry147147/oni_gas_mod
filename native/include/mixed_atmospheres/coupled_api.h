#ifndef MIXED_ATMOSPHERES_COUPLED_API_H
#define MIXED_ATMOSPHERES_COUPLED_API_H
#include "api.h"

#ifdef __cplusplus
extern "C" {
#endif

// Explicit coupled closed-domain reference backend. Independent of the legacy
// gas ABI. No vacuum, free surfaces, phase transitions or production acceptance.
#define MA_COUPLED_ABI_VERSION 1U
#define MA_COUPLED_CLOSED_REFERENCE_MODEL 1U
typedef struct ma_coupled_world ma_coupled_world;
// Optional sparse-metadata extension v1; base ABI/layouts remain v1.
#define MA_COUPLED_PASSIVE_ABI_VERSION 1U
typedef struct ma_coupled_passive { uint64_t identity; double quantity; } ma_coupled_passive;

typedef struct ma_coupled_config {
    uint32_t struct_size, abi_version, width, height;
    double cell_size, depth, gravity, diffusivity, viscosity;
    double maximum_timestep, maximum_outgoing_fraction;
    uint32_t conduction, preconditioner; // 0 automatic, 1 Jacobi, 2 IC(0)
    uint32_t model, reserved;
    uint64_t reserved2;
} ma_coupled_config;

typedef struct ma_coupled_species {
    double molar_mass, cv, energy_offset, condensed_density;
    uint32_t phase, reserved; // 0 gas, 1 liquid, 2 solid (not admitted)
} ma_coupled_species;

typedef struct ma_coupled_cell_input {
    double internal_energy, momentum_x, momentum_y, liquid_pressure;
    uint32_t wall, reserved;
} ma_coupled_cell_input;

typedef struct ma_coupled_cell {
    double internal_energy, momentum_x, momentum_y, pressure;
    double temperature, liquid_fraction, total_mass;
    uint32_t wall, reserved;
} ma_coupled_cell;

typedef struct ma_coupled_snapshot {
    uint64_t revision;
    double time;
    uint64_t cells, mass_values;
    uint32_t species, reserved;
    double internal_energy, kinetic_energy, potential_energy;
    uint64_t reserved2[2];
} ma_coupled_snapshot;

typedef struct ma_coupled_step_result {
    double seconds, completed_time;
    uint64_t revision, substeps, rejected_substeps;
    uint64_t pressure_iterations, nonlinear_iterations, thermal_iterations;
    double maximum_linear_residual_ratio, maximum_volume_residual;
    double maximum_conservation_error, initial_projection_heat, final_projection_heat, pressure_work_roundoff;
    uint64_t reserved[2];
} ma_coupled_step_result;

MA_API uint32_t MA_CALL ma_coupled_abi_version(void);
// Separate per-thread diagnostic buffer, valid until the next coupled API call.
MA_API const char* MA_CALL ma_coupled_last_error(void);
MA_API uint32_t MA_CALL ma_coupled_passive_abi_version(void);
MA_API ma_status MA_CALL ma_coupled_species_count(uint32_t* output);
MA_API ma_status MA_CALL ma_coupled_species_definition(uint32_t index, ma_coupled_species* output);
MA_API ma_status MA_CALL ma_coupled_species_id(uint32_t index, char* buffer, uint64_t capacity, uint64_t* required);

// Row-major cells; mass[cell*species_count+species]. Exact input lengths required.
// All cells stage before publication. Walls must be empty; every other cell is
// occupied (gas or fully filled liquid). Unknown model/flags/geometry reject.
// Output is null on failure. Caller retains all inputs. No temperature conversion.
MA_API ma_status MA_CALL ma_coupled_create(const ma_coupled_config* config,
    const ma_coupled_cell_input* cells, uint64_t cell_count, const double* masses, uint64_t mass_count,
    ma_coupled_world** output);
MA_API void MA_CALL ma_coupled_destroy(ma_coupled_world* world);
MA_API ma_status MA_CALL ma_coupled_get_config(const ma_coupled_world* world, ma_coupled_config* output);

// Immutable fork: the source and all older views remain unchanged. Complete the
// candidate, diagnostics and snapshot before returning it. No partial substep
// publishes. On failure output is null and the result buffer is unchanged.
MA_API ma_status MA_CALL ma_coupled_advance(const ma_coupled_world* source, double seconds,
    uint64_t attempt_budget, ma_coupled_world** output, ma_coupled_step_result* result);

// One completed revision for all cells/masses/readings/totals. Preflight all
// capacities before copying; insufficient capacity changes no output buffers.
// Handles are immutable and support concurrent reads. Buffers must be distinct,
// valid allocations of at least their declared capacities. Never race destroy.
MA_API ma_status MA_CALL ma_coupled_copy_snapshot(const ma_coupled_world* world,
    ma_coupled_snapshot* snapshot, ma_coupled_cell* cells, uint64_t cell_capacity,
    double* masses, uint64_t mass_capacity);

// Sparse row offsets have cell_count+1 entries, start at zero and end at entry
// count. Within each cell, identities are positive, sorted and unique. Disease
// identity = uint32 bits of its nonzero int32 hash. Radiation metadata identity
// = 0x100000000; it contributes no energy, force, mass or heat capacity.
// Finite positive quantities only. Upper limit: 8,000,000 sparse records.
MA_API ma_status MA_CALL ma_coupled_create_full(const ma_coupled_config* config,
    const ma_coupled_cell_input* cells, uint64_t cell_count, const double* masses, uint64_t mass_count,
    const uint64_t* offsets, uint64_t offset_count, const ma_coupled_passive* entries, uint64_t entry_count,
    ma_coupled_world** output);
MA_API ma_status MA_CALL ma_coupled_passive_count(const ma_coupled_world* world, uint64_t* count);
MA_API ma_status MA_CALL ma_coupled_copy_full_snapshot(const ma_coupled_world* world,
    ma_coupled_snapshot* snapshot, ma_coupled_cell* cells, uint64_t cell_capacity,
    double* masses, uint64_t mass_capacity, uint64_t* offsets, uint64_t offset_capacity,
    ma_coupled_passive* entries, uint64_t entry_capacity);
// The base copy function rejects metadata-bearing handles; it cannot silently
// provide an incomplete export. Empty metadata still permits the base API.

#ifdef __cplusplus
}
#endif
#endif
