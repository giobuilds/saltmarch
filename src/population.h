#ifndef POPULATION_H
#define POPULATION_H

/* =========================================================
 * population.h  --  Residents and needs  (Phase 5)
 *
 * DESIGN
 * ======
 * Each House building has a PopData block that tracks:
 *   - current resident count (0–HOUSE_CAPACITY)
 *   - a needs timer (fires every NEEDS_INTERVAL seconds)
 *   - a happiness flag (1 = needs met last tick, 0 = not)
 *
 * On each needs tick:
 *   If the house is road-connected to a Warehouse (Phase 3) AND every
 *   good its tier's TierDef (below) lists is in stock:
 *     consume 1 of each listed good
 *     generate GOLD_PER_RESIDENT * residents Gold
 *     happiness = 1
 *     if residents < HOUSE_CAPACITY: residents++  (growth)
 *   Else:
 *     happiness = 0
 *     if residents > 0: residents--  (decline)
 *
 * Production chains, Phase 1: a house's needs list is now data-driven
 * by its actual BuildingType (Farmers' BUILDING_HOUSE vs. Workers'
 * BUILDING_HOUSE_WORKER — see TIER_DEFS in population.c), all-or-
 * nothing across however many goods that tier lists, same as
 * game_tick_buildings' multi-input production. Upgrading a house
 * (game_upgrade_house, game.c) is just mutating its BuildingType in
 * place — this PopData block, and everything else that indexes by
 * building slot, needs no migration.
 *
 * PopData blocks are stored in a parallel array in GameState
 * indexed by building slot — pop_data[i] corresponds to
 * buildings[i].  Non-house buildings have inactive PopData.
 * ========================================================= */

#include "resource.h"
#include "building.h"   /* Phase 3: Building.connected */
#include "simclock.h"   /* Phase 1b: fixed-tick clock  */
#include <stdint.h>

/* Residents per house.
 *
 * Ten until NEEDS_PLAN Phase 3, six until LIFE_PLAN Phase 7, and ten
 * again now — for a reason the round trip makes clearer than either
 * number does alone.
 *
 * Per-resident costs do not change with capacity; per-HOUSE costs are
 * amortised over however many live there. So a larger house makes
 * refined goods cheaper per head and a smaller one makes them dearer,
 * which is why cutting this to six cost the base tier headroom and why
 * restoring it buys some back.
 *
 * What decides it now is the household rather than the economy. Two
 * parents raising children with twelve months between births have about
 * eight minors at home at any time, so ten is what a family actually
 * needs. CAPACITY IS NOT ENFORCED AGAINST CHILDREN — a house may exceed
 * it while its own children are young (residents_breed) — only against
 * people ARRIVING from somewhere else. */
#define HOUSE_CAPACITY       10
#define NEEDS_INTERVAL      30.0f  /* seconds between needs checks  */
/* The needs check fires every NEEDS_INTERVAL seconds, counted in whole
 * sim ticks so the F9 hash never reads an accumulating float. */
#define NEEDS_INTERVAL_TICKS \
    ((uint32_t)(NEEDS_INTERVAL * SIM_TICKS_PER_SEC))
/* ---- wages and the treasury (LIFE_PLAN Phase 7) ------------
 * GOLD_PER_RESIDENT IS GONE. Housing used to mint gold — a house paid
 * `3 x residents` every needs tick and where it came from was never
 * asked — which meant a household of children was as good an earner as
 * a household of workers, and the player's income was really a
 * population count wearing a currency's clothes.
 *
 * Gold now enters the world through WORK. A building earns what its
 * output is worth, pays its crew, and the player taxes both halves.
 * See island_tick_buildings (island.c) for the arithmetic and
 * docs/new-happiness-design.md for the model it comes from.
 *
 * The consequence is deliberate and is the point of the phase: a child
 * costs rations and returns nothing until it is eighteen, so a
 * generation being raised is a genuine fiscal event rather than a free
 * one. */

/* What one worker is paid per production cycle they complete. */
#define WAGE_PER_WORKER      2

/* Where the tax rate starts, in per mille, and how far the player may
 * push it. Ten percent is meant to be comfortable; fifty is meant to
 * hurt. */
#define TAX_RATE_DEFAULT_PERMILLE  100
#define TAX_RATE_MAX_PERMILLE      500
#define TAX_RATE_STEP_PERMILLE      25

