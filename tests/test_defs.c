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
    expect_name(BUILDING_HOUSE,        "House");
    expect_name(BUILDING_ROAD,         "Road");
    expect_name(BUILDING_MARKETPLACE,  "Marketplace");
    expect_name(BUILDING_HOP_FARM,     "Hop Farm");
    expect_name(BUILDING_MALTHOUSE,    "Malthouse");
    expect_name(BUILDING_BREWERY,      "Brewery");
    expect_name(BUILDING_SHIPYARD,     "Shipyard");
    expect_name(BUILDING_HOUSE_WORKER, "Worker's House");

    /* Every enum value must have a row at all — a NULL name means a
     * designated row was forgotten entirely. */
    for (i = 0; i < BUILDING_TYPE_COUNT; i++)
        if (!BUILDING_DEFS[i].name) {
            printf("  FAIL: def[%d] has no row (name is NULL)\n", i);
            failures++;
        }

    /* The two properties the swap actually broke. */
    CHECK(BUILDING_DEFS[BUILDING_SHIPYARD].hud_placeable == 1,
          "Shipyard is HUD-placeable (ship-build popup reachable)");
    CHECK(BUILDING_DEFS[BUILDING_HOUSE_WORKER].hud_placeable == 0,
          "Worker's House is upgrade-only, not on the HUD");

    printf(failures ? "\nFAILED (%d)\n" : "\nPASSED\n", failures);
    return failures ? 1 : 0;
}
