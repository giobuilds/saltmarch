# Saltmarch Architecture

The one-line version: **the world is a pure function of
(world seed, ordered command log)**, and everything else in the design is a
consequence of defending that property.

One deliberate exception, added once worlds started outliving the
patience required to replay them: a server checkpoint stores state
rather than history, and a world resumed from one begins at that
snapshot instead of at tick 0 (SERVER.md, "Log truncation"). The
property still holds *above* the snapshot, which is what the scrubber,
the desync detector and lockstep all actually need. Fixtures and the
determinism gate keep full logs, so the stronger claim is still the one
CI proves on every platform.

This document describes the architecture as a whole — what exists today and
where it is headed. The execution detail lives in two companion documents:
[MMO_PLAN.md](MMO_PLAN.md) (the deterministic-sim and multiplayer phases)
and [UI_PLAN.md](UI_PLAN.md) (the UI layer redesigned to match). CLAUDE.md
holds the working conventions for contributors.

## Today: a subsystem-per-file single-player sim

Saltmarch is a C99 isometric city-builder on SDL3's callback app model
(`SDL_MAIN_USE_CALLBACKS`). `src/main.c` implements only the four SDL
callbacks and contains no game logic — it wires subsystems together and
owns the frame's render order.

- **Two build targets, one codebase.** `libsaltmarch_sim` is the world —
  map, buildings, population, agents, ships, market, the command funnel and
  the save/replay format — and links no SDL at all. The client (window,
  input, rendering, UI overlays, feed, co-op sockets) links it. Anything
  needing both SDL and `GameState` lives in client.c; `ci/sim-sdl-free.sh`
  fails the build if SDL creeps back across the line.
- **One top-level struct.** `GameState` (game.h) owns every subsystem: the
  tile `Map`, `Camera`, `InputState`, the `buildings[]` array with its
  parallel `pop_data[]`, and the `Stockpile`. A single `GameState*` rides in
  SDL's `appstate`; there is no global mutable state.
- **Subsystem pattern.** Each `src/*.c/.h` pair is self-contained with an
  init/free pair and per-frame update/render functions, called from main.c
  in a fixed order. New subsystems follow the same shape rather than
  reaching into each other's internals.
- **Static def vs. instance.** Building types are rows in a static
  `BUILDING_DEFS` table (footprint, placement rules, colors, production);
  placed buildings are instances referencing it. Placement validation and
  rendering are generic over the table.
- **One projection.** `iso_to_screen()` / `screen_to_iso()` in render.c are
  the only conversion points between tile space and screen pixels.
- **Determinism seeds already planted.** Map generation is a pure function
  of its seed (value noise + LCG RNG), and the save file is tiny — the two
  preconditions the future architecture builds on.

## Where it is going: the deterministic core

MMO_PLAN.md evolves the game into an MMO-style shared ocean without ever
building a conventional game server. Two invariants do all the work:

1. **No mutation bypasses the command funnel.** Every state change —
   placing a building, trading, demolishing, sailing, colonising — is a
   `Command` record applied through a single `sim_apply()` dispatch, in log
   order, at tick boundaries.
2. **Nothing inside the sim reads a clock.** The sim advances in fixed
   integer ticks (`TICK_MS = 100`); timers are tick counts; the RNG steps
   only inside the sim; iteration order is fixed. Wall clock exists only at
   the edges, converting elapsed real time into "how many ticks to run".
   Rendering interpolates between ticks; the sim never does.

Together these make the world replayable: any client reproduces the entire
world by re-running the log from the seed. That single property is then
cashed in repeatedly:

- **Saving** is writing seeds + log; **loading** is a replay.
- **Verification** is a replay: an in-game F9 check re-simulates and
  compares state hashes, and CI replays a recorded session on Linux, macOS
  and Windows — cross-platform determinism fuzzing as the project's first
  regression test.