/* ---- compliance, and why it has a floor --------------------
 * docs/new-happiness-design.md: "Businesses and residents are taxed.
 * Sustained unhappiness reduces tax compliance. THIS CLOSES A POSITIVE
 * FEEDBACK LOOP AND WILL DEATH-SPIRAL WITHOUT DAMPING." Unhappy means
 * less tax, which means less funding, which means unhappier.
 *
 * Three of the four mitigations it prescribes live here:
 *
 *   FLOOR      — compliance never falls below COMPLIANCE_MIN_PERMILLE,
 *                so revenue cannot reach zero however bad things get.
 *   HYSTERESIS — unhappiness must persist COMPLIANCE_PATIENCE_TICKS
 *                before compliance falls at all, and recovery climbs
 *                twice as fast as decline. A bad quarter costs nothing;
 *                a bad decade costs.
 *   NOT DOMINANT — the tax rate moves happiness by at most
 *                TAX_HAPPINESS_MAX rungs of ten, so a funding shortfall
 *                cannot cascade across every other input at once.
 *
 * The fourth, a treasury that may go negative rather than hard-stop at
 * zero, is TREASURY_FLOOR_GOLD in game.h. */
#define COMPLIANCE_FULL_PERMILLE   1000
#define COMPLIANCE_MIN_PERMILLE     300
#define COMPLIANCE_STEP_PERMILLE     20
#define COMPLIANCE_PATIENCE_TICKS    10
#define TAX_HAPPINESS_MAX             2

/* ---- happiness (NEEDS_PLAN Phase 2) ------------------------
 * 0 is "not at all happy" and 10 is "completely happy". Every basic
 * need met puts a house at NEUTRAL; luxuries take it above.
 *
 * INTEGER, AND IT HAS TO BE. This is hashed world state, and this
 * project's determinism doctrine has no room for a float there: a
 * float accumulated into hashed state fails as two machines disagreeing
 * rather than as a wrong answer on one.
 *
 * HAPPINESS IS ALSO THE RESERVE, which is why Phase 2 needs no second
 * field for it. A house's happiness moves ONE STEP per needs tick
 * toward what its supplies deserve, so a well-fed neighbourhood that
 * loses its larder drifts down over ten ticks rather than losing
 * somebody on the first one — and a hungry one that is rescued has to
 * earn its way back up. The hysteresis the plan asks for falls out of
 * the drift; nothing counts anything. */
#define HAPPINESS_MAX        10
#define HAPPINESS_NEUTRAL     5    /* every basic need met          */
#define HAPPINESS_GROW        8    /* at or above this, people come */

/* Gold to walk the first tier's upgrade edge. Lives here rather than
 * in game.h since SUPPLY_CHAIN Phase 2: the price of an edge belongs
 * beside the edge, and each tier now carries its own in
 * TierDef.upgrade_gold. */
#define TIER_UPGRADE_COST_GOLD 300

/* Most goods any single population tier needs at once. Raised from 3
 * in SUPPLY_CHAIN Phase 2: Artisans and Investors both list five. */
/* Six since SUPPLY_CHAIN Phase 7. Phase 2 set it to five from the
 * widest list the plan then had; Merchants reached six when Wool
 * Cloaks and Plantain Fry were given a tier rather than left as chains
 * nothing eats. */
#define MAX_TIER_GOODS 6

/* One population tier's need-list, keyed by the house BuildingType
 * that represents it. RES_COUNT in a needs[] slot means "unused" —
 * same sentinel convention as BuildingDef.consumes[]. Defined here
 * (not just in population.c) in case a future file needs to inspect
 * a tier's requirements directly (e.g. a UI showing "needs Beer").
 *
 * ---- the upgrade edge (SUPPLY_CHAIN Phase 2) ----
 * `next_tier` is what this house upgrades INTO, or BUILDING_NONE for a
 * tier with nowhere to go. That makes the tier model a graph rather
 * than a ladder, which is what the plan's three house lines need:
 * Marshfolk → Artisans, Wrights → Engineers, Merchants → Investors are
 * three edges in this table, not three branches in code. The Academy's
 * "any house → Scholars" is a fourth.
 *
 * `upgrade_gold` is what that edge costs, per tier rather than one
 * global constant, since a Merchant house is not priced like a
 * cottage.
 *
 * `requires_building` is a building the island must have, active and
 * connected, before the edge can be walked — BUILDING_NONE for "no
 * prerequisite". It exists for the Academy (Phase 8) and is otherwise
 * unused today; the rule is written now so Phase 8 adds a table row
 * rather than a special case. */
/* ---- basics and luxuries (NEEDS_PLAN Phase 1) --------------
 * `needs[]` became two lists, because one list could only express one
 * idea: everything is equally required, and missing any of it is death.
 *
 *   basic[]   -- survival. A Marsh Cottage's are Fish and Grain, which
 *                is a fisher's hut and a farm: the opening a player
 *                actually reaches for now keeps them alive.
 *   luxury[]  -- comfort. What raises happiness above neutral and what
 *                makes a population grow.
 *
 * A tier's basics INCLUDE the basics of the tier below it, so Fish and
 * Grain never stop being wanted and a first island keeps its value
 * after it succeeds — which is the opposite of what happened before,
 * where upgrading a house killed the demand that built it.
 *
 * RES_COUNT is the unused sentinel in both, as in BuildingDef.consumes[]. */
