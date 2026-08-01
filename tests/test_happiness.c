/*  test_happiness.c  --  the ladder, and the goodwill it buys
 *                        (NEEDS_PLAN Phase 2)
 *
 * The bug this phase exists to kill: a house lost a resident on the
 * FIRST needs tick it went short, so a supply chain that stuttered was
 * punished exactly as hard as one that was never built. A player who
 * had done everything right watched their marshfolk leave because a
 * pasture was between wool ticks.
 *
 * Happiness is 0..10 and moves one step per tick toward what the
 * supplies deserve, which makes the ladder its own buffer — no second
 * field counts consecutive failures, because the number already
 * remembers. So the assertions here are mostly about TIME:
 *
 *   - a full larder climbs to the top and the house grows;
 *   - a house that loses everything keeps its people for ten ticks;
 *   - partial basics are miserable but survivable indefinitely, which
 *     is what "basics are not all-or-nothing" has to mean;
 *   - a rescue climbs back at the same pace it fell;
 *   - and a house with no road is scored as though the island were
 *     empty, however full the warehouse is.
 *
 * Linked against the sim alone: no SDL, no UI.
 */

#include "game.h"
#include "population.h"
#include "building.h"
#include "resource.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }    \
        else         { printf("  ok:   %s\n", (msg)); }                \
    } while (0)

/* One connected Marsh Cottage on an otherwise empty island, and a
 * stockpile the test fills by hand. */
typedef struct { Building b[1]; PopData p[1]; Stockpile s; } World;

static void world_init(World *w, int residents)
{
    memset(w, 0, sizeof(*w));
    w->b[0].active    = 1;
    w->b[0].type      = BUILDING_HOUSE;
    w->b[0].connected = 1;
    pop_init(&w->p[0]);
    w->p[0].residents = residents;
    stockpile_init(&w->s);
}

/* Marshfolk basics and luxuries, by name rather than by index, so this
 * file keeps meaning what it says if the table is reordered. */
static void stock_basics(World *w, int n)
{
    w->s.amount[RES_FISH]  = n;
    w->s.amount[RES_GRAIN] = n;
}
static void stock_luxuries(World *w, int n)
{
    w->s.amount[RES_OILSKINS]  = n;
    w->s.amount[RES_MARSH_GIN] = n;
}

/* One needs tick. pop_update only acts on the interval boundary, so the
 * timer is walked up to it rather than the caller running 300 ticks. */
static void needs_tick(World *w)
{
    w->p[0].timer = NEEDS_INTERVAL_TICKS - 1;
    pop_update(w->p, w->b, 1, &w->s);
}

/* ---- 1. a full larder climbs, and the house grows ---------- */
static void test_plenty(void)
{
    World w;
    int   i, start;

    printf("\n=== a house with everything ===\n");
    world_init(&w, 3);
    start = w.p[0].residents;

    for (i = 0; i < 6; i++) {
        stock_basics(&w, 10);
        stock_luxuries(&w, 10);
        needs_tick(&w);
    }

    CHECK(w.p[0].happiness == HAPPINESS_MAX,
          "everything supplied reaches the top of the ladder");
    CHECK(w.p[0].residents > start, "and people move in");
    CHECK(w.s.amount[RES_GOLD] > 0, "a fed house pays its way");

    /* It climbed rather than jumping: five steps from neutral. */
    world_init(&w, 3);
    stock_basics(&w, 10);
    stock_luxuries(&w, 10);
    needs_tick(&w);
    CHECK(w.p[0].happiness == HAPPINESS_NEUTRAL + 1,
          "one tick of plenty is one step, not a leap to the top");
}

