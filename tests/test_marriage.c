/*  test_marriage.c  --  households, and the children that follow
 *                      (LIFE_PLAN Phase 6)
 *
 * Phase 5 gave people ages and deaths, and left an island peopled
 * entirely by adult immigrants. This is where an island starts making
 * its own people.
 *
 * THE ONE STRUCTURAL RISK IN THIS PHASE is `spouse`: it is a residents[]
 * INDEX, and slots are reused. A marriage that outlives its partner's
 * slot is a widow married to whoever moves in next — a bug that would
 * never crash, would never fail a hash, and would simply produce quiet
 * nonsense. Half the assertions below exist for that one hazard.
 *
 * THE OTHER THING MEASURED HERE is how much of an island's growth is
 * actually born rather than shipped in. Phase 6 does not add a source of
 * population: it decides WHO fills a slot that pop_update had already
 * opened. So the number to watch is a composition, not a total.
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

/* An adult of `years`, living at `home`.
 *
 * `birth_house` is set to -1 EXPLICITLY and not left to the memset,
 * which is the one trap in building these by hand: zero is a valid
 * building slot, so a founder zeroed rather than initialised claims to
 * have been born in house 0 — and two of them would then read as
 * siblings and refuse to marry, for a reason nothing in the test would
 * show. spawn_resident sets it the same way and for the same reason. */
static void person(Resident *r, uint32_t id, int years, int home, int sex)
{
    memset(r, 0, sizeof(*r));
    r->active      = 1;
    r->id          = id;
    r->age_months  = (int32_t)(years * MONTHS_PER_YEAR);
    r->home_idx    = home;
    r->spouse      = -1;
    r->sex         = sex;
    r->pregnancy   = 0;
    r->birth_house = -1;
}

/* Marriage is a monthly draw, so a test that calls it once is testing
 * the draw rather than the pairing. Run a few years of months. */
static void marry_for_years(Resident r[], int count, int years)
{
    int m;
    for (m = 0; m < years * MONTHS_PER_YEAR; m++)
        residents_marry(r, count, 12345u, (uint64_t)(m + 1) * CALENDAR_MONTH_TICKS);
}

/* ---- 1. who pairs with whom ------------------------------- */
static void test_pairing(void)
{
    Resident r[4];

    printf("\n=== a house makes a household ===\n");

    person(&r[0], 1, 30, 0, SEX_FEMALE);
    person(&r[1], 2, 28, 0, SEX_MALE);
    person(&r[2], 3, 35, 1, SEX_FEMALE);   /* another house entirely */
    person(&r[3], 4, 33, 1, SEX_MALE);

    marry_for_years(r, 4, 5);

    CHECK(r[0].spouse == 1 && r[1].spouse == 0,
          "two adults under one roof marry each other");
    CHECK(r[2].spouse == 3 && r[3].spouse == 2,
          "and the pair next door marry each other, not across the street");

    /* Reciprocity is the invariant every other read depends on. */
    {
        int i, mutual = 1;
        for (i = 0; i < 4; i++)
            if (r[i].spouse >= 0 && r[r[i].spouse].spouse != i) mutual = 0;
        CHECK(mutual, "every marriage points both ways");
    }
}

/* A lone adult has nobody to marry; a child is nobody to marry. */
static void test_who_is_eligible(void)
{
    Resident r[3];

    printf("\n=== and not everyone is eligible ===\n");

    person(&r[0], 1, 30, 0, SEX_FEMALE);
    marry_for_years(r, 1, 10);
    CHECK(r[0].spouse < 0, "an adult alone in a house stays unmarried");

    person(&r[0], 1, 30, 0, SEX_FEMALE);
    person(&r[1], 2,  8, 0, SEX_MALE);      /* a child */
    person(&r[2], 3, 70, 0, SEX_MALE);      /* retired */
    marry_for_years(r, 3, 10);
    CHECK(r[1].spouse < 0, "a child does not marry");
    CHECK(r[0].spouse < 0 && r[2].spouse < 0,
          "and neither does an elder — nor the adult left with only those two");
}

/* Once married, stays married: a second pass must not re-pair anybody.
 *
 * WHO pairs with whom is deliberately not asserted. The scan is in index
 * order but the draw is per pair and per month, so the first pair to
 * come up lucky is the first pair to marry — 0 with 2 is as correct an
 * outcome as 0 with 1. Pinning that down would be asserting the hash
 * rather than the rule. What must hold is that everybody ends up
 * accounted for, mutually, and that it then stops moving. */
