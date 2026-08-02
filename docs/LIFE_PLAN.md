# Residents, lives and labour — the design

> Status: **Phases 1-7 done; Phase 8 next.** Phase 7 leaves a known
> economic gap — the island does not fund its own imports; see its
> entry. The calendar is settled — see
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
out of it.

**§1-3 are the arithmetic as it stood when this plan was written**, and
are kept in that state deliberately: they are why the design has the
shape it has, and rewriting them to today's numbers would hide the
reasoning. §4 onward carry what has since been measured.

| tier | basics | total | after Phase 2 |
|---|---|---|---|
| Marshfolk | 0.47 | 0.68 | **0.38** |
| Wrights | 0.26 | 0.82 | **0.47** |
| Merchants | 0.24 | 0.96 | **0.55** |

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

### 4. Age-weighted rations buy less than this document first claimed

**This section has been wrong twice, and both errors were found by
measurement rather than by review.**

*The first version* said the half ration takes demand to 77.5% across
the board, giving 0.53 / 0.64 / 0.75. It applied that factor to the
whole bill. It cannot: `tier_good_amount()` charges **refined goods per
household**, so the number of mouths never entered their cost. Only
Marshfolk eat anything raw — Fish and Grain. Everything above them is
manufactured, and structurally immune to the lever.

*The second version* proposed two fixes, and **Phase 5 measured both
before building them. Each makes the problem worse:**

| | Merchants today | projected |
|---|---|---|
| as shipped | 0.55 | 1.10 |
| charge provisions per person | **3.31** | 4.67 |
| add Fish as a basic | 0.66 | 1.16 |
| both | 3.42 | 4.82 |

The reason is one line: the problem is *too much work per eater*, and
both proposals **add demand**. Removing the per-household discount
multiplies a bill sixfold; adding a need adds to it. A ~22% ration
discount on part of the bill can never offset either.

**And the number the whole blocker rested on was invented.** The 0.55
adult fraction was a guess written into this document. `test_ageing.c`
now runs a real island for sixty years and measures it:

> **typical 80%, worst single year 41%, worst five-year mean 48%**
> — across ten fixed seeds

An island peopled by adult immigrants who then age in place sits far
above the guess. So **nothing in the economy needed rebalancing at
all** — which is the finding, and it only exists because the guess was
replaced by a measurement before anything was built on it.

*The statistic is the worst SUSTAINED stretch, not the worst year.* A
single bad year is absorbed by the happiness ladder — ten months of
buffer, which exists for exactly this — so the year is the wrong number
and the five-year mean is the right one.

*And the first attempt at measuring it was itself wrong.* `game_init()`
seeds from the **clock**, so the test ran one randomly generated world,
reported 50%, and CI ran a different world and reported 45%. A
measurement that changes per run is a sample being quoted as a
constant. It uses fixed seeds now, and so do the other tests that build
villages.

### 4b. Merchants are import-only, by decision

At the typical 80% every tier closes comfortably. Through the **worst
sustained five years** (48% adults) Marshfolk reach 0.65 and Wrights
**0.99** — clear, but Wrights only just — and Merchants reach **1.15**.

**Wrights being at 0.99 is worth staring at.** They have one percent of
headroom through a bad demographic stretch, which means they are the
next tier to break if anything at all is added to their needs or taken
from their labour. Phase 6's children will take from their labour.

Three fixes were measured and rejected (the table above, plus
shortening the luxury list, which changes what the tier is). So the
decision is that **a merchant town does not feed itself.** The sea
already supports that, and it is arguably what a merchant town *is*.

`tests/test_closure.c` asserts this rather than exempting it, so the day
somebody makes Merchants self-sufficient the test reports that the
policy has changed instead of quietly passing.

### 5. Which sets the productivity floor

Modifiers multiply the ratio by `1 / p`. Against the measured floor
(50% adults) and the tiers that must feed themselves:

