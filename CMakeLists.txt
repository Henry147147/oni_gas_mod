cmake_minimum_required(VERSION 3.20)
project(MixedAtmospheres VERSION 0.1.0 LANGUAGES CXX)

option(MA_BUILD_TESTS "Build standalone physics and ABI tests" ON)
option(MA_SANITIZERS "Enable address and undefined-behavior sanitizers" OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Used only by the explicit experimental FluidWorld geometry/EOS mode. The
# existing GasWorld/C ABI do not select it. Keep its independent test project.
add_library(ma_fluid_reference STATIC native/experimental/liquid_reference.cpp)
set_target_properties(ma_fluid_reference PROPERTIES POSITION_INDEPENDENT_CODE ON)
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
set_target_properties(ma_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(ma_core PUBLIC native/include)
target_link_libraries(ma_core PUBLIC ma_fluid_reference)

add_library(mixed_atmospheres SHARED native/src/api.cpp)
target_link_libraries(mixed_atmospheres PRIVATE ma_core)
target_include_directories(mixed_atmospheres PUBLIC native/include)
target_compile_definitions(mixed_atmospheres PRIVATE MA_EXPORTS)
set_target_properties(mixed_atmospheres PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)

foreach(target ma_core ma_fluid_reference mixed_atmospheres)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /fp:precise)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    endif()
endforeach()
if(MA_SANITIZERS AND NOT MSVC)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
    foreach(target ma_core ma_fluid_reference mixed_atmospheres)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endforeach()
endif()

add_executable(ma_demo native/apps/demo.cpp)
target_link_libraries(ma_demo PRIVATE ma_core)

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
    foreach(target ma_free_surface_tests ma_gas_vacuum_tests ma_mixture_properties_tests)
        if(MSVC)
            target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
        else()
            target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
        endif()
    endforeach()
endif()

add_executable(ma_fluid_benchmark native/apps/fluid_benchmark.cpp)
target_link_libraries(ma_fluid_benchmark PRIVATE ma_core)

install(TARGETS mixed_atmospheres RUNTIME DESTINATION . LIBRARY DESTINATION .)
