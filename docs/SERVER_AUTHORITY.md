# Server authority — the plan for concealed PvP

The answer to [VISIBILITY.md](VISIBILITY.md), now that the question has
been decided: strangers may raid each other's convoys, **and** may not
read each other's books. Those two together admit exactly one shape —
the server owns the world and each client is told only what it is
entitled to know.

Nothing here is built.

## A correction to VISIBILITY.md

That document costed this option as "a different program that shares
some art". Having looked properly, that is wrong, and the reason is
worth stating because it changes the decision's price.

**Four seams this needs already exist, each built for another reason.**

1. **Every mutation is already a `Command` through `sim_apply`.** The
   server already validates and rejects; ownership gates already
   enforce who may act on what. A server-authoritative build changes
   nothing here. This is MMO_PLAN Phase 1a's funnel doing a job it was
   not designed for.

2. **`saltmarch_host` is already the authoritative world.** It owns the
   canonical log, ticks on its own monotonic clock whether or not
   anyone is connected, validates every submission and checkpoints the
   result. It is not a relay that would need promoting; it is already
   the thing that decides what happened.

3. **The UI already reads a copy, never the world.** Every overlay
   builder takes `(UiList *, const UiSnapshot *, const UiState *)` and
   never a `GameState *` — enforced by the compiler, and true of every
   builder in the tree. `ui_snapshot.h` says out loud that a snapshot
   "does not say where it came from. Live sim, a replayed past tick, a
   remote server's state — the UI cannot tell." UI_PLAN Phase 0 built
   this seam for testability. It is exactly the seam server authority
   requires, and it is finished.

4. **Full-state serialisation exists and is tested.** `snapshot.c`
   encodes a world in ~30 KB with a checksum and a hash, and
   `test_snapshot.c` proves a restored world evolves identically. A
   *filtered* snapshot is a modification of a working encoder, not a
   new invention.

And `fx_reject` already correlates submitted commands with their
outcomes by `{player_id, seq}` — which is the bookkeeping optimistic
prediction needs, built to explain refusals at the widget that caused
them.

So this is a substantial project, but an **evolutionary** one. The
determinism work was not a detour that now has to be undone; it built
the machinery that makes the next step affordable.

## What survives, and where it lives

The claim that determinism, replay and the F9 self-check are "lost" is
imprecise. They move.

| Property | Today | After |
|---|---|---|
| Deterministic sim | client and server | **server** |
| Replay from `(seed, log)` | client and server | **server** |
| Checkpoints, snapshot format | server | server, plus per-client views |
| F9 self-check | client | server (where a persistent world needs it) |
| Desync hash exchange | both | **gone — nothing to disagree about** |
| Lockstep tick gate | client | **gone** |
| Scrubber | client, from its own log | server-served history, or owner-only |

Two of those are deletions rather than losses. When the server is the
only authority there is no second opinion to reconcile, so
`net_tick_allowed`, the hash ring and the resync path all stop having a
job. The transport hardening that made them correct is not wasted — the
flow control, timeouts and budgets underneath them are exactly what an
untrusted-client server needs, and strangers are precisely who they
were hardened against.

## The view model

The question a filtered snapshot answers is *what is this player
entitled to know*. A first cut:

**Your own islands: everything.** You hold complete information about
what you own — stockpiles, buildings, agents, production, escrow. No
reason to hide a player's own books from them, and every reason not to:
it is what makes local prediction possible.

**Other islands: existence and public face.** Name, profile, position,
whether settled, and who holds the charter. Ownership is already public
in spirit — the world map names it. Not stockpiles, not buildings, not
production.

**The market: fully public.** Faction quotes move with everyone's
trades and are a price signal, not a secret. This is the honest,
intended channel through which strangers affect one another.

**Ships and shipments: the lane, not the cargo.** This was left open as
"the crux, and the part that needs a game design rather than a filter".
Settled now, and the maritime work turned out to have answered most of
it:

- A foreign ship at sea shows its **existence, owner and endpoints**.
  Its **hold does not**. That is what keeps interception a gamble
  rather than a shopping trip — you attack a ship because of where it
  is going, not because you have read its manifest.
