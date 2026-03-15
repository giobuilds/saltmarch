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