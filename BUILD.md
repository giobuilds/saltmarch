# Building Saltmarch on Fedora Linux

## 1. Install dependencies

```bash
sudo dnf install SDL3-devel SDL3_ttf-devel
```

Both are `find_package(... REQUIRED)` in `CMakeLists.txt`, so cmake
fails to configure without either.

**No font package is needed.** Liberation Sans is bundled in
`assets/fonts/` and loaded relative to the executable, so the same build
works on Linux, macOS and Windows. `cmake --build` stages `assets/`
beside the binary automatically — there is no install step.

If SDL3 or SDL3_ttf are not in the Fedora repos for your version, build
from source:

```bash
git clone https://github.com/libsdl-org/SDL.git --branch SDL3 --depth 1
cd SDL
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build

git clone https://github.com/libsdl-org/SDL_ttf.git --branch main --depth 1
cd SDL_ttf
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build
```

## 2. Configure and build

```bash
cd saltmarch           # this project directory
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

For a release build (optimised, no debug symbols):

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## 3. Run

```bash
./build/saltmarch
```

## Controls

### Camera

| Input | Action |
|---|---|
| `W` / Arrow Up | Pan up |
| `S` / Arrow Down | Pan down |
| `A` / Arrow Left | Pan left |
| `D` / Arrow Right | Pan right |
| Mouse wheel | Zoom toward the cursor (0.8x–1.3x) |
| Mouse move | Highlight the tile under the cursor |
| `Escape` | Quit |

### Building

| Input | Action |
|---|---|
| Left click a HUD slot | Select that building (click again to deselect) |
| Left click the map | Place it — opens a confirmation offering resources *or* an equivalent price in Gold |
| Left click + drag | Lay a run of roads. Roads are free and skip the confirmation, since a per-tile prompt would make dragging unusable |
| Right click | Close the topmost overlay; if none is open, deselect |

### Interacting with what you have built

Click a placed building with nothing selected. It must be road-connected
to a Warehouse, or nothing happens.

| Building | Opens |
|---|---|
| Marketplace | Buy/sell goods for Gold |
| House | Upgrade to a Worker's House (which then also needs Beer) |
| Shipyard | Build a ship |

### Bottom-right buttons

| Button | Action |
|---|---|
| Islands | Archipelago overview: switch island, move ships, colonise, set trade routes |
| Red X | Demolish tool — click a building to destroy it, with confirmation |
| Cog | Menu: New Game / Load / Save / Quit |

## Troubleshooting

- **"SDL3 not found" / "SDL3_ttf not found"** – install `SDL3-devel` and
  `SDL3_ttf-devel`, then re-run cmake. Delete `build/` first if cmake has
  already cached a failed configure.
- **No text anywhere, `Fonts unavailable` in the log** – the bundled
  font was not found. It should sit at `assets/fonts/` next to the
  executable; a plain `cmake --build` puts it there. If you moved the
  binary, move `assets/` with it. The log names every location searched.
- **Black screen** – check `SDL_Log` output in the terminal; renderer
  errors are printed there.
- **Segfault at startup** – run under `gdb ./build/saltmarch` and check
  the backtrace with `bt`.
- **A building sits idle and produces nothing** – it needs all three of:
  a road path to a Warehouse (disconnected buildings are outlined red), a
  worker physically present, and every input in stock. The log says which
  is missing.
