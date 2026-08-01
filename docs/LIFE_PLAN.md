# Residents, lives and labour — the design

> Status: **Phase 1 done; Phase 2 next.** The calendar is settled — see
> [The calendar](#the-calendar), taken from Stellaris after Cities:
> Skylines' answer was examined and rejected.
>
> This supersedes the *unit of computation* in
> [new-happiness-design.md](new-happiness-design.md), which says
> wellbeing is scored per district and "population is a count inside a
> group struct, not a collection of agents". That is precisely what this
> document replaces. The six factors, their weights and the log-income
> curve survive intact — see [The wellbeing
> projection](#the-wellbeing-projection).

## Why

Three things are true of the current model, and each is a ceiling on
what the game can say.

**A building's labour is a light switch.** `agents_assign_jobs`
(agent.c) keeps a `claimed[]` array and skips any building another agent
has taken, so `Building.worker_count` is 0 or 1 and nothing else.
Production is a step function of labour: nothing at zero workers, full
rate at one, and identical at six. The gap between 0 and 1 is infinite
and everything above it is flat.

**A resident is a number.** `PopData.residents` is an integer, and the
`Agent` that walks to work is derived state rebuilt from it every
session. Nobody has a name, an age, a spouse or a history, so nothing
that happens to an island can happen to a *person*.

**Happiness buys nothing but arrival.** NEEDS_PLAN made happiness a
ten-rung ladder that decides whether people move in or leave. It does
not touch what they do while they are here.

## The shape

**Buildings have a job capacity.** A Fisher's Hut staffed by five people
lands five fish where one lands one. Today's rate becomes the
*per-worker* rate, not the building rate — the direction that matters,
and the section below says why.

**Full staffing is worth more than the sum of its workers.** Five
workers land nine fish, not five. This is the load-bearing number in the
whole document; it is what pays for everybody who does not work.

**Residents are people.** A name, an age, and one of four stages:
infant, teenager, adult, retired. Only adults work. They are born, they
marry, they die. A newly built house arrives peopled — those residents
are immigrants, adults with no parents on this island, which is both
the simplest rule and the right fiction.

**Status modifies productivity.** Slept well or poorly, ate well or
poorly, married or single, employed or unemployed. These raise and lower
what a worker produces, as an integer percentage with a hard floor.

**The needs tick becomes the month.** Days, months, seasons and years
out of the clock the sim already keeps — and it is the *same* clock,
not a second one running alongside.

**Residents outlive the session.** A life is 7-8 hours of play. You
inherit people mid-life and they die on you at a moment you did not
choose, which is the point.

## The arithmetic that decides it

`tests/test_closure.c` measures **workers per resident** and fails the
build when a buildable tier cannot be staffed. Every number below comes
out of it. Today:

| tier | basics | total | headroom to the wall |
|---|---|---|---|
| Marshfolk | 0.47 | 0.68 | 32% |
| Wrights | 0.26 | 0.82 | 18% |
| Merchants | 0.24 | **0.96** | **4%** |

### 1. The rate change alone is free

"Five workers, five fish" leaves output per worker exactly where it is,
so the ratio does not move at all. Marshfolk stay at 0.68. No
`tick_seconds` needs rebalancing for this half.

*The opposite framing is a catastrophe*, and it is one sentence away:
if today's rate were redefined as the FULL-capacity rate and one worker
gave a fifth of it, output per worker would divide by five and every
tier would land at 3-5 workers per resident. The capacity must be
additive on top of the current rate. This is the single easiest way to
destroy the economy in this document.

### 2. Only adults working divides everything

Demand scales with residents; supply now scales with adults. So every
ratio divides by the adult fraction. At a plausible 55%:

| tier | today | adults only |
|---|---|---|
| Marshfolk | 0.68 | 1.24 |
| Wrights | 0.82 | 1.49 |
| Merchants | 0.96 | **1.75** |

**All three buildable tiers go over the wall, Marshfolk included.** This
is not a balance problem discovered in playtesting; it is division, and
it happens on the day the age pyramid lands.

### 3. So the capacity bonus is mandatory, not decorative

With adult fraction *f* and a full-staffing multiplier *m*, the ratio is
`R / (f · m)`. Staying under the wall needs `m > R / f`:

| tier | required multiplier at f = 0.55 |
|---|---|
| Marshfolk | > 1.24 |
| Wrights | > 1.49 |
| Merchants | > **1.75** |

**m = 1.8** clears all three — a Fisher's Hut at five workers landing
nine fish. Merchants come out at 0.97, which is under the wall by three
percent and no more. Merchants are the tightest tier in the game and
this document does not improve that; it preserves it.

### 4. Age-weighted rations are what buy real headroom

If infants and the retired eat half a raw ration, demand falls to 77.5%
of today's while supply is unchanged:

| tier | today | with capacity bonus + age-weighted rations |
|---|---|---|
| Marshfolk | 0.68 | 0.53 |
| Wrights | 0.82 | 0.64 |
| Merchants | 0.96 | **0.75** |

That is *better than today* across the board, and it is the version to
build. `tier_good_amount()` is already the one place consumption is
decided (NEEDS_PLAN Phase 5 made it public precisely so the closure test
charges what the sim charges), so age-weighting is a change to one
function and its callers.

### 5. Which sets the productivity floor at 0.75

Modifiers multiply the ratio by `1 / p`. Starting from Merchants at
0.75, the wall is reached at **p = 0.75**. So a worker's productivity
may never fall below three quarters of nominal, and that floor is not a
tuning knob — it is the closure guard wearing a different hat.

**Note what this means without the two levers above: Merchants have 4%
of headroom today, so ANY modifier able to dip below 1.0 breaks that
tier immediately.** Status modifiers cannot land before the capacity
bonus and the age-weighted rations.

### 6. The feedback loop is the dangerous part

Happiness → productivity → goods → happiness is a closed positive loop.
Unhappy people produce less, so there is less food, so they are
unhappier. [new-happiness-design.md](new-happiness-design.md) identifies
exactly this shape for the taxation loop and prescribes the fix: a
floor, hysteresis, and no single input allowed to dominate.

**Two thirds of that is already built.** The happiness ladder moves one
rung per needs tick and carries ten rungs of reserve, which is the
hysteresis, in hashed integer state. The floor is §5. What remains is
the third rule: status must be several independent inputs, so one bad
harvest cannot move every one of them at once.

## Residents are world state now

This is the largest change in the document and everything else is
downstream of it. Names, ages, marriages and deaths enter `sim_hash`,
come out of the sim's LCG in a fixed order, and land in the snapshot.

**Split identity from motion.** `Agent` is 1060 bytes and 1024 of that
is `path[]` — derived, never saved, rebuilt every session. Identity does
not belong in it:

| struct | holds | size | hashed | saved |
|---|---|---|---|---|
| `Resident` | name indices, age, stage, spouse, status, tenure | ~24 B | yes | yes |
| `Agent` | position, path, state machine | 1060 B | worker tally only | no |

512 Residents is 12 KB in the snapshot. 512 Agents would be half a
megabyte. And Agents are only needed for residents who commute, so the
`MAX_AGENTS` 512 ceiling stops being a ceiling on *population* and
becomes one on *the working population* — roughly 930 residents at a 55%
adult fraction, against 512 today.

**A name is two `uint16` indices** into static tables, not a string:
four bytes, reproducible from the seed, and no allocation anywhere in
the sim.

**Ask VISIBILITY.md before showing a rival your residents.** Named
individuals are the first thing in this game that is person-shaped, and
`docs/VISIBILITY.md` is the document that decides what a client is
allowed to know. Redaction already exists per-client (SERVER_AUTHORITY
Phase 3); this is a question about what it should cover, not about
whether the mechanism is there.

## The calendar

**Settled: the needs tick is the month.** Everything else is a multiple
of it, and there is exactly one clock in the game.

| unit | length | is |
|---|---|---|
| **month** | 300 sim ticks / 30s | one needs tick — eating, growth, gold |
| season | 3 months | 90s |
| **year** | 12 months | **6 minutes** |
| day | 1s, 30 to the month | cosmetic; never a unit anything is decided on |
| lifespan | 70-80 years | **7-8 hours** |

### How this was arrived at

The first draft of this document posed the calendar as a trilemma —
*realistic lifespans, a legible calendar, sessions of sane length, pick
two* — because `AGENT_SHIFT_DURATION` (60s) plus `AGENT_REST_DURATION`
(15s) makes a 75-second working day, and a 360-day year built on that
is 7.5 real hours with a 525-hour lifetime.

**Cities: Skylines resolves it by compression.** A cim's default
lifespan is about **6 in-game years** — a tenth of a human life — and
one calendar month advances per in-game day. The date stays legible;
the *lifespan* is what gives. It shipped in the genre's biggest title,
so it works, but it comes with two documented costs: players reject the
default in numbers (the popular ageing mods offer 20-24 and 80-96 year
variants against the stock 5-6), and cims **die in cohorts**, which is
that game's best-known population complaint.

**Stellaris does not resolve it. It removes it.** 30-day months, 12
months, a 360-day year, one real second per day — so a year is six
minutes and a decade is an hour — and *normal pops do not age or die of
old age at all*. Only **leaders** have lives: a base guaranteed lifespan
of 80 years, recruitment at a randomised age of 28-50, and a per-month
death chance once the guarantee expires. Because only a handful of
entities have lifespans, an 80-year life and a 6-minute year coexist
with no contradiction anywhere.

**The assumption the trilemma smuggled in was that a life must fit
inside a session.** It does not. You inherit a Stellaris leader
mid-life and they die on you, unplanned, at a moment you did not
choose — which is more dramatic than watching a full arc, not less, and
is why nobody writes angry threads about Stellaris lifespans.

Dropping that assumption dissolves the trilemma: realistic lifespans and
a legible calendar are compatible the moment lives are allowed to span
sessions.

*Anno 1800 is the fourth data point and the one that argues against all
of this.* It has no calendar and no ageing whatever — population is a
number — and Anno is what Saltmarch structurally **is**: needs tiers,
luxuries, islands, ships, an order book. The genre's closest relative
deliberately does not build this. That is not a reason to stop; it is a
reason the ageing has to earn its place by producing play Anno cannot,
which in this design it does, through status modifiers feeding
productivity (§5, Phase 7).

### One clock, not two

`NEEDS_INTERVAL_TICKS` is 300 sim ticks and is already the economic
heartbeat: integer, hashed, and where consumption, growth and gold
happen. It is structurally the same object as Stellaris's monthly tick,
so the calendar costs no new state beyond a counter.

**The shift cycle should be retuned to match it.** 60s of work plus 15s
of rest is 75 seconds, which beats against the 30-second needs tick for
no reason — two unrelated periods drifting past each other, a wart that
predates this document. Retune to **24s work + 6s rest** and one work
cycle *is* one month *is* one needs tick: 300 ticks, one clock,
everything else a multiple. It is a behaviour change and moves the hash,
and it should land in Phase 4 with the calendar rather than being left
as a latent oddity for the ageing code to trip over.

Days survive only as decoration, exactly as in Stellaris — 30 to the
month so the month is uniform, and nothing is ever decided on one.

### Ages must be jittered at spawn

**Stated as an invariant, not a tuning note.** Residents born or spawned
together age together, become adults together, retire together and die
together, taking every worker in a chain with them at once. Cities:
Skylines has this defect and it is the loudest complaint about its
population model; Stellaris avoids it by recruiting leaders at a
*randomised* 28-50.

Saltmarch is primed for it: `pop_init` starts a house at five residents
and growth adds one per needs tick, so a player who lays six cottages in
a minute creates thirty people of identical age.

New residents therefore arrive with ages drawn from the sim's LCG across
a spread — adults of 20-45 for a newly built house, since those are
immigrants — and this must be in from the phase that introduces ageing.
It is one line then and an economy-breaking retrofit later.

## The wellbeing projection

[new-happiness-design.md](new-happiness-design.md) says wellbeing is
computed per district and per tier, "never per resident", because
population is a count. Individual residents make that decision moot at
this scale, and the six factors get *easier*, not harder:

| factor | with residents |
|---|---|
| Social support | married/single, household size, neighbours |
| Income | wages, unchanged |
| Health | age, stage, whether they ate |
| Freedom | employed/unemployed, reachable job variety |
| Generosity | unchanged |
| Corruption | unchanged |

The weights, the normalisation and the `log()` all stay exactly where
that document puts them: **above** the sim, a read-only projection over
the UI snapshot, where floats are harmless because no two machines ever
have to agree on them.

**Happiness → productivity is the one part that crosses into hashed
state, and that part is integer.** A percentage, one division, at the
end. `output = (workers * rate_num * pct) / (rate_den * 100)`. Never a
float in the production path — 3.5 fish is not just illogical, it is a
desync.

### A cast, not a census

**The simulation tracks everyone; the interface tells you about the
handful that matter this month.** 512 named people is 512 things a
player cannot care about, and a wellbeing screen that lists them all is
unreadable by construction.

This is the *other* half of what Stellaris gets right, and it is a UI
decision rather than a simulation one — leaders matter there because
there are about ten of them, not because pops are cheap. Surface heads
of household, the foreman of a chain that is failing, whoever just
died. One legible line —

> *Bess Cobbleworth, 61, has worked the fishing hut for nineteen years
> and is hungry*

— is a game. Five hundred rows of the same thing is a spreadsheet, and
the difference costs nothing in the sim.

Note the ordering risk: this only works if a resident's *history*
(where they have worked, how long) is cheap to state. Tenure is already
wanted by the social-support factor, so one small field serves both —
worth knowing before Phase 3 fixes the `Resident` layout.

## Phases

One commit each, whole ladder green, and the determinism hash moves once
per phase so every move is attributable. `tests/test_closure.c` is the
gate for 1, 2, 5 and 7; it currently assumes one building is one worker
and has to learn about staffing, adult fraction and the productivity
floor as those land.

**1 — a building holds a crew. DONE.** `agents_assign_jobs`' `claimed[]`
flag became a headcount checked against `building_worker_cap()`;
`b->timer += b->worker_count` replaced `b->timer++`, inside a `while`
loop so a crew that earns more than one unit in a tick keeps the
remainder. No ageing, no identity, no bonus. Save v32 — no field changed
shape, but a log recorded when one agent could claim a whole Fisher's
Hut replays into a different world under a rule where five can.

*The crew size is derived from the building's CATEGORY, not stored per
def.* The categories already say how big a thing is — `BCAT_WORKSHOP` is
documented as "one artisan's worth of processing", `BCAT_FACTORY` as
heavy industry — so one table states the rule instead of ninety numbers
nobody can compare. Footprint deliberately does not enter it: only 1x1
and 2x2 exist, and a fishing crew is bigger than the hut it lands its
catch at. A producing category naming no crew falls back to 1, which is
exactly the old behaviour; `tests/test_defs.c` asserts nothing takes
that path.

*Ratio-neutral, and measured rather than asserted.* `test_closure`'s
table is unchanged — 0.68 / 0.82 / 0.96 — because five workers landing
five fish leaves output per worker exactly where it was. What changed is
how many BUILDINGS those workers stand in: land and capital, not labour.
That is why `building_worker_cap()` appears nowhere in that test.

*And the determinism fixture was wrong a third time.* The hash did not
move, because `replay_record_demo_session` had never placed a PRODUCING
building — so no agent was ever hired and `island_tick_buildings()`
skipped every building on the island for the fixture's whole life. The
production path had never once run under the cross-platform gate. Fixed
in its own commit with a Fisher's Hut, and the fixture's return value
now insists somebody was employed and something was made.

*A Sawmill was tried first and broke it*, which is worth recording:
at ~240 Gold placed it left 135 of the island's 1000 for food, so the
Grain order partially filled, the Oilskins were refused outright, and
the house the fixture exists to feed went hungry. **A fixture has a
budget, and adding to it spends something the older assertions were
relying on.** The hut is 60 Gold, needs no input, and produces the very
thing the house eats — so the fixture now buys deliberately less Fish
than its residents will consume, and "more Fish than were ever
purchased" is a claim about production that nothing else can explain.
Fixture hash `215c62c28be52bd6` → `4901bec1db08ed02`.

*One thing learned about `worker_count` while testing it:* it is an
instantaneous tally, not a roster. Agents work 60s, commute, rest 15s
and commute back, so reading it on one arbitrary tick can say 0 about a
building that is plainly producing. Nothing new — it is what the field
has always meant — but a test that sampled it once would fail
intermittently for reasons unrelated to hiring, so `test_staffing.c`
takes the peak over a window. It is also why an island's real output is
about four fifths of its headcount, which is the shift/needs-tick
misalignment Phase 4 retunes.

**2 — full staffing is worth more.** The super-linear bonus, m ≈ 1.8, as
integer numerator/denominator per def. Nothing else changes. This is the
phase that makes where labour goes a decision at all, and the phase that
buys the headroom Phase 5 spends.

**3 — residents have names.** The `Resident` struct, hashed, saved,
snapshotted. Everyone is an adult, nobody ages, nothing behaves
differently. Save, snapshot and protocol versions all move; the hash
moves for the added state only. Deliberately inert, so the
serialisation is proven before anything depends on it.

*Get the layout right here*, because every later phase widens it and a
snapshot format is expensive to revisit: tenure is wanted by both the
social-support factor (Phase 8) and the cast lines that make an
individual worth reading about, so it is one field serving two callers
rather than an afterthought.

**4 — the calendar.** The needs tick becomes the month; season, year and
the cosmetic day are multiples of it. Read-only to begin with: a date on
screen and seasons as flavour. **The shift retune (60+15 → 24+6) lands
here**, so one work cycle is one month before anything depends on the
alignment.

**5 — ageing, birth and death.** Stages gate work. Two things land in
the SAME phase and neither is optional:

- **Age-weighted rations**, because this is the phase that would
  otherwise put every tier over the wall — the closure test has to be
  green on the commit that introduces the pyramid, not the one after it.
- **Age jitter at spawn**, because a cohort that ages together dies
  together, and retrofitting that is an economy-breaking change rather
  than a balance pass.

**6 — marriage and households.** Pairing from the sim's LCG in a fixed
order. Feeds social support, and gives the ghost feed something worth
saying.

**7 — status modifies productivity.** Slept, ate, married, employed →
an integer percentage with the 0.75 floor from §5. Several independent
inputs, so no single shortage moves all of them.

**8 — the wellbeing projection.** The six factors, per resident, floats,
entirely above the sim — surfaced as a cast rather than a census.

## What this does not do

It does not build districts, wages, taxes, corruption or crime. Phases
1-7 are the simulation's hashed truth about goods and people; the
explanation offered to the player is Phase 8, and it can be wrong
without anything desyncing.

It does not make residents individually addressable by commands. A
player builds houses and industry; they do not order Bess Cobbleworth to
the fishing hut. Every command in this document is still about
buildings.
