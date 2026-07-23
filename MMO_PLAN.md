# Saltmarch MMO Plan — Deterministic Shared-Ocean Architecture

Implementation plan for evolving Saltmarch from a single-player city-builder
into an MMO-style shared-ocean game: private player islands, one shared ocean
of ships, colonisable islands, NPC factions, other human players. No fog of
war. This document is written to be executed phase by phase by an AI coding
agent (Claude Opus) or a human; each phase is independently shippable and
verifiable by building and running the game.

## Architecture in one paragraph

The world is a **pure function of (world seed, ordered command log)**. Every
state mutation flows through a single `sim_apply(Command)` funnel; the
simulation advances in **fixed integer ticks**, never variable `delta_time`;
therefore any client can reproduce the entire world by replaying the log, and
"multiplayer" is nothing more than agreeing on the log's order. Islands stay
private single-player instances — **only ships cross the wire**. The shared
ocean is an append-only feed of voyage records that every client renders
identically. NPC factions are just other feed participants (a cron job is
indistinguishable from a slow player). A server, when one finally exists, is
Saltmarch's own sim compiled headless behind `extern "C"`, hosted by a thin
C++ process — optionally on Carbon's `scheduler` (github.com/carbonengine,
MIT) with one greenlet per island. This is EVE Online's architecture
(deterministic single-shard command replay) at solo-dev scale, and Saltmarch
is pre-adapted to it: maps are already seed-deterministic and the save file
is already tiny.

## Ground rules (read before every phase)

1. **Respect CLAUDE.md.** Subsystem pattern (init/free + update/render pairs
   wired in main.c), no game logic in main.c, all conversions through
   `iso_to_screen()`/`screen_to_iso()`, text through fonts.c.
2. **Warnings are bugs.** `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`
   must stay clean on GCC/Clang and `/W4` on MSVC (see the CI jobs on the
   `ci-macos-windows` branch). Expect `-Wconversion` friction when
   integer-ising float timers — fix properly, never cast-to-silence blindly.
3. **The sim may not touch SDL.** Anything that will live in the future
   headless sim library (`sim_*`, island/building/population/agent/ship/
   resource logic) must compile without `SDL3/SDL.h`. Rendering, input, and
   UI keep SDL. Where sim files currently include SDL headers only for types
   like `Uint64`, migrate to `<stdint.h>` types.
4. **Preserve the island pipeline ordering constraint.** connectivity.c keeps
   BFS scratch in file statics; each island's `island_update()` pipeline must
   run to completion before the next island's begins. Do not interleave.
5. **Never let a mutation bypass the command funnel** once Phase 1 lands. If
   a new feature mutates world state, it does so by emitting a `Command`.
   This is the invariant the whole architecture stands on.
6. **One phase per branch/PR.** Each phase ends with the game building
   cleanly and running correctly. Do not start phase N+1 with phase N red.

## Determinism doctrine

- **World time is a tick counter.** `TICK_MS = 100` (10 ticks/sec),
  `uint64_t sim_tick_no`. Wall clock exists only at the edges: the frame
  loop converts elapsed real time into "how many ticks to run", and the
  future server converts wall-clock offline time into "how many ticks to
  catch up". Inside the sim, nothing reads a clock.
- **Timers are integer tick counts.** `Building.timer` (float seconds),
  PopData's needs timer, agent shift/rest/assign timers, ship voyage
  progress — all become `uint32_t` ticks remaining/elapsed. `BuildingDef.
  tick_seconds` gains a derived `tick_count = seconds * 10`.
- **Rendering interpolates; the sim never does.** Smooth ship motion and
  agent walking are the renderer lerping between tick states. Floats are
  fine in render.c; they are suspect in the sim.
- **Agents are derived state but sim-relevant** (worker presence gates
  production), so agent updates must also be tick-driven and deterministic.
  Agent `row/col` positions may remain floats *initially* (same-machine
  replay tolerates it since replay uses identical code and inputs); the F9
  detector and CI replay harness (below) will tell you if and where
  cross-platform float divergence actually bites. If it does, migrate agent
  positions to Q16.16 fixed point (`int32_t`, 16 fractional bits) at that
  point — do not do this speculatively.
