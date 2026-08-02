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

Requires `SDL3-devel` and `SDL3_ttf-devel` (Fedora package names; see docs/BUILD.md for building SDL3 from source if not packaged).

The build produces six targets (`libsaltmarch_ui.a` holds the SDL-free UI layer): `saltmarch` (the game), `libsaltmarch_sim.a` (the simulation, no SDL), `libsaltmarch_net.a` (the lockstep protocol, no SDL, shared by game and server), `libsaltmarch_ui.a` (layout/hit-test kit + the UI's read-only snapshot, no SDL), `saltmarch_replay` (headless CLI over the sim) and `saltmarch_host` (the persistent server — see docs/SERVER.md). Verification, in the order it is cheapest to run:

```bash
cmake --build build -j$(nproc)          # zero warnings is the bar
./tests/run.sh                          # headless behaviour assertions
./ci/sim-sdl-free.sh                    # the sim/client boundary holds
./ci/smoke-test.sh ./build/saltmarch 5  # the binary actually lives
./ci/host-smoke.sh ./build              # server + client over real TCP
./build/saltmarch_replay --record-ui u.smlog --seed 777 && \
  ./build/saltmarch_replay --replay u.smlog --verify-ui   # UI click replay
./build/saltmarch_replay --record f.smlog --seed 12345 && \
  ./build/saltmarch_replay --replay f.smlog     # determinism gate
./ci/sanitize.sh                        # the same, under ASan/UBSan/MSan
```

`ci/sanitize.sh` is the runtime half of the warning flags — it configures its own build directories (`build-asan`, `build-msan`) so it never disturbs yours, and runs the whole ladder under instrumentation. Two passes, because no one sanitizer covers both halves: **address,undefined** over everything including the SDL client and a real TCP server, and **memory** over the SDL-free libraries only, since MemorySanitizer needs every linked object instrumented. The second is the one that matters most here: the world is hashed, and a hash over uninitialised memory is stable within one run and different across machines — the failure mode no single-platform test can see. It found real undefined behaviour on its first run (`game_load` and the join path both formed a `Command *` over a byte offset past a snapshot), and re-introducing the UI_PLAN M3 bug on purpose makes it fail. Slow (2-3x), so it is its own CI job rather than part of the local loop.

`.github/workflows/ci.yml` runs all of this on Linux, macOS and Windows with warnings as errors. Anything genuinely visual still needs a human at the keyboard.

The build is configured with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion` (see CMakeLists.txt) — treat new warnings as bugs to fix, not noise to suppress.

Runs fullscreen at a fixed 1920x1080 logical resolution (`SCREEN_W`/`SCREEN_H` in game.h) via `SDL_LOGICAL_PRESENTATION_STRETCH`.

**Looking at it.** `--screenshot FILE [--screenshot-frames N] [--screenshot-overlay charts|book|yard|stores|world|trade]` draws N frames, saves the last one and exits. It reads the renderer this program owns (`SDL_RenderReadPixels`), so it works under `SDL_VIDEODRIVER=offscreen` with no display — and it has to, because on a Wayland compositor nothing external can capture the window at all (GNOME refuses the screenshot D-Bus API to anything but its own portal; X11 grabbers cannot see a Wayland surface). A screenshot run opens a real 1920x1080 window rather than a fullscreen one, so the capture is at logical resolution instead of whatever the display happens to be. This is the missing half of "anything genuinely visual still needs a human at the keyboard": the human still judges it, but now there is something to look at.

```bash
SDL_VIDEODRIVER=offscreen ./build/saltmarch --screenshot /tmp/charts.bmp \
    --screenshot-frames 90 --screenshot-overlay charts
```

## Architecture

Uses SDL3's callback-based app model (`SDL_MAIN_USE_CALLBACKS`), not a manual event loop. `src/main.c` implements only `SDL_AppInit` / `SDL_AppEvent` / `SDL_AppIterate` / `SDL_AppQuit` and contains no game logic — it wires subsystems together and owns the frame's render order. When adding a new subsystem, follow this pattern: give it an init/free pair and a per-frame update/render function, then call it from main.c in the right order rather than reaching into other subsystems' internals.

**The sim and the client are separate build targets** (MMO_PLAN Phase 6). `SALTMARCH_SIM_SOURCES` in CMakeLists.txt is the world — game, command, map, camera, building, resource, population, connectivity, agent, island, ship, faction, replay, simlog — and it links no SDL: use `sim_log()` instead of `SDL_Log`, stdio instead of `SDL_IOStream`, `<stdint.h>` types instead of `Uint64`. The client half (main, client, input, render, ui, the `*_ui` overlays, fonts, feed, net) may use SDL freely. Code that needs both SDL and `GameState` goes in client.c. When the sim needs to reach a client subsystem, it does so through a function pointer the client installs (see `GameState.net_submit` / `net_attach()`), never a direct call — `ci/sim-sdl-free.sh` fails the build otherwise.

**`GameState` (game.h) is the single top-level struct** owning every subsystem: `Map`, `Camera`, `InputState`, the `buildings[]`/`building_count` array, `Stockpile`, and `pop_data[]`. One `GameState*` is stashed in SDL's `appstate` and threaded through every callback. There is no global state outside of `BUILDING_DEFS` and `RESOURCE_NAMES` (static const tables).

**Coordinate systems**: the map is a `MAP_ROWS x MAP_COLS` (64x64) grid of `Tile` (row, col). `render.c`'s `iso_to_screen()` / `screen_to_iso()` are the only conversion points between tile space and screen pixels — they account for `Camera.offset_x/y` and `Camera.zoom`. Any new code that needs to place something on the map or hit-test a click must go through these two functions rather than re-deriving the projection math. `screen_to_iso()` uses `floorf()` (not integer cast) because negative-coordinate truncation was a past bug (see "Bug fix 001" in docs/README.md).

**Two parallel arrays keyed by building slot index**: `gs->buildings[i]` (the placed instance) and `gs->pop_data[i]` (population data, meaningful only when `buildings[i].type == BUILDING_HOUSE`). When iterating buildings for anything population-related, index both arrays together rather than searching.

**Building data is split into static def vs. instance**, mirroring a class/instance split:
- `BUILDING_DEFS[BUILDING_TYPE_COUNT]` (building.c) — one static entry per building *type*: footprint size, placement rule bitmask (`PlacementFlags`), color, and production fields (`produces`/`consumes`/`tick_seconds`). `RES_COUNT` as `produces` or `consumes` is the sentinel for "no resource."
- `Building` — a placed *instance*: type, position, active flag, and a `timer` float that accumulates toward the next production tick.

New building types are added by extending the `BuildingType` enum and adding a matching row to `BUILDING_DEFS`; placement validation and rendering are generic over the def table and need no per-type special-casing unless the new type needs a new `PlacementFlags` rule.

**Resource flow**: `Stockpile` (resource.h) holds one `int amount[RES_COUNT]` clamped at zero, owned by `GameState`. Buildings read/write it via `stockpile_add()` during their production tick (driven by `Building.timer` vs. `BuildingDef.tick_seconds`), and houses read/write it via `pop_update()` every `NEEDS_INTERVAL` (30s) — consuming fish+grain, producing gold, growing or shrinking `residents` based on whether needs were met.

**Is an overlay open?** Ask `game_topmost_overlay()` / `game_overlay_open()` (game.h), never a hand-rolled list of the `*_open` flags. That list was already wrong once — the mouse wheel zoomed the world behind open modals because the zoom code never asked.

**Frame-rate independence**: all continuous movement (camera pan, production timers, population needs) is scaled by `GameState.delta_time`, computed once per frame in `game_update()` from the SDL tick delta. Don't add new per-frame increments without multiplying by `delta_time`.

**Rendering fallback pattern**: `render.c` draws everything as SDL_RenderGeometry diamonds (`draw_diamond()`), colored per `TILE_COLOURS`/`BuildingDef` color fields. Sprite-based rendering existed once (docs/README.md's "Phase 6: sprite rendering" section) and was deliberately removed; `src/sprite.c`/`src/sprite.h` are gone from the tree. Don't reintroduce sprites without checking with the user first.

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
- `client.c/h` — the per-frame client update: camera, hover, road drag, and the real-time → fixed-tick pump (this was `game_update()`)
- `command.c/h` — the command funnel: every mutation is a logged `Command`
- `island.c/h` — one island's pipeline; note the ordering constraint in its header
- `ship.c/h`, `faction.c/h`, `feed.c/h` — voyages, the NPC market, the ghost feed
- `net.c/h` — the lockstep protocol (its own SDL-free library); `server/saltmarch_host.c` is the dedicated server over it
- `account.c/h` + `sha256.c/h` — who a connection is entitled to be (AUTH_PLAN Phase 1). A sidecar beside the world holding `sha256(salt || token)`; the sim must never learn accounts exist, so these live in `libsaltmarch_net` and net.c decides identity while the host owns the file. `config.c/h` is the client's half: the tokens servers have issued, under `SDL_GetPrefPath`, keyed by host:port
- `replay.c/h` + `replay_main.c` — record/replay harness and the `saltmarch_replay` CLI
- `simlog.c/h` — `sim_log()`, the sim's SDL-free replacement for `SDL_Log`
- `snapshot.c/h` — full world state as bytes: what a server checkpoint stores and what `MSG_WORLD` sends, so joining costs what the world weighs rather than how long it has existed. Explicit field-by-field encoding, never a struct dump; see its header for what is and is not world state
- `ui_kit.c/h` — layout cursor, widget lists, hit-testing, the `RejectReason`→text table (UI_PLAN Phase 0; SDL-free by construction — layout may never consult font metrics)
- `ui_snapshot.c/h` — the per-frame copy of the world that UI builders read *instead of* `GameState`, so UI code cannot mutate the sim or step its RNG
- `ghost_faction.c/h` — seeds an NPC island by re-addressing a recorded human session (MMO_PLAN later phases); coordinates are snapped, ship-scoped commands dropped
- `scrub_view.c/h` — the F8 time-travel bar; `game_scrub_*` in game.h freezes the sim and refuses submissions while viewing the past
- `intent.h` — the recorded input stream: each click carries the sim tick its frame's snapshot was drawn from, which is what makes replaying it meaningful
- `replay_ui.c` — the record/replay CLI plus the UI harness that re-drives recorded clicks through the real builders and hit-tests
- `fx_reject.c/h` — correlates submitted commands with their results by `{player_id, seq}` and raises a flash at the tile/widget that emitted a rejected one
- `confirm_view.c/h` + `confirm_ui.c/h` — the single confirmation popup; it stores the `Command` it will submit and renders it (`command_describe`)
- `vitals.c/h` — the alert strip's rules, including the sim's own health (F9 result, tick backlog, feed age)
- `inventory_view.c/h` + `inventory_ui.c/h` — the stores overlay (`I`)
- `hud_view.c/h` — the build bar: category tabs, affordability, hit decoding, and the HUD metrics (`HUD_HEIGHT` et al). `ui.c` is now its drawer plus the menu overlay
- `exchange_view.c/h` — the exchange surface: marketplace (`EXCHANGE_QUOTES`) and harbour escrow (`EXCHANGE_OFFER`), one builder, one drawer (`trade_ui.c`): rows, pagination, refusals, hit decoding. `trade_ui.c` is now only its drawer
- `sea_view.c/h` — where everything spatial goes on the world map: routes as paths through their waypoints, waypoints by name, shipments at their real positions, pirate lairs (UI_PLAN N5). SDL-free, so the map's geometry has a headless test; `world_ui.c` is now its drawer and owns no projection of its own
- `yard_view.c/h` + `yard_ui.c/h` — the shipyard (`Y`): the three hulls with their guns/hold/cost trade, and the fleet with condition and convoys (UI_PLAN N6). Guns are `ship_fighting_strength()`, not the class table's number — a damaged hull is worth what it is worth now. No refit: the sim has no command for one
- `chart_view.c/h` + `chart_ui.c/h` — the passages (`C`): the routes out of an island, what this player knows of them, what a map costs on the book and how long that water stays in use (UI_PLAN N4). Reads `Sea` directly — N1's one recorded exception to "the UI sees only the snapshot", because route geometry is generated from the seed. Rows are *retained* like the book's, and for a sharper reason: when a passage retires the pair's variant-1 slot names different water, so a rebuilt row would swap one map for another under the cursor. The expiry countdown and the sim's rotation share `sea_pair_next_rotation()`. Also the home of expeditions (UI_PLAN N7) — one button per destination, whose refusal is computed **in `sim_survey`'s order**, not in order of usefulness, so the button and the sim never say different things
- `book_view.c/h` + `book_ui.c/h` — the order book (`B`): your resting orders and the draft composer. Deliberately NOT a third `ExchangeKind` (UI_PLAN N3); its rows are *retained* across frames, so `book_view_update()` folds (previous view, snapshot) rather than rebuilding — an order that fills stays on screen struck through instead of sliding the rows below it under the cursor
- Overlay convention going forward: a `*_build()` in the SDL-free UI library produces a `UiList`; a `*_draw()` in the client renders that same list; hit-testing queries it. Draw and hit-test can no longer disagree about where a button is

## History / conventions

docs/README.md's "Phase N" sections are a changelog of major feature additions (procedural gen → building placement → resource ticks → population → sprites → sprite removal/zoom) written in past commits — read it before assuming a feature (like sprites) is still in the current build; check `CMakeLists.txt`'s `SALTMARCH_SIM_SOURCES` / `SALTMARCH_CLIENT_SOURCES` lists against `src/` to see what's actually compiled. docs/MMO_PLAN.md's phases are all done (1–6, plus every "later phase"), and its closing section lists what is deliberately not built yet — authentication, account tooling, sharding, and SERVER_AUTHORITY's optional Phase 5; docs/SUPPLY_CHAIN.md's eight phases are all done; docs/SERVER.md covers the dedicated server, including the transport hardening and the snapshot format that bounds join cost; docs/MARITIME_PLAN.md is built (order book, routes and charts, hulls and pirates) and docs/SERVER_AUTHORITY.md is built through Phase 3 (state pushes, local prediction, per-client redaction) with only its optional Phase 5 left; docs/AUTH_PLAN.md is built through Phase 2 (`account.c`/`sha256.c` server-side, `config.c` client-side, `saltmarch_host --accounts`) with TLS and passwords still design; docs/NEEDS_PLAN.md is complete (basics vs luxuries, the happiness ladder, per-resident consumption, and the closure guard in `tests/test_closure.c` that keeps a base tier staffable); docs/LIFE_PLAN.md is built through Phase 7 (multi-worker buildings, residents as named individuals, the calendar, ageing and death, households of ten founded from a per-island settler allowance and grown only by birth, a reserve of unhoused couples who emigrate if nobody roofs them, and gold that enters the world as taxed wages rather than being minted by housing) with Phase 8 (status modifies productivity) and Phase 9 (the wellbeing projection) left. Phase 7b lowered the working age to twelve (adulthood, marriage and childbearing stay at eighteen) and made a house a LINE rather than a tenancy — the eldest child born there inherits it when the elders die — which doubled the working share to ~24% and put the base tier back under the wall at 0.98. **Two things remain unmeasured**: whether a fully-built island funds its own imports (the affordability probe builds a deliberately import-dependent village and is the wrong fixture for it), and anything demographic at all in the determinism gate — `replay_record_demo_session` runs 2000 ticks, six and a half months, so nobody in it ages, marries or inherits; its calendar decision was settled at Phase 4 — the needs tick is the month; docs/VISIBILITY.md and docs/PRIVACY.md are still design notes — what a client is allowed to know, and personal data. docs/UI_PLAN.md is complete: every screen the sim needs now exists. docs/ARCHITECTURE.md explains how the pieces fit.
