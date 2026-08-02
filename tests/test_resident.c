/*  test_resident.c  --  a resident is a person  (LIFE_PLAN Phase 3)
 *
 * This phase is deliberately INERT: residents are created, named, aged
 * at arrival and carried through hash, save and snapshot, and then
 * nothing reads them. So there is no behaviour to assert, and the
 * assertions here are about the two things that are expensive to fix
 * later and cheap to fix now:
 *
 *   1. IDENTITY IS REPRODUCIBLE. Names come from hashing
 *      (world_seed, id) rather than from a stored string or an RNG
 *      stream. If that were not deterministic, every later phase would
 *      inherit a desync nobody could localise.
 *
 *   2. THE FORMAT SURVIVES A ROUND TRIP. A snapshot is what a save
 *      embeds and what MSG_WORLD sends, so a field dropped here is a
 *      field silently lost on every join and every reload.
 *
 * And one that is neither, but is the reason the spread exists at all:
 * arrivals must not be the same age, or an island dies in cohorts.
 *
 * Linked against the sim alone: no SDL, no UI.
 */

#include "game.h"
#include "island.h"
#include "resident.h"
#include "snapshot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }     \
        else         { printf("  ok:   %s\n", (msg)); }                 \
    } while (0)

/* ---- 1. stages come out of the age ------------------------- */
static void test_stages(void)
{
    Resident r;

    printf("\n=== four stages, one number ===\n");
    memset(&r, 0, sizeof(r));

    r.age_months = 4 * MONTHS_PER_YEAR;
    CHECK(resident_stage(&r) == LIFE_INFANT, "a four-year-old is a child");
    r.age_months = 15 * MONTHS_PER_YEAR;
    CHECK(resident_stage(&r) == LIFE_TEEN, "fifteen is a youth");
    r.age_months = 30 * MONTHS_PER_YEAR;
    CHECK(resident_stage(&r) == LIFE_ADULT, "thirty is an adult");
    r.age_months = 70 * MONTHS_PER_YEAR;
    CHECK(resident_stage(&r) == LIFE_RETIRED, "seventy is an elder");

    /* The boundaries, because an off-by-one here would put children in
     * the workforce the day Phase 5 gates work on the stage. */
    r.age_months = AGE_ADULT_YEARS * MONTHS_PER_YEAR - 1;
    CHECK(resident_stage(&r) == LIFE_TEEN, "the month before eighteen is not");
    r.age_months = AGE_ADULT_YEARS * MONTHS_PER_YEAR;
    CHECK(resident_stage(&r) == LIFE_ADULT, "and the month of it is");

    CHECK(strcmp(life_stage_name(LIFE_ADULT), "adult") == 0,
          "and each stage has a word for it");
    CHECK(life_stage_name(-1) != NULL, "including one that is out of range");
}

/* ---- 2. a name is a pure function of who you are ----------- */
static void test_names_are_derived(void)
{
    Resident a, b;
    char     n1[64], n2[64], n3[64];

    printf("\n=== names are derived, not stored ===\n");
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    a.id = 7; b.id = 7;

    resident_name(&a, 12345u, n1, sizeof(n1));
    resident_name(&b, 12345u, n2, sizeof(n2));
    CHECK(strcmp(n1, n2) == 0 && n1[0] != '\0',
          "the same id in the same world is the same person");

    resident_name(&a, 999u, n3, sizeof(n3));
    CHECK(strcmp(n1, n3) != 0,
          "and a different world seed peoples the island differently");

    /* Distinct ids must mostly give distinct names, or an island reads
     * as a family reunion. Not asserted as ALL distinct — 48x40 names
     * against a few hundred people will collide, and a collision looks
     * like kin rather than like a bug. */
    {
        int  i, distinct = 0;
        char seen[64][64];

        for (i = 0; i < 64; i++) {
            int j, dup = 0;
            a.id = (uint32_t)i;
            resident_name(&a, 12345u, seen[i], sizeof(seen[i]));
            for (j = 0; j < i; j++)
                if (strcmp(seen[i], seen[j]) == 0) dup = 1;
            if (!dup) distinct++;
        }
        CHECK(distinct >= 60,
              "sixty-four neighbours are nearly all different people");
    }

    /* A tiny buffer must truncate, not scribble. This runs under ASan
     * in CI, which is what makes the assertion worth having. */
    {
        char tiny[4];
        a.id = 3;
        resident_name(&a, 12345u, tiny, sizeof(tiny));
        CHECK(strlen(tiny) < sizeof(tiny), "a short buffer is respected");
        resident_name(NULL, 12345u, tiny, sizeof(tiny));
        CHECK(tiny[0] == '\0', "and no resident is no name");
    }
}

