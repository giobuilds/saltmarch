# Saltmarch

An isometric city builder written from scratch in C99 with SDL3. Procedural archipelago generation, road logistics, production
chains, a walking population that must physically staff its workplaces,
ship colonisation and automated trade routes.

Deliberately dependency-light and educational: no sprite system, no UI
toolkit, no game engine. Everything is drawn with flat-shaded
`SDL_RenderGeometry` diamonds, rectangles and lines, plus SDL_ttf text.
Source files carry long header comments explaining *why* each subsystem
works the way it does — several record bugs that shaped the design.

## Build & run

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
./build/saltmarch
```

Requires `SDL3` and `SDL3_ttf` (see `BUILD.md`). Builds with
`-Wall -Wextra -Wpedantic -Wshadow -Wconversion`; **zero warnings is the
bar** — treat any new one as a bug, not noise.

There is no test framework. Verification is a clean build, running the
binary, and throwaway headless C programs that link the built `.o` files
and assert real behaviour (see *Verification* below).

## Controls

| Input | Action |
|---|---|
| `W A S D` / arrow keys | Pan camera |
| Mouse wheel | Zoom toward cursor (0.8x–1.3x) |
| Left click (HUD slot) | Select / deselect a building to place |
| Left click (map) | Place selected building — opens a cost confirmation |
| Left click + drag | Place a run of roads (roads skip the confirmation) |
| Left click (placed building) | Interact: Marketplace → trade, House → upgrade, Shipyard → build ship |
| Right click | Close the topmost overlay, else deselect |
| Bottom-right buttons | Archipelago map · Demolish tool · Menu |
| `Escape` | Quit |

## Architecture

```
saltmarch/
├── src/
│   ├── main.c                 SDL3 callbacks, click cascade, render order
│   ├── game.c/h               GameState: archipelago, overlays, save/load
│   ├── island.c/h             One island: map, economy, population
│   ├── map.c/h                Tile grid + profile-driven generation
│   ├── camera.c/h             Pan/zoom (also defines SCREEN_W/H)
│   ├── input.c/h              Keys, mouse, held-button drag state
│   ├── render.c/h             Isometric projection and all world drawing
│   ├── building.c/h           BuildingDef table, placement, costs
│   ├── resource.c/h           ResourceType, Stockpile, prices
│   ├── population.c/h         PopData, per-tier needs
│   ├── agent.c/h              Walking residents, labour supply
│   ├── connectivity.c/h       Road-network BFS and pathfinding
│   ├── ship.c/h               Voyages, cargo, trade routes
│   ├── fonts.c/h              SDL_ttf wrapper
│   ├── ui.c/h                 HUD bar and menu overlay
│   ├── trade_ui.c/h           Marketplace buy/sell screen
│   ├── world_ui.c/h           Archipelago overview and ship control
│   ├── build_confirm_ui.c/h   Cost confirmation (resources or Gold)
│   ├── demolish_confirm_ui.c/h  "Destroy this building?"
│   └── tier_upgrade_ui.c/h    Spend-Gold-to-confirm (upgrade, build ship)
├── CMakeLists.txt
├── BUILD.md
└── UI_PLAN.md                 Planned UI reorganisation (not yet started)
```

### Key design decisions

**`GameState` owns the archipelago; `Island` owns everything per-landmass.**
Map, camera, stockpile, buildings, population and agents all live in
`Island` (`island.h`). `GameState` keeps only what is genuinely global:
input, timing, which island is being viewed, and the UI overlay flags.
`game_cur_island()` is the single accessor; `game_set_current_island()`
closes every overlay on switch, because every `*_idx` field in
`GameState` is current-island-relative.

**Per-island stockpiles are load-bearing.** Goods do not teleport between
islands. A Malthouse can only consume Grain and Hops stored on its *own*
island, and cargo in a ship's hold belongs to no stockpile at all. That
invariant is what makes trade routes mean something, and it is asserted
directly: world totals may only change through production or consumption,
never through movement.

**Every settled island simulates every frame**, not just the visible one
(`island_update`). Its header documents an ordering constraint worth
reading before touching it: `connectivity.c` keeps its BFS scratch in file
statics, so each island's pipeline must complete before the next begins.

**Production is gated three ways.** A building ticks only if it is
road-connected to a Warehouse (`connectivity.c`), has a worker physically
present (`agent.c`), and has *every* input in stock — multi-input recipes
are all-or-nothing, so a Malthouse never consumes Grain while short of
Hops.

**Islands differ by design.** `MapProfile` (`map.h`) drives generation
thresholds, and `map_init()` validates the result against the profile's
minimum-resource contract, reseeding until it holds. The home island
grows **no hops at all** — that scarcity is the reason to build ships and
colonise, not an accident. `map->seed` stores the *requested* seed, not
the working one, so saves reproduce exactly.

**Overlays follow one pattern** (`*_ui.c`): sizing `#define`s, geometry
helpers derived from `panel_rect()`, and fully independent `draw` and
`hit_test` that both recompute from constants. Nothing is retained across
frames, which is what makes hit-testing headlessly testable.