static void test_marriage_is_not_re_drawn(void)
{
    Resident r[4];
    int      before[4], i, all_married = 1, stable = 1;

    printf("\n=== a marriage is not asked about twice ===\n");

    person(&r[0], 1, 30, 0, SEX_FEMALE);
    person(&r[1], 2, 28, 0, SEX_MALE);
    person(&r[2], 3, 31, 0, SEX_FEMALE);
    person(&r[3], 4, 29, 0, SEX_MALE);

    marry_for_years(r, 4, 20);
    for (i = 0; i < 4; i++) {
        if (r[i].spouse < 0 || r[r[i].spouse].spouse != i) all_married = 0;
        before[i] = r[i].spouse;
    }
    CHECK(all_married, "four adults in a house become two mutual couples");

    marry_for_years(r, 4, 20);
    for (i = 0; i < 4; i++)
        if (r[i].spouse != before[i]) stable = 0;
    CHECK(stable, "and twenty more years does not shuffle them");
}

/* ---- 2. THE DANGLING INDEX -------------------------------- */
static void test_death_widows(void)
{
    Resident r[2];
    PopData  pop[2];

    printf("\n=== and what a death leaves behind ===\n");

    memset(pop, 0, sizeof(pop));
    pop[0].active = 1; pop[0].residents = 2;

    person(&r[0], 1, 30, 0, SEX_FEMALE);
    person(&r[1], 2, 28, 0, SEX_MALE);
    marry_for_years(r, 2, 5);
    CHECK(r[0].spouse == 1, "a couple, to begin with");

    /* Kill one outright rather than waiting on the death draw: this is a
     * test about the link, not about the odds. */
    r[1].age_months = 200 * MONTHS_PER_YEAR;
    {
        int m;
        for (m = 0; m < 240 && r[1].active; m++)
            residents_age(r, 2, pop, 12345u, (uint64_t)(m + 1) * 300);
    }
    CHECK(!r[1].active, "one of them dies");
    CHECK(r[0].spouse < 0,
          "and the survivor is widowed, not left pointing at an empty slot");
}

/* The hazard in full: a slot is reused, and the newcomer must not
 * inherit the marriage that used to live there. */
static void test_a_reused_slot_is_a_stranger(void)
{
    Resident r[2];
    PopData  pop[2];
    int      m;

    printf("\n=== a stranger moves into the empty room ===\n");

    memset(pop, 0, sizeof(pop));
    pop[0].active = 1; pop[0].residents = 2;

    /* Married while both were of an age to marry — an elder is not
     * marriageable, so the old one has to get old the ordinary way. */
    person(&r[0], 1, 30, 0, SEX_FEMALE);
    person(&r[1], 2, 60, 0, SEX_MALE);
    marry_for_years(r, 2, 5);
    CHECK(r[0].spouse == 1, "a couple, one of them much older");

    r[1].age_months = 90 * MONTHS_PER_YEAR;
    for (m = 0; m < 240 && r[1].active; m++)
        residents_age(r, 2, pop, 12345u, (uint64_t)(m + 1) * 300);
    CHECK(!r[1].active, "the old one dies and slot 1 falls empty");

    /* Somebody new takes the slot — exactly what residents_sync does. */
    person(&r[1], 99, 26, 0, SEX_MALE);
    CHECK(r[0].spouse < 0 && r[1].spouse < 0,
          "the newcomer is not married to the widow by accident of address");

    marry_for_years(r, 2, 10);
    CHECK(r[0].spouse == 1 && r[1].spouse == 0,
          "though they may of course marry on their own account");
}

/* ---- 3. conception, gestation, birth ---------------------- */
/* Runs `months` of the breeding cycle and reports how many months the
 * mother spent carrying, so the nine can be counted rather than assumed. */
static int breed_for_months(Resident r[], int *count, uint32_t *next_id,
                            PopData pop[], int months)
{
    int m, carried = 0;
    for (m = 0; m < months; m++) {
        residents_breed(r, count, next_id, pop, 1, 12345u,
                        (uint64_t)(m + 1) * CALENDAR_MONTH_TICKS);
        if (r[0].pregnancy > 0) carried++;
    }
    return carried;
}

static void test_a_child_takes_nine_months(void)
{
    Resident r[8];
    PopData  pop[2];
    int      count = 2, carried;
    uint32_t next_id = 50;

    printf("\n=== nine months, and then a child ===\n");

    memset(r, 0, sizeof(r));
    memset(pop, 0, sizeof(pop));
    pop[0].active = 1; pop[0].residents = 2;

    person(&r[0], 1, 30, 0, SEX_FEMALE);
    person(&r[1], 2, 28, 0, SEX_MALE);
    marry_for_years(r, 2, 5);
    CHECK(residents_fertile_couple_at(r, 2, 0), "a young couple keeps house");
    CHECK(residents_adults_at(r, 2, 0) == 2, "and both of them work");

    /* Long enough that the conception draw is certain to have come up. */
    carried = breed_for_months(r, &count, &next_id, pop, 120);

    CHECK(count > 2, "a child is born");
    CHECK(r[2].age_months == 0, "and is a newborn, not an arrival");
    CHECK(r[2].birth_house == 0, "who knows which house they were born in");
    CHECK(pop[0].residents > 2,
          "and the house is one larger for it — birth drives the count up");
    CHECK(carried >= PREGNANCY_MONTHS,
          "somebody carried for at least the nine months it takes");
}

