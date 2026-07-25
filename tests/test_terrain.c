/*  test_terrain.c  --  crops and deposits  (SUPPLY_CHAIN Phase 1)
 *
 * Phase 1 adds terrain and no content: the map learns to say "there is
 * clay here" and "this soil grows potatoes" before any building wants
 * either. So there is nothing to play yet, and these assertions are the
 * only thing standing between a quiet generation bug and forty
 * buildings placed on top of it in Phases 3-8.
 *
 * Two questions, asked separately:
 *
 *   1. Does every profile actually produce what its chains will start
 *      from? A Highland with no iron is a broken game rather than
 *      interesting scarcity, and it would not be noticed until someone
 *      tried to build the mine three phases from now.
 *   2. Do the new placement rules refuse and accept in the right
 *      places? Asked against defs written here rather than table rows,
 *      via building_place_check_def(), because Phase 1 deliberately
 *      ships no building that wants a deposit.
 *
 * SDL-free: sim library only.
 */

#include <stdio.h>
#include <string.h>
#include "map.h"
#include "building.h"

static int failures = 0;

#define CHECK(cond, msg)                                              \
    do {                                                              \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }    \
        else         { printf("  ok:   %s\n", (msg)); }                \
    } while (0)

/* How many tiles on this map carry `bit` / `dep`. */
static int count_crop(const Map *m, uint32_t bit)
{
    int r, c, n = 0;
    for (r = 0; r < m->rows; r++)
        for (c = 0; c < m->cols; c++)
            if (m->tiles[r][c].fertility & bit) n++;
    return n;
}

static int count_deposit(const Map *m, Deposit d)
{
    int r, c, n = 0;
    for (r = 0; r < m->rows; r++)
        for (c = 0; c < m->cols; c++)
            if (m->tiles[r][c].deposit == d) n++;
    return n;
}

/* Find a buildable tile satisfying a predicate. */
static int find_where(const Map *m, int (*pred)(const Tile *),
                      int *out_r, int *out_c)
{
    int r, c;
    for (r = 0; r < m->rows; r++)
        for (c = 0; c < m->cols; c++)
            if (m->tiles[r][c].buildable && pred(&m->tiles[r][c])) {
                *out_r = r; *out_c = c; return 1;
            }
    return 0;
}

static int is_bare_grass(const Tile *t)
{
    return t->type == TILE_GRASS && t->deposit == DEPOSIT_NONE;
}
static int has_iron(const Tile *t) { return t->deposit == DEPOSIT_IRON; }

/* ---- 1. each profile grows and holds what it advertises ---- */

