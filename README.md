# Project Architecture (High Level)
anno1800-clone/
├── src/
│   ├── main.c           ← your skeleton
│   ├── game.c/h         ← game state, main loop logic
│   ├── map.c/h          ← isometric tile map
│   ├── render.c/h       ← all SDL draw calls
│   ├── input.c/h        ← mouse/keyboard handling
│   ├── economy.c/h      ← production chains, resources
│   ├── population.c/h   ← residents, needs, tiers
│   └── ui.c/h           ← HUD, panels, menus
├── assets/
│   ├── tiles/
│   └── sprites/
├── CMakeLists.txt
└── Makefile

# Deliverables

File        Responsibility
main.c      SDL callbacks only — no logic, no globals
map.h/c     40×40 tile grid; TILE_GRASS, WATER, FOREST, SAND
camera.h/c  offset_x/y scroll state; centred on FullHD at init
input.h/c   Tracks held WASD/arrow keys + mouse position
render.h/c  Isometric projection math + coloured diamond drawing
game.h/c    Owns all sub-systems; game_update() drives camera + hover



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