typedef struct {
    BuildingType house_type;
    ResourceType basic[MAX_TIER_GOODS];
    ResourceType luxury[MAX_TIER_GOODS];
    BuildingType next_tier;
    int          upgrade_gold;
    BuildingType requires_building;
} TierDef;

/* The tier a house type belongs to, or NULL if it is not residential.
 * Exposed (rather than kept static in population.c) so the confirm
 * popup can show a tier's needs without keeping a second copy of the
 * table — the drift that would follow is exactly what UI_PLAN
 * decision 3 exists to prevent. */
const TierDef *tier_def_for(BuildingType type);

/* A Scholar's House needs the basics of WHERE ITS PEOPLE CAME FROM.
 *
 * One reached from a Marsh Cottage wants Fish, Grain and Books; one
 * reached from a Wright's House wants Sausages, Bread and Books. That
 * makes it the first thing in this game whose needs depend on its
 * history rather than its kind, and it follows from the existing rule
 * that ANY house may become a Scholar's House where an Academy stands
 * — a scholar's household need not have been a merchant's first.
 *
 * `origin` is PopData.origin_tier: the house type this one was upgraded
 * FROM, or BUILDING_NONE for a house that was never upgraded (and for
 * every house in a save written before this field existed, which is why
 * the fallback has to be a real answer rather than an assertion).
 *
 * Writes at most MAX_TIER_GOODS entries into `out`, RES_COUNT-padded,
 * and returns how many are real. For every tier except Scholars this is
 * just a copy of `basic[]`. */
int tier_basic_needs(const TierDef *tier, BuildingType origin,
                     ResourceType out[MAX_TIER_GOODS]);

/* ---- the two ways up (SUPPLY_CHAIN Phase 8) ----------------
 * A house on a line climbs to its own next tier. An island with an
 * Academy offers a SECOND future to every house on it, skipping
 * whatever line it was on — a scholar's household need not have been a
 * merchant's first.
 *
 * So an upgrade is no longer "the" upgrade: it is a choice of branch,
 * which is why CMD_UPGRADE_HOUSE carries one and the confirm popup can
 * offer two buttons. SUPPLY_CHAIN Phase 3 predicted this exactly when
 * it noted that a second edge "would mean a Marsh Cottage has two
 * possible futures, and the confirm popup would need to ask which". */
typedef enum {
    TIER_BRANCH_LINE    = 0,   /* the tier's own next_tier          */
    TIER_BRANCH_ACADEMY = 1    /* Scholars, from any house at all   */
} TierBranch;

/* Where `branch` leads from `from`, or BUILDING_NONE if it leads
 * nowhere (a terminal line, or the Academy branch asked of a house
 * that is already a Scholar's). */
BuildingType tier_branch_target(BuildingType from, int branch);

/* The branches that lead ANYWHERE from `from`, in the order the
 * confirm popup shows them; returns how many (0-2) and fills `out`.
 *
 * Shared on purpose. The popup builds one button per branch and
 * game_confirm_upgrade stores one Command per branch, and if those two
 * enumerated them separately the second button could submit the first
 * branch the moment a house had only its Academy future left. Draw
 * order and submit order are the same list. */
int tier_branches(BuildingType from, int out[2]);

/* What `from` must have alongside it before it can upgrade, or
 * BUILDING_NONE. The caller looks the building up in its own world —
 * the sim in GameState, the UI in its snapshot — because that lookup
 * is the one part of the rule that genuinely differs between them. */
BuildingType tier_upgrade_requires(BuildingType from, int branch);

/* May a house of type `from` upgrade, given this island's stock and
 * whether tier_upgrade_requires()'s building is present?
 *
 * THE shared rule (UI_PLAN decision 3): sim_upgrade_house calls it to
 * decide, and the confirm popup calls it to predict, so the checklist
 * a player reads and the verdict they get cannot disagree. Returns
 * REJ_OK and writes *out_to when the upgrade may proceed; otherwise a
 * reason and BUILDING_NONE.
 *
 *   REJ_UNAVAILABLE    – not a house, or nowhere to upgrade to
 *   REJ_NEEDS_BUILDING – the prerequisite is missing
 *   REJ_NEEDS_GOODS    – the next tier's needs are not all in stock
 *   REJ_CANT_AFFORD    – not enough Gold
 *
 * Needs are checked for PRESENCE, not consumed: they are what the tier
 * will want every needs tick from then on, so requiring them is
 * asking "can you keep this neighbourhood supplied", not charging a
 * one-off price. */
