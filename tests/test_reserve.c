/*  test_reserve.c  --  the people with nowhere to live
 *                      (LIFE_PLAN Phase 7)
 *
 * Phase 6b made a house a family and left the grown children with
 * nobody to marry. This is the queue that fixes it: an adult who
 * marries and cannot be housed, or whose parents' house is over
 * capacity, joins a RESERVE — and leaves the island if nobody roofs
 * them.
 *
 * THREE THINGS HERE ARE LOAD-BEARING AND EASY TO GET SILENTLY WRONG:
 *
 *   1. FIFO, and the clock never resets. Somebody who has waited
 *      twenty-three of their twenty-four months must not be sent to the
 *      back of the queue because a house was laid for somebody else, or
 *      because they were moved to another island.
 *   2. A lone occupant. One person may take a roof and wait for a
 *      spouse, so settling returns 1 as legitimately as 2 — and a house
 *      holding one unmarried adult has to be asked again next month
 *      rather than treated as full.
 *   3. The founder allowance is spent PER HOUSE, once. A house that
 *      starves to empty and is re-settled must draw from the reserve,
 *      never from the island's immigration quota. The prototype got
 *      this wrong and turned a hundred places into sixty-three houses.
 *
 * Linked against the sim alone: no SDL, no UI.
 */

#include "game.h"
#include "island.h"
#include "resident.h"
#include "calendar.h"
#include "building.h"
#include "map.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }     \
        else         { printf("  ok:   %s\n", (msg)); }                 \
    } while (0)

/* Somebody waiting for a roof since `since`. birth_house is set
 * explicitly rather than left to the memset: zero is a real building
 * slot, so a zeroed person claims to have been born in house 0 and
 * would read as everybody else's sibling. */
static void waiting(Resident *r, uint32_t id, int sex, int32_t since,
                    int32_t birth_house)
{
    memset(r, 0, sizeof(*r));
    r->active        = 1;
    r->id            = id;
    r->age_months    = 25 * MONTHS_PER_YEAR;
    r->home_idx      = RESIDENT_HOMELESS;
    r->spouse        = -1;
    r->sex           = sex;
    r->birth_house   = birth_house;
    r->reserve_since = since;
}

/* ---- 1. first come, first housed --------------------------- */
static void test_fifo(void)
{
    Resident r[4];

    printf("\n=== first come, first housed ===\n");

    /* Deliberately NOT in slot order: the queue is ordered on how long
     * each has waited, not on where they happen to sit in the array. */
    waiting(&r[0], 1, SEX_FEMALE, 5000, -1);
    waiting(&r[1], 2, SEX_MALE,    100, -1);   /* the longest wait */
    waiting(&r[2], 3, SEX_FEMALE,  900, -1);
    waiting(&r[3], 4, SEX_MALE,   7000, -1);

    CHECK(residents_reserve_count(r, 4) == 4, "four people are waiting");

    CHECK(residents_settle_house(r, 4, 7) == 2,
          "a house takes two of them");
    CHECK(r[1].home_idx == 7,
          "the one who has waited longest gets the roof");
    CHECK(r[2].home_idx == 7,
          "and the longest-waiting of the other sex joins them");
    CHECK(r[1].spouse == 2 && r[2].spouse == 1,
          "who are married on moving in");
    CHECK(r[0].home_idx == RESIDENT_HOMELESS &&
          r[3].home_idx == RESIDENT_HOMELESS,
          "the later arrivals keep waiting");
}

/* The clock is the whole of the fairness guarantee. */
static void test_the_clock_never_resets(void)
{
    Resident r[3];

    printf("\n=== and the clock does not restart ===\n");

    waiting(&r[0], 1, SEX_FEMALE, 100, -1);
    waiting(&r[1], 2, SEX_MALE,   200, -1);
    waiting(&r[2], 3, SEX_FEMALE, 300, -1);

    residents_settle_house(r, 3, 4);      /* houses r[0] and r[1] */
    CHECK(r[2].reserve_since == 300,
          "somebody passed over keeps every month they have served");
}