static void test_carrying_is_not_working(void)
{
    Resident r[4];

    printf("\n=== and a pregnancy costs the island a pair of hands ===\n");

    person(&r[0], 1, 30, 0, SEX_FEMALE);
    person(&r[1], 2, 28, 0, SEX_MALE);
    CHECK(residents_adults_at(r, 2, 0) == 2, "two adults, two workers");

    r[0].pregnancy = PREGNANCY_MONTHS;
    CHECK(residents_adults_at(r, 2, 0) == 1,
          "she is carrying, so the house is down to one");
    CHECK(residents_mouths_at(r, 2, 0) == 2,
          "but she still eats — a pregnancy costs labour, not rations");

    r[0].pregnancy = 0;
    CHECK(residents_adults_at(r, 2, 0) == 2, "and she goes back to work");
}

/* Growth is birth and nothing else now: an unmarried pair never grows. */
static void test_nobody_immigrates_into_a_house(void)
{
    Resident r[8];
    PopData  pop[2];
    int      count = 2;
    uint32_t next_id = 50;

    printf("\n=== and nobody arrives by boat ===\n");

    memset(r, 0, sizeof(r));
    memset(pop, 0, sizeof(pop));
    pop[0].active = 1; pop[0].residents = 2;

    person(&r[0], 1, 30, 0, SEX_FEMALE);
    person(&r[1], 2, 28, 0, SEX_MALE);            /* never married */
    CHECK(!residents_fertile_couple_at(r, 2, 0),
          "two lodgers are not a household");

    breed_for_months(r, &count, &next_id, pop, 240);
    CHECK(count == 2 && pop[0].residents == 2,
          "twenty years later the house is exactly as full as it was");
}

/* Siblings share a birth_house, and a house is a family now. */
static void test_siblings_do_not_marry(void)
{
    Resident r[4];
    int      i, any = 0;

    printf("\n=== and a brother does not marry his sister ===\n");

    person(&r[0], 1, 20, 0, SEX_FEMALE);
    person(&r[1], 2, 22, 0, SEX_MALE);
    r[0].birth_house = 0;      /* both born in this house */
    r[1].birth_house = 0;

    marry_for_years(r, 2, 30);
    CHECK(r[0].spouse < 0 && r[1].spouse < 0,
          "thirty years under one roof and they never marry");

    /* But a child of the house and somebody born elsewhere may. */
    r[1].birth_house = 7;
    marry_for_years(r, 2, 30);
    for (i = 0; i < 2; i++) if (r[i].spouse >= 0) any = 1;
    CHECK(any, "though somebody born elsewhere is fair game");
}

static void test_fertility_ends(void)
{
    Resident r[2];

    printf("\n=== and it does not go on forever ===\n");

    person(&r[0], 1, 30, 0, SEX_FEMALE);
    person(&r[1], 2, 28, 0, SEX_MALE);
    marry_for_years(r, 2, 5);
    CHECK(residents_fertile_couple_at(r, 2, 0), "young, and a couple");

    r[0].age_months = (AGE_FERTILE_MAX_YEARS + 1) * MONTHS_PER_YEAR;
    CHECK(!residents_fertile_couple_at(r, 2, 0),
          "one of them past the limit is enough to end it");

    r[0].age_months = 30 * MONTHS_PER_YEAR;
    r[1].age_months = (AGE_FERTILE_MAX_YEARS + 1) * MONTHS_PER_YEAR;
    CHECK(!residents_fertile_couple_at(r, 2, 0),
          "and it is checked on both of them, not just the first");
}

/* ---- 4. a real island, for decades ------------------------ */
/* The same village test_ageing.c lays, and for the same reason: a
 * measurement taken on a fresh island measures nothing, because a fresh
 * island has no houses on it. */