- Marshfolk reach the wall at **p = 0.65**, Wrights at **p = 0.99**.
- So Wrights can bear essentially **no** productivity loss during a bad
  demographic stretch, and any floor below 1.0 is a bet that the
  demographic trough and the unhappiness trough never coincide.

That is tighter than this document once assumed and is a real
constraint on Phase 7: **status modifiers have very little room below
1.0**, and most of their range should be upward — a well-rested, well-
fed, married worker producing *more* rather than a miserable one
producing less.

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

**2 — full staffing is worth more. DONE.** `building_work_advance()`
returns **2w-1**: one worker alone pays the overhead — hauling, tending,
keeping the fire in — and the second arrives to find it paid. A full
Fisher's Hut of five lands nine fish where five lone workers in five
huts land five. Save v33.

*One rule, no second table.* Output per worker at a full workplace is
(2c-1)/c, which is 1.67 at a workshop's three and 1.83 at a factory's
six — so bigger workplaces reward filling more, because the one
worker's overhead spreads further. That falls out of the formula rather
than being tuned, and it is the right direction: economies of scale
belong to the large. At one worker it returns 1, so nothing is taken
away from a half-empty island; the bonus is only ever a reward.

*This is the phase that makes where labour goes a decision.* Under
Phase 1's linear rate, four huts with one worker each were identical to
one hut with four, so the choice was empty. Now concentration wins.

*The closure table moved, deliberately and for the first time:*
0.68 / 0.82 / 0.96 → **0.38 / 0.47 / 0.55**. Fixture hash
`4901bec1db08ed02` → `e9a9639f4bcd06a0`.

*And the test grew a projection that immediately found §4 wrong.*
`test_closure` now computes what an age pyramid would do to each
buildable tier, splitting the bill by whether demand scales with mouths
or with houses — which is how the half-ration lever turned out to be
worth nothing to Wrights and Merchants, and how Merchants turned out to
project onto the wall. Better found here than at Phase 5, where the fix
would be a rebalance of the whole def table rather than a decision.

**3 — residents have names. DONE.** `Resident` — 24 bytes, exactly as
budgeted — hashed, saved and snapshotted. Nobody ages, nothing behaves
differently, and nothing reads them yet. Save v34, snapshot v13,
protocol 24. Fixture hash `e9a9639f4bcd06a0` → `4e89f60ee1e27388`.

*Names are DERIVED, not stored.* This sim has no mutable RNG stream —
outcomes come from hashing an identity (`survey_hash`, survey.c), which
is stronger than a stream because there is no shared cursor to step out
of order. A name is a pure function of (`world_seed`, `id`): identical
on every machine, absent from the snapshot, two table lookups to ask
for. 48 given names against 40 surnames is 1920 combinations, and the
collisions that do occur read as kin rather than as a bug.

*The stage is derived too*, from `age_months` alone. A stored stage
would be a second place for the truth to live and a second thing to
keep consistent.

*Age spread landed here rather than at Phase 5.* The array is being
created now, so seeding arrivals at 20-45 costs one hash; retrofitting
it after ageing ships would be an economy-breaking change. That is the
anti-cohort invariant, and `tests/test_resident.c` asserts it against a
real village rather than against a constructed struct.

*What the tests are for, since there is no behaviour to test:*
identity must be reproducible, or every later phase inherits a desync
nobody can localise; and the format must survive a round trip, because
a snapshot is what a save embeds and what `MSG_WORLD` sends — a field
dropped here is a field silently lost on every join and every reload.
The strongest assertion available is that an encoded world and its
decoded twin hash identically.

*One thing this phase nearly repeated.* The cohort test first ran ticks
on a fresh island, found nobody, printed a note and passed — a fresh
island has no buildings at all. That is the same silent non-coverage
found three times in `replay.c`'s fixture, and it took the same fix: lay
a village, and fail rather than shrug if it cannot be laid.