RejectReason tier_upgrade_check(BuildingType from, int branch,
                                const int stock[RES_COUNT],
                                int prereq_present,
                                BuildingType *out_to);

/* The same rule against tiers the caller supplies, with
 * tier_upgrade_check() as the table-driven wrapper over it. The seam
 * exists for the same reason building_place_check_def()'s does: the
 * table has two tiers today and the rule has to be provable at five
 * needs and with a prerequisite building, neither of which exists
 * until Phases 4 and 8. */
RejectReason tier_upgrade_check_def(const TierDef *tier, const TierDef *next,
                                    const int stock[RES_COUNT],
                                    int prereq_present,
                                    BuildingType *out_to);

/* How many units of `g` a house of `residents` people wants per needs
 * tick. Raw goods scale with the number of mouths; refined goods are one
 * per household however many live there — you eat as a person, you own
 * manufactured things as a household (NEEDS_PLAN Phase 3).
 *
 * Public since Phase 5 for one reason: tests/test_closure.c measures
 * whether an island can staff its own supply, and that measurement is
 * only worth anything if it charges what pop_update charges. A test that
 * kept its own copy of this rule would go on happily certifying the
 * economy it was written against — green, and about a game that no
 * longer exists.
 *
 * `residents` of 0 answers 1 rather than 0: an empty house still wants
 * something, or nothing would ever be delivered to it. */
int tier_good_amount(ResourceType g, int residents);

/* ---- Per-house population data ------------------------- */
typedef struct {
    int      active;    /* 1 if this slot holds a House             */
    int      residents; /* current population (0–HOUSE_CAPACITY)    */
    uint32_t timer;     /* sim ticks since last needs tick          */
    /* 0..HAPPINESS_MAX. Was a 0/1 flag; NEEDS_PLAN Phase 2 made it a
     * ladder, and the ladder is also the buffer — see the constants
     * above. Residents arrive at HAPPINESS_GROW and leave at 0. */
    int      happiness;

    /* The house type this one was upgraded FROM, or BUILDING_NONE.
     * Only a Scholar's House reads it (see tier_basic_needs), but it is
     * recorded for every upgrade rather than only that one, because a
     * field written on one path and read on another is a field that
     * eventually is not written. World state: hashed, saved,
     * snapshotted. */
    int      origin_tier;

    /* Has this house EVER been settled? (LIFE_PLAN Phase 7.)
     *
     * The founder allowance is spent per HOUSE, once. Without this a
     * house that starves to empty is re-founded from the allowance the
     * next month, and a village that keeps failing quietly burns the
     * island's whole immigration quota — which is exactly what the
     * prototype did, turning 100 places into 63 houses. A re-settled
     * house draws from the reserve or stands empty. */
    int      founded;
} PopData;

/* Initialise a PopData block for a newly placed house.
 * Starts with 5 residents — enough to feel alive immediately. */
void pop_init(PopData *p);

/* Called once per sim tick for all active houses.
 * Advances timers, fires needs ticks, updates stockpile.
 * `pop`       – array of PopData, one per building slot
 * `buildings` – parallel array (same indexing) — buildings[i].connected
 *               gates whether pop[i]'s needs can be met at all this tick
 * `count`     – number of building slots to check
 * `s`         – island stockpile (read and written)
 * Takes no dt: it advances one fixed tick. */
/* `mouths_at(ctx, house_idx)` answers how many RATIONS that house eats
 * — an adult a whole one, a child or an elder a half (LIFE_PLAN Phase
 * 5). Raw goods only: refined goods are charged per household, so the
 * number of mouths never entered their cost and halving some of them
 * cannot reduce it.
 *
 * Pass NULL and every resident eats a full ration, which is the
 * pre-Phase-5 behaviour the tests written before ages rely on. */
/* `tax_rungs` is what the island's tax rate is worth on the happiness
 * ladder — zero or negative, and already capped by
 * island_tax_happiness(). Passed in rather than read, because
 * population.c has no island to ask and should not learn about one.
 * Zero is the pre-Phase-7 behaviour every older test relies on. */
void pop_update(PopData pop[], const Building buildings[], int count,
               Stockpile *s,
               int (*mouths_at)(const void *ctx, int house_idx),
               const void *ctx, int tax_rungs);

/* Return the total population across all active houses. */
int pop_total(const PopData pop[], int count);

/* Returns 1 if `type` is a residential building — i.e. one that has a
 * TierDef and so is driven by pop_update(). Callers must use this
 * rather than testing `type == BUILDING_HOUSE`: agents_sync() (agent.c)
 * did exactly that and consequently stopped managing agents entirely
 * for any house upgraded to BUILDING_HOUSE_WORKER, while its PopData
 * kept updating. One predicate, one place to update per new tier. */
int pop_is_house_type(BuildingType type);

#endif /* POPULATION_H */
