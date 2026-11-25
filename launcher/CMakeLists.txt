# EternalChampions.exe - launcher (ver launcher/PLAN.md). Incluido por game/CMakeLists.txt
# (EC_BUILD_LAUNCHER); reutiliza SDL2/PNG ja encontrados la.
cmake_minimum_required(VERSION 3.16)
if(NOT TARGET SDL2::SDL2)
    find_package(SDL2 REQUIRED CONFIG)
    find_package(PNG REQUIRED)
endif()

# Estudio de Remaster: biblioteca ec_remaster_core + remaster_core_selftest (saida em build/)
if(NOT TARGET ec_remaster_core)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/remaster ${CMAKE_BINARY_DIR}/remaster_build)
endif()

add_executable(EternalChampions WIN32
    src/main.cpp
    src/app.cpp
    src/render.cpp
    src/png_io.cpp
    src/font.cpp
    src/theme.cpp
    src/input.cpp
    src/widgets.cpp
    src/sha256.cpp
    src/launch.cpp
    src/studio.cpp
    src/preview_job.cpp
    src/json.cpp
    src/bitmap_font.cpp
    src/art_job.cpp
    src/ini.cpp
    src/schema.cpp
    src/shader_params.cpp
    src/platform.cpp
    src/screens/splash.cpp
    src/screens/main_menu.cpp
    src/screens/rom_screen.cpp
    src/screens/about.cpp
    src/screens/placeholder.cpp
    src/screens/game_run.cpp
    src/screens/confirm.cpp
    src/screens/options_screen.cpp
    src/screens/settings_screens.cpp
    src/screens/saves.cpp
    src/screens/controls.cpp
    src/screens/studio_screen.cpp
    src/screens/fm_view.cpp
)
target_include_directories(EternalChampions PRIVATE src)
target_compile_features(EternalChampions PRIVATE cxx_std_17)
target_compile_definitions(EternalChampions PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
    target_compile_options(EternalChampions PRIVATE /utf-8 /W3)
endif()
target_link_libraries(EternalChampions PRIVATE ec_remaster_core SDL2::SDL2main SDL2::SDL2 PNG::PNG comdlg32 gdi32 shell32 user32 ole32)
set_target_properties(EternalChampions PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR})
