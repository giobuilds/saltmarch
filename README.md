to build
cmake -B build -DCMAKE_BUILD_TYPE=debug
cmake --build build -j$(nproc)
./build/anno_clone

# Project Architecture (High Level)
anno1800-clone/
├── src/
│   ├── main.c
│   ├── game.c/h
│   ├── map.c/h
│   ├── camera.c/h
│   ├── input.c/h
│   ├── render.c/h
│   ├── building.c/h
│   ├── resource.c/h
│   └── ui.c/h
├── assets/
│   ├── tiles/
│   └── sprites/
├── CMakeLists.txt
└── BUILD.md

# Deliverables

*File*            *Responsibility*
_main.c_          SDL3 callbacks only (AppInit, AppEvent, AppIterate, AppQuit). Creates window, wires all subsystems together, owns the render order. No game logic.
_map.h/c_         64×64 isometric tile grid. TileType enum, Tile struct (type, elevation, buildable, fertility, movement_cost). Procedural island generator using two-octave value noise + radial island mask + LCG RNG.
_camera.h/c_      Camera struct (offset_x/y). camera_init() centres the map on screen. Pan speed constant CAMERA_PAN_SPEED (pixels/sec, frame-rate independent).
_input.h/c_       InputState struct. Tracks held WASD/arrow keys, raw mouse coords, converted logical coords (logical_x/y), and single-frame click events (left_click, right_click). input_clear_clicks() resets after consumption.
_render.h/c_      All SDL draw calls. Isometric projection math (iso_to_screen, screen_to_iso with floorf fix). Draws tile map, placed buildings, placement ghost (green/red), hover outline, resource stockpile panel (top-left segmented bars).
_game.h/c_        Top-level GameState struct that owns every subsystem. game_init(), game_update() (delta time, camera pan, mouse conversion, building ticks), game_place_building().
_building.h/c_    BuildingDef static table (name, footprint, placement rules, production fields). Building instance struct (type, position, active flag, production timer). Placement validation (bounds, buildable, fertility, coast/forest adjacency). building_place().
_resource.h/c_    ResourceType enum (WOOD, FISH, GRAIN, GOLD). Stockpile struct (int amount[RES_COUNT]). stockpile_add() with zero clamp. RES_COUNT used as sentinel for "no resource".
_ui.h/c_          HUD bar (building slots with colour swatch + footprint dot grid, cog button with pixel-art icon). Menu overlay (dimmed background, panel, New Game/Save/Quit buttons). All hit-testing functions. Tooltip stub ready for SDL_ttf.
_CMakeLists.txt_  CMake build config. C99, -Wall -Wextra -Wpedantic -Wshadow -Wconversion. Links SDL3::SDL3 and m (libm for floorf).
_BUILD.md_        Build and run instructions for Fedora. SDL3 install, cmake commands, controls reference.


# Phase 2: procedural island generator with value noise

- Expand map from 40x40 to 64x64 tiles
- Add Fertility bitmask and buildable/movement_cost fields to Tile
- Implement LCG RNG and two-octave value noise island generator
- Apply radial island mask to force ocean at map edges
- Seed map from SDL_GetTicksNS() for a different island each launch
- Fix delta-time camera panning (frame-rate independent, 400px/sec)
- Fix NULL undeclared error in map.c (add stddef.h)
- Fix frustum cull to use SCREEN_W/H constants

# Phase 3: building placement with HUD and menu overlay

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

# Bug fix 001

Fix isometric tile hit-test accuracy in screen_to_iso()

- Offset input point to diamond centroid before inverting projection
  (was testing against top-left corner of bounding box, not tile centre)
- Replace (int) cast with floorf() for correct negative coordinate rounding
- Add math.h include and link libm for floorf()

# Phase 4: resource system with production ticks and stockpile HUD

- Add ResourceType enum (Wood, Fish, Grain, Gold) and Stockpile struct
- BuildingDef gains produces/consumes/tick_seconds fields
- Building instance gains float timer for production accumulation
- game_tick_buildings() fires per-frame: consumes input, adds output,
  idles if input stock insufficient
- render_resources() draws segmented bar panel top-left (10 segs = 100 units)
- Production rates: Lumberjack 5s, Fisher's Hut 6s, Farm 8s
- RES_COUNT used as sentinel for "no resource" in building definitions
- Add resource.c to CMakeLists.txt

# Phase 5: SDL_ttf text rendering and population system

- Add fonts.h/c wrapping SDL_ttf (Liberation Sans, 14pt/11pt)
- Real text in resource panel, HUD tooltips, menu labels and title
- Add PopData struct with per-house residents, timer, happiness
- House building type (1x1, any land, 10 residents capacity)
- Needs tick every 30s: Fish AND Grain required
- Needs met: consume 1 each, generate 2 gold per resident, grow
- Needs unmet: lose 1 resident per tick
- Population counter top-right (Pop: N)
- Link SDL3_ttf in CMakeLists.txt

# Phase 6: sprite rendering from original spritesheets

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