# The sea — geometry, orders, routes and charts

Why ships cross open water, made into mechanics. Four reasons were
given for a voyage: supplying your own islands, fulfilling a booked
market order, intercepting somebody else's merchantman, and hunting
pirates. Today only the first exists, and the sea it crosses is not a
place — it is a constant.

Phases 1 and 2 are built (see the phase list at the end); routes,
charts, server authority and predation are not. Sequenced against
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

Trade, survey and predation all dispatch **a person, a hull and a
payload**, which is worth building once rather than three times.

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

**The hash may not depend on where in the array an order sits.** This
is the one thing about the book that is not obvious and did break in
development. Orders are addressed by id, never by slot, so a checkpoint
compacts the dead slots out — which means a book holding one live order
in slot 5 must hash identically to the same order in slot 0. Hashing a
dead slot's `active` flag, the way the ship array legitimately does,
makes "how many orders have ever been cancelled here" part of the world
and desyncs a restore on nothing at all. Ships get away with it because
they *are* addressed by index and their snapshot preserves slots; the
book is the opposite case and needs the opposite rule.

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

### The faction's liquidity: home ports (as built)

The question was where a counterparty with no island posts orders from,
given that every order sits at a harbour and every fill is a crossing
that costs a merchant and a hull. The answer taken is **home ports**:
the market holds the last two islands, settled and owned by
`PLAYER_FACTION` from tick 0, not colonisable, and quotes there like
anybody else. Its fills ship, take the time the water takes, and tie up
its own hulls — so the NPC is a trader with a location, distance to the
market is a real cost, and its harbour is a place that could be
blockaded.

**One company stock, several harbours.** Its inventory and gold stay
global, and its quotes with them, so every existing caller of
`faction_bid`/`faction_ask` is untouched. That is safe only because
posting reserves: an order at each port draws down the same stock when
it is posted, exactly as a player's several orders draw down one
stockpile. `trade_balance`/`trade_credit` in game.c are the single place
that knows a counterparty's goods might not be in an island's
stockpile, so the reserve, the refund and the settlement cannot end up
disagreeing about it.

**It cannot quote everything.** Two sides times every good times every
port would exhaust the book by itself, so it quotes the six goods it is
furthest from baseline on — where it most wants to trade. That makes
the selection economic rather than a rotation, and means the market
leans against its own imbalance.

Two things this deliberately did not do, both worth revisiting:

- **Per-port inventory**, and with it different prices at the faction's
  own harbours. That is the piece that would make arbitrage between its
  ports a thing; it needs quotes to take a port, which touches every
  caller including the build-cost estimate and the F10 overlay.
- **Charters on home ports.** The market does not rent its harbours
  from itself, so the upkeep tick skips them entirely. If a player is
  ever to take a faction port, that is the rule that has to change.

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
per-player, and a **chart**, which is spent on the crossing. Both are
hashed and replayed, and both become private once stockpiles are — but
they are not the same kind of object, and the next section is about why
that matters.

Knowledge arrives two ways. **Research** lets you manufacture charts for
a route you have learned. **A looted chart** both reveals a route and
carries one passage on it — which makes piracy a way to acquire
geography, not just cargo.

### Two charts, and why that matters more than it reads

The Chart House produces **blank** charts; those become **route**
charts, which are what a private passage costs. So the loop is:

```
Scholars research  ─┐
                    ├─→ knowledge of route R  (permanent, per-player, private)
survey mission     ─┘
Chart House:  Paper + Glass       → blank chart      (fungible)
              blank chart + know R → route chart (R)  (names a passage)
sailing R:    spends one route chart (R)
looting:      takes a route chart, and with it the passage
```

Knowledge unlocks the *recipe*; the Chart House is the standing supply;
each crossing spends one. That is what keeps a discovered route an
ongoing cost rather than a switch that stays flipped.

So there are two goods, and they are different kinds of thing:

- A **blank chart** is fungible. One is exactly as good as another.
  `RES_CHARTS` already exists — Paper + Glass → Chart House, added in
  SUPPLY_CHAIN Phase 8 — and fits this exactly, with no enum change and
  no `SAVE_VERSION` bump. Scholars go on wanting it, which is the right
  reading anyway: a scholar's household consumes charts as a good.
  Whether it should now *display* as "Blank Charts" is a naming call.
- A **route chart names a passage.** It is not fungible, and that is
  the wrinkle: `Stockpile.amount[RES_COUNT]` is a flat count per
  resource, with nowhere to record *which* route a chart is for.

The cheap answer is the shape `Island` already uses for `escrow`: a
parallel per-route array, `route_charts[MAX_ROUTES]`, held per island
and hashed like everything else. No item-instance system, no identity
machinery — the same trick, indexed by route instead of by resource.

**This corrects something claimed earlier in this document.** Route
charts cannot simply trade on the order book: an order is keyed by
`ResourceType`, and "a chart" is not enough information to fill. Either
the book learns to carry a *kind and an id* — which is a
generalisation worth deciding before the matcher is written rather than
retrofitting after — or route charts move only by harbour escrow and by
looting. Blank charts trade normally either way.

This also makes the Chart House a permanent industry with recurring
demand rather than a one-off unlock, and keeps piracy a way of
acquiring geography.

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
and one blank chart**, the way a booking dispatches one merchant, one
boat and the goods.

**It takes a set time and it may fail.** The duration is another
integer tick count, like a route leg. The outcome must be a
deterministic function of the mission's identity — and that pattern
already exists and is documented, in `voyage_is_raided`:

> *"FNV-1a over the voyage's identity. Not cryptographic and not trying
> to be: it needs to be well-mixed, integer-only and identical on every
> platform, which rules out anything touching floating point or the C
> library's rand."*

A survey's success should be hashed the same way, over
`(world_seed, scholar, hull, departure tick, target route)`. This is
precisely the place somebody reaches for `rand()` and makes two
machines disagree about whether a passage was found. That symmetry is worth building deliberately rather than
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

## Hulls

**Research boats are built at the Shipyard**, which means the Shipyard
stops producing one kind of vessel. `sim_build_ship` makes a single
ship today and `CMD_BUILD_SHIP` documents `b` as "shipyard index
(unused today)" — so the hull type has a free payload slot waiting for
it, and `Ship` gains a type alongside `owner`.

That is a small change with a long tail: cargo capacity, speed, whether
a hull may carry a merchant or a scholar, and later whether it can
fight, all become per-type rather than the constants
`SHIP_CARGO_CAPACITY` and `SHIP_VOYAGE_TICKS` are today. Worth
introducing the type early even while there are only two, because
retrofitting it after combat exists means touching combat too.

## Phases

1. ~~**Sea geometry.**~~ **Done.** `src/sea.c`: island positions, named
   waypoints, routes as paths with per-leg integer durations, and
   `world_ui` projecting from sea coordinates instead of hand-placed
   screen fractions. Voyages take the route's length rather than one
   constant — crossings now run roughly 77 to 341 ticks around the 200
   they all used to take. The Sea is a pure function of the world seed
   and is regenerated like a Map, so it needed no save, snapshot or
   protocol change.
2. ~~**The order book.**~~ **Done.** `src/orderbook.c` plus the
   rules in `game.c`: `CMD_PLACE_ORDER` / `CMD_CANCEL_ORDER`, reservation
   at posting, deterministic matching at every tick boundary, and a
   match becoming a `Booking` that lands after `sea_crossing_ticks`. A
   trade identity is a `(kind, id)` pair rather than a `ResourceType`,
   which settles one of the open questions below and is what lets a
   route chart be tradeable in phase 3 without growing `RES_COUNT`.

   Merchants and hulls are now reserved capital: a booking takes one of
   each from the **selling** island and gives them back when they have
   sailed home, so trade capacity is a round trip and a standing build
   decision. Capacity is derived from the buildings standing — a base
   of one each per settled island, plus populated Merchant Houses and
   Shipyards — never stored, so it follows demolition and depopulation
   for free. An ask whose island has nothing free is skipped rather
   than stalled on, for the same reason a self-crossing pair is: one
   seller must not be able to shut a good for everyone.

   The faction is a market maker with **home ports**: it holds the last
   two islands, quotes there every `FACTION_QUOTE_INTERVAL_TICKS`, and
   its fills ship and tie up its own hulls like anyone else's. See "the
   faction's liquidity" below for what that settled and what it left.

   **Phase 2 is complete.**
