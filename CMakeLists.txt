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
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT MSVC)
    # Export only the extern "C" ma_* ABI. Archive members (ma_core,
    # ma_fluid_reference and, below, the static C++ runtime) are never
    # re-exported; the version script also hides std:: template instances
    # emitted in api.cpp, which namespace std's default visibility keeps public.
    set(ma_export_map "${CMAKE_CURRENT_BINARY_DIR}/mixed_atmospheres.map")
    file(CONFIGURE OUTPUT "${ma_export_map}" CONTENT "{\n  global: ma_*;\n  local: *;\n};\n" @ONLY)
    target_link_options(mixed_atmospheres PRIVATE "LINKER:--exclude-libs,ALL" "LINKER:--version-script=${ma_export_map}")
    set_property(TARGET mixed_atmospheres APPEND PROPERTY LINK_DEPENDS "${ma_export_map}")
    if(NOT MA_SANITIZERS)
        # No libstdc++.so.6/GLIBCXX or libgcc_s dependency. The static runtime
        # still binds glibc symbols at the build host's versions (GCC 15 on
        # glibc 2.43 needs GLIBC_2.38), so build release binaries on the oldest
        # supported glibc. Sanitizer builds keep dynamic runtimes.
        target_link_options(mixed_atmospheres PRIVATE -static-libstdc++ -static-libgcc)
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
    foreach(target ma_core ma_fluid_reference mixed_atmospheres)
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
    foreach(target ma_free_surface_tests ma_gas_vacuum_tests ma_mixture_properties_tests ma_occupied_staggered_tests)
        if(MSVC)
            target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
        else()
            target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
        endif()
    endforeach()
    if(MA_BUILD_STAGGERED_EXPERIMENTS)
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
