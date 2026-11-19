cmake_minimum_required(VERSION 3.28)
project(ares_cli CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ARES_ROOT /home/mips/eternalchampions/ares)

# Variáveis que o bootstrap do ares normalmente define
set(ARES_NAME "ares")
set(ARES_WEBSITE "ares-emu.net")
set(ARES_LEGAL_COPYRIGHT_SHORT "2004-2026 ares team, Near")
set(ARES_VERSION "1.0")
set(ARES_VERSION_CANONICAL "1.0.0")
set(ARES_PROFILE_ACCURACY OFF CACHE BOOL "" FORCE)
set(ARES_ENABLE_CHD OFF CACHE BOOL "" FORCE)
set(ARES_CORES md CACHE STRING "List of systems" FORCE)
set(ARES_BUILD_LOCAL ON CACHE BOOL "" FORCE)

list(APPEND CMAKE_MODULE_PATH
  "${ARES_ROOT}/cmake/common"
  "${ARES_ROOT}/cmake/finders"
  "${ARES_ROOT}/cmake/linux"
)

include(compilerconfig)
include(defaults)
include(helpers)

# sourcery: gera resource.cpp/hpp a partir de resource.bml (núcleo e mia)
add_subdirectory("${ARES_ROOT}/tools/sourcery" sourcery-build)
set(ENV{PATH} "${CMAKE_CURRENT_BINARY_DIR}/sourcery-build:$ENV{PATH}")

add_subdirectory("${ARES_ROOT}/nall/nall" nall-build)
add_subdirectory("${ARES_ROOT}/libco" libco-build)
add_subdirectory("${ARES_ROOT}/thirdparty" thirdparty-build)
add_subdirectory("${ARES_ROOT}/ares" ares-build)
# O CMakeLists do núcleo só expõe o include via INTERFACE (consumidores);
# os próprios fontes (ex.: <md/md.hpp>) precisam do dir como PRIVATE.
target_include_directories(ares PRIVATE "${ARES_ROOT}/ares")
add_subdirectory("${ARES_ROOT}/mia" mia-build)
target_include_directories(mia PRIVATE "${ARES_ROOT}")

add_executable(ares_cli main.cpp)
target_link_libraries(ares_cli PRIVATE ares::mia ares::ares nall)
target_include_directories(ares_cli PRIVATE
  "${ARES_ROOT}"
  "${ARES_ROOT}/nall"
  "${ARES_ROOT}/ares"
  "${ARES_ROOT}/mia"
)