3. **Routes and charts.** Public and private, chart consumption,
   per-route insurance, Scholars research.

   **3a is built:** the sea now generates three routes between every
   pair — one public lane and two private passages — each a distinct
   path, with every private one faster than the lane. Concealment is
   not here yet: all three are generated, and nothing yet tracks what a
   player knows or spends a chart on, so bookings still take the public
   lane. `sea_route_variant` / `sea_route_id` are the handles the rest
   of the phase hangs off; a route id is what a chart will name and
   what `TRADE_ROUTE_CHART` will trade under.

   **3b is built:** `src/knowledge.c` — the first state in the world
   that belongs to a *player* rather than to a place or a thing, since
   a player who loses their colony does not forget the sea. Knowing a
   passage and holding a map of it are separate: the known bit is
   permanent, the chart is spent, one per round trip. A booking sails
   the fastest route its seller can actually use, and the chart is
   reserved when the booking is made rather than on arrival — the same
   discipline as goods and gold, or one map would send out ten cargoes.

   Charts are tradeable in the book as `TRADE_ROUTE_CHART`, which is
   the payoff for the (kind, id) trade identity settled in Phase 2: a
   chart for one passage is a different object from a chart for
   another, and neither could ever have had a `ResourceType` slot. The
   market sells them, priced by the ticks the passage saves over the
   lane — an interim source, since survey and research are the intended
   ones and are blocked on what a failed survey costs.

   **3c is built:** insurance is priced per ROUTE rather than per island
   pair — one number for the water between two islands stopped being
   enough the moment there were three ways across it. A private passage
   starts dearer to cover and is raided far more often, so "faster but
   unsafe" is now a number rather than a sentence. A raided shipment
   lands nothing: the buyer, who did not choose the passage, gets their
   gold back, and the seller bears it — which is what makes a policy
   worth buying and choosing the fast water a decision. Cover is a
   standing policy per harbour (`CMD_SET_INSURANCE`) rather than a flag
   on each order, because `Command`'s four payload slots are already
   spent and because it reads better as what it is.

   **3d is built:** the survey mission. An expedition commits one
   scholar, one research boat and one blank chart; it takes 900 ticks
   and may come back with a passage, with nothing, or not at all. The
   chart is spent on departure either way — that is the gamble — and a
   lost expedition takes the boat and a resident of the Scholars' House
   that sent them. Research boats are laid down at a Shipyard.

   You cannot name the route you are looking for: a survey is aimed at
   an *island*, and the sim picks an undiscovered passage between here
   and there. Asking for a route by id would mean already knowing it
   existed, which is what the expedition is paying to find out.

   **3e is built:** private passages rotate. Each pair holds a pool of
   six and keeps two in play; a year in, the oldest goes out of use, a
   fresh one comes in behind it, and every chart of the retired passage
   becomes waste paper — though you still remember where the water was.
   Pairs rotate on their own offsets, so the sea shifts continuously
   rather than invalidating every chart in the world on one tick.

   **Phase 3 is complete.**
4. **Server authority** ([SERVER_AUTHORITY.md](SERVER_AUTHORITY.md)) —
   turns "everyone knows where the fast route is" into real
   concealment, and makes stockpiles private.