- **Iteration order is part of determinism.** Never iterate a hash-order or
  pointer-order container in the sim. Arrays indexed 0..count, always.
- **RNG:** the sim's LCG must be stepped only from within `sim_tick()`/
  `sim_apply()`, never from render or UI code. Audit call sites.

---

## Phase 1 — Command funnel, fixed timestep, F9 desync detector

**Goal:** every mutation flows through `sim_apply()`; the sim runs on fixed
ticks; the game can prove its own determinism at the press of a key. No
networking. This phase is the gate for everything else.

### 1a. Command type and log

New files `src/command.h` / `src/command.c`:

```c
typedef enum {
    CMD_PLACE_BUILDING,   /* island, row, col, type, pay_with_gold */
    CMD_PLACE_ROAD,       /* island, row, col */
    CMD_DEMOLISH,         /* island, building idx */
    CMD_SELL_RESOURCE,    /* island, res, qty */
    CMD_BUY_RESOURCE,     /* island, res, qty */
    CMD_UPGRADE_HOUSE,    /* island, building idx */
    CMD_BUILD_SHIP,       /* island, shipyard idx */
    CMD_SHIP_TRANSFER,    /* ship idx, res, qty (sign = load/unload) */
    CMD_SHIP_DEPART,      /* ship idx, dest island */
    CMD_COLONISE,         /* ship idx, island idx */
    CMD_COUNT
} CommandKind;

typedef struct {
    uint64_t    tick;      /* sim tick at which this command applies */
    uint32_t    player_id; /* 0 for now; becomes identity in Phase 5 */
    CommandKind kind;
    int32_t     a, b, c, d; /* payload, meaning per kind */
} Command;
```

- `GameState` gains `Command *cmd_log; int cmd_count; int cmd_cap;`
  (grow-by-doubling heap array; freed in `game_free`).
- `int sim_apply(GameState *gs, const Command *c)` validates and executes.
  It is the ONLY place the existing mutation functions get called from.
  Returns 0 and does nothing on invalid commands (a replayed log must
  tolerate commands that fail validation identically each time).
