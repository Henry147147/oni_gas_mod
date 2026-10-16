cmake_minimum_required(VERSION 3.20)
project(MixedAtmospheres VERSION 0.1.0 LANGUAGES CXX)

option(MA_BUILD_TESTS "Build standalone physics and ABI tests" ON)
option(MA_SANITIZERS "Enable address and undefined-behavior sanitizers" OFF)
option(MA_BUILD_STAGGERED_EXPERIMENTS "Build unfinished standalone Cartesian/hybrid remap experiments" OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Used only by the explicit experimental FluidWorld geometry/EOS mode. The
# existing GasWorld/C ABI do not select it. Keep its independent test project.
add_library(ma_fluid_reference STATIC native/experimental/liquid_reference.cpp)
set_target_properties(ma_fluid_reference PROPERTIES POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
target_include_directories(ma_fluid_reference PUBLIC native/experimental)

add_library(ma_core STATIC
    native/src/species.cpp
    native/src/mixture.cpp
    native/src/process.cpp
    native/src/pressure.cpp
    native/src/acoustics.cpp
    native/src/volume_work.cpp
    native/src/transport.cpp
    native/src/passive_quantities.cpp
    native/src/thermal.cpp
    native/src/reaction.cpp
    native/src/phase_change.cpp
    native/src/atmosphere.cpp
    native/src/grid.cpp
    native/src/fluid_world.cpp
    native/src/fluid_thermodynamics.cpp
    native/src/persistence.cpp)
# Hidden: these objects are linked into the shared library, which exports only
# the C ABI. Static linking into tests/apps is unaffected.
set_target_properties(ma_core PROPERTIES POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
target_include_directories(ma_core PUBLIC native/include)
target_link_libraries(ma_core PUBLIC ma_fluid_reference)

add_library(mixed_atmospheres SHARED native/src/api.cpp)
target_link_libraries(mixed_atmospheres PRIVATE ma_core)
target_include_directories(mixed_atmospheres PUBLIC native/include)
target_compile_definitions(mixed_atmospheres PRIVATE MA_EXPORTS)
set_target_properties(mixed_atmospheres PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)

# Explicit managed/native boundary for the existing closed coupled reference.
# No default runtime dispatch or change to the existing gas ABI.
add_library(mixed_atmospheres_coupled SHARED EXCLUDE_FROM_ALL native/src/coupled_api.cpp)
target_link_libraries(mixed_atmospheres_coupled PRIVATE ma_core)
target_include_directories(mixed_atmospheres_coupled PUBLIC native/include)
target_compile_definitions(mixed_atmospheres_coupled PRIVATE MA_EXPORTS)
add_executable(ma_coupled_api_tests EXCLUDE_FROM_ALL native/tests/coupled_api_tests.cpp)
target_link_libraries(ma_coupled_api_tests PRIVATE mixed_atmospheres_coupled ma_core)
add_executable(ma_passive_transport_tests EXCLUDE_FROM_ALL native/tests/passive_transport_tests.cpp)
target_link_libraries(ma_passive_transport_tests PRIVATE mixed_atmospheres_coupled ma_core)
add_executable(ma_coupled_mutation_tests EXCLUDE_FROM_ALL native/tests/coupled_mutation_tests.cpp)
target_link_libraries(ma_coupled_mutation_tests PRIVATE mixed_atmospheres_coupled ma_core)
# Retained 2-D MAC transport component; no default world/game dispatch.
add_library(ma_mac_transport STATIC EXCLUDE_FROM_ALL native/src/mac_transport.cpp)
target_include_directories(ma_mac_transport PUBLIC native/include)
target_link_libraries(ma_mac_transport PUBLIC ma_core)
add_executable(ma_mac_transport_tests EXCLUDE_FROM_ALL native/tests/mac_transport_tests.cpp)
target_link_libraries(ma_mac_transport_tests PRIVATE ma_mac_transport)
add_library(ma_mac_reconstruction STATIC EXCLUDE_FROM_ALL native/src/mac_reconstruction.cpp)
target_link_libraries(ma_mac_reconstruction PUBLIC ma_mac_transport)
add_executable(ma_mac_reconstruction_tests EXCLUDE_FROM_ALL native/tests/mac_reconstruction_tests.cpp)
target_link_libraries(ma_mac_reconstruction_tests PRIVATE ma_mac_reconstruction)
add_library(ma_mac_closed_world STATIC EXCLUDE_FROM_ALL native/src/mac_closed_world.cpp)
target_link_libraries(ma_mac_closed_world PUBLIC ma_mac_reconstruction)
target_link_libraries(ma_mac_reconstruction_tests PRIVATE ma_mac_closed_world)
add_executable(ma_mac_closed_world_tests EXCLUDE_FROM_ALL native/tests/mac_closed_world_tests.cpp)
target_link_libraries(ma_mac_closed_world_tests PRIVATE ma_mac_closed_world)
add_executable(ma_mac_gravity_tests EXCLUDE_FROM_ALL native/tests/mac_gravity_tests.cpp)
target_link_libraries(ma_mac_gravity_tests PRIVATE ma_mac_closed_world)
add_executable(ma_mac_riemann_tests EXCLUDE_FROM_ALL native/tests/mac_riemann_tests.cpp)
target_link_libraries(ma_mac_riemann_tests PRIVATE ma_mac_closed_world)
if(MSVC)
    target_compile_options(ma_mac_transport PRIVATE /W4 /WX /permissive- /fp:strict)
    target_compile_options(ma_mac_transport_tests PRIVATE /W4 /WX /permissive- /fp:strict)
    target_compile_options(ma_mac_reconstruction PRIVATE /W4 /WX /permissive- /fp:strict)
    target_compile_options(ma_mac_reconstruction_tests PRIVATE /W4 /WX /permissive- /fp:strict)
    target_compile_options(ma_mac_closed_world PRIVATE /W4 /WX /permissive- /fp:strict)
    target_compile_options(ma_mac_closed_world_tests PRIVATE /W4 /WX /permissive- /fp:strict)
    target_compile_options(ma_mac_gravity_tests PRIVATE /W4 /WX /permissive- /fp:strict)
    target_compile_options(ma_mac_riemann_tests PRIVATE /W4 /WX /permissive- /fp:strict)
else()
    target_compile_options(ma_mac_transport PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -ffp-contract=off -fno-fast-math)
    target_compile_options(ma_mac_transport_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -ffp-contract=off -fno-fast-math)
    target_compile_options(ma_mac_reconstruction PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -ffp-contract=off -fno-fast-math)
    target_compile_options(ma_mac_reconstruction_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -ffp-contract=off -fno-fast-math)
    target_compile_options(ma_mac_closed_world PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -ffp-contract=off -fno-fast-math)
    target_compile_options(ma_mac_closed_world_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -ffp-contract=off -fno-fast-math)
    target_compile_options(ma_mac_gravity_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -ffp-contract=off -fno-fast-math)
    target_compile_options(ma_mac_riemann_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -ffp-contract=off -fno-fast-math)
endif()
set_target_properties(mixed_atmospheres_coupled PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
if(MSVC)
    target_compile_options(mixed_atmospheres_coupled PRIVATE /W4 /WX /permissive- /fp:precise)
    set_source_files_properties(native/src/passive_quantities.cpp PROPERTIES COMPILE_OPTIONS /WX)
    target_compile_options(ma_coupled_api_tests PRIVATE /W4 /WX /permissive- /fp:precise)
    target_compile_options(ma_passive_transport_tests PRIVATE /W4 /WX /permissive- /fp:precise)
    target_compile_options(ma_coupled_mutation_tests PRIVATE /W4 /WX /permissive- /fp:precise)
else()
    target_compile_options(mixed_atmospheres_coupled PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    set_source_files_properties(native/src/passive_quantities.cpp PROPERTIES COMPILE_OPTIONS -Werror)
    target_compile_options(ma_coupled_api_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    target_compile_options(ma_passive_transport_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    target_compile_options(ma_coupled_mutation_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT MSVC)
    # Export only the extern "C" ma_* ABI. Archive members (ma_core,
    # ma_fluid_reference and, below, the static C++ runtime) are never
    # re-exported; the version script also hides std:: template instances
    # emitted in api.cpp, which namespace std's default visibility keeps public.
    set(ma_export_map "${CMAKE_CURRENT_BINARY_DIR}/mixed_atmospheres.map")
    file(CONFIGURE OUTPUT "${ma_export_map}" CONTENT "{\n  global: ma_*;\n  local: *;\n};\n" @ONLY)
    target_link_options(mixed_atmospheres PRIVATE "LINKER:--exclude-libs,ALL" "LINKER:--version-script=${ma_export_map}")
    set_property(TARGET mixed_atmospheres APPEND PROPERTY LINK_DEPENDS "${ma_export_map}")
    target_link_options(mixed_atmospheres_coupled PRIVATE "LINKER:--exclude-libs,ALL" "LINKER:--version-script=${ma_export_map}")
    set_property(TARGET mixed_atmospheres_coupled APPEND PROPERTY LINK_DEPENDS "${ma_export_map}")
    if(NOT MA_SANITIZERS)
        # No libstdc++.so.6/GLIBCXX or libgcc_s dependency. The static runtime
        # still binds glibc symbols at the build host's versions (GCC 15 on
        # glibc 2.43 needs GLIBC_2.38), so build release binaries on the oldest
        # supported glibc. Sanitizer builds keep dynamic runtimes.
        target_link_options(mixed_atmospheres PRIVATE -static-libstdc++ -static-libgcc)
        target_link_options(mixed_atmospheres_coupled PRIVATE -static-libstdc++ -static-libgcc)
    endif()
endif()

foreach(target ma_core ma_fluid_reference mixed_atmospheres)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /fp:precise)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    endif()
endforeach()
if(MA_SANITIZERS AND NOT MSVC)
    # UBSan otherwise reports and continues with exit 0, so CTest could pass
    # with undefined behavior diagnostics. Make every sanitizer finding fatal.
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
    foreach(target ma_core ma_fluid_reference mixed_atmospheres mixed_atmospheres_coupled ma_coupled_api_tests ma_passive_transport_tests)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endforeach()
endif()

add_executable(ma_demo native/apps/demo.cpp)
target_link_libraries(ma_demo PRIVATE ma_core)

# Material-coordinate retained-node experiment only: this is deliberately a
# separate library, not a ma_core/DLL source or selectable FluidWorld mode.
add_library(ma_occupied_staggered STATIC EXCLUDE_FROM_ALL native/src/occupied_staggered.cpp)
target_link_libraries(ma_occupied_staggered PUBLIC ma_core)
if(MSVC)
    target_compile_options(ma_occupied_staggered PRIVATE /W4 /WX /permissive- /fp:precise)
else()
    target_compile_options(ma_occupied_staggered PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()

# Bounded evidence for the prospective canonical owner. Neither target is a
# default build/test dependency, and neither links the EOS, world or game DLL.
add_library(ma_canonical_diagnostics STATIC EXCLUDE_FROM_ALL native/src/canonical_material_diagnostics.cpp)
target_include_directories(ma_canonical_diagnostics PUBLIC native/include)
add_executable(ma_canonical_diagnostics_tests EXCLUDE_FROM_ALL native/tests/canonical_material_diagnostics_tests.cpp)
target_link_libraries(ma_canonical_diagnostics_tests PRIVATE ma_canonical_diagnostics)
foreach(target ma_canonical_diagnostics ma_canonical_diagnostics_tests)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive- /fp:strict)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
            -fno-fast-math -ffp-contract=off -frounding-math)
    endif()
endforeach()

# Bounded stored-increment arithmetic only; no EOS or material-step dependency.
add_library(ma_retained_scalar STATIC EXCLUDE_FROM_ALL
    native/src/retained_scalar.cpp native/src/retained_scalar_product.cpp
    native/src/retained_scalar_difference.cpp)
target_link_libraries(ma_retained_scalar PUBLIC ma_canonical_diagnostics)
add_executable(ma_retained_scalar_tests EXCLUDE_FROM_ALL native/tests/retained_scalar_tests.cpp)
target_link_libraries(ma_retained_scalar_tests PRIVATE ma_retained_scalar)
add_executable(ma_retained_scalar_product_tests EXCLUDE_FROM_ALL native/tests/retained_scalar_product_tests.cpp)
target_link_libraries(ma_retained_scalar_product_tests PRIVATE ma_retained_scalar)
add_executable(ma_retained_scalar_difference_tests EXCLUDE_FROM_ALL native/tests/retained_scalar_difference_tests.cpp)
target_link_libraries(ma_retained_scalar_difference_tests PRIVATE ma_retained_scalar)
if(MSVC)
    target_compile_options(ma_retained_scalar PRIVATE /W4 /WX /permissive- /fp:strict)
    target_compile_options(ma_retained_scalar_tests PRIVATE /W4 /WX /permissive- /fp:strict)
    target_compile_options(ma_retained_scalar_product_tests PRIVATE /W4 /WX /permissive- /fp:strict)
    target_compile_options(ma_retained_scalar_difference_tests PRIVATE /W4 /WX /permissive- /fp:strict)
else()
    target_compile_options(ma_retained_scalar PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fno-fast-math -ffp-contract=off -frounding-math)
    target_compile_options(ma_retained_scalar_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fno-fast-math -ffp-contract=off -frounding-math)
    target_compile_options(ma_retained_scalar_product_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fno-fast-math -ffp-contract=off -frounding-math)
    target_compile_options(ma_retained_scalar_difference_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fno-fast-math -ffp-contract=off -frounding-math)
endif()

# Actual-EOS retained material owner, still isolated from ma_core and the DLL.
# No default build or CTest dispatch selects this experimental fixed-h method.
add_library(ma_canonical_material STATIC EXCLUDE_FROM_ALL
    native/src/canonical_material.cpp
    native/src/canonical_material_storage.cpp
    native/src/canonical_material_expressions.cpp
    native/src/canonical_material_ledgers.cpp
    native/src/canonical_material_linear.cpp
    native/src/canonical_material_views.cpp
    native/src/canonical_material_thermo.cpp
    native/src/canonical_material_step.cpp
    native/src/canonical_material_serialization.cpp)
target_link_libraries(ma_canonical_material PUBLIC ma_core ma_retained_scalar)
add_executable(ma_canonical_material_tests EXCLUDE_FROM_ALL native/tests/canonical_material_tests.cpp)
target_link_libraries(ma_canonical_material_tests PRIVATE ma_canonical_material)
add_executable(ma_canonical_retained_initialization_tests EXCLUDE_FROM_ALL native/tests/canonical_retained_initialization_tests.cpp)
target_link_libraries(ma_canonical_retained_initialization_tests PRIVATE ma_canonical_material)
add_executable(ma_canonical_retained_step_tests EXCLUDE_FROM_ALL native/tests/canonical_retained_step_tests.cpp)
target_link_libraries(ma_canonical_retained_step_tests PRIVATE ma_canonical_material)
add_executable(ma_canonical_gravity_tests EXCLUDE_FROM_ALL native/tests/canonical_gravity_tests.cpp)
target_link_libraries(ma_canonical_gravity_tests PRIVATE ma_canonical_material)
if(MSVC)
    target_compile_options(ma_canonical_material PRIVATE /W4 /WX /permissive- /fp:strict)
    target_compile_options(ma_canonical_material_tests PRIVATE /W4 /WX /permissive- /fp:strict)
    target_compile_options(ma_canonical_retained_initialization_tests PRIVATE /W4 /WX /permissive- /fp:strict)
    target_compile_options(ma_canonical_retained_step_tests PRIVATE /W4 /WX /permissive- /fp:strict)
    target_compile_options(ma_canonical_gravity_tests PRIVATE /W4 /WX /permissive- /fp:strict)
else()
    target_compile_options(ma_canonical_material PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fno-fast-math -ffp-contract=off -frounding-math)
    target_compile_options(ma_canonical_material_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fno-fast-math -ffp-contract=off -frounding-math)
    target_compile_options(ma_canonical_retained_initialization_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fno-fast-math -ffp-contract=off -frounding-math)
    target_compile_options(ma_canonical_retained_step_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fno-fast-math -ffp-contract=off -frounding-math)
    target_compile_options(ma_canonical_gravity_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fno-fast-math -ffp-contract=off -frounding-math)
endif()

if(MA_BUILD_STAGGERED_EXPERIMENTS)
    # Unfinished Cartesian transport is kept separate from the accepted retained
    # material-state library. Its strict boundary-rate test still fails.
    add_library(ma_occupied_staggered_remap STATIC EXCLUDE_FROM_ALL native/src/occupied_staggered_remap.cpp)
    target_link_libraries(ma_occupied_staggered_remap PUBLIC ma_occupied_staggered)

    # A0 geometry shares the Cartesian grid definitions; no source is copied.
    add_library(ma_occupied_staggered_hybrid_geometry STATIC EXCLUDE_FROM_ALL native/src/occupied_staggered_hybrid.cpp)
    target_link_libraries(ma_occupied_staggered_hybrid_geometry PUBLIC ma_occupied_staggered_remap)

    # A1 instantaneous protected-cap transport only; no composed timestep or DLL.
    add_library(ma_occupied_staggered_hybrid_remap STATIC EXCLUDE_FROM_ALL native/src/occupied_staggered_hybrid_remap.cpp)
    target_link_libraries(ma_occupied_staggered_hybrid_remap PUBLIC ma_occupied_staggered_hybrid_geometry)
    foreach(target ma_occupied_staggered_remap ma_occupied_staggered_hybrid_geometry ma_occupied_staggered_hybrid_remap)
        if(MSVC)
            target_compile_options(${target} PRIVATE /W4 /WX /permissive- /fp:precise)
        else()
            target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
        endif()
    endforeach()
    message(STATUS "Unfinished staggered experiments enabled: positive_staggered_rezone remains a known failing strict gate")
endif()

if(MA_BUILD_TESTS)
    enable_testing()
    add_executable(ma_tests native/tests/tests.cpp)
    target_link_libraries(ma_tests PRIVATE ma_core mixed_atmospheres)
    add_test(NAME physics_and_abi COMMAND ma_tests)
    add_executable(ma_fluid_tests native/tests/fluid_world_tests.cpp)
    target_link_libraries(ma_fluid_tests PRIVATE ma_core)
    add_test(NAME integrated_fluid_world COMMAND ma_fluid_tests)
    add_executable(ma_free_surface_tests native/tests/fluid_free_surface_tests.cpp)
    target_link_libraries(ma_free_surface_tests PRIVATE ma_core)
    add_test(NAME integrated_fluid_free_surfaces COMMAND ma_free_surface_tests)
    add_executable(ma_gas_vacuum_tests native/tests/fluid_gas_vacuum_tests.cpp)
    target_link_libraries(ma_gas_vacuum_tests PRIVATE ma_core)
    add_test(NAME gas_vacuum_consistency COMMAND ma_gas_vacuum_tests)
    add_executable(ma_pressure_tests native/tests/pressure_tests.cpp)
    target_link_libraries(ma_pressure_tests PRIVATE ma_core)
    add_test(NAME pressure_preconditioning_and_rollback COMMAND ma_pressure_tests)
    add_executable(ma_mixture_properties_tests native/tests/mixture_properties_tests.cpp)
    target_link_libraries(ma_mixture_properties_tests PRIVATE ma_core)
    add_test(NAME mixture_property_compatibility COMMAND ma_mixture_properties_tests)
    add_executable(ma_occupied_staggered_tests native/tests/occupied_staggered_tests.cpp)
    target_link_libraries(ma_occupied_staggered_tests PRIVATE ma_occupied_staggered)
    add_test(NAME retained_occupied_staggered COMMAND ma_occupied_staggered_tests)
    # Explicit retained-node replacement check for the documented vacuum fan.
    # Never run the failed older Eulerian gate by selecting this target.
    add_executable(ma_retained_gas_vacuum_tests EXCLUDE_FROM_ALL native/tests/retained_gas_vacuum_tests.cpp)
    target_link_libraries(ma_retained_gas_vacuum_tests PRIVATE ma_occupied_staggered)
    if(MSVC)
        target_compile_options(ma_retained_gas_vacuum_tests PRIVATE /W4 /WX /permissive- /fp:strict)
    else()
        target_compile_options(ma_retained_gas_vacuum_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -ffp-contract=off -fno-fast-math)
    endif()
    foreach(target ma_free_surface_tests ma_gas_vacuum_tests ma_mixture_properties_tests ma_occupied_staggered_tests)
        if(MSVC)
            target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
        else()
            target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
        endif()
    endforeach()
    if(MA_BUILD_STAGGERED_EXPERIMENTS)
        add_executable(ma_retained_geometry_tests EXCLUDE_FROM_ALL native/tests/retained_geometry_tests.cpp)
        target_link_libraries(ma_retained_geometry_tests PRIVATE ma_occupied_staggered_hybrid_geometry)
        if(MSVC)
            target_compile_options(ma_retained_geometry_tests PRIVATE /W4 /WX /permissive- /fp:strict)
        else()
            target_compile_options(ma_retained_geometry_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -ffp-contract=off -fno-fast-math)
        endif()
        add_executable(ma_occupied_staggered_remap_tests native/tests/occupied_staggered_remap_tests.cpp)
        target_link_libraries(ma_occupied_staggered_remap_tests PRIVATE ma_occupied_staggered_remap)
        add_test(NAME positive_staggered_rezone COMMAND ma_occupied_staggered_remap_tests)
        add_executable(ma_occupied_staggered_hybrid_tests native/tests/occupied_staggered_hybrid_tests.cpp)
        target_link_libraries(ma_occupied_staggered_hybrid_tests PRIVATE ma_occupied_staggered_hybrid_geometry)
        add_test(NAME retained_hybrid_geometry COMMAND ma_occupied_staggered_hybrid_tests)
        add_executable(ma_occupied_staggered_hybrid_remap_tests native/tests/occupied_staggered_hybrid_remap_tests.cpp)
        target_link_libraries(ma_occupied_staggered_hybrid_remap_tests PRIVATE ma_occupied_staggered_hybrid_remap)
        add_test(NAME protected_hybrid_material_rezone COMMAND ma_occupied_staggered_hybrid_remap_tests)
        add_executable(ma_occupied_staggered_hybrid_event_tests native/tests/occupied_staggered_hybrid_event_tests.cpp)
        target_link_libraries(ma_occupied_staggered_hybrid_event_tests PRIVATE ma_occupied_staggered_hybrid_remap)
        add_test(NAME protected_hybrid_first_boundary_event COMMAND ma_occupied_staggered_hybrid_event_tests)
        add_executable(ma_occupied_staggered_hybrid_passive_tests native/tests/occupied_staggered_hybrid_passive_tests.cpp)
        target_link_libraries(ma_occupied_staggered_hybrid_passive_tests PRIVATE ma_occupied_staggered_hybrid_remap)
        add_test(NAME protected_hybrid_passive_diagnostic COMMAND ma_occupied_staggered_hybrid_passive_tests)
        add_executable(ma_occupied_staggered_hybrid_nonlinear_tests native/tests/occupied_staggered_hybrid_nonlinear_tests.cpp)
        target_link_libraries(ma_occupied_staggered_hybrid_nonlinear_tests PRIVATE ma_occupied_staggered_hybrid_remap)
        add_test(NAME protected_hybrid_nonlinear_passive COMMAND ma_occupied_staggered_hybrid_nonlinear_tests)
        set(ma_staggered_experiment_test_targets
            ma_occupied_staggered_remap_tests ma_occupied_staggered_hybrid_tests
            ma_occupied_staggered_hybrid_remap_tests ma_occupied_staggered_hybrid_event_tests
            ma_occupied_staggered_hybrid_passive_tests ma_occupied_staggered_hybrid_nonlinear_tests)
        add_custom_target(ma_staggered_experiment_tests DEPENDS ${ma_staggered_experiment_test_targets})
        foreach(target IN LISTS ma_staggered_experiment_test_targets)
            if(MSVC)
                target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
            else()
                target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
            endif()
        endforeach()
        set_tests_properties(positive_staggered_rezone retained_hybrid_geometry protected_hybrid_material_rezone
            protected_hybrid_first_boundary_event protected_hybrid_passive_diagnostic protected_hybrid_nonlinear_passive PROPERTIES LABELS "staggered-experimental")
        set_property(TEST positive_staggered_rezone APPEND PROPERTY LABELS "staggered-known-unresolved")
    endif()
endif()

add_executable(ma_fluid_benchmark native/apps/fluid_benchmark.cpp)
target_link_libraries(ma_fluid_benchmark PRIVATE ma_core)

install(TARGETS mixed_atmospheres RUNTIME DESTINATION . LIBRARY DESTINATION .)