/* ---- 2. one is a real answer ------------------------------- */
static void test_a_lone_occupant(void)
{
    Resident r[2];

    printf("\n=== a roof for one ===\n");

    waiting(&r[0], 1, SEX_FEMALE, 100, -1);

    CHECK(residents_settle_house(r, 1, 3) == 1,
          "one person takes a house on their own");
    CHECK(r[0].home_idx == 3, "and lives there");
    CHECK(r[0].spouse < 0, "unmarried, because there was nobody");
    CHECK(r[0].reserve_since == 0, "and is no longer waiting");

    /* A spouse turns up later. The house is NOT full — it holds one
     * unmarried adult — so it must be offered them. */
    waiting(&r[1], 2, SEX_MALE, 900, -1);
    CHECK(residents_settle_house(r, 2, 3) == 1,
          "a house with one person in it takes one more");
    CHECK(r[1].home_idx == 3, "who moves in");
    CHECK(r[0].spouse == 1 && r[1].spouse == 0,
          "and they marry — a lone occupant is not left alone forever");
}

/* A queue that is all one sex houses one person and stalls, rather
 * than pairing two women or refusing to house anybody. */
static void test_a_skewed_queue(void)
{
    Resident r[3];
    int      housed;

    printf("\n=== and a run of daughters ===\n");

    waiting(&r[0], 1, SEX_FEMALE, 100, -1);
    waiting(&r[1], 2, SEX_FEMALE, 200, -1);
    waiting(&r[2], 3, SEX_FEMALE, 300, -1);

    housed = residents_settle_house(r, 3, 5);
    CHECK(housed == 1, "one of them takes the roof");
    CHECK(residents_reserve_count(r, 3) == 2,
          "and the rest are still waiting — nobody is paired with a "
          "sister for want of anyone else");
}

/* Siblings are not paired out of the reserve either. */
static void test_siblings_are_not_paired(void)
{
    Resident r[2];

    printf("\n=== and not with a brother ===\n");

    waiting(&r[0], 1, SEX_FEMALE, 100, 4);   /* both born in house 4 */
    waiting(&r[1], 2, SEX_MALE,   200, 4);

    CHECK(residents_settle_house(r, 2, 9) == 1,
          "only one of a brother and sister is housed together");
    CHECK(r[0].spouse < 0, "and they are not married to each other");
}

/* ---- 3. leaving ------------------------------------------- */
static void test_emigration(void)
{
    Resident r[3];
    uint64_t now = (uint64_t)RESERVE_TOLERANCE_MONTHS * CALENDAR_MONTH_TICKS
                 + 1000;

    printf("\n=== and those nobody roofed in time ===\n");

    waiting(&r[0], 1, SEX_FEMALE, 0, -1);          /* served the full wait */
    waiting(&r[1], 2, SEX_MALE, (int32_t)now, -1); /* just arrived         */
    waiting(&r[2], 3, SEX_FEMALE, 0, -1);
    r[2].home_idx = 2;                             /* housed: not waiting  */

    CHECK(residents_emigrate(r, 3, now, NULL, NULL) == 1,
          "one person gives up and leaves");
    CHECK(!r[0].active, "the one who waited longest is gone");
    CHECK(r[1].active, "the newcomer is not");
    CHECK(r[2].active, "and nobody with a roof is touched");
}

/* The relocation hook is offered first, and taking somebody still
 * vacates the slot they left. */
static int took_them(void *ctx, int idx)
{
    int *seen = (int *)ctx;
    *seen = idx;
    return 1;
}

static void test_relocation_still_vacates(void)
{
    Resident r[1];
    int      seen = -1;
    uint64_t now = (uint64_t)RESERVE_TOLERANCE_MONTHS * CALENDAR_MONTH_TICKS
                 + 1;

    printf("\n=== moved on, not duplicated ===\n");

    waiting(&r[0], 1, SEX_FEMALE, 0, -1);

    CHECK(residents_emigrate(r, 1, now, took_them, &seen) == 1,
          "the relocation hook is offered them");
    CHECK(seen == 0, "and told which resident");
    CHECK(!r[0].active,
          "and the slot they left is vacated — otherwise the same person "
          "stands on two islands, eating twice");
}