/* Lays a warehouse, a road and `n` houses on the current island, the
 * same shape replay.c's fixture uses. Returns 1 if the village went
 * down. Placement SUBMITS commands, so nothing exists until the ticks
 * at the end have run. */
static int build_houses(GameState *gs, int n)
{
    Island *isl = game_cur_island(gs);
    int     wr, wc, i, laid = 0;

    for (wr = 0; wr + 4 < MAP_ROWS && !laid; wr++)
        for (wc = 0; wc + 2 + n < MAP_COLS && !laid; wc++) {
            int rr = wr + 2, ok = 1, h;

            if (!building_can_place(&isl->map, BUILDING_WAREHOUSE, wr, wc))
                continue;
            if (!building_can_place(&isl->map, BUILDING_ROAD, rr, wc)) continue;
            for (h = 0; h < n; h++)
                if (!building_can_place(&isl->map, BUILDING_HOUSE,
                                        rr, wc + 1 + h)) ok = 0;
            if (!ok) continue;

            game_place_building(gs, wr, wc, BUILDING_WAREHOUSE, 1);
            game_place_building(gs, rr, wc, BUILDING_ROAD,      1);
            for (h = 0; h < n; h++)
                game_place_building(gs, rr, wc + 1 + h, BUILDING_HOUSE, 1);
            laid = 1;
        }

    if (!laid) return 0;
    for (i = 0; i < 400; i++) sim_run_one_tick(gs);
    return 1;
}

/* ---- 3. THE HEADLINE: arrivals are not a cohort ------------
 * Residents who arrive together age together, retire together and die
 * together, taking every worker in a chain with them at once. */
static void test_arrivals_are_not_a_cohort(void)
{
    GameState *gs = game_init();
    /* Seeded: game_init() takes its seed from the CLOCK, so an
     * unseeded test is a different world every run — which is how
     * test_ageing came to report one number locally and another in
     * CI. */
    if (gs) game_new_seeded(gs, 4242u);
    Island    *isl;
    int        i, oldest = 0, youngest = 1 << 30, live = 0;

    printf("\n=== and they are not all the same age ===\n");
    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }

    /* A FRESH ISLAND HAS NO BUILDINGS AT ALL, so this has to lay some.
     * The first draft of this test just ran ticks and read whoever
     * turned up, found nobody, printed a note and passed — the exact
     * shape of silent non-coverage that has now been found three times
     * in replay.c's fixture. If the village cannot be laid, that is a
     * failure and not a shrug. */
    if (!build_houses(gs, 3)) {
        printf("  FAIL: could not lay houses to put anybody in\n");
        failures++;
        game_free(gs);
        return;
    }
    isl = game_cur_island(gs);

    for (i = 0; i < isl->resident_count; i++) {
        const Resident *p = &isl->residents[i];
        if (!p->active) continue;
        live++;
        if (p->age_months > oldest)   oldest   = p->age_months;
        if (p->age_months < youngest) youngest = p->age_months;
    }

    CHECK(live > 1, "the houses have people in them");

    CHECK(oldest > youngest,
          "arrivals are of different ages, so they will not die together");
    CHECK(youngest >= AGE_ADULT_YEARS * MONTHS_PER_YEAR,
          "and every one of them is old enough to work");

    game_free(gs);
}

