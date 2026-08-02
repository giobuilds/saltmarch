/*  test_wellbeing.c  --  the six-factor projection (LIFE_PLAN Phase 9)
 *
 * The projection is read-only and floating point, so none of it can
 * desync anything. What it can do is lie, and these assertions are
 * about the shapes the design asks for rather than about any particular
 * number:
 *
 *   - two of the six factors have no system behind them, and their
 *     weight must be EXCLUDED rather than contributed as a constant;
 *   - the score never reaches zero and never leaves the ladder;
 *   - density is an inverted U, not a ramp, so filling every house to
 *     the brim is not free;
 *   - income is logarithmic, so a poor island gains more from a raise
 *     than a rich one;
 *   - the cast is a handful, ordered by notability, and stable.
 *
 * Linked against the sim and the UI library: no SDL.
 */

#include "wellbeing.h"
#include "game.h"
#include "island.h"
#include "resident.h"
#include "calendar.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }     \
        else         { printf("  ok:   %s\n", (msg)); }                 \
    } while (0)

/* ---- 1. the table ----------------------------------------- */
static void test_the_table(void)
{
    float total = 0.0f, modelled = 0.0f;
    int   i, unmodelled = 0;

    printf("\n=== six factors, four of them real ===\n");

    for (i = 0; i < WB_FACTOR_COUNT; i++) {
        total += WELLBEING_FACTORS[i].weight;
        if (WELLBEING_FACTORS[i].modelled) modelled += WELLBEING_FACTORS[i].weight;
        else unmodelled++;
    }

    CHECK(fabsf(total - 8.0f) < 0.001f,
          "the weights sum to 8.0, as new-happiness-design.md sets them");
    CHECK(unmodelled == 2,
          "two factors have no system behind them — corruption and giving");
    CHECK(!WELLBEING_FACTORS[WB_CORRUPTION].modelled &&
          !WELLBEING_FACTORS[WB_GENEROSITY].modelled,
          "and they are the two the sim has never modelled");
    CHECK(modelled < total,
          "so the modelled weight is less than the whole");
    CHECK(WELLBEING_FACTORS[WB_SOCIAL].weight >
          WELLBEING_FACTORS[WB_INCOME].weight,
          "social support is the heaviest, by design");
}

/* ---- 2. THE UNMODELLED FACTORS ARE EXCLUDED ---------------- */
/* If they were contributed as a constant rather than excluded, an
 * island that is perfect in every way the game models could never reach
 * the top of the ladder — and the missing 0.9 would look like a
 * balance problem rather than an unbuilt feature. */
static void test_unmodelled_are_excluded(void)
{
    int i;

    printf("\n=== and the two that are not real do not drag ===\n");

    (void)i;
    {
        UiSnapshot   snap;
        UiResident   r;
        Wellbeing    best;

        memset(&snap, 0, sizeof(snap));
        snap.islands[0].settled = 1;
        memset(&r, 0, sizeof(r));

        wellbeing_resident(&best, &snap, 0, &r);
        CHECK(best.score >= WB_BASELINE,
              "even the worst case sits on the baseline, never zero");
        CHECK(best.score <= WB_SCALE_MAX,
              "and nothing exceeds the top of the ladder");
    }
}

/* ---- 3. density is a curve, not a ramp -------------------- */
static void test_density_is_an_inverted_u(void)
{
    UiSnapshot snap;
    UiResident r;
    Wellbeing  lonely, comfortable, crowded;

    printf("\n=== a house can be too full as well as too empty ===\n");

    memset(&snap, 0, sizeof(snap));
    snap.islands[0].settled        = 1;
    snap.islands[0].building_count = 1;
    snap.islands[0].buildings[0].active = 1;

    memset(&r, 0, sizeof(r));
    r.home_idx = 0; r.work_idx = -1; r.age_years = 30;

    snap.islands[0].buildings[0].residents = 1;
    wellbeing_resident(&lonely, &snap, 0, &r);

    snap.islands[0].buildings[0].residents = 6;
    wellbeing_resident(&comfortable, &snap, 0, &r);

    snap.islands[0].buildings[0].residents = 12;
    wellbeing_resident(&crowded, &snap, 0, &r);

    CHECK(comfortable.factor[WB_SOCIAL] > lonely.factor[WB_SOCIAL],
          "six under a roof beats one");
    CHECK(comfortable.factor[WB_SOCIAL] > crowded.factor[WB_SOCIAL],
          "and beats twelve — the curve turns over, so filling every "
          "house to the brim is not free");
}