static void test_profiles(void)
{
    /* Several seeds, because "works on 4242" is not the claim — the
     * claim is that map_init's retry loop makes the contract hold for
     * any seed a player might be handed. */
    static const uint32_t SEEDS[] = { 1u, 4242u, 777u, 12345u, 99991u };
    Map map;
    size_t s;

    printf("--- profile contracts ---\n");

    for (s = 0; s < sizeof(SEEDS) / sizeof(SEEDS[0]); s++) {
        char msg[128];
        uint32_t seed = SEEDS[s];

        map_init(&map, seed, PROFILE_TEMPERATE);
        snprintf(msg, sizeof(msg),
                 "temperate %u: grain, clay and sand to build with", seed);
        CHECK(count_crop(&map, FERTILE_GRAIN) >= 120 &&
              count_deposit(&map, DEPOSIT_CLAY) >= 14 &&
              count_deposit(&map, DEPOSIT_SAND) >= 10, msg);

        map_init(&map, seed, PROFILE_HIGHLAND);
        snprintf(msg, sizeof(msg),
                 "highland %u: hops, and the metal nothing else has", seed);
        CHECK(count_crop(&map, FERTILE_HOP) >= 20 &&
              count_deposit(&map, DEPOSIT_IRON) >= 18 &&
              count_deposit(&map, DEPOSIT_COAL) >= 12 &&
              count_deposit(&map, DEPOSIT_GOLD_ORE) >= 5, msg);

        map_init(&map, seed, PROFILE_WOODLAND);
        snprintf(msg, sizeof(msg), "woodland %u: forest and clay", seed);
        CHECK(count_deposit(&map, DEPOSIT_CLAY) >= 10 &&
              count_deposit(&map, DEPOSIT_COAL) >= 8, msg);

        map_init(&map, seed, PROFILE_ATOLL);
        snprintf(msg, sizeof(msg),
                 "atoll %u: sand, and the only pearls anywhere", seed);
        CHECK(count_deposit(&map, DEPOSIT_SAND) >= 20 &&
              count_deposit(&map, DEPOSIT_PEARLS) >= 10, msg);
    }

    /* Gold and pearls are the scarcity the map is built around: if a
     * temperate home island had them, there would be no argument for
     * ever leaving it. */
    map_init(&map, 4242u, PROFILE_TEMPERATE);
    CHECK(count_deposit(&map, DEPOSIT_GOLD_ORE) == 0 &&
          count_deposit(&map, DEPOSIT_PEARLS) == 0,
          "the home island has no gold and no pearls");

    /* Southern crops have no profile yet (Phase 5). Asserting they are
     * absent is what makes their arrival visible rather than silent. */
    {
        int p, southern = 0;
        for (p = 0; p < PROFILE_COUNT; p++) {
            map_init(&map, 4242u, (MapProfile)p);
            southern += count_crop(&map, FERTILE_COTTON | FERTILE_CANE |
                                         FERTILE_COCOA  | FERTILE_COFFEE |
                                         FERTILE_TOBACCO| FERTILE_MAIZE |
                                         FERTILE_PLANTAIN | FERTILE_LAC);
        }
        CHECK(southern == 0, "no northern profile grows a southern crop");
    }
}

/* ---- 2. crops come in patches, not speckle ---------------- */

static void test_crop_patches(void)
{
    Map map;
    int r, c, boundaries = 0, pairs = 0;

    printf("--- crop layout ---\n");

    map_init(&map, 4242u, PROFILE_TEMPERATE);

    CHECK(count_crop(&map, FERTILE_POTATO) > 0 &&
          count_crop(&map, FERTILE_FLOWERS) > 0,
          "a temperate island grows both of its secondary crops");

    /* Neighbouring grass tiles should usually agree about their
     * secondary crop. A per-tile draw would disagree about half the
     * time; patches disagree only at their edges. */
    for (r = 0; r < map.rows; r++)
        for (c = 0; c + 1 < map.cols; c++) {
            const Tile *a = &map.tiles[r][c], *b = &map.tiles[r][c + 1];
            uint32_t mask = FERTILE_POTATO | FERTILE_FLOWERS;
            if (a->type != TILE_GRASS || b->type != TILE_GRASS) continue;
            pairs++;
            if ((a->fertility & mask) != (b->fertility & mask)) boundaries++;
        }

    CHECK(pairs > 0 && boundaries * 5 < pairs,
          "secondary crops form patches, not per-tile speckle");

    /* Pasture is the exception: it is not exclusive with anything,
     * because a field of grain and a field of sheep competing for the
     * same ground is the decision it exists to create. */
    CHECK(count_crop(&map, FERTILE_PASTURE) ==
          count_crop(&map, FERTILE_GRAIN | FERTILE_HOP),
          "every fertile tile can also be grazed");
}

/* ---- 3. the placement rules -------------------------------- */

