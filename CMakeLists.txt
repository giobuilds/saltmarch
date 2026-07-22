cmake_minimum_required(VERSION 3.20)
project(saltmarch C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion)
elseif(MSVC)
    # Without this branch MSVC builds with no warning flags at all, so
    # the project's "zero warnings is the bar" rule would silently apply
    # to three platforms out of four.
    #
    # /W4 is the closest practical equivalent to -Wall -Wextra. MSVC has
    # no direct -Wconversion analogue; C4244/C4267 (narrowing) are the
    # useful part of it and are on at /W4.
    #
    # /std:c17 is not optional: the code uses ~46 designated initialisers
    # (RESOURCE_NAMES, PROFILE_PARAMS, BUILDING_DEFS costs), which MSVC
    # only accepts in C mode from C11 onward. The default C mode rejects
    # them outright.
    add_compile_options(/W4 /std:c17)
endif()

# Opt-in, used by CI: make any warning fail the build. Off by default so
# local work is never blocked mid-experiment, on in CI so the bar is
# actually enforced rather than merely stated.
option(SALTMARCH_WERROR "Treat compiler warnings as errors" OFF)
if(SALTMARCH_WERROR)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-Werror)
    elseif(MSVC)
        add_compile_options(/WX)
    endif()
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
    src/island.c
    src/world_ui.c
    src/ship.c
)

add_executable(saltmarch ${SOURCES})

# Copy assets next to the binary after every build. fonts.c resolves the
# font via SDL_GetBasePath() (the executable's directory), which is what
# makes one code path work on Linux, macOS and Windows -- but only if
# the assets are actually there. Doing it as a POST_BUILD step means a
# plain `cmake --build` is enough; no install step required.
add_custom_command(TARGET saltmarch POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:saltmarch>/assets
    COMMENT "Staging assets/ beside the executable"
)

target_link_libraries(saltmarch PRIVATE
    SDL3::SDL3
    SDL3_ttf::SDL3_ttf
    m
)
