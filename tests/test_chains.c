/*  test_chains.c  --  can the economy actually be built?
 *                     (SUPPLY_CHAIN Phase 3)
 *
 * The plan names this the assertion it most needs: "a chain specified
 * but not reachable" is the failure a content phase produces, and
 * nothing else catches it. A tier can want Soap; a Soap Boilery can
 * want Tallow; a Tallow Works can want Pigs; and if no profile grows
 * pasture, the whole branch is decoration and the tier is permanently
 * unhappy. Nothing in the def table says so — every row is
 * individually well-formed.
 *
 * So this walks the graph. Which buildings can be PLACED on a given
 * set of islands is a question about generated terrain, so it is asked
 * of real maps over several seeds; which goods can then be MADE is a
 * fixpoint over the def table. What comes out is the set of goods that
 * economy can actually produce, and every tier's needs must be inside
 * it.
 *
 * Written generically over BUILDING_DEFS and TIER_DEFS rather than
 * against Phase 3's thirteen buildings, because Phases 4-8 add forty
 * more and this is the test that should keep failing usefully.
 *
 * SDL-free: sim library only.
 */

#include "map.h"
#include "building.h"
#include "population.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }    \
        else         { printf("  ok:   %s\n", (msg)); }                \
    } while (0)

/* ---- what can stand on these islands ---------------------- */

/* Can `type` be placed anywhere on this map? Asks the real validator
 * at every tile rather than reasoning about the def's flags, so a
 * building refused for a reason nobody thought of still counts as
 * refused. */
static int placeable_on(const Map *m, BuildingType type)
{
    int r, c;
    for (r = 0; r < m->rows; r++)
        for (c = 0; c < m->cols; c++)
            if (building_place_check(m, type, r, c) == REJ_OK) return 1;
    return 0;
}

/* Mark every building placeable on any of `profiles`. */
static void survey(const MapProfile *profiles, int n, uint32_t seed,
                   int out_placeable[BUILDING_TYPE_COUNT])
{
    Map map;
    int i, t;

    memset(out_placeable, 0, sizeof(int) * BUILDING_TYPE_COUNT);
    for (i = 0; i < n; i++) {
        map_init(&map, seed, profiles[i]);
        for (t = 0; t < BUILDING_TYPE_COUNT; t++)
            if (!out_placeable[t] && placeable_on(&map, (BuildingType)t))
                out_placeable[t] = 1;
    }
}

/* ---- what those buildings can make ------------------------ */

/* Fixpoint: a good is makeable if some placeable building produces it
 * and every input that building consumes is itself makeable. Iterate
 * until nothing new appears — chains are short, so this converges in a
 * handful of passes and the bound is just a guard against a cycle.
 *
 * Goods traded between islands are deliberately NOT counted: the
 * question is what an economy can produce, and "buy it from the
 * faction" would answer yes for everything and assert nothing. */
static void reachable_goods(const int placeable[BUILDING_TYPE_COUNT],
                            int out_makeable[RES_COUNT])
{
    int pass, t, j, changed = 1;

    memset(out_makeable, 0, sizeof(int) * RES_COUNT);
    out_makeable[RES_GOLD] = 1;   /* houses generate it; nothing makes it */

    for (pass = 0; pass < BUILDING_TYPE_COUNT && changed; pass++) {
        changed = 0;
        for (t = 0; t < BUILDING_TYPE_COUNT; t++) {
            const BuildingDef *d = &BUILDING_DEFS[t];
            int ok = 1;

            if (!placeable[t] || d->produces == RES_COUNT) continue;
            if (out_makeable[d->produces]) continue;

            for (j = 0; j < MAX_BUILDING_INPUTS; j++)
                if (d->consumes[j] != RES_COUNT && !out_makeable[d->consumes[j]])
                    ok = 0;

            if (ok) { out_makeable[d->produces] = 1; changed = 1; }
        }
    }
}