- **Multiplayer** is agreeing on the log's order. Islands stay private
  single-player instances; **only ships cross the wire**, as immutable
  voyage records fixed at departure.
- **Offline progression** is the server continuing to tick. One sim, one
  truth — there is deliberately no separate closed-form "offline
  production" path.

## The multiplayer ladder

Each rung is independently shippable and none requires the next:

1. **Command funnel + fixed timestep + F9** — the gate for everything.
2. **Voyages as timestamps** — a voyage is fully described by an immutable
   record at departure; this record is the future wire format.
3. **Elastic NPC faction** — the fixed price tables become a real
   counterparty with finite gold, inventory, and mean-reverting quotes.
   Faction state is ordinary sim state: hashed, tick-driven, funnel-only.
4. **Ghost multiplayer** — clients append voyage records to a dumb
   append-only JSONL feed and render each other's ships as cosmetic,
   non-interactive ghosts. Transport is an external sync script; the game
   performs zero networking. An NPC "faction" is a cron job writing to the
   same feed — a bot is indistinguishable from a slow player.
5. **Lockstep co-op** — two clients run the identical sim; a host stamps
   and broadcasts command order; desync recovery is log replay, never
   state-patching. Privacy is validation, not hiding: `sim_apply` rejects
   commands against islands you don't own. Player-to-player exchange
   happens the only way it architecturally can — a ship voyage into a
   harbor escrow.
6. **The headless twin** — the sim is an SDL-free static library
   (`libsaltmarch_sim`) with a CLI front end, `saltmarch_replay`, that
   loads a `.smlog`, replays it and prints the state hash. CI records a
   session with the game and replays it with the tool on three platforms,
   so "the sim is separable" is a test rather than a claim. The server,
   `saltmarch_host`, is that library plus a clock, a socket and a
   checkpoint file: it owns the canonical log, ticks continuously
   (including while every player is logged off — that is where offline
   progression lives, with no separate formula for it), and checkpoints
   to an ordinary `.smlog`. Its transport is the client's own net.c,
   generalised from one guest to many. (This is EVE Online's single-shard
   command-replay architecture at solo-dev scale.) Deferred with reasons
   in SERVER.md: authentication, log truncation, sharding — and Carbon,
   which the host does not use and why.

## The UI layer mirrors the sim's discipline

UI_PLAN.md applies the same philosophy above the sim boundary:

- **UI is a pure function of a snapshot.** Overlays build widget lists from
  an immutable per-frame `UiSnapshot` (plain structs, SDL-free), never from
  live `GameState` — so UI code cannot mutate sim state or step the RNG by
  construction, and the same UI can render a live sim, a replayed past
  tick, or a remote server without knowing the difference.
- **The UI speaks Command.** Buttons emit identity-stable Commands (enum
  identities, never row/page positions) through the same funnel as
  everything else; confirm popups render the literal Command they submit.
- **Rejection is a rendered signal.** `sim_apply` returns a `RejectReason`;
  a shared pure `sim_validate()` serves both hover prediction and
  authoritative rejection, so client feedback can never drift from the
  sim's verdict.
- **Latency is grammar, not lag.** Submitted-but-unapplied commands render
  in a distinct queued style that hardens at the tick boundary — the
  lockstep delay buffer becomes visible game language.
- **The purity pays for testing.** Because hit-tests are pure functions,
  recorded input intents replay through the real UI code in CI — a
  click-through regression suite in an environment with no display
  automation.

## Boundaries that stay fixed

- The sim never touches SDL; rendering, input and UI never mutate the sim.
- The whole tree stays C99, server included. The option to write a host
  in C++ was left open by MMO_PLAN and deliberately not taken (SERVER.md).
- Island tiles are never synced between players; there is no fog of war.
- Text rendering goes through fonts.c; projections through render.c's two
  conversion functions; warnings are bugs
  (`-Wall -Wextra -Wpedantic -Wshadow -Wconversion`, `/W4`).