/* ---- 4. the allowance is spent once per house -------------- */
static void test_the_allowance_is_per_house(void)
{
    GameState *gs = game_init();
    Island    *isl;
    int        before, idx = -1, i;

    printf("\n=== a hundred settlers, not a hundred attempts ===\n");
    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }

    game_new_seeded(gs, 4242u);
    isl = game_cur_island(gs);

    /* One house, laid the ordinary way. */
    for (i = 0; i < MAP_ROWS && idx < 0; i++) {
        int c;
        for (c = 0; c < MAP_COLS; c++)
            if (building_can_place(&isl->map, BUILDING_HOUSE, i, c)) {
                game_place_building(gs, i, c, BUILDING_HOUSE, 1);
                sim_run_one_tick(gs);
                idx = i * 0 + (isl->building_count - 1);
                break;
            }
    }
    if (idx < 0) { printf("  FAIL: nowhere to build\n"); failures++;
                   game_free(gs); return; }

    CHECK(isl->pop_data[idx].founded,
          "laying a house spends a founder place and settles it");
    before = isl->founder_allowance;

    /* Empty it, as starvation would, and offer it a household again. */
    isl->pop_data[idx].residents = 0;
    for (i = 0; i < isl->resident_count; i++)
        if (isl->residents[i].home_idx == idx) isl->residents[i].active = 0;

    island_settle_house(isl, idx, gs->world_seed);
    CHECK(isl->founder_allowance == before,
          "re-settling an emptied house spends no second place");
    CHECK(isl->pop_data[idx].residents == 0,
          "and with an empty reserve it simply stands empty");

    game_free(gs);
}

/* ---- 5. capacity is enforced against grown children -------- */
static void test_grown_children_move_out(void)
{
    Resident r[16];
    PopData  pop[2];
    int      i, n = HOUSE_CAPACITY + 3;

    printf("\n=== and a crowded house sends its grown children out ===\n");

    memset(pop, 0, sizeof(pop));
    pop[0].active = 1; pop[0].residents = n;

    /* Two married parents and a crowd of grown, unmarried children. */
    for (i = 0; i < n; i++) {
        waiting(&r[i], (uint32_t)(i + 1), i % 2, 0, 0);
        r[i].home_idx      = 0;
        r[i].reserve_since = 0;
        r[i].age_months    = (int32_t)((20 + i) * MONTHS_PER_YEAR);
    }
    r[0].spouse = 1; r[1].spouse = 0;      /* the parents */
    r[0].birth_house = -1; r[1].birth_house = -1;

    residents_leave_home(r, n, pop, 1, 9000);
    CHECK(pop[0].residents == n - 1,
          "one grown child leaves — one a month, not a whole generation");
    CHECK(r[0].home_idx == 0 && r[1].home_idx == 0,
          "and never a parent: the household stands");

    /* Run it until the house is back within capacity. */
    for (i = 0; i < 20 && pop[0].residents > HOUSE_CAPACITY; i++)
        residents_leave_home(r, n, pop, 1, (uint64_t)(9000 + i * 300));
    CHECK(pop[0].residents == HOUSE_CAPACITY,
          "and it stops exactly at capacity");
    CHECK(residents_reserve_count(r, n) == 3,
          "the three who left are waiting for a roof");
}

int main(void)
{
    printf("== the reserve (LIFE_PLAN Phase 7) ==\n");

    test_fifo();
    test_the_clock_never_resets();
    test_a_lone_occupant();
    test_a_skewed_queue();
    test_siblings_are_not_paired();
    test_emigration();
    test_relocation_still_vacates();
    test_the_allowance_is_per_house();
    test_grown_children_move_out();

    printf("\n%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