/* ---- the assertions --------------------------------------- */

static void report_tier(const TierDef *tier, const int makeable[RES_COUNT],
                        const char *where, int *unmet)
{
    int k;
    for (k = 0; k < MAX_TIER_GOODS; k++) {
        if (tier->needs[k] == RES_COUNT) continue;
        if (!makeable[tier->needs[k]]) {
            printf("  FAIL: %s cannot be made %s, so %s are stuck unhappy\n",
                   RESOURCE_NAMES[tier->needs[k]], where,
                   BUILDING_DEFS[tier->house_type].name);
            (*unmet)++;
        }
    }
}

static void test_tiers_are_satisfiable(void)
{
    static const uint32_t SEEDS[] = { 1u, 4242u, 777u, 12345u, 99991u };
    static const MapProfile HOME[]      = { PROFILE_TEMPERATE };
    static const MapProfile HOME_HIGH[] = { PROFILE_TEMPERATE, PROFILE_HIGHLAND };
    static const MapProfile NORTH[] = {
        PROFILE_TEMPERATE, PROFILE_HIGHLAND, PROFILE_WOODLAND, PROFILE_ATOLL
    };
    static const MapProfile NORTH_SOUTH[] = {
        PROFILE_TEMPERATE, PROFILE_HIGHLAND, PROFILE_WOODLAND, PROFILE_ATOLL,
        PROFILE_PLANTATION, PROFILE_JUNGLE
    };
    int placeable[BUILDING_TYPE_COUNT], makeable[RES_COUNT];
    size_t s;

    printf("--- every tier can be supplied ---\n");

    for (s = 0; s < sizeof(SEEDS) / sizeof(SEEDS[0]); s++) {
        char msg[96];
        int  unmet = 0;

        /* Marshfolk are the tier you meet first, on the island you
         * start on. If they cannot be fed from home alone, the opening
         * of the game is unwinnable. */
        survey(HOME, 1, SEEDS[s], placeable);
        reachable_goods(placeable, makeable);
        report_tier(tier_def_for(BUILDING_HOUSE), makeable,
                    "on a temperate island", &unmet);
        snprintf(msg, sizeof(msg),
                 "seed %u: Marshfolk are satisfiable from home alone", SEEDS[s]);
        CHECK(unmet == 0, msg);

        /* Wrights want Beer, and beer wants hops, and a temperate
         * island grows none — which is the whole reason to colonise a
         * highland. So they are asked of the pair. */
        unmet = 0;
        survey(HOME_HIGH, 2, SEEDS[s], placeable);
        reachable_goods(placeable, makeable);
        report_tier(tier_def_for(BUILDING_HOUSE_WORKER), makeable,
                    "from a temperate island and a highland", &unmet);
        snprintf(msg, sizeof(msg),
                 "seed %u: Wrights are satisfiable from home plus a highland",
                 SEEDS[s]);
        CHECK(unmet == 0, msg);
    }

    /* Artisans (SUPPLY_CHAIN Phase 4, completed in Phase 5) are the
     * first tier that cannot be supplied by any one climate: iron and
     * glass are northern, cotton is southern, and Fur Coats needs
     * both. Asked of the whole archipelago. */
    for (s = 0; s < sizeof(SEEDS) / sizeof(SEEDS[0]); s++) {
        char msg[96];
        int  unmet = 0;

        survey(NORTH_SOUTH, (int)(sizeof NORTH_SOUTH / sizeof NORTH_SOUTH[0]),
               SEEDS[s], placeable);
        reachable_goods(placeable, makeable);
        report_tier(tier_def_for(BUILDING_HOUSE_ARTISAN), makeable,
                    "from a northern and a southern island", &unmet);
        snprintf(msg, sizeof(msg),
                 "seed %u: Artisans are satisfiable across the archipelago",
                 SEEDS[s]);
        CHECK(unmet == 0, msg);
    }

    /* And the negatives, which are what make the other climates matter
     * rather than decorate. The home island alone must NOT be able to
     * brew, and the whole NORTH must not be able to make a Fur Coat —
     * the moment it can, the southern colony is optional and the
     * shipping lane is scenery. */
    survey(HOME, 1, 4242u, placeable);
    reachable_goods(placeable, makeable);
    CHECK(!makeable[RES_BEER],
          "a temperate island still cannot brew — the highland is the point");
    CHECK(!placeable[BUILDING_HOP_FARM],
          "because it grows no hops");

    survey(NORTH, (int)(sizeof NORTH / sizeof NORTH[0]), 4242u, placeable);
    reachable_goods(placeable, makeable);
    CHECK(!makeable[RES_COTTON] && !makeable[RES_CLOTH],
          "no northern island can card cotton");
    CHECK(!makeable[RES_FUR_COATS],
          "so Artisans cannot be reached without sailing south");
}

