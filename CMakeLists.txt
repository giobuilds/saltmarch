cmake_minimum_required(VERSION 3.20)
project(anno_clone C)

# ---- C standard ------------------------------------------
# K&R C is C89/C90.  We use C99 here because it is a strict
# superset that also allows // comments and mixed declarations.
# SDL3 itself requires at least C99.
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---- Compiler warnings -----------------------------------
# Turn on a broad set of warnings to catch mistakes early.
# -Wextra adds checks beyond -Wall.
# -Wpedantic flags anything not strictly conforming to the standard.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow          # warn if a local variable shadows an outer one
        -Wconversion      # warn on implicit type conversions
    )
endif()

# ---- Find SDL3 -------------------------------------------
# SDL3 ships a CMake config file (SDL3Config.cmake).
# If SDL3 is installed system-wide (e.g. via dnf install SDL3-devel)
# CMake will find it automatically.
# If you built SDL3 from source, set SDL3_DIR to its build directory:
#   cmake -DSDL3_DIR=/path/to/SDL3/build ..
find_package(SDL3 REQUIRED CONFIG)
find_package(SDL3_ttf REQUIRED CONFIG)
find_package(SDL3_image REQUIRED CONFIG)

# ---- Source files ----------------------------------------
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
    src/sprite.c
)

# ---- Executable ------------------------------------------
add_executable(anno_clone ${SOURCES})

# Link against the SDL3 shared library target.
# SDL3::SDL3 is the target name the SDL3 CMake config exports.
target_link_libraries(anno_clone PRIVATE SDL3::SDL3 m SDL3_ttf::SDL3_ttf SDL3_image::SDL3_image)   # m = libm for floorf()

# ---- Install (optional) ----------------------------------
install(TARGETS anno_clone DESTINATION bin)

# ---- Build type info message -----------------------------
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C compiler: ${CMAKE_C_COMPILER}")
