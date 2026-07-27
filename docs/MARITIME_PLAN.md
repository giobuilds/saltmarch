# The sea — geometry, orders, routes and charts

Why ships cross open water, made into mechanics. Four reasons were
given for a voyage: supplying your own islands, fulfilling a booked
market order, intercepting somebody else's merchantman, and hunting
pirates. Today only the first exists, and the sea it crosses is not a
place — it is a constant.

Nothing here is built. Sequenced against
[SERVER_AUTHORITY.md](SERVER_AUTHORITY.md), which makes the concealment
this design assumes actually true; most of what follows works without
it, less well.

## The gap underneath everything

**Islands have no position.** There is no coordinate in `Island`, none
in `GameState`. The only positional data in the tree is `NODE_POS` in
`world_ui.c` — hand-placed *screen fractions*, for drawing, eight of
them.

And every voyage takes the same time:

```c
#define SHIP_VOYAGE_TICKS ((int)(SHIP_VOYAGE_SECONDS * SIM_TICKS_PER_SEC))
```

Distance does not exist as a gameplay quantity. Waypoint paths,
variable durations, sight radius, distance-priced risk and "more than
eight islands" all need a **sea coordinate space**, and none of them can
be built before it. This is the first phase for that reason, not
because it is the most interesting.

## Four layers

**Geometry.** Islands sit at positions in a sea. A *route* is a path: an
ordered list of waypoints between two islands, each leg carrying an
integer duration in ticks. Voyages follow a route rather than teleport
along a constant.

**The order book.** Players post buy and sell orders. The sim matches
them deterministically. A match becomes a *booking*, and a booking
dispatches a merchant, a boat and the goods along a route from one
harbour to the other.

**Routes and charts.** Public routes are slow and protected; private
routes are fast and dangerous. Passage on a private route consumes a
**chart**, which is a good with a supply chain — and Scholars research
which routes can be charted at all.

**Predation.** Intercepting a merchantman, and hunting the pirates that
prey on everyone. The only layer with no existing mechanic behind it.

## Geometry

Islands gain a world position. Waypoints are named positions in the sea
that routes pass through — straits, shoals, a reef with a reputation —
so a lane can be described in Saltmarch's own lore rather than in
coordinates, and so there is something to talk about besides the two
ends. Routes carry names for the same reason.

Those names are generated or authored content, like the island names
already are. That keeps them clear of PRIVACY.md's rule that no
player-supplied free text may enter world state: a named strait is not
a player typing into a replicated, replayed log.

A leg's duration is an **integer tick count**, stored, not derived from
floating-point distance. This matters more than it looks. Arrival today
is a pure integer test —

```c
sim_tick_no >= departure_tick + SHIP_VOYAGE_TICKS
```

— and `ship.h` is explicit that `progress` is a *cached cosmetic
derivation* that `sim_hash` never reads. That discipline is what keeps
the world identical across platforms and must survive this change:
**per-leg integer durations in hashed state, floats only for drawing.**
Accumulating float distance into the sim is how determinism dies
quietly, and it would not be caught until two clients on different
architectures disagreed.

Positions also finally give `world_ui.c` something to draw from.
`NODE_POS` is hand-placed and I have already had to rewrite it once
going from four islands to eight; it does not survive twenty. Node
placement becomes a projection of world coordinates, and the
non-overlap guard in `test_world.c` keeps its job.

## The order book

**Orders.** A player posts a buy or a sell: island, resource, quantity,
limit price. That fits the existing command payload without inventing
anything — `Command` carries four `int32` slots, and quantity's sign
can carry the side, exactly as `CMD_SHIP_TRANSFER` already uses sign
for load-versus-unload. `a` = island, `b` = resource, `c` = quantity
(positive buys, negative sells), `d` = limit price.

**Matching is sim state and must be deterministic.** Price-time
priority, with ties broken by order id, and order ids assigned in
command-log order so a replay reproduces every fill. The matcher runs
at tick boundaries like everything else. Orders, the book and open
bookings are all hashed.

**A booking reserves a merchant, a boat and the goods**, and the
merchant and the boat **return to the island they set out from** when
the trade completes. They are capital, not fuel: what is consumed is
the goods and the time. That makes trade capacity a standing build
decision — you can only run as many simultaneous trades as you have
hulls and merchants — rather than a drain that has to be topped up.
The obvious source of merchants is the Merchant House tier from
SUPPLY_CHAIN Phase 7, which gives the third house line a mechanical
purpose beyond consuming luxuries.

**The faction posts orders too.** `exchange_view.h` already states the
thesis — *"a bot is indistinguishable from a slow player"* — and
already parameterises one screen by counterparty, NPC or player. A book
where the faction is a market maker alongside everyone else is that
thesis finished, not a second system beside it. Its current elastic
quote becomes its posted bid and ask.

## Routes and charts

**Public routes** are known to everyone, slower, and protected — the
faction patrols them, which is its third role after market maker and
insurer. **Private routes** are faster and unprotected.

**Every private route is generated with the world and concealed.** The
routes themselves are world state — deterministic from the world seed,
like the islands — so the sim always knows all of them. What is private
is *who has learned which*, and that is per-player, permanent, and not
public: a rival cannot see which passages you have opened.

**Passage consumes a chart.** Route *access* is therefore two things
with different lifetimes: **knowledge**, which is permanent and
per-player, and a **chart**, which is a good, is spent on the crossing,
and is already hashed, replayed, private once stockpiles are private,
and tradeable on the book being built above.