/* ---- nothing in the table is orphaned --------------------- */

static void test_no_dead_goods(void)
{
    int placeable[BUILDING_TYPE_COUNT], makeable[RES_COUNT];
    /* The whole archipelago, north and south (SUPPLY_CHAIN Phase 5).
     * "Somewhere in the world" has to mean the world as it actually
     * is, or the check quietly stops covering the newest climate. */
    static const MapProfile ALL[] = {
        PROFILE_TEMPERATE, PROFILE_HIGHLAND, PROFILE_WOODLAND, PROFILE_ATOLL,
        PROFILE_PLANTATION, PROFILE_JUNGLE
    };
    int r, t, j, orphan = 0, unused = 0;

    printf("--- the def table hangs together ---\n");

    survey(ALL, (int)(sizeof ALL / sizeof ALL[0]), 4242u, placeable);
    reachable_goods(placeable, makeable);

    /* Every good must be makeable somewhere in the world. A good
     * nothing produces is a resource row that can only ever be bought,
     * which is almost always a chain someone forgot to finish. */
    for (r = 0; r < RES_COUNT; r++)
        if (!makeable[r]) {
            printf("  FAIL: nothing anywhere can produce %s\n",
                   RESOURCE_NAMES[r]);
            orphan++;
        }
    CHECK(orphan == 0, "every good has a producer somewhere in the world");

    /* And every good must have a consumer: a tier that wants it, a
     * building that eats it, or a building that costs it. Otherwise it
     * is a good you make and can only sell, which is a chain that
     * leads nowhere. */
    for (r = 0; r < RES_COUNT; r++) {
        int wanted = (r == (int)RES_GOLD);   /* gold is wanted by everything */

        for (t = 0; t < BUILDING_TYPE_COUNT && !wanted; t++) {
            if (BUILDING_DEFS[t].cost[r] > 0) wanted = 1;
            for (j = 0; j < MAX_BUILDING_INPUTS && !wanted; j++)
                if ((int)BUILDING_DEFS[t].consumes[j] == r) wanted = 1;
        }
        for (t = 0; t < BUILDING_TYPE_COUNT && !wanted; t++) {
            const TierDef *tier = tier_def_for((BuildingType)t);
            int k;
            if (!tier) continue;
            for (k = 0; k < MAX_TIER_GOODS && !wanted; k++)
                if ((int)tier->needs[k] == r) wanted = 1;
        }
        if (!wanted) {
            printf("  FAIL: nothing wants %s — it can only be sold\n",
                   RESOURCE_NAMES[r]);
            unused++;
        }
    }
    CHECK(unused == 0, "every good is wanted by something");

    /* Prices exist for everything tradeable, or the market lists a
     * good at nothing. */
    {
        int unpriced = 0;
        for (r = 0; r < RES_COUNT; r++) {
            if (r == (int)RES_GOLD) continue;
            if (SELL_PRICE[r] <= 0 || BUY_PRICE[r] <= 0) {
                printf("  FAIL: %s has no price\n", RESOURCE_NAMES[r]);
                unpriced++;
            } else if (BUY_PRICE[r] <= SELL_PRICE[r]) {
                printf("  FAIL: %s buys for no more than it sells\n",
                       RESOURCE_NAMES[r]);
                unpriced++;
            }
        }
        CHECK(unpriced == 0, "every good is priced, with the spread the right way");
    }
}

