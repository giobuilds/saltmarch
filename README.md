Project Architecture (High Level)
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