**4 — the calendar. DONE.** `calendar.c` turns a tick into a date and
**stores nothing** — a date is a pure function of `sim_tick_no`, so the
calendar added no hashed state, no snapshot field and no version of its
own. Save v35 moved for the shift retune alone.

*One clock, and now it really is one.* `CALENDAR_MONTH_TICKS` is written
in terms of the needs interval rather than as 300, so the two cannot
drift apart. The shift durations went 60+15 → **24+6**, which is 30
seconds, which is one month, which is one needs tick: a resident works
one shift and rests once per month. That wart predated this whole
document and was why an island's real output sat at about four fifths of
its headcount. `tests/test_calendar.c` asserts the alignment against the
constants the sim actually uses, not against 300.

*Twelve months, four seasons, six-minute years,* and the day is
decoration that nothing is ever decided on. A 70-year life is seven
hours of play — people outlive sessions, which is the point.

*Looked at, and the first version was wrong.* The date box was placed
under the population readout at y=38..60 — straight through the vitals
strip, which starts at y=44 against the same right edge. Both boxes drew
correctly; the composition did not, and no assertion would ever have
caught it. It sits beside `Pop:` on the same row now. What the
screenshot could NOT show is the date advancing: a screenshot run burns
under a second of wall clock, so only ten ticks pass. Advancement is
covered by assertion — every tick of a month and a full year — not by
the picture.

**5 — ageing and death. DONE.** Ages advance one month per calendar
month, stages gate work, rations are age-weighted, and people die past a
guaranteed span. Fixture hash `4f2068b0c0a4a622` → `bb7e381ae6ab6c0d`.

*Birth moved to Phase 6.* A birth needs a couple and marriage is Phase
6, so a birth here would have been arbitrary. Deferring it also means
this phase's population is immigrants who age and die — which makes the
adult fraction **measurable before children enter it**, and that number
was the whole blocker.

*The labour gate is one line.* `agents_sync` spawns one agent per ADULT
rather than per resident, so a child has no agent, therefore no
workplace, therefore no shift. Nothing in agent.c or island.c had to
learn what an age is; both take a callback.

*Deaths are resident-driven, growth is count-driven.* A death removes
the Resident and decrements the house; growth is decided by the needs
tick and `residents_sync` follows. Keeping those directions apart is
what stops the reconciliation fighting itself — a death that only
removed a Resident would be undone by the next sync, and one that only
decremented the count would kill an arbitrary person rather than the
old one.

