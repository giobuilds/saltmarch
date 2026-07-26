# Saltmarch server

`saltmarch_host` is the persistent world: a C99 process that owns the
canonical command log, ticks in real time whether or not anyone is
connected, and checkpoints the world to a file. It links
`libsaltmarch_sim` (the game's own simulation) and `libsaltmarch_net`
(the game's own protocol). There is no server-side game logic — the
server runs `sim_run_one_tick()`, the same function the client runs,
because a second implementation of the world is exactly the thing this
architecture exists to avoid.

## Running one

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

./build/saltmarch_host --world world.smlog --seed 12345
```

First run creates the world; every run after that resumes `world.smlog`
where it left off. Ctrl-C writes a final checkpoint and exits 0.

| Flag | Meaning |
|---|---|
| `--port N` | listen port (default 7777) |
| `--world FILE` | checkpoint to load/create (default `world.smlog`) |
| `--seed N` | seed for a **new** world; ignored when resuming |
| `--checkpoint-seconds N` | checkpoint interval, `0` = only at shutdown (default 60) |
| `--ticks N` | run N ticks, then checkpoint and exit — how the tests drive it |
| `--quiet` | silence the simulation's narration |

Connecting:

```bash
./build/saltmarch --join your.host:7777          # new player
./build/saltmarch --join your.host:7777 --as 3   # come back as player 3
```

The client cannot tell a dedicated server from a friend running
`--host`; it is the same protocol either way.

## What the server guarantees

- **Continuous time.** The server ticks on a monotonic clock, so a world
  progresses while its players are asleep. Its accumulator is
  deliberately *not* clamped the way the client's is: time the server
  owes is time it pays back, by running the same ticks faster. There is
  no separate "offline production" formula, and there must never be one
  — divergent offline and online rates would break the promise that
  logging off is safe and fair.
- **Restart is not a rollback.** A checkpoint is `(seed, tick, command
  log)` — an ordinary `.smlog`. `saltmarch_replay --replay world.smlog`
  reconstructs the same world and prints the same hash, so the server's
  state format is verifiable with the same tool CI uses.
- **Identity survives disconnects.** Each connection is assigned a
  player id at join; a client may ask for one back with `--as N`, which
  is honoured if that player owns an island and nobody is currently
  connected as them.
- **Privacy is validation.** `sim_apply` rejects commands against
  islands the sender does not own, so being on the same server does not
  give anyone reach into your island. This is enforced by the sim, not
  by the server.

## What it does not do yet

- **No authentication.** `--as N` is an honour system: anyone who knows
  an id can claim it while its owner is away. Fine for friends, not fine
  for a public server. This is the first thing to fix before opening one
  up.
- **No log truncation.** Join transfers the entire command log and the
  client replays it, so join cost grows with world age. The plan's
  answer is a checkpoint containing state rather than history, which
  needs a full-state serialisation format the save file deliberately
  does not have today. At 10 ticks/sec on a 64x64 grid there is a lot of
  headroom before this bites, and `NET_MAX_PEERS` connections is a small
  world anyway.
- **No sharding.** One process, one archipelago, one thread.

## Transport hardening plan

An audit of `net.c` in July 2026 found the *architecture* sound — the
lockstep model holds, and `sim_apply`'s ownership gates and per-payload
range checks mean a malicious `Command` off the wire cannot index out of
bounds — but the transport itself had no flow control in either
direction and no timeouts anywhere, and the desync detector did not
work. This section is what was done about it, in the order it was done.
All four phases have landed; the numbering is kept because the
reasoning is the part worth keeping.

Because peers now exchange `MSG_PING`, `NET_PROTO_VERSION` is 4: a
client older than this is refused at the handshake with a version
mismatch, rather than connecting and then looking idle.

### Phase A — availability (a single peer must not be able to stop the world)

1. **Outbound queueing.** `send_all` retried on `EWOULDBLOCK` forever
   with 1 ms sleeps, and `broadcast()` runs from the tick loop, so one
   peer that stopped reading froze the whole server. Each peer gets an
   outbound buffer; sends queue and are flushed non-blocking from
   `net_pump`. A peer whose queue stops draining for
   `NET_SEND_STALL_MS` is dropped — a stall timeout rather than a byte
   cap, because a legitimate `MSG_WORLD` at join is genuinely large and
   "slow but moving" must not be mistaken for "gone".
2. **Inbound budget and ceiling.** `recv_into_buf` looped until
   `EWOULDBLOCK`, growing without bound, so a peer sending at line rate
   both livelocked the pump and exhausted memory. Bounded to
   `NET_RECV_BUDGET` bytes per peer per pump (the remainder waits for
   the next pump, which is what a partial frame already did), with a
   hard ceiling of one maximum frame plus slack.
3. **Timeouts and liveness.** A connection that never said `HELLO` held
   its slot forever; eight of them locked every real player out, and
   half-open peers were never noticed. Adds a handshake deadline, an
   idle deadline, `SO_KEEPALIVE`, and `MSG_PING` — an explicit
   keepalive rather than leaning on `MSG_HASH`, so that a guest sitting
   in the F8 scrubber (sim frozen, no hash reports) is not mistaken for
   a dead one.
4. **`HELLO` once per connection.** There was no `said_hello` guard, so
   a repeated `HELLO` re-ran identity assignment — which, seeing the
   peer's current id as connected-and-landed, handed out a *fresh* one,
   re-sent the entire command log, and granted *another* starting
   island. One connection could take every island in the archipelago.
   A second `HELLO` now drops the peer.

Phase A changes what a conforming peer must do (`MSG_PING`), so
`NET_PROTO_VERSION` goes to 4.

### Phase B — the desync detector (it is the only safety net there is)

5. **The hash ring held one usable slot.** `slot = hash_ring_n %
   HASH_RING` with `hash_ring_n` capped at `HASH_RING` meant every write
   after the 16th boundary went to slot 0, while slots 1–15 kept ticks
   from the world's first 80 seconds forever. Split the write cursor
   from the fill count.
6. **Boundaries were skipped whenever more than one tick ran per
   iteration.** Both branches hashed only when `last_hash_tick ==
   sim_tick_no` after the fact — but the server's accumulator is
   deliberately unclamped, so catch-up bursts stepped straight over the
   boundaries and recorded nothing, and a client did the same after a
   stall. Hashing moves into `net_on_tick()`, called once per completed
   tick, keyed on `sim_tick_no % NET_HASH_INTERVAL == 0` so both sides
   agree on where the boundaries are without reference to when either
   joined.
7. **A failed guest send forked the world silently.** `net_submit_local`
   returned the send result, and `command_submit` falls back to local
   stamping on 0 — applying a command the host would never see. A guest
   with a live session now consumes the command either way; the fallback
   belongs to a torn-down session, not a failed write.

### Phase C — abuse and the remaining sharp edges

8. **Rate-limit `MSG_CMD`.** Every accepted command joins the
   never-truncated authoritative log and is broadcast to all peers, so
   one client submitting flat out permanently inflates join cost and
   checkpoint size. A per-tick budget, and a peer that keeps overrunning
   it is dropped.
9. **Bound the join replay.** `game_install_world` runs
   `while (sim_tick_no < tick)` on a tick taken straight off the wire: a
   corrupt or hostile host sends `2^63` and the client hangs forever.
   Sanity-bounded at the trust boundary, with the cost of a large but
   legitimate replay logged rather than silently freezing the frame.
10. **`TCP_NODELAY`.** Never set, though `netinet/tcp.h` was already
    included. Nagle can add ~40 ms to the 13-byte `TICK_AUTH` and `CMD`
    frames — squarely on the lockstep latency path.
11. **Smaller items.** A connect timeout (an unreachable host froze
    startup for the OS default); IPv6; the guest's `peers[0]`
    assumption; the receive buffer never shrinking after a large
    `MSG_WORLD`; `WSACleanup` being conditional on a socket having been
    opened.

### Phase D — tests

`test_server.c` and `test_lockstep.c` were happy-path only, and both
had to be taught to call `net_on_tick` or they would have gone on
passing while exercising no hashing at all.

`tests/test_net_hardening.c` covers items 5–8 over the in-memory
transport, which exists precisely so the protocol can be driven
deterministically. The desync case is a real regression test, not a
smoke test: it was run against the pre-fix code and *passed*, because a
generous step budget let the old detector fire hundreds of ticks late.
What makes it bite is the budget — twenty steps, two or three
boundaries — so it asserts that the desync is caught **promptly**,
which is the property that was actually lost. Confirmed failing before
the fix and passing after.

Items 3 and 4 cannot be expressed over the mem transport: both are
about a connection misbehaving, and that transport has no socket to
hold open and no way to say "connect, then don't". They are probed over
real TCP in `ci/host-smoke.sh`, which already owns that territory —
one connection that says nothing and must be dropped at the handshake
deadline, and one that introduces itself twice and must lose the
connection. Both skip cleanly where the shell has no `/dev/tcp`.

**Not in this plan:** authentication and log truncation, both listed
above. Neither is a transport bug, and neither is fixed by any of the
work here.

## Why it is C99, and what happened to Carbon

MMO_PLAN.md proposed prototyping the host twice — plain threads versus
Carbon's `scheduler` (github.com/carbonengine, MIT), one greenlet per
island — and adopting Carbon only if the greenlet model demonstrably
simplified the code. The host was written in C99 instead, and Carbon was
not prototyped. The honest reasoning:

1. **The host has no concurrency to simplify.** The thing Carbon's
   scheduler is good at is many independent, blocking-ish actors. This
   server is a single deterministic tick loop over four islands with a
   non-blocking socket drain either side of it; it idles at ~0% CPU. A
   greenlet per island would be four greenlets that must run in a fixed
   order to preserve determinism (`island_update`'s ordering constraint
   is not incidental — connectivity.c keeps BFS scratch in file
   statics). Concurrency here buys nothing and costs the one property
   the whole design is built on.
2. **Its real payoff is sharding, which is not close.** Carbon's value
   in the EVE lineage is moving actors across processes. That matters at
   thousands of islands, not four. Adopting it now would be paying the
   integration cost years before the benefit.
3. **C++ would have been the only C++ in the repo.** The client and sim
   are C99 and stay that way (a project non-goal is porting the client
   to C++). One language means the server shares the game's warning
   flags, its build, its tests, and its `.o` files rather than an
   `extern "C"` boundary and a second toolchain on three CI platforms.
4. **It could not have been evaluated honestly here anyway.** The
   development environment for this phase had no access to fetch or
   build Carbon, so prototype (b) would have been a paper exercise
   presented as a comparison.

**What would change this.** Carbon becomes worth revisiting when the
world stops fitting one loop: island counts in the hundreds, or an
appetite to shard islands across processes. At that point the boundary
to cross is the host process only — `libsaltmarch_sim` is C99 with no
opinion about who calls it, which is what keeps this decision cheap to
reverse. That is the actual insurance the Phase 6 split bought, and it
is why the architecture, not the library, was always the point.