static int build_village(GameState *gs, int houses)
{
    Island *isl = game_cur_island(gs);
    int     wr, wc, laid = 0;

    for (wr = 0; wr + 5 < MAP_ROWS && !laid; wr++)
        for (wc = 0; wc + houses + 2 < MAP_COLS && !laid; wc++) {
            int rr = wr + 2, hr = wr + 3, ok = 1, h;

            if (!building_can_place(&isl->map, BUILDING_WAREHOUSE, wr, wc))
                continue;
            for (h = 0; h < houses; h++) {
                if (!building_can_place(&isl->map, BUILDING_ROAD,  rr, wc + h))
                    ok = 0;
                if (!building_can_place(&isl->map, BUILDING_HOUSE, hr, wc + h))
                    ok = 0;
            }
            if (!ok) continue;

            game_place_building(gs, wr, wc, BUILDING_WAREHOUSE, 1);
            for (h = 0; h < houses; h++) {
                game_place_building(gs, rr, wc + h, BUILDING_ROAD,  1);
                game_place_building(gs, hr, wc + h, BUILDING_HOUSE, 1);
            }
            laid = 1;
        }
    return laid;
}

#define YEARS_RUN 60

static void test_a_village_over_sixty_years(void)
{
    static const uint32_t SEEDS[3] = { 1u, 12345u, 777u };
    static char born_here[1 << 16];
    int  s, any_married = 0, any_born = 0, mutual_always = 1;
    long total_arrivals = 0, total_births = 0;

    printf("\n=== sixty years of a real village ===\n");

    for (s = 0; s < 3; s++) {
        GameState *gs = game_init();
        Island    *isl;
        int        t, i;
        uint32_t   first_id;
        long       births = 0;

        if (!gs) continue;
        game_new_seeded(gs, SEEDS[s]);
        if (!build_village(gs, 8)) { game_free(gs); continue; }
        isl      = game_cur_island(gs);
        first_id = isl->next_resident_id;
        memset(born_here, 0, sizeof(born_here));

        for (t = 0; t < YEARS_RUN * 12 * (int)CALENDAR_MONTH_TICKS; t++) {
            /* The larder kept full by hand, as test_ageing does: this is
             * a test of demography, not of logistics. */
            isl->stockpile.amount[RES_FISH]      = 900;
            isl->stockpile.amount[RES_GRAIN]     = 900;
            isl->stockpile.amount[RES_OILSKINS]  = 900;
            isl->stockpile.amount[RES_MARSH_GIN] = 900;
            sim_run_one_tick(gs);

            if (t % (int)CALENDAR_MONTH_TICKS) continue;

            for (i = 0; i < isl->resident_count; i++) {
                const Resident *r = &isl->residents[i];
                if (!r->active) continue;

                /* THE INVARIANT, CHECKED EVERY MONTH OF SIXTY YEARS.
                 * If a reused slot ever inherits a marriage, this is
                 * where it shows up. */
                if (r->spouse >= 0) {
                    any_married = 1;
                    if (r->spouse >= isl->resident_count ||
                        !isl->residents[r->spouse].active ||
                        isl->residents[r->spouse].spouse != i)
                        mutual_always = 0;
                }

                /* A founder arrives at 20 or older, so anybody under
                 * eighteen was born here. Marked by id, so each child is
                 * counted once however many months they are seen. */
                if (r->age_months < AGE_ADULT_YEARS * MONTHS_PER_YEAR &&
                    r->id < (1u << 16) && !born_here[r->id]) {
                    born_here[r->id] = 1;
                    births++;
                }
            }
        }

        printf("        seed %-6u  %ld arrivals, %ld of them born here\n",
               SEEDS[s], (long)(isl->next_resident_id - first_id), births);
        total_arrivals += (long)(isl->next_resident_id - first_id);
        total_births   += births;
        if (births > 0) any_born = 1;
        game_free(gs);
    }

    CHECK(any_married, "people marry on a real island, not just in a fixture");
    CHECK(mutual_always,
          "and every marriage stays mutual for sixty years of deaths");
    CHECK(any_born, "and an island produces some of its own people");

    /* Reported rather than asserted at a threshold. A composition this
     * far from an even split is a design question, not a regression, and
     * an assertion here would only pin down a number nobody has decided
     * on — see LIFE_PLAN Phase 6. */
    printf("        overall: %ld of %ld arrivals born on the island (%ld%%)\n",
           total_births, total_arrivals,
           total_arrivals ? total_births * 100 / total_arrivals : 0);
}

int main(void)
{
    printf("== marriage, households and birth (LIFE_PLAN Phase 6) ==\n");

    test_pairing();
    test_who_is_eligible();
    test_marriage_is_not_re_drawn();
    test_death_widows();
    test_a_reused_slot_is_a_stranger();
    test_a_child_takes_nine_months();
    test_carrying_is_not_working();
    test_nobody_immigrates_into_a_house();
    test_siblings_do_not_marry();
    test_fertility_ends();
    test_a_village_over_sixty_years();

    printf("\n%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
