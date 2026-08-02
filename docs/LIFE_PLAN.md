# Residents, lives and labour — the design

> Status: **Phases 1-5 done; Phase 6 next.** The calendar is settled — see
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

**6 — marriage, households and birth.** Pairing derived from identity in
a fixed order, and the births that follow from it. Feeds social support,
and gives the ghost feed something worth saying.

*Watch the adult fraction here.* Phase 5 measured 81% typical because
every resident arrives an adult. Children will push it down, and the
closure projection is asserted against the worst measured year — so this
is the phase where that assertion starts doing real work.

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