/* ---- 2. THE HEADLINE: a stutter costs nobody --------------- */
static void test_the_goodwill(void)
{
    World w;
    int   i, full;

    printf("\n=== the goodwill a well-fed house has earned ===\n");

    /* Get it to the top first. */
    world_init(&w, 4);
    for (i = 0; i < 8; i++) {
        stock_basics(&w, 10);
        stock_luxuries(&w, 10);
        needs_tick(&w);
    }
    full = w.p[0].residents;
    CHECK(w.p[0].happiness == HAPPINESS_MAX, "the house is thriving");

    /* Now the larder is bare. The first tick must cost NOBODY — this
     * is the whole phase in one assertion. */
    stock_basics(&w, 0);
    stock_luxuries(&w, 0);
    needs_tick(&w);
    CHECK(w.p[0].residents == full,
          "one missed tick costs no one — the bug this phase exists for");
    CHECK(w.p[0].happiness == HAPPINESS_MAX - 1,
          "it costs a rung of the ladder instead");

    /* How long the famine can run before it costs anybody. Measured
     * rather than asserted at a guessed number: the property is "a
     * thriving house has about a ladder's worth of reprieve", and the
     * exact rung leaving starts on is a tuning decision that should be
     * free to move without this test lying about it. */
    {
        int ticks = 1;   /* the one above already happened */

        while (w.p[0].residents == full && ticks < 50) {
            stock_basics(&w, 0);
            stock_luxuries(&w, 0);
            needs_tick(&w);
            ticks++;
        }
        CHECK(w.p[0].happiness == 0,
              "a famine walks the ladder all the way down");
        CHECK(ticks >= HAPPINESS_MAX,
              "and a thriving house keeps its people for a ladder's worth "
              "of ticks — five minutes of wall clock, not thirty seconds");
        CHECK(w.p[0].residents == full - 1,
              "the first departure costs exactly one, at the bottom");
    }
}

/* ---- 3. partial basics: miserable, not dead ---------------- */
static void test_partial_basics(void)
{
    World w;
    int   i;

    printf("\n=== fed badly, but fed ===\n");
    world_init(&w, 4);

    /* Fish but no Grain, for a long time. */
    for (i = 0; i < 20; i++) {
        w.s.amount[RES_FISH]  = 10;
        w.s.amount[RES_GRAIN] = 0;
        needs_tick(&w);
    }

    CHECK(w.p[0].happiness > 0 && w.p[0].happiness < HAPPINESS_NEUTRAL,
          "half the basics is below neutral and above zero");
    CHECK(w.p[0].residents == 4,
          "and nobody starves — 'basics are not all-or-nothing' means this");
    CHECK(w.s.amount[RES_FISH] < 10,
          "they eat what there is, rather than refusing a half meal");
}

/* ---- 4. rescue --------------------------------------------- */
static void test_rescue(void)
{
    World w;
    int   i;

    printf("\n=== a rescue ===\n");
    /* Six ticks of famine from neutral: enough to hit the floor and
     * start losing people, not enough to empty the house. A house that
     * empties completely does NOT repopulate itself — nobody is left
     * to be unhappy — and that is deliberate rather than an oversight
     * this test should paper over. */
    world_init(&w, 4);
    for (i = 0; i < 6; i++) { stock_basics(&w, 0); needs_tick(&w); }
    CHECK(w.p[0].happiness == 0, "the house has hit the floor");
    CHECK(w.p[0].residents > 0, "with somebody still in it");

    for (i = 0; i < HAPPINESS_GROW; i++) {
        stock_basics(&w, 10);
        stock_luxuries(&w, 10);
        needs_tick(&w);
    }
    CHECK(w.p[0].happiness >= HAPPINESS_GROW,
          "supplies restored, it climbs back to where people will come");
    CHECK(w.p[0].residents > 0, "with somebody left to welcome them");
}

/* ---- 5. a road is not optional ----------------------------- */
static void test_disconnected(void)
{
    World w;
    int   i;

    printf("\n=== a full warehouse and no road ===\n");
    world_init(&w, 4);
    w.b[0].connected = 0;

    for (i = 0; i < 12; i++) {
        stock_basics(&w, 100);
        stock_luxuries(&w, 100);
        needs_tick(&w);
    }
    CHECK(w.p[0].happiness == 0,
          "a house with no road is scored as though the island were empty");
    CHECK(w.s.amount[RES_FISH] == 100,
          "and nothing is delivered down a road that is not there");
}