- Existing entry points (`game_place_building_confirmed`,
  `game_try_place_road`, `game_demolish_building`, `game_sell_resource`,
  `game_buy_resource`, `game_upgrade_house`, `game_build_ship`,
  `game_ship_transfer`, `game_colonise`, and world_ui's ship-depart order)
  are split: the **UI-facing function** builds a `Command` stamped with the
  *next* tick and calls `command_submit(gs, &c)` (which appends to the log
  and applies at the tick boundary); the **sim-side body** moves into
  `sim_apply`'s dispatch. Commands are applied at tick boundaries, before
  that tick's update, in log order.
- Watch for hidden mutation paths: the road *drag* placement loop, the
  confirm-popup flows in build_confirm_ui.c / demolish_confirm_ui.c /
  tier_upgrade_ui.c / trade_ui.c — all must end in a submitted Command, not
  a direct call.

### 1b. Fixed timestep

- In `game_update()`, replace per-frame sim advancement with an
  accumulator: `acc_ms += delta_ms; while (acc_ms >= TICK_MS) {
  sim_run_one_tick(gs); acc_ms -= TICK_MS; }`. Camera pan, hover, UI, and
  everything cosmetic stay per-frame on `delta_time`.
- `sim_run_one_tick(gs)`: (1) apply all logged commands stamped for this
  tick, in order; (2) for each settled island, run the full
  `island_update()` pipeline converted to one tick (respect ordering
  constraint); (3) `ships_update()` for one tick; (4) `sim_tick_no++`.
- Convert timers to integer ticks (see doctrine). `island_update(isl, dt)`
  loses its `dt` parameter — it advances exactly one tick.

### 1c. State hash and F9 replay check

- `uint64_t sim_hash(const GameState *gs)`: FNV-1a over, per island:
  `stockpile.amount[]`, each active building's `{type,row,col,timer,
  worker-relevant fields}`, each active PopData's `{residents,happy,timer}`;
  then each ship's `{active,at/from/to,progress_ticks,cargo[]}`; then
  `sim_tick_no`. Exclude agents (derived), cameras, UI state.
- F9 debug key: allocate a scratch `GameState`, re-init from the same world
  seeds, replay `cmd_log` through the same `sim_run_one_tick` loop up to
  `sim_tick_no`, compare hashes. Draw `REPLAY: PASS` / `REPLAY: DESYNC @
  tick N` via `font_draw_text()` (bisect: hash every K ticks during replay
  and report the first mismatching interval).
- Every DESYNC is a real bug: an unrouted mutation, a float accumulator, or
  RNG stepped outside the sim. Fix until five minutes of varied play
  (place, demolish, trade, sail, colonise) passes F9 reliably.

### 1d. Save format v2 and replay files

- `game_save` writes: version, world seeds, `sim_tick_no`, command log.
  (Keep the old full-state path available behind the same version check for
  one release; loading v1 saves stays supported or is explicitly dropped —
  decide and document in the file header.)
- `game_load` = init from seeds + replay log. Loading IS the F9 test.
- Add a headless-adjacent CLI hook now if cheap, else defer to Phase 6:
  `./saltmarch --replay file.smlog --expect-hash H` exits 0/1. Wire it into
  the existing CI workflows on Linux/macOS/Windows — **cross-platform CI
  replay of one recorded session is the determinism fuzzer** and the
  project's first regression test.

**Exit criteria:** clean build all platforms; F9 passes after varied play;
save/load round-trips through replay; a recorded `.smlog` replays to the
same hash in CI on at least Linux + one other OS.

---

## Phase 2 — Voyages as timestamps (the wire format)

**Goal:** a voyage is fully described by an immutable record fixed at
departure — the exact shape that will later be published to the shared feed.

- `Ship` gains `uint64_t departure_tick;` set on depart. `progress` becomes
  a derived value: `(sim_tick_no - departure_tick) / (float)voyage_ticks`,
  computed where needed; keep the float field as a cached derivation so
  world_ui.c needs no change. Arrival check is integer: `sim_tick_no >=
  departure_tick + voyage_ticks`.
- Define `VoyageRecord { player_id, ship_id, from, to, departure_tick,
  cargo[RES_COUNT] }` in ship.h with comment: THIS IS THE WIRE FORMAT.
  Serialisation: one JSON line (write a tiny hand-rolled serialiser; no
  dependency).
- **Verify:** ships move identically to before; save mid-voyage, load,
  ship arrives at the correct tick; F9 still passes.

---

## Phase 3 — The NPC faction floor (elastic market)

**Goal:** kill the infinite-liquidity fixed price tables. One NPC faction
becomes a real counterparty with finite gold, real inventory, and elastic
prices. Works with one player; substrate for charters and insurance later.

- New `src/faction.c/h` (add to CMake SOURCES), subsystem pattern:

```c
typedef struct {
    int32_t gold;
    int32_t inventory[RES_COUNT];
    int32_t reservation[RES_COUNT]; /* secret target price, design knob */
} Faction;
int faction_bid(const Faction *f, ResourceType r); /* they buy at */
int faction_ask(const Faction *f, ResourceType r); /* they sell at */
void faction_update_tick(Faction *f);              /* slow mean reversion */
```

- Quote curve: price falls toward `reservation` as `inventory[r]` rises
  above a baseline, recovers as it drains. **Calibrate so that at starting
  inventory the quotes exactly reproduce today's `SELL_PRICE`/`BUY_PRICE`**
  — day one is behaviour-neutral.
- Reroute the three hardcoded call sites — `game_sell_resource`,
  `game_buy_resource`, `building_gold_equivalent_cost` — plus trade_ui.c's
  price labels through `faction_bid/ask`. Every trade moves faction
  inventory and gold in the opposite direction of the player's.
  **Conservation invariant** (assert in debug): player gold + faction gold
  is constant across any trade.
- Faction state is world sim state: it lives in `GameState`, is hashed by
  `sim_hash`, mutates only inside `sim_apply`/`sim_run_one_tick`, and its
  mean reversion is tick-driven. F9 must still pass.
- Debug overlay (menu or key): chart faction inventory/gold/quotes over
  time; reservation prices live-editable. This is the economy test harness.
- **Verify:** Sell-Max fish repeatedly → bid visibly steps down; wait → it
  recovers; faction can run out of gold and refuses to buy (message in
  trade UI); F9 passes; CI replay passes.

**Deferred by design:** port charters (needs the floor + an upkeep timer;
add once the floor is tuned), insurance (meaningless until ships can be
lost — do not build it before a loss mechanic exists), multiple factions
and scrip.

---

## Phase 4 — The shared feed: ghost multiplayer and NPC cron factions

**Goal:** every client publishes its voyages and renders everyone else's as
non-interactive ghosts. Full feed plumbing shipped before any interaction
rules. Islands remain fully private.

- **Feed = append-only JSONL file** of `VoyageRecord` lines plus a
  `player_id` handshake line. Client behaviour: on its own ship's departure,
  append to `feed_out.jsonl`; every ~30 s (wall clock, cosmetic layer — NOT
  sim state) re-read `feed_in.jsonl` and rebuild the ghost list.
- Transport is deliberately out of process: a shell script (`scripts/
  feedsync.sh`) using curl against any dumb HTTP endpoint (or rsync, or a
  shared folder) merges `feed_out` up and `feed_in` down. The game itself
  performs zero networking — keeps SDL client pure and the transport
  swappable.
- **Ghost time mapping:** ghosts are cosmetic and rendered from wall-clock
  timestamps in the feed (`departure_unix_ms` added to the record).
  Ghost voyages never enter `sim_hash`. The mapping between a peer's
  `departure_tick` and shared wall time is: publish both; render from wall
  time. Real interaction (Phase 5+) will need agreed ticks; ghosts don't.
- **NPC faction as a feed participant:** `scripts/npc_compact.sh` (or a tiny
  C tool reusing the serialiser) appends scripted voyages on a timer with a
  personality (regular trade runs; a colonisation announcement when an
  island stays unclaimed). This proves "a bot is indistinguishable from a
  slow player" and gives a populated ocean to develop against.
- world_ui.c: draw ghost voyages in a distinct muted style with owner name;
  hit-testing on ghosts shows an info tooltip only.
- **Verify:** run two instances of the game against a shared folder; each
  sees the other's ships crossing the world map within one poll interval;
  killing the sync script degrades gracefully (stale ghosts, no crash);
  F9/CI unaffected (ghosts are outside the sim).

---

## Phase 5 — First real interaction: lockstep two-player co-op

**Goal:** two humans in one deterministic world — the proof that the Phase 1
architecture was the multiplayer. Raw TCP, host-ordered ticks, few hundred
lines. Carbon still not required.

- Model: both clients run the identical sim. The host is the ordering
  authority: clients send submitted Commands to the host; the host stamps
  them with a tick, broadcasts the ordered stream; both sides apply
  identically. Periodic `sim_hash` exchange; mismatch = the guest resyncs
  by requesting the host's log tail and replaying (never state-patching).
- `player_id` becomes real: island ownership recorded at colonisation;
  `sim_apply` rejects commands whose `player_id` doesn't own the target
  island/ship. Two players therefore CANNOT edit each other's islands —
  privacy by validation, not by hiding state.
- Inter-player exchange happens the only way it architecturally can: a ship
  voyage. Add `BUILDING_HARBOR` (new BUILDING_DEFS row) with a small escrow
  stockpile; a foreign ship's `CMD_SHIP_TRANSFER` at another player's
  island may target only the harbor escrow; the owner accepts/rejects via
  a trade_ui-style panel emitting its own Command. Docking permission is a
  per-island toggle — blockade emerges free (a ship that can't dock can't
  deliver).
- Networking code lives in a new `src/net.c/h` subsystem (or a separate
  thread feeding a queue drained at tick boundaries); the sim remains
  network-ignorant.
- **Verify:** two machines (or two processes) complete a session: both
  colonise, trade via harbor escrow, hashes stay green for 15+ minutes;
  pulling the cable produces a clean disconnect and single-player
  continuation.

---

## Phase 6 — The headless twin and the server decision

**Goal:** the sim as an SDL-free static library, and an informed choice on
Carbon.

- CMake: split SOURCES into `SALTMARCH_SIM_SOURCES` (command, sim core, map,
  building, resource, population, agent, connectivity, island, ship,
  faction — all SDL-free by now) and client sources. New targets:
  `saltmarch_sim` (static lib, C99) and `saltmarch_replay` (CLI: load
  `.smlog`, run, print hash — this formalises the CI harness).
- The persistent server is a thin host process linking `saltmarch_sim`
  behind `extern "C"`: it owns the canonical log, stamps ticks in real time
  (including while players are offline — this is where wall-clock offline
  progression actually lives: the server simply keeps ticking), accepts
  Command submissions, broadcasts the ordered stream, checkpoints
  `(state, tick)` every few minutes and truncates the log so join-in-
  progress = latest checkpoint + tail.
- **Carbon evaluation, done honestly:** prototype the host twice, one day
  each: (a) plain C++/`std::thread` + a socket library; (b) Carbon
  `scheduler` (one greenlet per island stepping the sim, channels for ship-
  arrival messages between island greenlets) + Carbon `io` for transport.
  Adopt (b) only if the greenlet-per-island model demonstrably simplifies
  the code — its real payoff is later sharding of islands across processes
  (the actual EVE trick). Either way, only the server touches C++/Carbon;
  the C99 client and sim are untouched.
- **Offline catch-up rule:** the server ticks continuously, so "catch-up"
  is not a replay burden on clients. If server downtime catch-up is ever
  needed, it is a straight `sim_run_one_tick` loop — at 10 ticks/sec sim
  cost of a 64x64 grid, hours of world time replay in seconds. Never
  invent a separate closed-form "offline production" path: one sim, one
  truth. (Divergent offline/online production rates would corrupt the
  "logging off is safe and fair" promise.)

---

## Later phases (design-complete, do not start until 1–6 are green)

- **Port charters:** `COLONY_FOUNDING_GOLD` becomes a bid paid TO the
  faction; charter upkeep is a tick-driven gold drain; lapse relists the
  island. New players join by bidding on lapsed charters.
- **Loss mechanics, then insurance:** first NPC piracy events on voyages
  (deterministic, seeded from `(voyage_id, event_id)` so the feed stays a
  dumb log), then per-lane premium EMA of insured losses; the premium
  ticker on world_ui becomes the game's information layer.
- **Interception / tide-time PvP:** an intercept is a Command referencing a
  voyage; both sides deterministically compute the engagement from the
  ordered log plus seeded RNG. The feed/server never becomes a real-time
  physics arbiter.
- **Ghost factions from replays:** seed NPC islands with recorded human
  command logs replayed at offset ticks — believable neighbours, zero AI.
- **Time-travel debug scrubber:** checkpoint + log = re-simulate to any past
  tick; UI slider. Solo-dev killer tool.

## Risk register

| Risk | Phase | Mitigation |
|---|---|---|
| A mutation path escapes the funnel | 1 | F9 after every feature; grep for direct calls to sim mutators outside sim_apply; make sim-side functions `static` where possible |
| Cross-platform float divergence (libm, `-ffp-contract`) | 1 | CI `.smlog` replay on all three OSes; integer-ise the divergent subsystem only when CI actually catches it |
| Long-session replay cost for F9/load | 1 | Periodic embedded checkpoints in the log once replay exceeds ~2 s |
| `-Wconversion` churn from float→int timer migration | 1 | Migrate one subsystem per commit; build after each |
| Elastic market reads as a rigged slot machine at tiny scale | 3 | Tune for visible elasticity + visible mean reversion; debug telemetry overlay before tuning |
| Feed file races between game and sync script | 4 | Append-only writes, rename-into-place on sync, tolerate partial trailing line |
| Lockstep stalls on latency | 5 | Tick delay buffer (apply commands N ticks after submission); co-op tolerance is high |
| Carbon integration cost exceeds value | 6 | Timeboxed two-way prototype; Carbon is optional by construction — the architecture, not the library, is the point |

## Non-goals

Do not port the client to C++. Do not adopt Trinity, CarbonUI, CarbonAudio,
Destiny, or Resources — SDL3 already fills those roles here. Do not sync
island tiles between players, ever. Do not build fog of war. Do not build
insurance before ship loss exists. Do not add scrip/multiple currencies
before at least two factions exist.