Knowledge arrives two ways. **Research** lets you manufacture charts for
a route you have learned. **A looted chart** both reveals a route and
carries one passage on it — which makes piracy a way to acquire
geography, not just cargo.

`RES_CHARTS` already exists — Paper + Glass → Chart House, added in
SUPPLY_CHAIN Phase 8, wanted today only by Scholars. Making it the
vehicle for passage gives it a second and better purpose, makes the
Chart House a permanent industry with recurring demand rather than a
one-off unlock, and means the market trades the maps that let people
raid the market's own convoys.

**Scholars research which routes can be charted.** This is the one
genuinely new per-player persistent state, and it is *private* — which
means it cannot exist under full replication at all, and is one more
thing waiting on server authority.

**Charts are lootable, and NPC pirates carry them rarely.** That closes
a loop across all four reasons a ship goes to sea: hunt pirates, take a
chart, learn a passage, trade it faster, become worth raiding. It also
means a private route can leak — not by being observed, but by somebody
taking the map off a ship that was using it.

**Surveying is the other way to learn a route**, and it mirrors trade
exactly. A survey mission dispatches **one scholar, one research boat
and one chart**, the way a booking dispatches one merchant, one boat
and the goods. That symmetry is worth building deliberately rather than
twice: a mission is *a person, a hull and a payload*, and trade and
survey are two instances of it. It also gives the Scholar's House the
same kind of mechanical output the Merchant House has — people who go
somewhere and do something — rather than being a tier that only
consumes.

**Insurance stops being per island-pair.** `lane_premium[from][to]`
becomes per route, which is what makes "cheap on the protected lane,
expensive on the fast one" expressible — and because the premium
already drifts toward lanes where losses happen, the insurance market
becomes a *public* map of where pirates hunt that reveals no individual
voyage. That is a rare thing: a signal that informs everyone without
leaking anyone.

## What is public, and what is not

The information model this design implies, which is the answer
[VISIBILITY.md](VISIBILITY.md) was waiting for:

| | Public |
|---|---|
| Orders, and prints of matched trades | **Yes** — that is what a market is |
| Route existence, public routes | **Yes** |
| Insurance premium per route | **Yes** — the risk map |
| A booked voyage's **lane and time window** | **Yes** |
| Its exact departure tick, and its manifest | **No** |
| Stockpiles, production, cash | **No** |
| Which private routes you can chart | **No** |

That middle line is the whole balance. A pirate reads that a lane is
busy and must go and patrol it; interception becomes a search with a
cost in position, rather than sorting a list of everyone's cargo by
value. It is also why private routes are worth their risk: less
traffic, and no published window.

## Phases

1. **Sea geometry.** Island positions, waypoints, routes as paths,
   per-leg integer durations, `world_ui` projecting from world
   coordinates. Unblocks more than eight islands.
2. **The order book.** Orders, deterministic matching, bookings,
   merchants as a reserved resource. The faction as market maker.
3. **Routes and charts.** Public and private, chart consumption,
   per-route insurance, Scholars research.
4. **Server authority** ([SERVER_AUTHORITY.md](SERVER_AUTHORITY.md)) —
   turns "everyone knows where the fast route is" into real
   concealment, and makes stockpiles private.
5. **Predation.** Interception with ship attributes and escorts, and
   pirates as something you can hunt rather than a dice roll.

Phases 2 and 3 work under the current architecture; they are simply
less secret without phase 4. That is the point of this ordering — the
game design lands and is playable first, and the architecture change
upgrades it rather than gating it.

## Settled

- **Merchants and boats return** to the island they set out from once a
  trade completes. Capital, not fuel.
- **Charts are lootable**, and NPC pirates carry them rarely — so
  hunting pirates yields geography as well as cargo, and a private
  route can leak by having its map taken.
- **Research is private.** A rival cannot see which passages you have
  opened. This is the part that cannot exist under full replication.
- **Private routes are all generated with the world** and concealed;
  what a player unlocks is their own knowledge of them, not their
  existence.
- **Waypoints and routes are named**, in lore rather than coordinates.
- **Routes are discovered by research *or* by survey.** A survey
  dispatches one scholar, one research boat and one chart.

## Still open

- **How many routes between a pair?** One public plus a small number of
  private is probably enough; unbounded makes the book hard to read,
  and it interacts with how a route is chosen when several exist.
- **How long does a survey take, and can it fail?** A mission that can
  return empty makes exploration a gamble; one that cannot makes it a
  queue.
- **Where do research boats come from** — a Shipyard variant, or a
  refit of an ordinary hull? They are the first non-merchant vessel.
- **What does a survey consume the chart *for*?** A blank chart as raw
  material and a finished chart as the result would give the Chart
  House two products and make the loop legible; the alternative is that
  a survey spends a known route's chart to reach the water beyond it.

## Risks

**This is large, and it is two kinds of work.** Geometry and the order
book are game design; server authority is architecture. Doing them at
once makes a failure hard to attribute, which is why they are ordered
rather than merged.

**Determinism is the thing most likely to break.** Path following,
order matching and per-leg timing are all places where a float or an
unstable sort order would produce a world that is *nearly* the same —
the worst failure mode this project has, because it surfaces as a
desync between two machines rather than as a wrong answer on one. The
existing guard is `test_determinism` plus the record/replay gate, and
both should be extended before the matcher is written, not after.

**The book is a new surface for abuse.** Orders are cheap to post and
the matcher runs every tick. Rate limiting exists per peer from the
transport hardening, but an order book wants its own limits — open
order counts, minimum sizes — or it becomes a way to make the server
work hard for free.