5. **Predation.** Interception with ship attributes and escorts, and
   pirates as something you can hunt rather than a dice roll.

   **5a is built:** a hull is now a choice at the yard, and the choice
   is real because the axes trade against each other — guns cost hold.
   A merchantman carries the cargo that makes trading worth doing and
   cannot defend it; a warship takes anything at sea and carries almost
   nothing. The answer to "how do I move cargo through dangerous water"
   is therefore a second ship rather than a compromise ship, which is a
   decision about a fleet.

   An escort sails when its charge sails — on the same tick, to the
   same place, because the intercept rule matches on departure and "we
   left together" has to be true in the data. Its guns join the
   defence, so a raider's odds against a convoy are worse than against
   the merchantman alone.

   Losing costs the hold and wears the hull, and a worn hull fights
   worse. It never costs the ship: test_intercept has said since it was
   written that *"a hold is a setback, a ship is an evening"*, and PvP
   that can cost you the evening is PvP most people decline to be in.
   Damage is a reason to go home — a Shipyard refits, slowly enough
   that a refit is a real absence from the water.

   **Still to come:** pirates as entities rather than a derived
   boolean. Today `shipment_is_raided` decides a raid happened; nothing
   was there to fight, and the settled design says hunting pirates
   should yield geography as well as cargo (charts are rare NPC loot).
   That is the half of this phase with no existing mechanic behind it.

Phases 2 and 3 work under the current architecture; they are simply
less secret without phase 4. That is the point of this ordering — the
game design lands and is playable first, and the architecture change
upgrades it rather than gating it.

## Settled

- **A survey takes a set time and may fail**, with the outcome hashed
  from the mission's identity the way voyage raids already are.
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
- **A failed survey can cost the scholar and the boat**, not just the
  blank chart. That is what makes a survey an expedition rather than a
  dice roll with a fee: the crew is at risk, so sending one is a
  decision about people as well as money.
- **Route charts expire after a year**, and when one does a new private
  route comes into play in its place. Built in Phase 3e, and the
  architectural cost was paid the cheap way: each pair GENERATES a pool
  of six private passages and keeps two in play, with a one-byte cursor
  per pair saying which two. So the `Sea` is still a pure function of
  the seed — geometry, names, durations and the whole pool all
  regenerate identically everywhere — and the only world state is the
  cursor. Making the entire Sea mutable would have been simpler to
  write and much worse to live with: a desync in generated data is a
  bug in a generator, a desync in saved data is a bug anywhere. This is what stops the map from
  being solved: a passage learned is not a passage owned forever, the
  sea keeps changing shape, and the Chart House has a standing reason
  to exist rather than a one-off job.
- **Three routes exist between any pair of islands.** One public, two
  private. That is enough for a real choice — a safe default and two
  passages worth discovering, which can differ from each other in
  length and exposure rather than just being "the fast one" — while
  staying small enough that a player can hold the whole map of a pair
  in their head, and small enough that choosing between known routes is
  a decision rather than a menu. Phase 1 generates one route per pair;
  phase 3 widens `Sea` to three and conceals two of them.
- **A tradeable thing is a `(kind, id)` pair, not a `ResourceType`**
  (phase 2, as built). A chart for one passage is a different object
  from a chart for another, so they cannot share an enum slot, and
  giving each generated route its own would grow `RES_COUNT` — which
  sizes every stockpile, price table and snapshot field — with the size
  of the world. The pair packs into one `Command` payload slot.
- **You cannot trade with yourself, and trying does not wedge the
  book.** A self-crossing top of book steps aside rather than ending
  the pass; otherwise a player could shut every other trader out of a
  good, for free, by posting a bid and an ask nobody would take.

## Still open

- **Should the public lane carry a convoy penalty at all?** It does
  today (9/8), which is what guarantees every private passage is
  strictly faster. `SEA_UNITS_PER_TICK` was re-fitted from 21 to 26 to
  keep the average public crossing near 200 ticks, so the game as a
  whole did not get slower — but that means private routes are now
  faster than any voyage was before, rather than public being slower
  than it was. Whether that is the right side to have moved is a
  balance question best answered once charts actually gate them.

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
