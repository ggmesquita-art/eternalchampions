cmake_minimum_required(VERSION 3.16)
project(EternalChampionsRecomp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(GENESIS_NATIVE_IPO "OFF" CACHE STRING "" FORCE)

option(EC_GEN_DEV_TRACE "Compila os traces de som dev (chip_ring/snd_ring, F12/--snd-dump)" OFF)

find_package(SDL2 REQUIRED CONFIG)

set(RECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../segagenesisrecomp")
include("${RECOMP_ROOT}/cmake/GenesisRecompGenerated.cmake")

genesisrecomp_add_generated_sources(
    EC_GEN_SOURCES
    ec
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "../roms/eternal_champions_us.gen"
)

genesisrecomp_runner_sources(RUNNER_SOURCES TRACE ON)

add_executable(EternalChampionsRecomp
    ec_spec.c
    ec_boss.c
    ec_video.c
    ../neural/infer/ec_bplus_gl.c
    ../neural/infer/ec_bplus_ort.c
    ${EC_GEN_SOURCES}
    ${RUNNER_SOURCES}
)

target_include_directories(EternalChampionsRecomp PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../neural/infer"
    "${EC_GEN_SOURCES_DIR}"
    "${RECOMP_ROOT}/runner"
    "${RECOMP_ROOT}/runner/include"
    "${RECOMP_ROOT}/runner/video"
    "${RECOMP_ROOT}/runner/audio"
    "${RECOMP_ROOT}/runner/external/ymfm/src"
    "${RECOMP_ROOT}/runner/external/superzazu"
    "${RECOMP_ROOT}/runner/external/clowncommon"
    "${RECOMP_ROOT}/runner/external/minicoro"
    "${RECOMP_ROOT}/runner/external/xxhash"
    "${RECOMP_ROOT}/recompiler/src"
    "${RECOMP_ROOT}/external/m68k-recomp-core/common"
    "${RECOMP_ROOT}/external/m68k-recomp-core/profiles/genesis"
)

target_compile_definitions(EternalChampionsRecomp PRIVATE
    RECOMP_LAUNCHER=0
    GENESIS_HAS_RECOMP_NET=0
)
if(EC_GEN_DEV_TRACE)
    target_compile_definitions(EternalChampionsRecomp PRIVATE GEN_DEV_TRACE)
endif()

target_link_libraries(EternalChampionsRecomp PRIVATE
    SDL2::SDL2
    m
    pthread
    dl
)

genesisrecomp_runner_target(EternalChampionsRecomp)