- A booked shipment on the **public lane** publishes that lane, which
  is what MARITIME_PLAN already says: a convoy route is patrolled and
  its traffic is known.
- A booked shipment on a **private passage** publishes nothing. To a
  rival it is reported as though it took the lane.

That last rule is the one worth stating out loud, because it is a
synthesis rather than a filter decision: **a chart buys concealment as
well as speed.** Nothing was designed to do that — it falls out of
"private routes publish no window" meeting "the server decides what you
are told" — but it is exactly the right shape. It gives charts a second
reason to be worth their cost and their risk, and it means a rival who
sees your traffic vanish from the lanes knows only that you found
something, not what.

## Discovery — answered elsewhere

This section originally posed the question and guessed at sight radii
and lookouts. It has since been answered by the marketplace revision:
see [MARITIME_PLAN.md](MARITIME_PLAN.md). A booked order publishes a
**lane and a time window** but not an exact departure or a manifest, so
raiding becomes a search rather than a sort — and private routes,
reached by consuming a chart, carry less traffic and publish no window
at all.

Two consequences for this plan. Route *access* turns out to be
inventory rather than knowledge (a chart is a good), so no per-player
knowledge structure is needed for it. And the filtering phase below
should follow the maritime work rather than precede it, because that is
what decides a ship's public projection.

The original framing is kept below.

## The original question: discovery

Interception takes a target ship and its exact `departure_tick`. Today
both are free, which is why VISIBILITY.md called target selection a
sort rather than a judgement. Concealment removes that, and something
has to replace it, or PvP becomes impossible rather than merely
uncertain.

This is a design decision, not an implementation detail, and the view
model cannot be finished without it. Some shapes:

- **Sight radius.** Your ships and coastal buildings see vessels within
  a distance. Piracy becomes patrolling. Fits the world map that
  already draws voyages in progress.
- **Harbour manifests.** Docking at a foreign harbour publishes a
  departure — cargo, lane, time. Escrow trading already makes harbours
  the inter-player airlock; this makes them the intelligence surface
  too, and gives `docking_allowed` a second meaning.
- **Lookouts as a building.** An explicit structure that reveals
  traffic on a lane, so intelligence costs something and appears in the
  supply chain like everything else.
- **Insurance as a signal.** `lane_premium` already drifts toward lanes
  where losses happen. It is public, and it is already a map of where
  the pirates are.

My inclination is harbours plus a modest sight radius: it reuses two
mechanics that exist, it makes concealment an active choice
(`docking_allowed` already lets you close your ports), and it gives
raiding a cost in position rather than a query.

**This should be settled before the filtering phase**, because it
decides what a ship's public projection contains.

## Latency, and why the client still simulates

If the client stops simulating, every road tile placed waits a round
trip. That is fatal for a city builder, where placement is the verb.

The answer is that a player has complete information about their own
islands — so the client keeps simulating **its own**, predicts locally,
and the server corrects it. Foreign islands and foreign ships arrive as
views and are never predicted, because the client has no basis to
predict them.

Reconciliation has a foundation already: commands carry `{player_id,
seq}`, `fx_reject` matches outcomes back to the widget that submitted
them, and `snapshot.c` gives a cheap "resync this island" primitive if
prediction drifts. A mispredicted local island is repaired by the same
mechanism that repairs a desynced guest today.

## Phases

**Phase 1 — move the source of truth.** The client stops being the
authority; `MSG_WORLD` still carries everything, unfiltered. Nothing is
concealed yet and nothing looks different — which is the point, because
it makes the structural change testable on its own. The lockstep tick
gate and the hash exchange come out here.

### Phase 1, corrected by measurement

This section originally said the client *stops running the world*. That
is not affordable, and the number says so plainly: a snapshot of a live
world is **16.4 KB**. Streaming state at the tick rate would be 164 KB/s
per client — 1.3 MB/s upstream at eight of them — to say almost nothing
new each time.

So the source of truth moves without the client going idle:

- the server pushes a full state **once a second** (16 KB/s per client,
  which is affordable now and is what delta encoding and per-client
  filtering will later cut down);
