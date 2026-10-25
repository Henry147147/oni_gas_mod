# Windows x64 libraries cross-built on Linux with MinGW-w64 GCC (posix threads).
#
#   cmake -S . -B build-windows -G Ninja -DCMAKE_BUILD_TYPE=Release \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-x86_64.cmake \
#     -DMA_MINGW_ROOT=/path/to/usr -DMA_BUILD_ONI_NATIVE_BACKEND=ON
#   cmake --build build-windows --target mixed_atmospheres mixed_atmospheres_oni
#
# MA_MINGW_ROOT is the prefix holding bin/x86_64-w64-mingw32-g++-posix (default:
# the compilers on PATH). Every shipped DLL links the GCC, C++ and winpthreads
# runtimes statically (CMakeLists.txt, cmake/OniNativeBackend.cmake), so the
# game package needs no MinGW DLL. MA_WINE (a wine64 executable) runs CTest's
# Windows executables on Linux. Pass both as absolute paths when cross-building.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES MA_MINGW_ROOT MA_WINE)
if(NOT MA_MINGW_ROOT AND DEFINED ENV{MA_MINGW_ROOT})
    set(MA_MINGW_ROOT "$ENV{MA_MINGW_ROOT}")
endif()
if(NOT MA_WINE AND DEFINED ENV{MA_WINE})
    set(MA_WINE "$ENV{MA_WINE}")
endif()
# CMake projects with pre-CMP0137 policies do not forward custom cache entries
# to project-style IPO checks. Preserve the chosen prefix in this configure
# process's child environment too (never changes the invoking shell).
if(MA_MINGW_ROOT)
    set(ENV{MA_MINGW_ROOT} "${MA_MINGW_ROOT}")
endif()
set(ma_mingw_hints)
if(MA_MINGW_ROOT)
    list(APPEND ma_mingw_hints "${MA_MINGW_ROOT}/bin")
endif()
if(CMAKE_CXX_COMPILER)
    get_filename_component(ma_existing_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    list(APPEND ma_mingw_hints "${ma_existing_bin}")
endif()
set(ma_mingw x86_64-w64-mingw32)
find_program(CMAKE_C_COMPILER NAMES ${ma_mingw}-gcc-posix HINTS ${ma_mingw_hints} REQUIRED)
find_program(CMAKE_CXX_COMPILER NAMES ${ma_mingw}-g++-posix HINTS ${ma_mingw_hints} REQUIRED)
find_program(CMAKE_RC_COMPILER NAMES ${ma_mingw}-windres HINTS ${ma_mingw_hints} REQUIRED)
get_filename_component(ma_mingw_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
get_filename_component(ma_mingw_prefix "${ma_mingw_bin}" DIRECTORY)
set(CMAKE_FIND_ROOT_PATH "${ma_mingw_prefix}/${ma_mingw}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
# Test executables run under Wine without the toolchain's runtime DLLs.
set(CMAKE_EXE_LINKER_FLAGS_INIT -static)
# GCC 13 diagnoses harmless copies of small initializer-list pairs in the game
# kernel. Keep that warning visible without changing shared solver sources.
set(CMAKE_CXX_FLAGS_INIT -Wno-error=range-loop-construct)
if(MA_WINE)
    set(CMAKE_CROSSCOMPILING_EMULATOR "${MA_WINE}")
endif()
