# SPDX-License-Identifier: MIT
# Copyright (c) 2026 SunnySlopes

cmake_minimum_required(VERSION 3.20)
project(NoSlimeFinderBase VERSION 1.0.0 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# NoSlimeFinderBase
#
#   noslimefinder_base      static library with the search core, cubiomes and
#                           SlimeOptimizer sources (linked by the GUI JNI shim)
#   NoSlimeFinderBase-cli   standalone command line executable
#
# Dependencies are git submodules in this repository:
#   cubiomes/        https://github.com/xpple/cubiomes
#   slimeoptimizer/  https://github.com/CITYWIDESIGN/SlimeOptimizer
#
# After cloning:
#   git submodule update --init --recursive
#
# For a CMake-free build used by the GUI, see scripts/build_msys2.ps1.
# ---------------------------------------------------------------------------

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(NOSLIME_CUBIOMES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cubiomes"
    CACHE PATH "Path to the cubiomes checkout")
set(NOSLIME_OPTIMIZER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/slimeoptimizer"
    CACHE PATH "Path to the SlimeOptimizer checkout")

if (NOT EXISTS "${NOSLIME_CUBIOMES_DIR}/generator.c" OR
    NOT EXISTS "${NOSLIME_CUBIOMES_DIR}/biomes.h")
  message(FATAL_ERROR
    "cubiomes submodule is missing: ${NOSLIME_CUBIOMES_DIR}\n"
    "Run 'git submodule update --init --recursive', or pass "
    "-DNOSLIME_CUBIOMES_DIR=<path-to-cubiomes>.")
endif()

if (NOT EXISTS "${NOSLIME_OPTIMIZER_DIR}/include/geometry.hpp" OR
    NOT EXISTS "${NOSLIME_OPTIMIZER_DIR}/src/slime_check.cpp")
  message(FATAL_ERROR
    "SlimeOptimizer submodule is missing: ${NOSLIME_OPTIMIZER_DIR}\n"
    "Run 'git submodule update --init --recursive', or pass "
    "-DNOSLIME_OPTIMIZER_DIR=<path-to-slimeoptimizer>.")
endif()

set(NOSLIME_CORE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/noslime_search.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/biomes.cpp")

# Only the sources used by the search: no finders/carvers/loot/features.
set(NOSLIME_CUBIOMES_SOURCES
    "${NOSLIME_CUBIOMES_DIR}/generator.c"
    "${NOSLIME_CUBIOMES_DIR}/layers.c"
    "${NOSLIME_CUBIOMES_DIR}/biomenoise.c"
    "${NOSLIME_CUBIOMES_DIR}/biomes.c"
    "${NOSLIME_CUBIOMES_DIR}/noise.c"
    "${NOSLIME_CUBIOMES_DIR}/util.c")

set(NOSLIME_OPTIMIZER_SOURCES
    "${NOSLIME_OPTIMIZER_DIR}/src/geometry.cpp"
    "${NOSLIME_OPTIMIZER_DIR}/src/slime_check.cpp"
    "${NOSLIME_OPTIMIZER_DIR}/src/java_random.cpp")

add_library(noslimefinder_base STATIC
    ${NOSLIME_CORE_SOURCES}
    ${NOSLIME_CUBIOMES_SOURCES}
    ${NOSLIME_OPTIMIZER_SOURCES})

target_include_directories(noslimefinder_base
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
            "${NOSLIME_OPTIMIZER_DIR}/include")

# cubiomes rng.h defines lerp(); C++20 <math.h> imports std::lerp. Force the
# cubiomes header through first so the macro rename in pre_cubiomes.h applies.
set_source_files_properties(${NOSLIME_CORE_SOURCES} ${NOSLIME_OPTIMIZER_SOURCES}
    PROPERTIES COMPILE_OPTIONS
    "-include;${CMAKE_CURRENT_SOURCE_DIR}/cxx_compat/pre_cubiomes.h")

add_executable(NoSlimeFinderBase-cli
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/main.cpp")

target_link_libraries(NoSlimeFinderBase-cli PRIVATE noslimefinder_base)

find_package(Threads REQUIRED)
target_link_libraries(NoSlimeFinderBase-cli PRIVATE Threads::Threads)

set_target_properties(noslimefinder_base PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set_target_properties(NoSlimeFinderBase-cli PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if (MSVC)
  target_compile_options(noslimefinder_base PRIVATE /O2 /W3 /DNDEBUG)
  target_compile_options(NoSlimeFinderBase-cli PRIVATE /O2 /W3 /DNDEBUG)
  set_property(TARGET noslimefinder_base PROPERTY
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  set_property(TARGET NoSlimeFinderBase-cli PROPERTY
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
  target_compile_options(noslimefinder_base PRIVATE -O3 -DNDEBUG)
  target_compile_options(NoSlimeFinderBase-cli PRIVATE -O3 -DNDEBUG)

  if (MINGW OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(NoSlimeFinderBase-cli PRIVATE
        -static -static-libgcc -static-libstdc++)
  endif()
  if (NOT WIN32)
    target_link_libraries(NoSlimeFinderBase-cli PRIVATE m)
  endif()
endif()

include(GNUInstallDirs)
install(TARGETS NoSlimeFinderBase-cli
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
        DESTINATION "${CMAKE_INSTALL_DOCDIR}")
# Keep the MIT notice with the standalone executable and with the docs.
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
        DESTINATION "${CMAKE_INSTALL_BINDIR}" RENAME LICENSE.txt)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
        DESTINATION "${CMAKE_INSTALL_DOCDIR}")
