/*  test_tax.c  --  the treasury, and the loop it could fall into
 *                  (LIFE_PLAN Phase 7)
 *
 * Gold no longer appears because people exist. A building earns what
 * its output is worth, pays its crew, and the player taxes both halves
 * at a rate they set.
 *
 * THE REASON THIS FILE EXISTS is one paragraph in
 * docs/new-happiness-design.md:
 *
 *   "Businesses and residents are taxed. Sustained unhappiness reduces
 *    tax compliance. THIS CLOSES A POSITIVE FEEDBACK LOOP AND WILL
 *    DEATH-SPIRAL WITHOUT DAMPING." — and, in the same section,
 *   "Write a test that runs a district into sustained unhappiness and
 *    asserts recovery is reachable."
 *
 * That is the last test below, and it is the one that matters. The
 * others check the three damping rules individually, because a spiral
 * is much easier to diagnose as "the floor was wrong" than as "the
 * island died".
 *
 * THERE ARE TWO LOOPS, NOT ONE. The document describes unhappiness ->
 * compliance. The tax rate adds rate -> unhappiness -> compliance,
 * which is the player's own hand on the spiral, and it is damped the
 * same way: island_tax_happiness is capped so the rate can make a
 * comfortable island uncomfortable but cannot on its own empty a fed
 * house.
 *
 * Linked against the sim alone: no SDL, no UI.
 */

#include "game.h"
#include "island.h"
#include "population.h"
#include "calendar.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }     \
        else         { printf("  ok:   %s\n", (msg)); }                 \
    } while (0)

/* An island with `n` occupied houses, all at `happiness`. Built by hand
 * rather than simulated: this file is about the compliance rule, and a
 * real village would make it about the food supply instead. */
static void island_at(Island *isl, int n, int happiness)
{
    int i;

    memset(isl, 0, sizeof(*isl));
    isl->tax_rate_permille   = TAX_RATE_DEFAULT_PERMILLE;
    isl->compliance_permille = COMPLIANCE_FULL_PERMILLE;
    isl->building_count      = n;
    for (i = 0; i < n; i++) {
        isl->buildings[i].active = 1;
        isl->buildings[i].type   = BUILDING_HOUSE;
        isl->pop_data[i].active    = 1;
        isl->pop_data[i].residents = 4;
        isl->pop_data[i].happiness = happiness;
    }
}

/* ---- 1. hysteresis: a bad quarter costs nothing ------------ */
static void test_patience(void)
{
    Island isl;
    int    i;

    printf("\n=== a bad quarter is not a crisis ===\n");
    island_at(&isl, 4, 0);                 /* miserable, every house */

    for (i = 0; i < COMPLIANCE_PATIENCE_TICKS; i++)
        island_update_compliance(&isl);

    CHECK(isl.compliance_permille == COMPLIANCE_FULL_PERMILLE,
          "ten months of misery costs no compliance at all");

    island_update_compliance(&isl);
    CHECK(isl.compliance_permille < COMPLIANCE_FULL_PERMILLE,
          "and the eleventh is where it starts to bite");
}

/* ---- 2. the floor ----------------------------------------- */
static void test_the_floor(void)
{
    Island isl;
    int    i;

    printf("\n=== and it never reaches zero ===\n");
    island_at(&isl, 4, 0);

    /* A century of misery. Compliance must not run away, because a
     * revenue of zero is a treasury that can never buy its way out. */
    for (i = 0; i < 1200; i++) island_update_compliance(&isl);

    CHECK(isl.compliance_permille == COMPLIANCE_MIN_PERMILLE,
          "a hundred years of misery stops exactly at the floor");
    CHECK(isl.compliance_permille > 0,
          "so an island in its worst state still pays something");
}

