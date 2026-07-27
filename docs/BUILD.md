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

A build produces:

| Target | What it is |
|---|---|
| `saltmarch` | the game (SDL3, SDL3_ttf, the bundled font) |
| `libsaltmarch_sim.a` | the simulation as a static library — no SDL |
| `libsaltmarch_net.a` | the lockstep protocol, shared by the game and the server — no SDL |
| `saltmarch_replay` | headless CLI over the sim: replay a `.smlog`, print the hash, exit 0/1 |
| `saltmarch_host` | the persistent server (see [SERVER.md](SERVER.md)) |

## 3. Run

```bash
./build/saltmarch
```

Headless, with no display and no SDL runtime involved:

```bash
./build/saltmarch_replay --record fixture.smlog --seed 12345
./build/saltmarch_replay --replay fixture.smlog
```

A session's clicks can be recorded and replayed through the real UI:

```bash
./build/saltmarch_replay --record-ui ui.smlog --seed 777
./build/saltmarch_replay --replay ui.smlog --verify-ui
```

Each recorded click carries the sim tick its frame was drawn from, so the
harness rebuilds exactly the screen the player saw, hit-tests the same
position, and checks that the same command falls out. `--dump-ui FILE`
writes the widget lists as text for eyeballing or diffing.

`--replay` rebuilds the world twice from (seed + command log) and compares
state hashes, so a non-zero exit means the simulation went
nondeterministic. `--expect-hash <hex>` additionally pins the result to a
known value. This is what CI runs on Linux, macOS and Windows.

Multiplayer, all three shapes:

```bash
./build/saltmarch --host 7777              # host a friend
./build/saltmarch --join 1.2.3.4:7777      # join a host or a server
./build/saltmarch_host --world world.smlog # a world that keeps ticking
```

## Controls

### Camera

| Input | Action |
|---|---|
| `W` / Arrow Up | Pan up |
| `S` / Arrow Down | Pan down |
| `A` / Arrow Left | Pan left |
| `D` / Arrow Right | Pan right |
| Mouse wheel | Zoom toward the cursor (0.8x–1.3x); ignored while an overlay is open |
| Mouse move | Highlight the tile under the cursor |
| `‹` `›` header (top centre) | Step between the islands you have settled |
| `I` | Open the stores overlay (everything this island holds) |
| `F8` | Time-travel scrubber: freeze the world and re-simulate to any past tick |
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