/* ---- 4. the format survives the round trip ----------------- */
static void test_snapshot_round_trip(void)
{
    GameState     *a = game_init(), *b = game_init();
    unsigned char *buf = NULL;
    size_t         len = 0;
    int            i, isl_i, matched = 0, checked = 0;

    printf("\n=== a person survives being sent over a wire ===\n");
    if (!a || !b) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(a, 4242u);

    /* With people in it, or the round trip proves only that zero
     * residents survive being sent, which they would either way. */
    CHECK(build_houses(a, 3), "a village to put people in");

    CHECK(snapshot_encode(a, &buf, &len) && buf != NULL,
          "the world encodes");
    if (!buf) { game_free(a); game_free(b); return; }

    CHECK(snapshot_decode(b, buf, len), "and decodes into another world");

    for (isl_i = 0; isl_i < MAX_ISLANDS; isl_i++) {
        const Island *x = &a->islands[isl_i], *y = &b->islands[isl_i];

        if (x->resident_count != y->resident_count) {
            printf("  FAIL: island %d lost residents in transit\n", isl_i);
            failures++;
            continue;
        }
        if (x->next_resident_id != y->next_resident_id) {
            printf("  FAIL: island %d forgot who it would name next\n", isl_i);
            failures++;
        }
        for (i = 0; i < x->resident_count; i++) {
            checked++;
            if (memcmp(&x->residents[i], &y->residents[i],
                       sizeof(Resident)) == 0) matched++;
        }
    }
    CHECK(matched == checked, "and every person came through unchanged");

    /* The strongest available statement: the whole world agrees. If a
     * resident field were dropped, this is what would catch it even if
     * the loop above were later relaxed. */
    CHECK(sim_hash(a) == sim_hash(b),
          "the two worlds hash identically, residents included");

    free(buf);
    game_free(a);
    game_free(b);
}

/* ---- 5. residents follow the houses ----------------------- */
static void test_sync_tracks_the_houses(void)
{
    Resident  r[MAX_RESIDENTS];
    Building  b[2];
    PopData   p[2];
    int       count = 0, live, i;
    uint32_t  next  = 1;

    printf("\n=== people arrive and leave with the house ===\n");

    memset(r, 0, sizeof(r));
    memset(b, 0, sizeof(b));
    memset(p, 0, sizeof(p));

    b[0].active = 1; b[0].type = BUILDING_HOUSE;
    p[0].active = 1; p[0].residents = 4;

    residents_sync(r, &count, &next, b, p, 1, 12345u);
    live = 0;
    for (i = 0; i < count; i++) if (r[i].active) live++;
    CHECK(live == 4, "four residents for a house of four");
    CHECK(next == 5u, "and four ids were spent");

    p[0].residents = 6;
    residents_sync(r, &count, &next, b, p, 1, 12345u);
    live = 0;
    for (i = 0; i < count; i++) if (r[i].active) live++;
    CHECK(live == 6, "two more move in");

    p[0].residents = 1;
    residents_sync(r, &count, &next, b, p, 1, 12345u);
    live = 0;
    for (i = 0; i < count; i++) if (r[i].active) live++;
    CHECK(live == 1, "and five leave");

    /* Ids are never reused, so a slot recycled for a new arrival is a
     * new person rather than a returning one — which matters the moment
     * anything remembers somebody. */
    {
        uint32_t before = next;
        p[0].residents = 3;
        residents_sync(r, &count, &next, b, p, 1, 12345u);
        CHECK(next == before + 2, "new arrivals get new ids, never old ones");
    }

    /* A building that is not a house houses nobody. */
    b[1].active = 1; b[1].type = BUILDING_WAREHOUSE;
    p[1].active = 1; p[1].residents = 5;
    residents_sync(r, &count, &next, b, p, 2, 12345u);
    live = 0;
    for (i = 0; i < count; i++) if (r[i].active && r[i].home_idx == 1) live++;
    CHECK(live == 0, "and nobody lives in the Warehouse");
}

int main(void)
{
    printf("== residents (LIFE_PLAN Phase 3) ==\n");

    test_stages();
    test_names_are_derived();
    test_arrivals_are_not_a_cohort();
    test_snapshot_round_trip();
    test_sync_tracks_the_houses();

    printf("\n%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