### Verification

No test framework and no `xdotool`, so:

1. Clean rebuild with the full warning set — zero warnings.
2. Run the binary; confirm it starts and does not crash.
3. Write a throwaway C program that links the built `.o` files, builds a
   scenario by hand, and asserts **real behaviour** — "the Brewery
   actually produced Beer", not "the enum has the right value". Two of
   this project's more subtle bugs were caught only this way.
4. Anything genuinely visual needs a human at the keyboard.

---

# Changelog

Phase numbering restarts per feature effort; the groupings below are the
honest sequence.

## Foundations

### Phase 2: procedural island generator with value noise

- Expand map from 40x40 to 64x64 tiles
- Add Fertility bitmask and buildable/movement_cost fields to Tile
- Implement LCG RNG and two-octave value noise island generator
- Apply radial island mask to force ocean at map edges
- Seed map from SDL_GetTicksNS() for a different island each launch
- Fix delta-time camera panning (frame-rate independent, 400px/sec)
- Fix NULL undeclared error in map.c (add stddef.h)
- Fix frustum cull to use SCREEN_W/H constants

### Phase 3: building placement with HUD and menu overlay

- Add BuildingDef/Building system with 4 building types
  (Fisher's Hut, Warehouse, Farm, Lumberjack)
- Placement validation: bounds, buildable tiles, fertility,
  coast and forest adjacency checks
- Ghost preview: green = valid, red = invalid placement
- HUD bar with building slots, footprint dot grid, cog button
- Cog opens menu overlay with New Game (stub), Save (stub), Quit
- Fix mouse coordinate conversion via SDL_RenderCoordinatesFromWindow
- Add fullscreen mode with SDL_LOGICAL_PRESENTATION_STRETCH
- Right-click deselects building / closes menu

### Bug fix 001

Fix isometric tile hit-test accuracy in screen_to_iso()

- Offset input point to diamond centroid before inverting projection
  (was testing against top-left corner of bounding box, not tile centre)
- Replace (int) cast with floorf() for correct negative coordinate rounding
- Add math.h include and link libm for floorf()

### Phase 4: resource system with production ticks and stockpile HUD

- Add ResourceType enum (Wood, Fish, Grain, Gold) and Stockpile struct
- BuildingDef gains produces/consumes/tick_seconds fields
- Building instance gains float timer for production accumulation
- game_tick_buildings() fires per-frame: consumes input, adds output,
  idles if input stock insufficient
- render_resources() draws segmented bar panel top-left (10 segs = 100 units)
- Production rates: Lumberjack 5s, Fisher's Hut 6s, Farm 8s
- RES_COUNT used as sentinel for "no resource" in building definitions
- Add resource.c to CMakeLists.txt

### Phase 5: SDL_ttf text rendering and population system

- Add fonts.h/c wrapping SDL_ttf (Liberation Sans, 14pt/11pt)
- Real text in resource panel, HUD tooltips, menu labels and title
- Add PopData struct with per-house residents, timer, happiness
- House building type (1x1, any land, 10 residents capacity)
- Needs tick every 30s: Fish AND Grain required
- Needs met: consume 1 each, generate 2 gold per resident, grow
- Needs unmet: lose 1 resident per tick
- Population counter top-right (Pop: N)
- Link SDL3_ttf in CMakeLists.txt

### Phase 6: sprite rendering from original spritesheets

- Add sprite.h/c: loads terrain/building textures from 3 spritesheet files
- Extract tiles by grid index at startup (no pre-processing needed)
- Teal/black/magenta background removal via per-pixel alpha walk before scaling
- Building composite: left wall + right wall + per-building roof tile
- Roof index per building: house=0, farm=1, warehouse=2, fishers_hut=8, lumberjack=3
- TILE_W/H updated to 256x128 to match spritesheet tile dimensions
- render_map() uses SDL_RenderTexture(), falls back to diamonds if sprites missing
- render_buildings() anchors building base to diamond bottom (sy + TILE_H)
- camera_init() offset_y adjusted for new tile height
- Link SDL3_image in CMakeLists.txt
- Fix SDL3 API: SDL_MapRGB requires SDL_GetPixelFormatDetails(surf->format)

### Sprite removal

Phase 6 was reverted: `sprite.c/h`, the spritesheet assets and the
SDL3_image dependency were all deleted, returning to flat-shaded diamond
rendering. Zoom (0.8x–1.3x, cursor-anchored) was added in the same pass.
The `assets/` tree no longer exists.

### Menus, tooltips and save/load

- Wire up New Game and Save (previously logging stubs); add Load
- Real text for menu labels, title and HUD hover tooltips
- Binary save format: map seed, buildings, population, stockpile, camera —
  the tile grid is never written, since `map_init(seed)` regenerates it
- Warehouses raise the per-resource storage cap; `Tile.movement_cost` and
  unused fertility bits deleted as dead data

## Economy, roads and labour

- **Starting Gold and building costs.** 1000 Gold; `BuildingDef.cost[]`.
  Raw producers are Gold-only, because the stockpile starts empty and
  requiring Wood up front would be unwinnable.
- **Roads** as a placeable `BuildingType` (raising `MAX_BUILDINGS` to
  600), later made free and drag-placeable.
- **Road connectivity gating.** Multi-source BFS from every Warehouse;
  disconnected buildings produce nothing and are outlined red.
- **Marketplace and manual trade screen**, later extended with buying —
  which doubles as the guaranteed safety valve when terrain lacks a good.
- **Population agents with real labour supply.** One agent per resident,
  pathfinding along roads, and a producer only ticks while a worker is
  physically `AGENT_WORKING` there.
- **Build-confirmation popup** offering resources *or* a Gold-equivalent
  price, **demolish tool** with confirmation, and starter houses removed
  so the player places their own.

## Production chains and population tiers

- **Multi-input production**: `BuildingDef.consumes[]` becomes an array,
  all-or-nothing.
- **The Beer chain**: Hop Farm → Malthouse (Grain + Hops) → Brewery,
  giving the long-dormant `FERTILE_HOP` bit a consumer at last.
- **Population tiers**: needs become a data-driven per-tier table, and a
  House can be upgraded to a Worker's House, which additionally needs Beer.
- **HUD filtering** (`hud_placeable`) so buildings reached through
  gameplay rather than the bar leave no gap in it.

## Islands, colonisation and trade

- **Phase 0 — enum-audit fixes.** Three defects from the production-chains
  work, all one root cause: growing an enum without auditing the sites
  that enumerated the old one. Hops/Malt/Beer were never actually
  tradeable; the resource panel's colour table was positional and had
  four entries for seven resources; and `agents_sync` silently stopped
  managing agents for upgraded houses.
- **Phase 1 — the `Island` refactor.** Per-island map, stockpile,
  buildings, population and agents. Behaviour deliberately unchanged.
  Save format v2, and `game_load` made genuinely atomic (it previously
  cleared state *before* reading and could half-clobber the world on a
  truncated file, despite its doc comment promising otherwise).
- **Phase 2 — resource profiles and the archipelago overview.** Four
  islands — Saltford, Brinehold, Tidefast and Marrowbay, on
  temperate / highland / woodland / atoll terrain — with a
  validate-and-reseed generator, plus a world-map overlay. Measured: the
  home island Saltford offers **zero** placeable Hop Farm sites, while
  Brinehold offers hundreds.
- **Phase 3 — shipyards, ships and colonisation.** Save format v3. Cargo
  in transit belongs to no stockpile. Acceptance test: a colony produced
  Hops while the home stockpile stayed bit-for-bit unchanged.
- **Phase 4 — automated trade routes.** A ship repeats a voyage,
  unloading and reloading on arrival. Departure is unconditional, since
  waiting for cargo would deadlock a route the first time supply ran dry.
  World conservation asserted across 1200 simulated steps.

## Known gaps

- **The trade screen is the nearest UI cliff** — it overflows the window
  at 10 tradeable goods and there are 6. See `UI_PLAN.md`.
- **Mouse wheel is not overlay-aware**: scrolling over an open modal
  zooms the world behind it.
- **`building_can_place()`'s `reason` string is never shown** — every
  caller passes `NULL`.
- **Marrowbay generates more farmland than its atoll flavour implies**; nothing
  depends on it, but it reads oddly.
- Scarcity currently constrains only Hops and Wood. Fish and Grain are
  available on every island.