- the client keeps simulating **between pushes**, and every push
  overwrites whatever it had. Its local sim becomes a prediction rather
  than an authority — the same code, demoted;
- the lockstep tick gate goes, because a client no longer needs
  permission to advance: it will be corrected;
- the desync hash exchange goes with it, and not merely because it is
  redundant. Under prediction the two sides are *expected* to disagree
  between pushes, so a detector that treats disagreement as a fault
  would fire constantly and correctly.

That last point is the real reason the hash has to come out rather than
be left switched off: it stops meaning what it means. Its job — *these
two worlds have silently diverged* — is not a thing that can go wrong
any more, because there is only one world and the other is a guess.

This folds a little of the old Phase 2 into Phase 1. The alternative
was a phase that could not be shipped, which is worse than a phase
boundary that moved.

**As built.** `MSG_STATE` carries the same payload as `MSG_WORLD` —
snapshot plus the pending command tail — sent every
`AUTHORITY_PUSH_INTERVAL_TICKS` instead of once at join, and installed
through the same `game_install_from_snapshot` path. Authority is a
property of the host (`net_set_authoritative`), announced to each
client in `MSG_WELCOME`, so the dedicated server and an in-client co-op
host can behave differently over one protocol: the server pushes, the
co-op host still runs lockstep. `test_lockstep` therefore still
describes something real rather than something being deleted.

Not yet done, and next: the client rendering foreign islands from a
view rather than from its own copy, then the per-client filtering that
is the actual concealment (Phase 3 below).

**Phase 2 — local prediction.** The client simulates the islands it
owns and reconciles against the server. Placement feels instant again.

**Phase 3 — filtering.** ~~The server builds a per-client view~~
**Done.** `snapshot_encode_for()` redacts a copy of the world and
encodes that, so a view is byte-for-byte the same format as a full
snapshot and no decoder ever learns that views exist. An authoritative
server sends views; a lockstep co-op host still sends the whole world,
because a guest computing the world itself needs all of it —
concealment and lockstep are alternatives, not layers.

What a rival no longer holds: your stockpile, your buildings and
population, your expeditions, your trade and research capacity, your
insurance arrangements, your ships' holds, and — the one that matters
most — your charts and your memory of the sea. What survives: that the
island exists, who holds it, what it is called, whether it will let you
dock, the harbour escrow you trade across, and the order book.

`tests/test_visibility.c` asks the question the only honest way: it
inspects the client's own `GameState` after a push rather than what the
UI chose to draw. Ten of its assertions were checked against a build
with redaction disabled and fail there.

**Still open.** The client's local prediction now runs on a redacted
world, so it simulates foreign islands that have no buildings and no
stock. Nothing accumulates — the next push overwrites it — but the
client is wasting work on islands it cannot see and could, in a bad
frame, draw a half-invented foreign harbour. The fix is for prediction
to skip what it does not own, which is the remainder of Phase 2.

~~**Phase 4 — discovery.**~~ Answered by
[MARITIME_PLAN.md](MARITIME_PLAN.md): the order book is the
intelligence surface, and charts are how private routes are reached.

**Phase 5 — server-side history.** The scrubber and F9 served from the
server, if they are wanted for clients at all.

## What this costs, honestly

**Two implementations of nothing.** The sim stays single — that is the
whole point of `libsaltmarch_sim`, and the server-authority split does
not fork it.

**But the client gains a mode.** Offline single-player still wants a
local authoritative sim, so the client must work both ways: authority
local, or authority remote. That is a real branch, and the risk is that
it quietly becomes two code paths that drift. The mitigation is the
same seam as everywhere else — both modes produce a `UiSnapshot`, and
everything above that line cannot tell the difference.

**The tests change shape.** `test_lockstep` and much of
`test_net_hardening` describe a protocol that will no longer exist in
that form. The determinism gate, `test_snapshot` and `test_chains` are
untouched, because they test the sim rather than the transport.

**And a warning from this session.** Two of the bugs found here were
invisible to a green local run and needed a real socket or a real
allocator to surface. A server-authoritative build multiplies exactly
that class — divergence between predicted and authoritative state is a
timing bug by nature. The reconciliation path needs adversarial tests
from the first commit, not after CI complains.