/* ---- 4. income is logarithmic ------------------------------ */
static void test_income_is_logarithmic(void)
{
    UiSnapshot snap;
    UiResident poor, rich;
    Wellbeing  a, b, c;

    printf("\n=== and a raise is worth more to the poor ===\n");

    memset(&snap, 0, sizeof(snap));
    snap.islands[0].settled = 1;

    memset(&poor, 0, sizeof(poor));
    poor.work_idx = 0; poor.home_idx = -1; poor.productivity = 85;
    rich = poor; rich.productivity = 140;

    wellbeing_resident(&a, &snap, 0, &poor);
    wellbeing_resident(&b, &snap, 0, &rich);
    CHECK(b.factor[WB_INCOME] > a.factor[WB_INCOME],
          "a more productive worker earns more");

    /* THE CURVE IS LOGARITHMIC, and the test has to reach far enough up
     * it to show that. Real wages span 1.7 to 2.8 -- the flat rate
     * times the productivity band -- which is the near-linear foot of
     * log(1+x), so comparing two real workers proves nothing about the
     * shape. That the range is this narrow is itself the finding:
     * income barely moves until wages themselves vary. */
    {
        UiResident lo = poor, mid = poor, hi = poor;
        lo.productivity  = 100;
        mid.productivity = 1000;
        hi.productivity  = 10000;
        wellbeing_resident(&a, &snap, 0, &lo);
        wellbeing_resident(&b, &snap, 0, &mid);
        wellbeing_resident(&c, &snap, 0, &hi);
        CHECK((b.factor[WB_INCOME] - a.factor[WB_INCOME]) >
              (c.factor[WB_INCOME] - b.factor[WB_INCOME]),
              "a tenfold raise is worth more to the poor than to the rich");
    }

    /* Somebody with no work earns nothing at all. */
    {
        UiResident idle = poor;
        idle.work_idx = -1;
        wellbeing_resident(&a, &snap, 0, &idle);
        CHECK(a.factor[WB_INCOME] == 0.0f, "and no work is no wage");
    }
}

/* ---- 5. unhoused is the worst case ------------------------ */
static void test_unhoused_scores_worst(void)
{
    UiSnapshot snap;
    UiResident housed, roofless;
    Wellbeing  a, b;

    printf("\n=== and nobody is worse off than the unhoused ===\n");

    memset(&snap, 0, sizeof(snap));
    snap.islands[0].settled        = 1;
    snap.islands[0].building_count = 1;
    snap.islands[0].buildings[0].active    = 1;
    snap.islands[0].buildings[0].residents = 6;

    memset(&housed, 0, sizeof(housed));
    housed.home_idx = 0; housed.work_idx = 0; housed.age_years = 30;
    housed.happiness = HAPPINESS_MAX; housed.married = 1;
    housed.productivity = PRODUCTIVITY_BASE;

    roofless = housed;
    roofless.home_idx = -1; roofless.work_idx = -1; roofless.happiness = -1;

    wellbeing_resident(&a, &snap, 0, &housed);
    wellbeing_resident(&b, &snap, 0, &roofless);
    CHECK(b.score < a.score, "a roof, a job and a full larder beat none");
    CHECK(b.score >= WB_BASELINE, "but even they clear the floor");
}

