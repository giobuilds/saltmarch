# Needs, basics and luxuries — the settled design

> Status: **Phases 1-3 done; Phase 4 next.** This document is the decision record for
> the needs rework, written after the design was settled in conversation
> and checked against the code. It supersedes the needs half of
> [new-happiness-design.md](new-happiness-design.md), which remains the
> brief for the six-factor wellbeing *scoring* layer above it.

## Why

A player built a Marsh Cottage, a fisher's hut, a lumberjack and a farm,
connected them all by road, and watched their marshfolk die. They were
not doing it wrong. A Marsh Cottage wanted **Fish, Oilskins and Marsh
Gin**, all-or-nothing, every 30 seconds, and that opening supplies one of
the three — the other two are two-step chains behind fertility checks
(Sheep Pasture → Knitting House, Potato Field → Still).

Three separate faults, in the order they bite:

1. **The intuitive opening is wrong.** Food does not keep anyone alive
   here; a specific manufactured good does.
2. **All-or-nothing has no memory.** One missed tick costs a resident,
   so a chain that stutters is punished as hard as a chain never built.
3. **Upgrading kills demand.** An Artisan house stops wanting Fish
   entirely, so early infrastructure becomes worthless the moment it
   succeeds — which is the opposite of what the maritime layer wants
   from a home island.

## The shape

**Two need classes.**

- **Basic needs** are survival. Missing them does not kill immediately;
  it drains a reserve, and a house dies only when the reserve is empty.
- **Luxury needs** are comfort. They are what raise happiness above
  neutral and what make a population grow.

**Happiness is 0–10.** All basics met puts a house at neutral (5). Each
luxury met raises it. Sustained high happiness grows the population;
sustained absence of basics shrinks it. It replaces the binary `happy`
flag, and it stays an integer — it is hashed state, and this project's
determinism doctrine has no room for a float there.

**Tiers inherit.** A tier's basics include the basics of the tier it
came from. Fish and Grain never stop being wanted, so a first island
keeps its value for the whole game.

## The table

| Tier | Basic needs | Luxury needs |
|---|---|---|
| Marshfolk | Fish, Grain | Oilskins, Marsh Gin |
| Artisans | Fish, Grain, Preserves | Sewing Machines, Fur Coats, Spectacles, Windows |
| Wrights | Sausages, Bread | Soap, Beer, Plantain Fry |
| Engineers | Sausages, Bread, Lamps, Pocket Watches | Gramophones, Banquet |
| Merchants | Coffee, Flatbread | Rum, Marsh Hats, Wool Cloaks, Plantain Fry |
| Investors | Coffee, Flatbread, Sparkling Wine, Cigars, Chocolate | Jewellery, Perfume |
| Scholars | *the basics of the house it upgraded from* + Books | Charts, Coffee, Spectacles |

**Marshfolk's basics are the fix.** Fish and Grain are a fisher's hut and
a farm: the opening a player reaches for now keeps them alive, and the
two chains that used to kill them buy happiness instead.

**Scholars carry their origin.** A Scholar's House reached from a Marsh
Cottage wants Fish, Grain and Books; one reached from a Wright's House
wants Sausages, Bread and Books. That is per-INSTANCE, not per-type — the
first thing in this game whose needs depend on its history rather than
its kind — and it follows from the existing rule that any house may
become a Scholar's House where an Academy stands.

**Plantain Fry appears twice**, in Wrights' and Merchants' luxuries. That
is deliberate as recorded, and cheap to change: if the intent was to move
it rather than share it, it is one line in `TIER_DEFS`.

## Consumption

**Raw goods scale per resident. Refined goods are per house.** You eat as
a person; you own manufactured things as a household. The split uses
`RESOURCE_CATEGORIES` (`RCAT_RAW` / `RCAT_REFINED`), which already
exists, so no good needs reclassifying.

`HOUSE_CAPACITY` drops from 10 to **6**.

### The arithmetic that decided it

Every producer only ticks while an agent is physically working there, and
every agent is somebody's resident. So the load-bearing ratio is
**workers per resident**, and it must stay below 1 or the economy cannot
close. Measured against real `BUILDING_DEFS` rates by walking each good's
chain to its raw inputs:

| tier | workers/resident (cap 6) |
|---|---|
| Marshfolk | 0.68 |
| Wrights | 0.64 |
| Merchants | 0.96 |
| Investors | 1.24 |
| Artisans | 1.97 |

Four findings, two of them counter-intuitive enough to be worth keeping:

1. **Charging every good per resident does not close.** At capacity 10 it
   is ~2.1 workers per resident for the base tier alone — the economy
   diverges. Hence raw-only.
2. **Smaller houses make it WORSE.** Per-resident costs are unaffected by
   capacity while per-house costs are amortised over fewer people, so
   Marshfolk go 0.59 (cap 10) → 0.68 (cap 6) → 0.78 (cap 4). Capacity 6
   is a decision about the feel of a marsh village, taken knowing it
   costs headroom rather than buying it.
3. **Moving Sewing Machines to luxury buys nothing.** A refined good is
   charged per house either way, so the building count is identical. It
   is still right — Artisans now survive a Machine Shop outage instead of
   dying of it — but it is a change of meaning, not of cost.