/* ---- 6. it is a ladder, not a flag ------------------------- */
static void test_luxuries_are_a_scale(void)
{
    World w;
    int   i, one_lux, both_lux;

    printf("\n=== each luxury is worth something on its own ===\n");

    world_init(&w, 4);
    for (i = 0; i < 12; i++) {
        stock_basics(&w, 10);
        w.s.amount[RES_OILSKINS]  = 10;
        w.s.amount[RES_MARSH_GIN] = 0;
        needs_tick(&w);
    }
    one_lux = w.p[0].happiness;

    world_init(&w, 4);
    for (i = 0; i < 12; i++) { stock_basics(&w, 10); stock_luxuries(&w, 10);
                               needs_tick(&w); }
    both_lux = w.p[0].happiness;

    CHECK(one_lux > HAPPINESS_NEUTRAL,
          "one luxury of two lifts a house above neutral");
    CHECK(both_lux > one_lux, "and the second lifts it further");
    CHECK(both_lux == HAPPINESS_MAX, "both is the top");
}

/* ---- 7. what a house eats (NEEDS_PLAN Phase 3) ------------- */
static void test_consumption_scales(void)
{
    World w;

    printf("\n=== you eat as a person, you own as a household ===\n");

    /* Six mouths: six Fish and six Grain, and ONE of each luxury. */
    world_init(&w, 6);
    stock_basics(&w, 20);
    stock_luxuries(&w, 20);
    needs_tick(&w);
    CHECK(20 - w.s.amount[RES_FISH] == 6,
          "a house of six eats six Fish — raw scales with mouths");
    CHECK(20 - w.s.amount[RES_GRAIN] == 6, "and six Grain");
    CHECK(20 - w.s.amount[RES_OILSKINS] == 1,
          "and ONE set of Oilskins, however many live there");
    CHECK(20 - w.s.amount[RES_MARSH_GIN] == 1, "and one bottle of gin");

    /* One mouth: one of each raw good. A small house is cheap, which is
     * what makes a new colony survivable. */
    world_init(&w, 1);
    stock_basics(&w, 20);
    stock_luxuries(&w, 20);
    needs_tick(&w);
    CHECK(20 - w.s.amount[RES_FISH] == 1, "a house of one eats one Fish");
    CHECK(20 - w.s.amount[RES_OILSKINS] == 1,
          "and still one set of Oilskins — the household, not the head");

    /* Short delivery: the whole amount or the good is not met, but what
     * arrived is eaten. A shortage shows up as an empty warehouse and
     * unhappy people, not as goods left on a shelf. */
    world_init(&w, 6);
    w.s.amount[RES_FISH]  = 3;      /* half a house's worth */
    w.s.amount[RES_GRAIN] = 20;
    needs_tick(&w);
    CHECK(w.s.amount[RES_FISH] == 0, "a short delivery is eaten to the last");
    CHECK(w.p[0].happiness < HAPPINESS_NEUTRAL,
          "and feeding four of six people is not feeding the house");

    /* And the ceiling moved. */
    {
        int i;
        world_init(&w, 1);
        for (i = 0; i < 40; i++) {
            stock_basics(&w, 50);
            stock_luxuries(&w, 50);
            needs_tick(&w);
        }
        CHECK(w.p[0].residents == HOUSE_CAPACITY,
              "a thriving house fills to capacity and stops");
        CHECK(HOUSE_CAPACITY == 6, "which is six now, not ten");
    }
}

int main(void)
{
    printf("== happiness (NEEDS_PLAN Phase 2) ==\n");

    test_plenty();
    test_the_goodwill();
    test_partial_basics();
    test_rescue();
    test_disconnected();
    test_luxuries_are_a_scale();
    test_consumption_scales();

    printf("\n%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
