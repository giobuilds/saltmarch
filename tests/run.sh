#!/usr/bin/env bash
# tests/run.sh -- build and run the headless behaviour tests.
#
# Follows the project's verification convention: link the game's own
# compiled .o files (everything except main.c, which owns SDL_AppMain)
# into a small C program that asserts real behaviour, headless. Requires
# the game to have been built first (cmake --build build).
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
objdir="$root/build/CMakeFiles/saltmarch.dir/src"

if [ ! -d "$objdir" ]; then
    echo "build objects not found; run: cmake -B build && cmake --build build" >&2
    exit 1
fi

objs=$(ls "$objdir"/*.c.o | grep -v '/main.c.o')
sdlflags=$(pkg-config --cflags --libs sdl3 sdl3-ttf 2>/dev/null || echo "-lSDL3 -lSDL3_ttf")

status=0
for src in "$root"/tests/test_*.c; do
    name=$(basename "$src" .c)
    echo "=== $name ==="
    # shellcheck disable=SC2086
    cc -std=c99 -Wall -Wextra -I"$root/src" "$src" $objs $sdlflags -lm \
       -o "$root/build/$name"
    # Headless: no window/audio device needed for these.
    if ! SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$root/build/$name"; then
        status=1
    fi
    echo
done

exit $status