4. **Per-tier closure is the wrong test.** Upper tiers are *supposed* to
   be net importers; that is what the sea is for. What matters is the
   island-wide mix, which closes at roughly **one artisan house per nine
   cottages** before an island must trade for the difference.

So the property to guard in CI is the BASE tiers (Marshfolk, Wrights),
not every tier.

## Phases

Each is one commit with the whole verification ladder green. The
determinism fixture's hash moves at 1, 2 and 3 — deliberately, and once
per phase, which is what makes each move attributable.

**1 — the split, as data. DONE.** `TierDef` gains `basic[]` and
`luxury[]`; the table above replaces `needs[]`; `PopData` gains
`origin_tier`. Behaviour is unchanged in shape — still all-or-nothing,
now over the union of both lists — so exactly one thing moves at a time.
Save v29, snapshot v11, protocol 22 (a snapshot format change is a
protocol change: MSG_WORLD carries one). Fixture hash
8bc57efffab2590e → d0b0db6ac6a97f5e.

*What the existing tests caught, which is why they were worth having:*

- `test_chains` refused Plantain Fry in the Wrights' luxuries, because
  it asserts every tier is satisfiable "from home plus a highland" and
  Plantain Fry is a jungle good. That assertion was written when a tier
  had one list. It now checks BASICS against the named climates and
  LUXURIES against the whole archipelago — a northern Wright's House
  lives on sausages and bread and trades south for the rest, which is
  the intended shape rather than a fault. The luxury check is
  archipelago-wide and would have caught Wool Cloaks or Plantain Fry
  being dropped from the table altogether.
- `test_tier` asserted "nobody eats raw Grain any more" — the exact
  property that made a player's first farm feed nobody. It asserts the
  opposite now, and the comment says why.
- `test_vitals` expected the alert strip to ask for Oilskins once Fish
  was in store; it asks for Grain, because a basic is named before a
  luxury. That ordering is now its own assertion.

**2 — happiness replaces the flag. DONE.** `happy` (0/1) became
`happiness` (0..10). Basics met → NEUTRAL (5), each luxury an equal
share of the way to MAX, growth at GROW (8), departure at 0. Save v30,
snapshot v12, protocol 23. Fixture hash d0b0db6ac6a97f5e →
cf9c654ed9aa6283.

*The ladder is the reserve, which is why this needed no second field.*
Happiness moves ONE STEP per needs tick toward what the supplies
deserve, so nothing counts consecutive failures — the number already
remembers. A thriving house that loses its larder keeps everybody for
about ten ticks (five minutes of wall clock) and then loses one at a
time; a rescued one climbs back at the same pace. The hysteresis the
brief asked for falls out of the drift rather than being bolted on.

*Basics are genuinely not all-or-nothing now.* A house with one of its
two basics scores a fraction of neutral, eats what there is, and stays
alive indefinitely — miserable, not dead. Only a house with nothing
drifts to zero, and only a house at zero loses anybody. Luxuries are
read only once every basic is met: people buy gin after bread, not
instead of it.

*A house that empties completely does not repopulate itself*, because
there is nobody left to be unhappy. That is deliberate; `tests/
test_happiness.c` says so out loud rather than leaving it to be
discovered.

**3 — consumption scales. DONE.** Raw × residents, refined × 1.
`HOUSE_CAPACITY` 10 → 6, `GOLD_PER_RESIDENT` 2 → 3. A good counts as met
only if the whole amount was there — feeding four of six people is not
feeding the house — but whatever WAS there is eaten either way, so a
shortage shows up as an empty warehouse rather than as goods left on a
shelf. Save v31: no field changed shape, the CEILING did, and a world
saved at ten residents a house would quietly shed people to fit a game
whose ceiling is six.

**And the determinism fixture's hash did not move, which is a finding.**
It was supposed to. The reason it did not is that
`replay_record_demo_session` places one house, buys eight Wood, builds a
ship and sails it — and never connects that house to a warehouse or puts
a single Fish on the island. So `pop_update` has taken the "no road to
Warehouse" branch for the whole life of the fixture, and **the needs
economy has never been covered by the cross-platform determinism gate at
all**: not consumption, not growth, not the gold, not the ladder. Three
phases of change to that code are invisible to the check that exists to
notice change.

That is its own piece of work — a fixture that feeds a house needs a
road laid between two positions the recorder picks at runtime, and doing
it badly would mean a fixture that silently goes back to testing nothing.
It is listed here rather than bolted onto this phase because a fixture
change moves the hash for its own reason, and the whole discipline of
this plan is one reason per move.

**4 — say it on screen.** The alert strip separates *starving* (a basic
is missing) from *discontent* (a luxury is), and the confirm popup shows
a tier's two lists rather than one.

**5 — the closure test.** A headless test walks `BUILDING_DEFS` and
fails the build if a base tier exceeds ~0.8 workers per resident, so a
future good with a deep chain cannot quietly make Marshfolk
unsustainable.

## What this does not do

It does not build wages, taxes, ageing, crime or districts — none of
which exist, and all of which
[new-happiness-design.md](new-happiness-design.md) assumes. The six-factor
wellbeing model sits ABOVE this as a read-only projection over the UI
snapshot, where floats and `log()` are harmless because no two machines
ever have to agree on them. Goods → happiness is the simulation's hashed
truth; the six factors are the explanation offered to the player.
