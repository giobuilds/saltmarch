#ifndef POPULATION_H
#define POPULATION_H

/* population.h -- per-house needs, happiness and the tier ladder.
 * PopData is parallel to Building: pop_data[i] belongs to buildings[i],
 * and is inactive for anything that is not a house.
 * Design and history: docs/NEEDS_PLAN.md, docs/LIFE_PLAN.md. */

#include "resource.h"
#include "building.h"
#include "simclock.h"
#include <stdint.h>

/* Arrivals a house will accept. Not enforced against a household's own
 * children, who are never turned away for want of a bed. */
#define HOUSE_CAPACITY       10

#define NEEDS_INTERVAL      30.0f          /* seconds between needs checks */
#define NEEDS_INTERVAL_TICKS \
    ((uint32_t)(NEEDS_INTERVAL * SIM_TICKS_PER_SEC))

/* ---- wages and the treasury --------------------------------
 * Gold enters the world through work, not housing. A building earns
 * its output at faction_bid(), pays WAGE_PER_WORKER a head, and the
 * island taxes wages and profit at tax_rate_permille. */
#define WAGE_PER_WORKER      2

#define TAX_RATE_DEFAULT_PERMILLE  100
#define TAX_RATE_MAX_PERMILLE      500
#define TAX_RATE_STEP_PERMILLE      25   /* one press of the UI stepper */

/* Compliance: how much of the levy is actually paid. Falls only after
 * COMPLIANCE_PATIENCE_TICKS consecutive unhappy months, recovers at
 * twice the rate, and never drops below the minimum. */
#define COMPLIANCE_FULL_PERMILLE   1000
#define COMPLIANCE_MIN_PERMILLE     300
#define COMPLIANCE_STEP_PERMILLE     20
#define COMPLIANCE_PATIENCE_TICKS    10

/* Rungs of happiness the tax rate may move, at most. */
#define TAX_HAPPINESS_MAX             2

/* ---- happiness ---------------------------------------------
 * 0..10, moving one rung per needs tick toward what the larder
 * deserves. The ladder is also the buffer: a house that loses its
 * supply drifts down over ten ticks rather than emptying at once. */
#define HAPPINESS_MAX        10
#define HAPPINESS_NEUTRAL     5    /* every basic need met          */
#define HAPPINESS_GROW        8    /* at or above this, people come */

/* Gold to walk the first tier's upgrade edge; later tiers carry their
 * own in TierDef.upgrade_gold. */
#define TIER_UPGRADE_COST_GOLD 300

/* Most goods any one tier lists in basic[] or luxury[]. */
#define MAX_TIER_GOODS 6

/* One population tier. RES_COUNT in a needs slot means "unused". */
typedef struct {
    BuildingType house_type;
    ResourceType basic[MAX_TIER_GOODS];
    ResourceType luxury[MAX_TIER_GOODS];
    BuildingType next_tier;
    int          upgrade_gold;
    BuildingType requires_building;
} TierDef;

/* The tier `type` belongs to, or NULL if it is not residential. */
const TierDef *tier_def_for(BuildingType type);

/* Writes `tier`'s basic needs into `out`, RES_COUNT-padded, and returns
 * how many are real. A Scholar's House also inherits the basics of
 * `origin`, the house type it was upgraded from. */
int tier_basic_needs(const TierDef *tier, BuildingType origin,
                     ResourceType out[MAX_TIER_GOODS]);

/* The two ways up: a tier's own next_tier, or Scholars from any house
 * where an Academy stands. */
typedef enum {
    TIER_BRANCH_LINE    = 0,
    TIER_BRANCH_ACADEMY = 1
} TierBranch;

/* Where `branch` leads from `from`, or BUILDING_NONE. */
BuildingType tier_branch_target(BuildingType from, int branch);

/* The branches leading anywhere from `from`, in the order the confirm
 * popup shows them. Returns how many (0-2) and fills `out`. Shared by
 * the popup and the submit path so they cannot disagree. */
int tier_branches(BuildingType from, int out[2]);

/* What `from` must have alongside it to take `branch`, or
 * BUILDING_NONE. The caller looks it up in its own world. */
BuildingType tier_upgrade_requires(BuildingType from, int branch);

/* May a house of type `from` upgrade? Returns REJ_OK and writes
 * *out_to, or a reason and BUILDING_NONE: */
RejectReason tier_upgrade_check(BuildingType from, int branch,
                                const int stock[RES_COUNT],
                                int prereq_present,
                                BuildingType *out_to);

/* The same rule against tiers the caller supplies. */
RejectReason tier_upgrade_check_def(const TierDef *tier, const TierDef *next,
                                    const int stock[RES_COUNT],
                                    int prereq_present,
                                    BuildingType *out_to);

/* Units of `g` a house of `residents` wants per needs tick. Raw goods
 * scale with mouths; refined goods are one per household. `residents`
 * of 0 answers 1, so an empty house is still delivered to. */
int tier_good_amount(ResourceType g, int residents);

/* Per-house population data, parallel to buildings[]. */
typedef struct {
    int      active;      /* 1 if this slot holds a house             */
    int      residents;
    uint32_t timer;       /* sim ticks since the last needs tick      */
    int      happiness;   /* 0..HAPPINESS_MAX                         */
    int      origin_tier; /* upgraded FROM, or BUILDING_NONE          */
    int      founded;     /* ever settled; stops the founder allowance
                           * being spent twice on one house           */
} PopData;

/* Initialise a newly placed house: active, empty, neutral. A household
 * arrives via island_settle_house. */
void pop_init(PopData *p);

/* One sim tick for every active house: advances timers, and on a needs
 * tick consumes goods, moves happiness one rung, and empties a house
 * that has hit zero. Does not grow one -- that is residents_breed. */
void pop_update(PopData pop[], const Building buildings[], int count,
               Stockpile *s,
               int (*mouths_at)(const void *ctx, int house_idx),
               const void *ctx, int tax_rungs);

/* Total population across all active houses. */
int pop_total(const PopData pop[], int count);

/* Is `type` residential -- does it have a TierDef? Callers must use
 * this rather than testing for BUILDING_HOUSE. */
int pop_is_house_type(BuildingType type);

#endif /* POPULATION_H */
