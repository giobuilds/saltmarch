# Saltmarch — wellbeing and steward trackers

> **Partly superseded.** Two sections of this brief have been settled
> elsewhere and this document is no longer the authority on either:
>
> - **The needs half** — what a house wants and what happens when it
>   goes short — is built. See [NEEDS_PLAN.md](NEEDS_PLAN.md).
> - **The unit of computation** — "per district, per population tier,
>   never per resident; population is a count inside a group struct, not
>   a collection of agents" — is the thing [LIFE_PLAN.md](LIFE_PLAN.md)
>   replaces. Individual residents with names, ages and marriages make
>   that decision moot at this scale.
>
> **Everything else here still stands**, and this remains the brief for
> it: the six factors, their weights, the log-income curve, the
> perceived-versus-actual split, the taxation loop's damping rules, and
> the tracker/Steward model. LIFE_PLAN Phase 8 is where it lands, as a
> read-only projection above the simulation.

Design brief. Read the current codebase before planning; this document describes intent, not existing structure. Produce a phased plan and confirm it before writing code.

## Premise

Islands are scored on resident wellbeing using the six-factor model from the World Happiness Report, with the report's empirically fitted weights. The weighting deliberately ranks social factors above income, so pure economic optimisation does not win. Several independent trackers score the same simulation state differently; players are ranked as **Stewards** according to which tracker they lead.

## Unit of computation

Wellbeing is computed **per district, per population tier** — never per resident. Districts aggregate to islands for display and ranking. Population is a count inside a group struct, not a collection of agents.

Recompute on a slow tick (every N simulation ticks, N tunable, target ~1s of wall clock). Wellbeing is a lagging indicator; per-tick recalculation is wasted work.

## Scoring

Every factor normalises to 0..1 against a defined min/max, then multiplies by its weight. Normalise `log(wage)` rather than `wage` so the income curve stays logarithmic — doubling a poor district's income moves the needle hard, doubling a rich one's barely registers.

| Factor | Weight | Notes |
|---|---|---|
| Social support | 2.7 | heaviest by design |
| Income (log) | 1.7 | |
| Freedom | 1.5 | |
| Health | 1.2 | |
| Freedom from corruption | 0.7 | enters positively once inverted |
| Generosity | 0.2 | flavour modifier, not a pillar |

Weights sum to 8.0. Final score = weighted sum, scaled to a 0–10 ladder, plus a **baseline floor** so no district can reach zero.

Keep a **residual** term per district: a small persistent per-district offset, seeded at generation. Some districts are happier than their inputs predict and some are not, and the player has to work out why. This is where character lives.

All weights, ranges, and curve constants live in **one table in one file**, editable without touching logic.

## Factors

### Income
Residents are employed by production buildings and paid wages. Wages are spent at consumer buildings. Factor input is log of average district wage, normalised.

### Social support
Three inputs:

- **Tenure** — per-residence stability, 0..1, accruing asymptotically over ~200–300 ticks. Knocked back by shocks: adjacent demolition, industrial placement nearby, rezoning, roads cut through. This taxes the constant-rebuild optimisation loop.
- **Neighbour density** — an **inverted-U curve**, not a linear ramp. Isolation is bad, overcrowding is bad, moderate density peaks. This must pull against population maximisation.
- **Industrial adjacency** — subtractive. Combined with the above, zoning behaviour emerges without a zoning system.

Carry a **perceived** value alongside the real one, driven by visible neighbourhood events (crime report, neighbour helped, district abandoned). Perceived value is what feeds the score. Shares machinery with corruption perception below.

### Health
Life expectancy plus workplace injury rate. Injury scales with production intensity, creating a direct output-versus-wellbeing trade.

Setting-specific: marshland disease (ague). Drainage improves health but destroys wetland that other systems depend on.

### Freedom
Subjective autonomy, computed from existing systems — **no governance UI in this phase**:

- **Job diversity within commute range** — count of *distinct* employer types reachable. This is the important one: it puts permanent counter-pressure on the mono-industry island, which is otherwise the dominant strategy.
- **Goods variety per need** — count of distinct satisfiers available per need, not whether the need is met.
- **Housing vacancy** — zero vacancy means nobody can relocate. Some slack is required.
- **Leisure** — inverse of production intensity.

### Generosity
Charitable donation rate, entered as a **residual against income** — score giving relative to what the district's wealth predicts. Wealthy districts get no credit for donating pocket change; poor districts that give score highly.

### Corruption
Model **actual** and **perceived** corruption as separate values. The player sees the true number; residents react to the perceived one. Sources: corrupt traits on officials, cartel formation, lobbying. Scandals, cover-ups, and investigations move the two independently. Only perceived corruption enters the score.

## Taxation loop — stability requirement

Businesses and residents are taxed. Sustained unhappiness reduces tax compliance.

**This closes a positive feedback loop and will death-spiral without damping.** Unhappy → less tax → less service funding → unhappier. Required mitigations:

1. **Compliance floor** — never drops below a minimum (~30%).
2. **Hysteresis** — unhappiness must persist for a threshold number of ticks before compliance falls; recovery is faster than decline.
3. **Service funding must not be the dominant input** to any factor, so a funding shortfall cannot cascade across all six.
4. A treasury reserve or debt facility, so one bad quarter does not cascade.

Write a test that runs a district into sustained unhappiness and asserts recovery is reachable.

## Trackers

Trackers are **read-only projections over one simulation state**, not separate systems.

```c
typedef float (*IndexFn)(const WorldState *);
typedef struct { const char *name; IndexFn score; } StewardIndex;
```

Adding a tracker is one function, not a subsystem.

- **Wellbeing** — the six-factor score above.
- **Economic** — wages, goods produced, imports, exports.
- **Tourism** — bridges the two. Depends on wellbeing and beauty, pays out economically, so the wellbeing route is not purely altruistic. (Tourism belongs here, *not* under Freedom.)

**Design constraint:** trackers must be partially antagonistic. If all can be maxed simultaneously the ranking says nothing. The antagonism should fall out of the mechanics above — dense industrial monoculture maximises output while wrecking job diversity, neighbour density, and health. Verify this holds; if a strategy tops two trackers at once, the weights need revisiting.

Player title derives from which tracker they lead — a statement about how they played, not a completeness score.

## Not in this phase

- Political hierarchy (mayor / island governor / archipelago governor). Deferred. If added later, the tractable form is **individual sticky edicts** that each trade one factor against another, not selectable regime types. Edicts can be added incrementally; regimes would require the whole system up front.
- Any forced-labour mechanic. If it ever appears it must carry real weight and be difficult to reverse, not function as a balance lever.

## Build order

1. **Income + Health.** Most of income exists already. Health adds the production-versus-wellbeing trade. Together these are a complete, playtestable satisfaction loop.
2. **Corruption.** Cheap, and the perception/reality split is the most interesting single mechanic here.
3. **Social support.** Heaviest weight, so it needs the most tuning time.
4. **Freedom.**
5. **Generosity.** Lowest weight, last.

Trackers can land after step 1 with two factors stubbed.

## UI

Do not surface the formulas. Players should infer that packing houses tighter stopped helping, or that the mono-industry island is stagnating. Show outcomes and trends, not inputs and weights.