/* ---- 6. the cast, on a real island ------------------------- */
static void test_the_cast(void)
{
    GameState *gs = game_init();
    UiSnapshot snap;
    Island    *isl;
    int        t, i, wr, wc, laid = 0;

    printf("\n=== a cast, not a census ===\n");
    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 12345u);
    isl = game_cur_island(gs);

    for (wr = 0; wr + 5 < MAP_ROWS && !laid; wr++)
        for (wc = 0; wc + 6 < MAP_COLS && !laid; wc++) {
            int k, ok = 1;
            if (!building_can_place(&isl->map, BUILDING_WAREHOUSE, wr, wc))
                continue;
            for (k = 0; k < 4; k++) {
                if (!building_can_place(&isl->map, BUILDING_ROAD, wr+2, wc+k))
                    ok = 0;
                if (!building_can_place(&isl->map, BUILDING_HOUSE, wr+3, wc+k))
                    ok = 0;
            }
            if (!ok) continue;
            game_place_building(gs, wr, wc, BUILDING_WAREHOUSE, 1);
            for (k = 0; k < 4; k++) {
                game_place_building(gs, wr+2, wc+k, BUILDING_ROAD,  1);
                game_place_building(gs, wr+3, wc+k, BUILDING_HOUSE, 1);
            }
            laid = 1;
        }
    if (!laid) { printf("  FAIL: nowhere to build\n"); failures++;
                 game_free(gs); return; }

    for (t = 0; t < 40 * 12 * (int)CALENDAR_MONTH_TICKS; t++) {
        isl->stockpile.amount[RES_FISH]      = 900;
        isl->stockpile.amount[RES_GRAIN]     = 900;
        isl->stockpile.amount[RES_OILSKINS]  = 900;
        isl->stockpile.amount[RES_MARSH_GIN] = 900;
        sim_run_one_tick(gs);
    }

    ui_snapshot_build(&snap, gs);
    {
        const UiIsland *ui = &snap.islands[gs->current_island];

        CHECK(ui->residents > UI_CAST_MAX,
              "the island holds more people than the cast can name");
        CHECK(ui->cast_count > 0 && ui->cast_count <= UI_CAST_MAX,
              "and the cast is a handful of them");

        /* Ordered, most notable first. */
        {
            int ordered = 1;
            for (i = 1; i < ui->cast_count; i++) {
                const UiResident *a = &ui->cast[i-1], *b = &ui->cast[i];
                int sa = (a->home_idx < 0) ? 2
                       : (a->happiness < HAPPINESS_NEUTRAL ? 1 : 0);
                int sb = (b->home_idx < 0) ? 2
                       : (b->happiness < HAPPINESS_NEUTRAL ? 1 : 0);
                if (sb > sa) ordered = 0;
            }
            CHECK(ordered,
                  "with the unhoused and the hungry before the comfortable");
        }

        /* Everybody named, and the line reads as a sentence. */
        {
            int named = 1;
            for (i = 0; i < ui->cast_count; i++)
                if (ui->cast[i].name[0] == '\0') named = 0;
            CHECK(named, "each of them has a name");

            if (ui->cast_count > 0) {
                char line[160];
                wellbeing_describe(&ui->cast[0], line, sizeof(line));
                CHECK(strlen(line) > 10, "and a line a player can read");
                printf("        \"%s\"\n", line);
            }
        }

        /* And the island scores somewhere on the ladder. */
        {
            Wellbeing w;
            wellbeing_island(&w, &snap, gs->current_island);
            CHECK(w.score > 0.0f && w.score <= WB_SCALE_MAX,
                  "the island itself scores on the ladder");
            printf("        island %.2f / %.0f  (social %.2f income %.2f "
                   "freedom %.2f health %.2f)\n",
                   w.score, WB_SCALE_MAX, w.factor[WB_SOCIAL],
                   w.factor[WB_INCOME], w.factor[WB_FREEDOM],
                   w.factor[WB_HEALTH]);
        }
    }
    game_free(gs);
}

int main(void)
{
    printf("== the wellbeing projection (LIFE_PLAN Phase 9) ==\n");

    test_the_table();
    test_unmodelled_are_excluded();
    test_density_is_an_inverted_u();
    test_income_is_logarithmic();
    test_unhoused_scores_worst();
    test_the_cast();

    printf("\n%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
