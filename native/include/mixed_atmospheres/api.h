#ifndef MIXED_ATMOSPHERES_API_H
#define MIXED_ATMOSPHERES_API_H
#include <stdint.h>

#if defined(_WIN32)
# if defined(MA_EXPORTS)
#  define MA_API __declspec(dllexport)
# else
#  define MA_API __declspec(dllimport)
# endif
# define MA_CALL __cdecl
#else
# define MA_API __attribute__((visibility("default")))
# define MA_CALL
#endif
#ifdef __cplusplus
extern "C" {
#endif

typedef struct ma_world ma_world;
typedef enum ma_status { MA_OK = 0, MA_INVALID_ARGUMENT = 1, MA_NUMERICAL_ERROR = 2, MA_BUFFER_TOO_SMALL = 3, MA_INTERNAL_ERROR = 4 } ma_status;

typedef struct ma_world_config {
    uint32_t width;
    uint32_t height;
    double cell_size;
    double depth;
    double gravity;
    double diffusivity;
    uint32_t conduction;
    uint32_t open_top;
} ma_world_config;

typedef struct ma_sample {
    double total_mass;
    double temperature;
    double pressure;
    double internal_energy;
    double velocity_x;
    double velocity_y;
    uint32_t wall;
    uint32_t reserved;
} ma_sample;

typedef struct ma_atmosphere_settings {
    double minimum_oxygen_fraction;
    double maximum_oxygen_fraction;
    double minimum_oxygen_partial_pressure;
    double maximum_co2_fraction;
    double maximum_co2_partial_pressure;
    uint32_t hard_mode;
    uint32_t reserved;
} ma_atmosphere_settings;

typedef struct ma_breathability {
    double pressure;
    double oxygen_fraction;
    double oxygen_partial_pressure;
    double co2_fraction;
    double co2_partial_pressure;
    uint32_t failures;
    uint32_t reserved;
} ma_breathability;

typedef struct ma_step_result { double seconds; uint64_t substeps; } ma_step_result;

MA_API uint32_t MA_CALL ma_abi_version(void);
// Last error is per-thread; read it before another API call on that thread.
MA_API const char* MA_CALL ma_last_error(void);
MA_API ma_status MA_CALL ma_create_world(const ma_world_config* config, ma_world** output);
MA_API void MA_CALL ma_destroy_world(ma_world* world);
MA_API ma_status MA_CALL ma_get_config(const ma_world* world, ma_world_config* output);
MA_API ma_status MA_CALL ma_species_count(const ma_world* world, uint32_t* output);
MA_API ma_status MA_CALL ma_species_id(const ma_world* world, uint32_t species, char* buffer, uint64_t capacity, uint64_t* required);
MA_API ma_status MA_CALL ma_set_cell(ma_world* world, uint32_t x, uint32_t y, const double* masses, uint32_t count, double temperature, double velocity_x, double velocity_y);
MA_API ma_status MA_CALL ma_set_wall(ma_world* world, uint32_t x, uint32_t y, uint32_t wall);
MA_API ma_status MA_CALL ma_get_cell(const ma_world* world, uint32_t x, uint32_t y, ma_sample* sample, double* masses, uint32_t capacity);
MA_API ma_status MA_CALL ma_assess_atmosphere(const ma_world* world, uint32_t x, uint32_t y, const ma_atmosphere_settings* settings, ma_breathability* output);
MA_API ma_status MA_CALL ma_step(ma_world* world, double seconds, uint64_t substep_budget, ma_step_result* result);
MA_API ma_status MA_CALL ma_totals(const ma_world* world, double* masses, uint32_t capacity, double* energy, double* escaped_masses, double* escaped_energy);
MA_API ma_status MA_CALL ma_save(const ma_world* world, uint8_t* buffer, uint64_t capacity, uint64_t* required);
MA_API ma_status MA_CALL ma_load(const uint8_t* buffer, uint64_t size, ma_world** output);

#ifdef __cplusplus
}
#endif
#endif
