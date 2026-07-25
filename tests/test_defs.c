/*  test_defs.c  --  BUILDING_DEFS <-> BuildingType alignment guard
 *
 * The defs table was positional until the Shipyard / Worker's House
 * rows were found swapped relative to the enum: BUILDING_DEFS[10] held
 * the Shipyard def while type 10 is BUILDING_HOUSE_WORKER, so the HUD's
 * "Shipyard" slot placed Worker's Houses and a placed shipyard could
 * never open the ship-build popup. The rows are designated now; this
 * test pins every name to its enum value (and a few load-bearing
 * properties), so "added a row, forgot the enum order" fails here
 * instead of silently misplacing buildings.
 */

#include "building.h"
#include "resource.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }    \
        else         { printf("  ok:   %s\n", (msg)); }                \
    } while (0)

static void expect_name(BuildingType t, const char *name)
{
    char msg[96];
    snprintf(msg, sizeof(msg), "def[%d] is '%s'", (int)t, name);
    CHECK(BUILDING_DEFS[t].name && strcmp(BUILDING_DEFS[t].name, name) == 0,
          msg);
}

int main(void)
{
    int i;

    expect_name(BUILDING_FISHERS_HUT,  "Fisher's Hut");
    expect_name(BUILDING_WAREHOUSE,    "Warehouse");
    expect_name(BUILDING_FARM,         "Farm");
    expect_name(BUILDING_LUMBERJACK,   "Lumberjack");
    expect_name(BUILDING_HOUSE,        "Marsh Cottage");
    expect_name(BUILDING_ROAD,         "Road");
    expect_name(BUILDING_MARKETPLACE,  "Marketplace");
    expect_name(BUILDING_HOP_FARM,     "Hop Farm");
    expect_name(BUILDING_MALTHOUSE,    "Malthouse");
    expect_name(BUILDING_BREWERY,      "Brewery");
    expect_name(BUILDING_SHIPYARD,     "Shipyard");
    expect_name(BUILDING_HOUSE_WORKER, "Wright's House");
    expect_name(BUILDING_HARBOR,       "Harbor");
    expect_name(BUILDING_SAWMILL,      "Sawmill");
    expect_name(BUILDING_CLAY_PIT,     "Clay Pit");
    expect_name(BUILDING_BAKEHOUSE,    "Bakehouse");

    /* Every enum value must have a row at all — a NULL name means a
     * designated row was forgotten entirely. */
    for (i = 0; i < BUILDING_TYPE_COUNT; i++)
        if (!BUILDING_DEFS[i].name) {
            printf("  FAIL: def[%d] has no row (name is NULL)\n", i);
            failures++;
        }

    /* ---- categories (UI_PLAN Phase 2) --------------------
     * BCAT_NONE / RCAT_NONE are 0, so a row added without a category
     * lands there rather than in a real one. Asserting nothing sits at
     * NONE is what turns "I forgot" into a test failure instead of a
     * building quietly missing from its HUD tab. */
    for (i = 0; i < BUILDING_TYPE_COUNT; i++) {
        if (BUILDING_DEFS[i].category == BCAT_NONE) {
            printf("  FAIL: def[%d] (%s) has no category\n", i,
                   BUILDING_DEFS[i].name ? BUILDING_DEFS[i].name : "?");
            failures++;
        } else if ((int)BUILDING_DEFS[i].category >= BCAT_COUNT) {
            printf("  FAIL: def[%d] has an out-of-range category\n", i);
            failures++;
        }
    }
    CHECK(1, "every building def declares a category");

    for (i = 0; i < RES_COUNT; i++) {
        if (RESOURCE_CATEGORIES[i] == RCAT_NONE ||
            (int)RESOURCE_CATEGORIES[i] >= RCAT_COUNT) {
            printf("  FAIL: resource %d (%s) has no category\n", i,
                   RESOURCE_NAMES[i] ? RESOURCE_NAMES[i] : "?");
            failures++;
        }
    }
    CHECK(1, "every resource declares a category");

    /* Category names exist and are distinct — they are tab labels, and
     * two tabs reading the same thing is worse than none. */
    {
        int c, c2, ok = 1;
        for (c = 1; c < BCAT_COUNT; c++) {
            const char *n = building_category_name((BuildingCategory)c);
            if (!n || !n[0]) ok = 0;
            for (c2 = 1; c2 < c; c2++)
                if (strcmp(n, building_category_name((BuildingCategory)c2)) == 0)
                    ok = 0;
        }
        CHECK(ok, "building categories have distinct names");

        ok = 1;
        for (c = 1; c < RCAT_COUNT; c++) {
            const char *n = resource_category_name((ResourceCategory)c);
            if (!n || !n[0]) ok = 0;
            for (c2 = 1; c2 < c; c2++)
                if (strcmp(n, resource_category_name((ResourceCategory)c2)) == 0)
                    ok = 0;
        }
        CHECK(ok, "resource categories have distinct names");
    }

    /* A category is only useful if it actually groups. Every one of
     * them held a building until SUPPLY_CHAIN Phase 2 widened the set
     * ahead of the content that fills it: Factories is empty until the
     * foundries arrive in Phase 4, and the HUD already gives an empty
     * category no tab. So the assertion is now that exactly the
     * categories we know to be empty are empty — which fails both when
     * one is forgotten and when Phase 4 fills Factories without
     * updating this list. */
    {
        int c, unexpected = 0;
        for (c = 1; c < BCAT_COUNT; c++) {
            int n = 0, t, expected_empty = (c == BCAT_FACTORY);
            for (t = 0; t < BUILDING_TYPE_COUNT; t++)
                if ((int)BUILDING_DEFS[t].category == c &&
                    BUILDING_DEFS[t].hud_placeable) n++;
            if ((n == 0) != expected_empty) {
                printf("  FAIL: category '%s' has %d placeable buildings\n",
                       building_category_name((BuildingCategory)c), n);
                unexpected++;
            }
        }
        CHECK(unexpected == 0,
              "every category holds buildings, except Factories until Phase 4");
    }

    /* A couple of pinned assignments, so a careless re-categorisation
     * shows up as a failing test rather than a reshuffled HUD. */
    CHECK(BUILDING_DEFS[BUILDING_BREWERY].category == BCAT_WORKSHOP,
          "the Brewery is a workshop, not a field");
    CHECK(BUILDING_DEFS[BUILDING_LUMBERJACK].category == BCAT_EXTRACTION,
          "the Lumberjack takes something out of the land");
    CHECK(BUILDING_DEFS[BUILDING_FARM].category == BCAT_FARMING,
          "the Farm grows something");
    CHECK(BUILDING_DEFS[BUILDING_HARBOR].category == BCAT_MARITIME,
          "the Harbor is Maritime");
    CHECK(RESOURCE_CATEGORIES[RES_BEER] == RCAT_REFINED,
          "Beer is a refined good");
    CHECK(RESOURCE_CATEGORIES[RES_GOLD] == RCAT_CURRENCY,
          "Gold is currency, not a good");

    /* The two properties the swap actually broke. */
    CHECK(BUILDING_DEFS[BUILDING_SHIPYARD].hud_placeable == 1,
          "Shipyard is HUD-placeable (ship-build popup reachable)");
    /* A Wright's House was upgrade-only until SUPPLY_CHAIN Phase 3
     * made it the base of the second house line. Both base tiers are
     * on the HUD now, and every def in the table is placeable — so
     * the guard becomes "nothing is unreachable", which is what the
     * swapped-row bug actually broke. */
    {
        int t, unreachable = 0;
        for (t = 0; t < BUILDING_TYPE_COUNT; t++)
            if (!BUILDING_DEFS[t].hud_placeable) {
                printf("  FAIL: %s is on no HUD tab and reachable no other "
                       "way\n", BUILDING_DEFS[t].name);
                unreachable++;
            }
        CHECK(unreachable == 0, "every building can be reached from the HUD");
    }
    CHECK(BUILDING_DEFS[BUILDING_HOUSE_WORKER].cost[RES_GOLD] > 0,
          "and a Wright's House costs something, now that it is built");

    printf(failures ? "\nFAILED (%d)\n" : "\nPASSED\n", failures);
    return failures ? 1 : 0;
}
