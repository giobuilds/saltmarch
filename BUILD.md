# Building Anno Clone on Fedora Linux

## 1. Install dependencies

```bash
sudo dnf install SDL3-devel SDL3_ttf-devel liberation-sans-fonts
```

All three are required:

- **SDL3** and **SDL3_ttf** — both are `find_package(... REQUIRED)` in
  `CMakeLists.txt`, so cmake fails to configure without either.
- **liberation-sans-fonts** — a *runtime* dependency. `fonts.h` hardcodes
  `/usr/share/fonts/liberation-sans-fonts/LiberationSans-Regular.ttf`.
  Without it the game still builds and runs, but logs
  `Warning: fonts unavailable` and draws no text at all — which makes it
  effectively unusable, since resource counts, prices and every menu
  label are text.

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
cd anno_clone          # this project directory
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
./build/anno_clone
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
- **No text anywhere, `Warning: fonts unavailable` in the log** – install
  `liberation-sans-fonts`. The path is hardcoded in `src/fonts.h`; edit
  `FONT_PATH` if your distribution puts it elsewhere.
- **Black screen** – check `SDL_Log` output in the terminal; renderer
  errors are printed there.
- **Segfault at startup** – run under `gdb ./build/anno_clone` and check
  the backtrace with `bt`.
- **A building sits idle and produces nothing** – it needs all three of:
  a road path to a Warehouse (disconnected buildings are outlined red), a
  worker physically present, and every input in stock. The log says which
  is missing.
