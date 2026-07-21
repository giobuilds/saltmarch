cmake_minimum_required(VERSION 3.20)
project(anno_clone C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()

find_package(SDL3       REQUIRED CONFIG)
find_package(SDL3_ttf   REQUIRED CONFIG)

set(SOURCES
    src/main.c
    src/game.c
    src/map.c
    src/camera.c
    src/input.c
    src/render.c
    src/building.c
    src/ui.c
    src/resource.c
    src/fonts.c
    src/population.c
    src/connectivity.c
    src/trade_ui.c
    src/agent.c
    src/build_confirm_ui.c
    src/demolish_confirm_ui.c
    src/tier_upgrade_ui.c
)

add_executable(anno_clone ${SOURCES})

target_link_libraries(anno_clone PRIVATE
    SDL3::SDL3
    SDL3_ttf::SDL3_ttf
    m
)