*Death has a guaranteed span and then a rising monthly chance*
(Stellaris's shape). Everybody dying on their birthday at exactly N is
how a population dies in cohorts even when its ages are spread, because
a chain staffed by people hired the same year still loses them the same
year.

*Ageing is triggered by the CALENDAR, not by a per-house timer.*
`PopData.timer` staggers with when each house was built, so ageing off
it would have people in different streets getting older at different
rates. That alignment is what Phase 4 was for.

**6 — marriage, households and birth. DONE.** Adults who share a house
pair off on a monthly draw, and a house with a young couple fills its
next vacancy with a child instead of an immigrant. Save v34, protocol
25. Fixture hash `bb7e381ae6ab6c0d` → `e07bd51fd2d9ba87`.

*A HOUSE IS THE HOUSEHOLD.* Pairing is between people who already share
an address, which is what lets marriage cost nothing in bookkeeping:
nobody's `home_idx` ever changes, so no house's `pop_data.residents` has
to move between slots in the same tick `residents_sync` is reconciling
against it. Island-wide pairing with a spouse moving in was the
alternative, and it buys a nicer story for a reconciliation bug that
would be very hard to see.

*Phase 6 ADDS NO SOURCE OF POPULATION.* This is the decision the whole
phase rests on. Growth stays exactly where Phase 5 left it — count-
driven, decided by `pop_update` from happiness — and birth decides only
**who arrives** to fill the slot it opened. So there was no rebalance,
no second growth path to reconcile against the first, and "deaths are
resident-driven, growth is count-driven" survives intact. A birth is a
composition change, not an addition.

*Marriage is a monthly draw, not an immediate fact*, for the same reason
ages are jittered at spawn. If every eligible pair married the first
month they were eligible, every couple would start having children in
the same month and the island would be raising cohorts again — the exact
defect the age spread exists to prevent.

*The one structural hazard was `spouse` being an INDEX*, into an array
whose slots are reused. A marriage that outlives its partner's slot is a
widow married to whoever moves in next: no crash, no hash failure, just
quiet nonsense. Both removal paths — death and despawn — now widow the
partner before clearing the slot, every read is bounds- and reciprocity-
checked, and `test_marriage.c` re-checks mutuality every month of sixty
years rather than once at the end.

*The adult fraction moved, but less than expected:* 81% typical → ~74%,
with the worst five-year mean holding around 48%. So `test_closure`'s
`ADULT_FRACTION` of 0.48 needed no change, and the assertion the plan
predicted would "start doing real work" is doing it — it is simply not
yet under strain.

*And the measurement that matters is a composition:* across three seeds
over sixty years, **28 of 134 arrivals were born on the island (20%)**;
the rest sailed in. That is lower than it sounds like it should be, and
the reason is structural rather than a bug — see below.

**6b — a house is founded by a couple, and grows only by birth. DONE.**
The answer to the open question below, taken in the other direction
from the one it proposed. Save v35, snapshot v14, protocol 26. Fixture
hash `e07bd51fd2d9ba87` → `f5844c21ba0c995f`.

*What a player now does.* Ten thousand Gold rather than one thousand,
because population is something you found rather than something you
wait for. A house opens with **two residents, a woman and a man,
married on arrival**; they work; she conceives on a monthly draw, stops
working for the nine months she carries, and gives birth; the child
eats a half ration and holds no job until eighteen, then joins the
workforce. Watched end to end on seed 12345: a couple at year 0,
expecting at year 1, four children by year 5, and twelve adults across
two houses by year 23.

*Immigration into an existing house is gone.* `pop_update` no longer
grows a house at all — the happiness-driven `residents++` that used to
conjure a grown stranger into a spare bed is deleted. Growth is
`residents_breed` and nothing else. Decline stays exactly where it was,
and the asymmetry is deliberate: leaving is a decision about this
month, being born takes nine of them.

*This inverts Phase 6's own rule, and that is the point.* Phase 6 kept
growth count-driven and made birth a question of who filled the slot.
6b makes both directions resident-driven, which is simpler than the
split Phase 5 needed and is only possible because nobody immigrates any
more.

*A pregnancy costs labour, not rations.* `residents_adults_at` skips a
woman who is carrying, so she loses her agent and her workplace by the
same one-line route that keeps children out of work — and a two-person
household that is expecting is a one-person household. She still eats a
full ration.

*Siblings do not marry*, which needed a new field. Phase 6 paired
within a house on the reasoning that a house was six unrelated lodgers;
6b makes a house a family, and the identical rule would have married a
brother to his sister the month they both turned eighteen.
`birth_house` is -1 for a founder and the house index for anybody born
here, and two people who share a non-negative one are siblings.

*Names split by sex*, because a table that answered "Bess" for a man
would put the mistake on screen the moment anything displayed either.

**The cost, measured: the adult fraction fell to 33%.** Not a
statistic so much as the shape of a household — two parents and four
children is a third of the house able to work, on every seed, for the
eighteen years it takes the eldest to grow up. That is far too long for
the happiness ladder to absorb, so the economy has to survive it.

Marshfolk still close, at **0.89** against a wall of 1.00 — tight, and
meant to be. **Wrights no longer close (1.44) and are now
import-dependent by decision**, recorded in `test_closure.c` beside the
identical decision already taken for Merchants. Their bill is entirely
refined, a refined good is charged once per household, and the same
bill now falls on two workers instead of six. Worth recording because
it is the obvious wrong guess: **`HOUSE_CAPACITY` does not fix this.**
The bill is per household either way and the household has two adults
in it whether the ceiling is four or six, so the ratio does not move.
The only levers are the tier's needs list and the demography.

*Three fixtures were encoding the old world and had to be told.*
`test_happiness` asserted that a well-fed house gains residents — it
does not any more. `test_staffing` laid two houses and expected a
dozen people, and got four. That one exposed an older bug worth the
note: its village put the houses in the road's own row, so only the
first of them ever touched pavement, and the fixture had been measuring
one connected house all along. It passed regardless while that house
held five people. With a couple to a house it stopped passing, which is
the good kind of test failure.

*What is still not built.* Children have nobody to marry: pairing is
within a house, and everybody in a house is either their parent or
their sibling. So a household is one generation deep — the founders'
children grow up, work, and die unmarried, and the island depends on
the player laying new houses to bring new couples. Cross-house
marriage, with a spouse moving in, is the next piece and is what
`birth_house` was really added for.

---

**The open question Phase 6 left: housing caps people, not adults.**
*Answered at 6b, in the other direction:* housing still caps people,
and the population it caps is now grown at home rather than shipped in.
`HOUSE_CAPACITY` is 6 and `pop_init` seeds a house at 5, so a house is
full within one needs tick and stays full. The only demographic event
after that is a death opening a bed — and a house whose members are
dying is a house of old people, whose couple is usually past
`AGE_FERTILE_MAX_YEARS`. Hence 20%.

This document's own arithmetic assumes the *other* model. "Residents are
world state now" says the `MAX_AGENTS` ceiling "stops being a ceiling on
population and becomes one on the working population — roughly 930
residents at a 55% adult fraction, against 512 today." That sentence
only holds if children are **additional to** the capped household rather
than competing with adults for the same six beds.

Making that true means `HOUSE_CAPACITY` bounding ADULTS, with children
extra. It is a real change and not a small one: more mouths per house at
unchanged worker count, `GOLD_PER_RESIDENT` needing to stop paying
children, and `test_closure`'s tables re-derived against a population
half again as large. **It is deliberately not built here**, because it is
an economy-scale decision rather than a consequence of marriage, and
because Phase 6 is coherent and green without it.

*What was NOT built.* The ghost feed says nothing about weddings or
births yet — `feed.c` is client-side and the phase's sim work stands on
its own. Kinship is not modelled at all: nobody has recorded parents, so
nobody can marry a relative and nobody can inherit anything.

**7 — households, wages and the treasury. DONE, WITH ONE KNOWN GAP.**
Households of ten, fertility bounded by biology, a reserve of people
with no roof, and gold that enters the world as taxed wages instead of
being minted by housing. Save v36, snapshot v15, protocol 27. Fixture
hash `e07bd51fd2d9ba87` → `86899f082c03924e`.

*Built as one phase against advice.* Landing the economy first and the
demography second would have kept the game playable throughout; a
combined phase was chosen instead, and the cost was real — the suite
went red in six places at once and two genuine bugs (below) hid inside
that noise for a while.

**The demography.** A house holds ten and is laid EMPTY, settled by
`island_settle_house` from a hundred-place founder allowance and, after
that, out of the reserve. Menopause at 60 and twelve months between
births replace the child cap an earlier draft used: a quota answers
"how many" with a number nobody can defend, a recovery period answers
it with a rate. Children never leave home for want of a bed — a
household may exceed its capacity while its own children are young —
and an adult leaves when they marry, or when the house is over capacity
and they are the oldest unmarried child.

*The reserve is young couples with nowhere to live*, not homeless
infants. It is FIFO on `reserve_since`, which never resets — not when a
house is laid for somebody else and not when a migration carries them
to another island. Somebody may take a roof ALONE and wait for a
spouse, which is why settling returns 1 as legitimately as 2.

*Emigration is the only bound on population.* Without it, eighteen
children a woman with each daughter doing the same is roughly ninefold
growth per generation and `MAX_RESIDENTS` is reached in two or three
generations whatever the player does. After twenty-four months
unhoused, somebody leaves — to another island of the player's first, to
another player's second, and out of the world last.

**The money.** `GOLD_PER_RESIDENT` is gone. A building earns its output
valued at `faction_bid()`, pays `WAGE_PER_WORKER` a head, and the player
taxes wages and profit at a rate set through `CMD_SET_TAX_RATE`. The
treasury is the island's existing `RES_GOLD`, so trade income is
untouched and only the SOURCE of gold changed.

*Tax is levied monthly on an accumulated base, and that is correctness
rather than flavour.* A single production cycle is a few coins, and a
few coins times a tenth in integers is zero — an island of ten Fisher's
Huts collected NOTHING until the base was summed over a month and
divided once. Two earlier versions of this measured exactly zero
revenue before the cause was found.

*All four of `new-happiness-design.md`'s damping rules are built* and
`tests/test_tax.c` asserts each one separately, including the one that
document asks for by name: an island run into sustained unhappiness at
the maximum rate recovers to full compliance in eighteen months.

**THE KNOWN GAP: the island does not fund itself.** Measured against
the faction's own ask prices, a village of twenty-four spends about
2,950 gold a year on imports and collects about 430 in tax — a sevenfold
shortfall that bankrupts a ten-thousand-gold treasury in four years, on
every seed.

The cause is not the tax rule; it is the labour supply. The working
share is **13%** — two adults in a household of ten, less the third of
her fertile life a mother spends pregnant, against an island that also
feeds its reserve. At 13% nothing closes: Marshfolk project at 1.69
against a wall of 1.00. `tests/test_closure.c` no longer asserts that
the base tier feeds itself, because it does not; it now measures the
SIZE of the gap and fails if it grows, which is a watchdog on a known
imbalance rather than a guarantee.

*The levers, in the order I would try them:* `WAGE_PER_WORKER` (2 is a
guess and the wage base is what tax multiplies), the faction's bid/ask
spread (an island importing most of its needs bleeds by construction),
and `CONCEIVE_PERMILLE_PER_MONTH` (fewer, later children raise the
working share directly). Letting the reserve work is explicitly NOT on
the list — it was considered and rejected: it would make homelessness
free, and it needs an island-wide unhappiness term that feeds the very
loop §6 warns about.

*Two bugs worth recording.* `island_settle_house` ASSIGNED `live + got`
back over `pop_data.residents` instead of adding, which silently wrote a
house of forty down to zero the first month it ran — the counts and the
residents array are normally in step, and code that assumes they always
are will one day meet a snapshot where they are not. And
`residents_marry` kept a short form for tests with no `PopData` to
offer, which quietly sent every cross-household couple to the reserve
because with no counts there was never room anywhere; it was deleted
rather than documented.

*One good surprise.* Raising `HOUSE_CAPACITY` to ten spread every
per-household bill over two-thirds more people, and the Wrights tier —
declared import-dependent at Phase 6b — came back under the wall at
0.86 on today's numbers. The tier that was rescued by a decision got
rescued by arithmetic instead.

**7b — work at twelve, and the house stays in the family. DONE.**
Three changes, measured on the same seeds as Phase 7. No save, snapshot
or protocol change: no field moved, only the ages and the rules that
read them.

*The eviction rule is gone, and it should never have been there.* Phase
7's approved plan had adults leaving home ONLY on marriage. During the
build a house on a two-house island reached twenty people against a
capacity of ten — nobody could marry, because everyone under both roofs
was a parent or a sibling — and `residents_leave_home` was added to
push out the eldest unmarried child of an overfull house. That was a
rule change made mid-implementation and recorded in a commit message
rather than agreed. It is reverted.

*Inheritance is what the overflow actually wanted.* A house's ELDERS are
whoever was not born in it — the founding pair and any spouse who
married in. While one lives the house is theirs and the children marry
out. When the last is gone the eldest adult child born there inherits:
they keep the house and bring a spouse INTO it, and capacity does not
apply to that spouse. Without that exception a full family home could
never take anybody's husband or wife, the heir would have to move out,
and the line would end in the one house it was meant to continue.

*Work at twelve, adulthood at eighteen.* `AGE_TEEN_YEARS` moved 13 → 12
and the labour gate accepts `LIFE_TEEN`; marriage and fertility go on
asking for `LIFE_ADULT`. Tying both to one constant was the single
biggest thing holding the working share down — a household was two
workers and eight dependants because nobody under eighteen could do
anything. A worker eats a whole ration, which gives back part of the
gain and is the honest arithmetic rather than the flattering one.

**Measured, same seeds:**

| | Phase 7 | Phase 7b |
|---|---|---|
| worker share, worst five years | 12-14% | **23-25%** |
| worker share, lifetime | 17-19% | **39-44%** |
| Marshfolk projected | 1.69 (over) | **0.98 (under)** |
| Wrights projected | 2.19 | 1.19 |

**The base tier feeds itself again**, at 0.98 against a wall of 1.00 —
close, and meant to be: a founding village should spend nearly
everything it has on staying alive. `test_closure` asserts a real
guarantee once more instead of measuring the size of a gap.

*What is still NOT measured: whether a built island funds itself.* The
affordability probe builds a deliberately import-dependent village —
three Fisher's Huts buying all its grain and both luxuries — and it
still bankrupts in four years. That is expected of such an island at any
working share, and it is the wrong fixture for the question. What
changed is that self-sufficiency is now POSSIBLE where at Phase 7 it was
arithmetically not, so an island that builds its chains need not import
food at all. Proving it needs a fixture that lays the whole Marshfolk
chain, which does not exist yet.

*And the determinism fixture proved nothing about any of this* until it
was lengthened immediately afterwards — see below.

**The fixture, lengthened.** `replay_record_demo_session` ran **2000
ticks — six and a half months** — so nobody in it aged out of infancy,
married, conceived, inherited or turned twelve. Four phases of
demography reached the cross-platform gate without it ever exercising
them, which is the same silent non-coverage this document records
finding three times before.

It now runs **fifty years** (`DEMO_SESSION_TICKS`), over a village of
two houses, a Fisher's Hut and a Farm. Measured across five seeds it
raises 14-26 people of its own, 9-15 of whom reach working age, 2-12
marriages, and a reserve that fills on some seeds and not others.

*Three things had to change with it.* One house could not cover
marriage — a family has nobody in it who may marry anybody — so there
are two. Bought food cannot last fifty years at any affordable quantity
and would blow the warehouse cap besides, so the village grows its own:
the Farm is what makes the length survivable. And the old assertion
asked for happiness ABOVE neutral, which quietly required a luxury chain
the fixture never built; it asks for FED now, which is what a village of
basics honestly is.

*The first attempt at the layout found a site on none of five seeds.* It
demanded a coastal tile and a fertile 2x2 at fixed offsets from each
other — and coast and good soil are on opposite sides of an island,
which is obvious in hindsight and was not before it was measured. The
row of pavement is scanned for each building in turn now, which asks the
same question without insisting they be neighbours.

Costs about seven seconds to record and replay. Fixture hash
`86899f082c03924e` → `f71bc0b54f4e5556`.

**8 — status modifies productivity.** Slept, ate, married, employed →
an integer percentage with the 0.75 floor from §5. Several independent
inputs, so no single shortage moves all of them.

**9 — the wellbeing projection.** The six factors, per resident, floats,
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