static void test_placement_rules(void)
{
    Map map;
    BuildingDef mine, potato;
    int r, c;

    printf("--- crop and deposit placement ---\n");

    map_init(&map, 4242u, PROFILE_HIGHLAND);

    /* Defs written here, not table rows: Phase 1 ships no building
     * that wants a deposit, and the rule still has to be provable. */
    memset(&mine, 0, sizeof(mine));
    mine.name = "test mine";
    mine.tile_w = mine.tile_h = 1;
    mine.placement_flags = PLACE_ANY_LAND;
    mine.needs_deposit = DEPOSIT_IRON;

    memset(&potato, 0, sizeof(potato));
    potato.name = "test potato field";
    potato.tile_w = potato.tile_h = 1;
    potato.placement_flags = PLACE_ANY_LAND;
    potato.needs_fertility = FERTILE_POTATO;

    if (find_where(&map, has_iron, &r, &c)) {
        CHECK(building_place_check_def(&map, &mine, r, c) == REJ_OK,
              "a mine goes on the ore");
        CHECK(building_place_check_def(&map, &potato, r, c) == REJ_OK ||
              !(map.tiles[r][c].fertility & FERTILE_POTATO),
              "a deposit does not stop a crop from growing over it");
    } else {
        printf("  FAIL: a highland with no iron\n");
        failures++;
    }

    if (find_where(&map, is_bare_grass, &r, &c))
        CHECK(building_place_check_def(&map, &mine, r, c) == REJ_NEEDS_DEPOSIT,
              "a mine on bare grass is NEEDS_DEPOSIT");
    else
        printf("  skip: this map is all deposit\n");

    /* Naming a crop is stricter than asking for fertile soil, and the
     * two refusals are different sentences. */
    {
        int rr, cc, found = 0;
        for (rr = 0; rr < map.rows && !found; rr++)
            for (cc = 0; cc < map.cols && !found; cc++) {
                const Tile *t = &map.tiles[rr][cc];
                if (t->buildable && t->fertility != FERTILE_NONE &&
                    !(t->fertility & FERTILE_POTATO)) {
                    CHECK(building_place_check_def(&map, &potato, rr, cc) ==
                          REJ_NEEDS_CROP,
                          "fertile-but-wrong-crop is NEEDS_CROP, not "
                          "NEEDS_FERTILE");
                    found = 1;
                }
            }
        if (!found) printf("  skip: every fertile tile here grows potato\n");
    }

    /* A footprint is only as good as its worst tile. */
    {
        BuildingDef big = mine;
        int rr, cc, found = 0;
        big.tile_w = big.tile_h = 2;
        for (rr = 0; rr + 1 < map.rows && !found; rr++)
            for (cc = 0; cc + 1 < map.cols && !found; cc++) {
                if (map.tiles[rr][cc].deposit == DEPOSIT_IRON &&
                    map.tiles[rr][cc + 1].buildable &&
                    map.tiles[rr][cc + 1].deposit != DEPOSIT_IRON) {
                    CHECK(building_place_check_def(&map, &big, rr, cc) ==
                          REJ_NEEDS_DEPOSIT,
                          "a 2x2 mine half off the seam is refused");
                    found = 1;
                }
            }
        if (!found) printf("  skip: no half-covered 2x2 site on this map\n");
    }

    /* The Hop Farm is the one real building that names a crop, and it
     * must behave exactly as it did when a placement flag said so. */
    {
        int rr, cc, found = 0;
        for (rr = 0; rr < map.rows && !found; rr++)
            for (cc = 0; cc < map.cols && !found; cc++)
                if (map.tiles[rr][cc].fertility & FERTILE_HOP) {
                    CHECK(building_place_check(&map, BUILDING_HOP_FARM,
                                               rr, cc) == REJ_OK,
                          "a hop farm still goes on hop soil");
                    found = 1;
                }
        CHECK(found, "a highland has somewhere to put a hop farm");
    }
}

/* ---- 4. where each mineral is found ------------------------ */

