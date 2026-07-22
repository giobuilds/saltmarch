# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

An isometric city-builder written in C99 with SDL3. Single-executable game: procedural island generation, tile-based building placement, a resource/production chain, and a population/needs simulation. No sprites currently — terrain and buildings render as flat-shaded isometric diamonds (colored per tile/building type).

## Build & run

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
./build/saltmarch
```

Requires `SDL3-devel` and `SDL3_ttf-devel` (Fedora package names; see BUILD.md for building SDL3 from source if not packaged). There is no test suite, linter, or CI config in this repo — verification is manual (build cleanly, run the binary, exercise the feature in-window).

The build is configured with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion` (see CMakeLists.txt) — treat new warnings as bugs to fix, not noise to suppress.

Runs fullscreen at a fixed 1920x1080 logical resolution (`SCREEN_W`/`SCREEN_H` in game.h) via `SDL_LOGICAL_PRESENTATION_STRETCH`.

## Architecture

Uses SDL3's callback-based app model (`SDL_MAIN_USE_CALLBACKS`), not a manual event loop. `src/main.c` implements only `SDL_AppInit` / `SDL_AppEvent` / `SDL_AppIterate` / `SDL_AppQuit` and contains no game logic — it wires subsystems together and owns the frame's render order. When adding a new subsystem, follow this pattern: give it an init/free pair and a per-frame update/render function, then call it from main.c in the right order rather than reaching into other subsystems' internals.

**`GameState` (game.h) is the single top-level struct** owning every subsystem: `Map`, `Camera`, `InputState`, the `buildings[]`/`building_count` array, `Stockpile`, and `pop_data[]`. One `GameState*` is stashed in SDL's `appstate` and threaded through every callback. There is no global state outside of `BUILDING_DEFS` and `RESOURCE_NAMES` (static const tables).

**Coordinate systems**: the map is a `MAP_ROWS x MAP_COLS` (64x64) grid of `Tile` (row, col). `render.c`'s `iso_to_screen()` / `screen_to_iso()` are the only conversion points between tile space and screen pixels — they account for `Camera.offset_x/y` and `Camera.zoom`. Any new code that needs to place something on the map or hit-test a click must go through these two functions rather than re-deriving the projection math. `screen_to_iso()` uses `floorf()` (not integer cast) because negative-coordinate truncation was a past bug (see "Bug fix 001" in README.md).

**Two parallel arrays keyed by building slot index**: `gs->buildings[i]` (the placed instance) and `gs->pop_data[i]` (population data, meaningful only when `buildings[i].type == BUILDING_HOUSE`). When iterating buildings for anything population-related, index both arrays together rather than searching.

**Building data is split into static def vs. instance**, mirroring a class/instance split:
- `BUILDING_DEFS[BUILDING_TYPE_COUNT]` (building.c) — one static entry per building *type*: footprint size, placement rule bitmask (`PlacementFlags`), color, and production fields (`produces`/`consumes`/`tick_seconds`). `RES_COUNT` as `produces` or `consumes` is the sentinel for "no resource."
- `Building` — a placed *instance*: type, position, active flag, and a `timer` float that accumulates toward the next production tick.

New building types are added by extending the `BuildingType` enum and adding a matching row to `BUILDING_DEFS`; placement validation and rendering are generic over the def table and need no per-type special-casing unless the new type needs a new `PlacementFlags` rule.

**Resource flow**: `Stockpile` (resource.h) holds one `int amount[RES_COUNT]` clamped at zero, owned by `GameState`. Buildings read/write it via `stockpile_add()` during their production tick (driven by `Building.timer` vs. `BuildingDef.tick_seconds`), and houses read/write it via `pop_update()` every `NEEDS_INTERVAL` (30s) — consuming fish+grain, producing gold, growing or shrinking `residents` based on whether needs were met.

**Frame-rate independence**: all continuous movement (camera pan, production timers, population needs) is scaled by `GameState.delta_time`, computed once per frame in `game_update()` from the SDL tick delta. Don't add new per-frame increments without multiplying by `delta_time`.

**Rendering fallback pattern**: `render.c` draws everything as SDL_RenderGeometry diamonds (`draw_diamond()`), colored per `TILE_COLOURS`/`BuildingDef` color fields. Sprite-based rendering existed in Phase 6 (see README.md) but was removed by the most recent commit — `src/sprite.c`/`src/sprite.h` still exist on disk but are **not** in `CMakeLists.txt`'s `SOURCES` and are dead code; don't wire them back in without checking with the user first, since their removal looks intentional.

**Text rendering** goes through `fonts.c` (thin SDL_ttf wrapper, `fonts_init()`/`fonts_quit()`/`font_draw_text()`), not raw SDL_ttf calls — HUD, tooltips, and menu labels all use it.

## File responsibilities

Each `src/*.c`/`*.h` pair is a self-contained subsystem; see the header comment block at the top of each file for its specific design notes (several encode non-obvious fixes, e.g. the `screen_to_iso()` centroid offset and the frame-rate-independent camera in camera.h). In file-reading order of typical relevance:
- `map.c/h` — tile grid, procedural island generation (two-octave value noise + radial mask + LCG RNG)
- `camera.c/h` — pan offset + zoom (`ZOOM_MIN`/`ZOOM_MAX`/`ZOOM_STEP` in camera.h)
- `input.c/h` — held keys, mouse position/clicks/scroll for one frame; `input_clear_clicks()` resets per-frame state
- `building.c/h` — building defs, placement validation, placement
- `resource.c/h` — resource enum + stockpile
- `population.c/h` — house needs/growth simulation
- `render.c/h` — all drawing and the iso<->screen projection
- `ui.c/h` — HUD bar, cog menu overlay, hit-testing
- `fonts.c/h` — SDL_ttf wrapper

## History / conventions

README.md's "Phase N" sections are a changelog of major feature additions (procedural gen → building placement → resource ticks → population → sprites → sprite removal/zoom) written in past commits — read it before assuming a feature (like sprites) is still in the current build; check `CMakeLists.txt`'s `SOURCES` list against `src/` to see what's actually compiled.