/* ---- 3. recovery is faster than decline -------------------- */
static void test_recovery_is_faster(void)
{
    Island isl;
    int    i, fell, rose;

    printf("\n=== and trust returns faster than it goes ===\n");

    /* Sunk well clear of the ceiling before either rate is measured:
     * one step down and two back up would clamp at full compliance and
     * report the two directions as equal, which is the measurement
     * lying rather than the rule being wrong. */
    island_at(&isl, 4, 0);
    for (i = 0; i < COMPLIANCE_PATIENCE_TICKS + 10; i++)
        island_update_compliance(&isl);
    {
        int before = isl.compliance_permille;
        island_update_compliance(&isl);
        fell = before - isl.compliance_permille;
    }

    /* Now everybody is content. The streak resets and it climbs. */
    for (i = 0; i < isl.building_count; i++)
        isl.pop_data[i].happiness = HAPPINESS_MAX;
    {
        int before = isl.compliance_permille;
        island_update_compliance(&isl);
        rose = isl.compliance_permille - before;
    }

    CHECK(fell > 0 && rose > 0, "it moves in both directions");
    CHECK(rose == fell * 2,
          "and climbs twice as fast as it fell — a rescued island is not "
          "condemned by the quarter it had");
}

/* ---- 4. the rate is one input among several ---------------- */
static void test_the_rate_cannot_dominate(void)
{
    Island isl;
    int    r, worst = 0;

    printf("\n=== and the rate cannot empty a fed house ===\n");

    island_at(&isl, 1, HAPPINESS_MAX);

    CHECK(island_tax_happiness(&isl) == 0,
          "the default rate costs a house nothing");

    for (r = 0; r <= TAX_RATE_MAX_PERMILLE; r += 25) {
        int rungs;
        isl.tax_rate_permille = r;
        rungs = island_tax_happiness(&isl);
        if (rungs < worst) worst = rungs;
        if (rungs > 0) { printf("  FAIL: a rate lifted happiness\n");
                         failures++; }
    }
    CHECK(worst >= -TAX_HAPPINESS_MAX,
          "and the cruellest rate in the range costs at most two rungs");
    CHECK(TAX_HAPPINESS_MAX < HAPPINESS_MAX,
          "which cannot reach the bottom of the ladder on its own — "
          "mitigation three, stated as arithmetic");
}

/* ---- 5. THE ONE THE DOCUMENT ASKS FOR ---------------------- */
/* "Write a test that runs a district into sustained unhappiness and
 *  asserts recovery is reachable." */
static void test_recovery_is_reachable(void)
{
    Island isl;
    int    i, sunk;

    printf("\n=== sustained misery, and the way back ===\n");

    island_at(&isl, 6, 0);

    /* Fifty years of it, at the maximum rate — the worst the player and
     * the world can do at once. */
    isl.tax_rate_permille = TAX_RATE_MAX_PERMILLE;
    for (i = 0; i < 600; i++) island_update_compliance(&isl);
    sunk = isl.compliance_permille;

    CHECK(sunk == COMPLIANCE_MIN_PERMILLE,
          "half a century at the maximum rate sinks it to the floor");

    /* The player relents: the rate comes down and the larder is full. */
    isl.tax_rate_permille = TAX_RATE_DEFAULT_PERMILLE;
    for (i = 0; i < isl.building_count; i++)
        isl.pop_data[i].happiness = HAPPINESS_MAX;

    for (i = 0; i < 200 && isl.compliance_permille < COMPLIANCE_FULL_PERMILLE;
         i++)
        island_update_compliance(&isl);

    CHECK(isl.compliance_permille == COMPLIANCE_FULL_PERMILLE,
          "and it comes all the way back — RECOVERY IS REACHABLE, which "
          "is the assertion new-happiness-design.md asks for by name");
    CHECK(i < 200, "in a bounded number of months, not eventually");
    printf("        (it took %d months to recover from the floor)\n", i);
}

int main(void)
{
    printf("== the treasury and the taxation loop (LIFE_PLAN Phase 7) ==\n");

    test_patience();
    test_the_floor();
    test_recovery_is_faster();
    test_the_rate_cannot_dominate();
    test_recovery_is_reachable();

    printf("\n%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