static void test_deposit_terrain(void)
{
    static const uint32_t SEEDS[] = { 1u, 4242u, 777u, 12345u, 99991u };
    Map    map;
    size_t s;
    int    p, clay_on_beach = 0, sand_off_beach = 0, pearls_dry = 0;
    int    overlaps = 0;

    printf("--- where the minerals are ---\n");

    for (s = 0; s < sizeof(SEEDS) / sizeof(SEEDS[0]); s++)
        for (p = 0; p < PROFILE_COUNT; p++) {
            int r, c;
            /* Per map, not across maps: "hill country" is defined by
             * this island's own elevation range, so a highland's clay
             * legitimately sits above a flat temperate island's iron. */
            int lowest_iron = 256, highest_clay = -1;

            map_init(&map, SEEDS[s], (MapProfile)p);
            for (r = 0; r < map.rows; r++)
                for (c = 0; c < map.cols; c++) {
                    const Tile *t = &map.tiles[r][c];
                    int beach = t->type == TILE_SAND;

                    if (t->deposit == DEPOSIT_CLAY && beach) clay_on_beach++;
                    if (t->deposit == DEPOSIT_SAND && !beach) sand_off_beach++;
                    if (t->deposit == DEPOSIT_PEARLS) {
                        int d, wet = 0;
                        static const int dr[4] = { -1, 1, 0, 0 };
                        static const int dc[4] = { 0, 0, 1, -1 };
                        for (d = 0; d < 4; d++) {
                            int nr = r + dr[d], nc = c + dc[d];
                            if (nr < 0 || nr >= map.rows ||
                                nc < 0 || nc >= map.cols) continue;
                            if (map.tiles[nr][nc].type == TILE_WATER) wet = 1;
                        }
                        if (!beach || !wet) pearls_dry++;
                    }
                    if (t->deposit == DEPOSIT_IRON &&
                        t->elevation < lowest_iron)
                        lowest_iron = t->elevation;
                    if (t->deposit == DEPOSIT_CLAY &&
                        t->elevation > highest_clay)
                        highest_clay = t->elevation;
                }

            if (lowest_iron <= highest_clay) overlaps++;
        }

    CHECK(sand_off_beach == 0, "sand is found on the beach and nowhere else");
    CHECK(clay_on_beach == 0,
          "clay stays inland, so it never competes for beach with sand");
    CHECK(pearls_dry == 0, "pearl beds are beach with water alongside");
    CHECK(overlaps == 0,
          "on every island, iron is hill country and clay the low ground");

    /* Every deposit has something to say when hovered — an unnamed
     * seam would draw an empty box over the tile. */
    {
        int d, named = 0;
        for (d = 1; d < DEPOSIT_COUNT; d++)
            if (deposit_label((Deposit)d)[0] &&
                deposit_name((Deposit)d)[0]) named++;
        CHECK(named == DEPOSIT_COUNT - 1, "every deposit has a hover label");
        CHECK(deposit_label(DEPOSIT_NONE)[0] == '\0' &&
              deposit_label((Deposit)999)[0] == '\0',
              "nothing and nonsense both label as empty");
    }
}

/* ---- 5. generation is still a pure function of the seed ---- */

static void test_determinism(void)
{
    Map a, b;

    printf("--- determinism ---\n");

    map_init(&a, 4242u, PROFILE_HIGHLAND);
    map_init(&b, 4242u, PROFILE_HIGHLAND);
    CHECK(memcmp(a.tiles, b.tiles, sizeof(a.tiles)) == 0,
          "the same seed and profile give the same island, deposits too");

    map_init(&b, 4243u, PROFILE_HIGHLAND);
    CHECK(memcmp(a.tiles, b.tiles, sizeof(a.tiles)) != 0,
          "a different seed gives a different island");
}

int main(void)
{
    printf("=== terrain: crops and deposits ===\n");

    test_profiles();
    test_crop_patches();
    test_placement_rules();
    test_deposit_terrain();
    test_determinism();

    if (failures) {
        printf("\n%d FAILED\n", failures);
        return 1;
    }
    printf("\nPASSED\n");
    return 0;
}
