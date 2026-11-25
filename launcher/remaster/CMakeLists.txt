# ec_remaster_core - nucleo do Estudio de Remaster (sem UI): voices.toml/perfis, catalogo de
# instrumentos a partir dos dumps, geracao de dumps headless, sintese da previa (tsf.h) e
# captura do audio original. Incluido a partir de game/CMakeLists.txt (Windows, Linux, macOS).
cmake_minimum_required(VERSION 3.16)

if(NOT WIN32)
    find_package(Threads REQUIRED)
endif()

add_library(ec_remaster_core STATIC
    voices_toml.cpp
    catalog.cpp
    dumps.cpp
    proc.cpp
    preview_synth.cpp
    original_capture.cpp
    tsf_impl.c
)
target_include_directories(ec_remaster_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(NOT WIN32)
    target_link_libraries(ec_remaster_core PUBLIC Threads::Threads)
    find_library(EC_M_LIB m)
    if(EC_M_LIB)
        target_link_libraries(ec_remaster_core PUBLIC ${EC_M_LIB})
    endif()
endif()
target_compile_features(ec_remaster_core PUBLIC cxx_std_17)
target_compile_definitions(ec_remaster_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
if(MSVC)
    target_compile_options(ec_remaster_core PRIVATE /utf-8 /W3)
endif()

add_executable(remaster_core_selftest tests/selftest.cpp)
target_link_libraries(remaster_core_selftest PRIVATE ec_remaster_core)
target_compile_definitions(remaster_core_selftest PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
if(MSVC)
    target_compile_options(remaster_core_selftest PRIVATE /utf-8 /W3)
endif()
set_target_properties(remaster_core_selftest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR})