/* ---- the chains are the ones the plan describes ----------- */

static void test_phase3_chains(void)
{
    struct { BuildingType b; ResourceType makes, from; } expect[] = {
        { BUILDING_SAWMILL,         RES_PLANKS,    RES_WOOD     },
        { BUILDING_SHEEP_PASTURE,   RES_WOOL,      RES_COUNT    },
        { BUILDING_KNITTING_HOUSE,  RES_OILSKINS,  RES_WOOL     },
        { BUILDING_POTATO_FIELD,    RES_POTATOES,  RES_COUNT    },
        { BUILDING_STILL,           RES_MARSH_GIN, RES_POTATOES },
        { BUILDING_CLAY_PIT,        RES_CLAY,      RES_COUNT    },
        { BUILDING_BRICKWORKS,      RES_BRICKS,    RES_CLAY     },
        { BUILDING_PIG_PEN,         RES_PIGS,      RES_COUNT    },
        { BUILDING_BUTCHERY,        RES_SAUSAGES,  RES_PIGS     },
        { BUILDING_TALLOW_WORKS,    RES_TALLOW,    RES_PIGS     },
        { BUILDING_SOAP_BOILERY,    RES_SOAP,      RES_TALLOW   },
        { BUILDING_WINDMILL,        RES_FLOUR,     RES_GRAIN    },
        { BUILDING_BAKEHOUSE,       RES_BREAD,     RES_FLOUR    },
    };
    size_t i;
    int    wrong = 0;

    printf("--- Phase 3's chains ---\n");

    for (i = 0; i < sizeof(expect) / sizeof(expect[0]); i++) {
        const BuildingDef *d = &BUILDING_DEFS[expect[i].b];
        if (d->produces != expect[i].makes ||
            d->consumes[0] != expect[i].from) {
            printf("  FAIL: %s is wired wrong\n", d->name);
            wrong++;
        }
    }
    CHECK(wrong == 0, "all thirteen buildings make what the plan says");

    /* The Pig Pen feeding two workshops is the first place the player
     * has to choose what a raw good becomes. */
    CHECK(BUILDING_DEFS[BUILDING_BUTCHERY].consumes[0] == RES_PIGS &&
          BUILDING_DEFS[BUILDING_TALLOW_WORKS].consumes[0] == RES_PIGS,
          "one Pig Pen feeds two different workshops");

    /* The three that could not have been written before Phase 1's
     * terrain: a crop, a deposit, and grazing. */
    CHECK(BUILDING_DEFS[BUILDING_POTATO_FIELD].needs_fertility == FERTILE_POTATO,
          "the Potato Field names its crop");
    CHECK(BUILDING_DEFS[BUILDING_CLAY_PIT].needs_deposit == DEPOSIT_CLAY,
          "the Clay Pit names its seam");
    CHECK(BUILDING_DEFS[BUILDING_PIG_PEN].needs_fertility == FERTILE_PASTURE &&
          BUILDING_DEFS[BUILDING_SHEEP_PASTURE].needs_fertility == FERTILE_PASTURE,
          "both pastures want grazing, and so compete with arable");
}

int main(void)
{
    printf("=== chains: is the economy buildable? ===\n");

    test_phase3_chains();
    test_tiers_are_satisfiable();
    test_no_dead_goods();

    if (failures) {
        printf("\n%d FAILED\n", failures);
        return 1;
    }
    printf("\nPASSED\n");
    return 0;
}
