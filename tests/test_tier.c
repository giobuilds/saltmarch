/* test_tier.c  --  the tier graph and the upgrade rule
 * (SUPPLY_CHAIN Phase 2) */

#include "population.h"
#include "building.h"
#include "confirm_view.h"
#include "ui_snapshot.h"
#include "game.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }    \
        else         { printf("  ok:   %s\n", (msg)); }                \
    } while (0)

/* ---- 1. the ceilings are actually raised ------------------- */

static void test_limits(void)
{
    printf("--- ceilings ---\n");

    CHECK(MAX_BUILDING_INPUTS >= 3, "a building can take three inputs");
    CHECK(MAX_TIER_GOODS >= 5, "a tier can want five goods");

    /* RES_WOOD is 0, so a consumes slot omitted from an initialiser
     * reads as Wood rather than as "unused". Widening the array made
     * that a live hazard in every existing row. */
    {
        int t, bad = 0;
        for (t = 0; t < BUILDING_TYPE_COUNT; t++) {
            const BuildingDef *d = &BUILDING_DEFS[t];
            int j;
            for (j = 0; j < MAX_BUILDING_INPUTS; j++)
                if (d->consumes[j] != RES_COUNT && d->consume_amt[j] <= 0) {
                    printf("  FAIL: %s slot %d names %s but consumes 0\n",
                           d->name, j, RESOURCE_NAMES[d->consumes[j]]);
                    bad++;
                }
        }
        CHECK(bad == 0, "no def leaves an unused input slot naming a resource");
    }
}

/* ---- 2. three inputs, all or nothing ----------------------- */

static void test_three_inputs(void)
{
    BuildingDef def;
    Stockpile   s;

    printf("--- three inputs ---\n");

    memset(&def, 0, sizeof(def));
    def.name = "test watchmaker";
    def.consumes[0] = RES_WOOD;  def.consume_amt[0] = 2;
    def.consumes[1] = RES_GRAIN; def.consume_amt[1] = 1;
    def.consumes[2] = RES_BEER;  def.consume_amt[2] = 3;

    stockpile_init(&s);
    CHECK(building_missing_input(&def, &s) == 0,
          "an empty store is short of the first input");

    stockpile_add(&s, RES_WOOD, 2);
    CHECK(building_missing_input(&def, &s) == 1,
          "with the first paid for, the second is what's missing");

    stockpile_add(&s, RES_GRAIN, 1);
    CHECK(building_missing_input(&def, &s) == 2,
          "the THIRD slot is checked at all — the point of the widening");

    stockpile_add(&s, RES_BEER, 2);
    CHECK(building_missing_input(&def, &s) == 2,
          "two of a needed three is still short");

    stockpile_add(&s, RES_BEER, 1);
    CHECK(building_missing_input(&def, &s) == -1, "all three paid, it runs");

    /* All-or-nothing means the check never depends on slot order. */
    {
        BuildingDef rev = def;
        rev.consumes[0] = RES_BEER;  rev.consume_amt[0] = 3;
        rev.consumes[2] = RES_WOOD;  rev.consume_amt[2] = 2;
        CHECK(building_missing_input(&rev, &s) == -1,
              "and it runs whichever order the slots are written in");
    }
}

/* ---- 3. the upgrade rule, at five needs -------------------- */

/* Two tiers the table does not have: a five-good tier, and one gated
 * behind a building. Both arrive in later phases; the rule that will
 * serve them is here now. */
static const TierDef T_FROM = {
    BUILDING_HOUSE,
    { RES_FISH, RES_COUNT, RES_COUNT, RES_COUNT, RES_COUNT, RES_COUNT },
    { RES_COUNT, RES_COUNT, RES_COUNT, RES_COUNT, RES_COUNT, RES_COUNT },
    BUILDING_HOUSE_WORKER, 250, BUILDING_NONE
};
static const TierDef T_FIVE = {
    BUILDING_HOUSE_WORKER,
    { RES_WOOD, RES_FISH, RES_GRAIN, RES_HOPS, RES_BEER, RES_COUNT },
    { RES_COUNT, RES_COUNT, RES_COUNT, RES_COUNT, RES_COUNT, RES_COUNT },
    BUILDING_NONE, 0, BUILDING_NONE
};
static const TierDef T_GATED = {
    BUILDING_HOUSE_WORKER,
    { RES_FISH, RES_COUNT, RES_COUNT, RES_COUNT, RES_COUNT, RES_COUNT },
    { RES_COUNT, RES_COUNT, RES_COUNT, RES_COUNT, RES_COUNT, RES_COUNT },
    BUILDING_NONE, 0, BUILDING_MARKETPLACE   /* stands in for the Academy */
};

static void test_upgrade_rule(void)
{
    int          stock[RES_COUNT];
    BuildingType to;
    int          r;

    printf("--- the upgrade rule ---\n");

    for (r = 0; r < RES_COUNT; r++) stock[r] = 0;
    stock[RES_GOLD] = 1000;

    CHECK(tier_upgrade_check_def(&T_FROM, &T_FIVE, stock, 1, &to) ==
          REJ_NEEDS_GOODS,
          "gold alone does not buy a tier any more");
    CHECK(to == BUILDING_NONE, "and a refusal names no destination");

    /* Feed them one good at a time: every one of the five is required,
     * including the fifth — the slot the widening added. */
    {
        static const ResourceType FIVE[5] =
            { RES_WOOD, RES_FISH, RES_GRAIN, RES_HOPS, RES_BEER };
        int i, still_refused = 0;
        for (i = 0; i < 5; i++) {
            stock[FIVE[i]] = 1;
            if (i < 4 &&
                tier_upgrade_check_def(&T_FROM, &T_FIVE, stock, 1, &to) ==
                REJ_NEEDS_GOODS)
                still_refused++;
        }
        CHECK(still_refused == 4, "each of the first four is a hard gate");
        CHECK(tier_upgrade_check_def(&T_FROM, &T_FIVE, stock, 1, &to) == REJ_OK,
              "with all five in stock it goes through");
        CHECK(to == BUILDING_HOUSE_WORKER, "and names where it is going");
    }

    /* Presence, not payment: the goods are what the tier will want
     * from then on, so the upgrade must not eat them. That is a
     * property of sim_upgrade_house, checked in section 5. Here: one
     * of each is enough. */
    CHECK(tier_upgrade_check_def(&T_FROM, &T_FIVE, stock, 1, &to) == REJ_OK,
          "one of each is enough — it is a supply test, not a price");

    /* Gold is checked last, so the message names the thing you have to
     * go and build rather than the money you also happen to lack. */
    stock[RES_GOLD] = 10;
    CHECK(tier_upgrade_check_def(&T_FROM, &T_FIVE, stock, 1, &to) ==
          REJ_CANT_AFFORD,
          "short of gold with the goods in hand is CANT_AFFORD");
    stock[RES_WOOD] = 0;
    CHECK(tier_upgrade_check_def(&T_FROM, &T_FIVE, stock, 1, &to) ==
          REJ_NEEDS_GOODS,
          "short of both, the goods are what it complains about");
    stock[RES_WOOD] = 1;
    stock[RES_GOLD] = 1000;

    /* The prerequisite building — the Academy's rule, written now. */
    CHECK(tier_upgrade_check_def(&T_FROM, &T_GATED, stock, 0, &to) ==
          REJ_NEEDS_BUILDING,
          "a gated tier refuses without its building");
    CHECK(tier_upgrade_check_def(&T_FROM, &T_GATED, stock, 1, &to) == REJ_OK,
          "and allows it with one");
    CHECK(tier_upgrade_check_def(&T_FROM, &T_GATED, stock, 0, &to) ==
          REJ_NEEDS_BUILDING,
          "the building is checked before the goods, being the bigger ask");

    /* A tier with nowhere to go. */
    CHECK(tier_upgrade_check_def(&T_FIVE, NULL, stock, 1, &to) ==
          REJ_UNAVAILABLE,
          "the top of a line has nowhere to upgrade to");
}

/* ---- 4. the real table is wired up ------------------------- */

static void test_real_table(void)
{
    int          stock[RES_COUNT], r;
    BuildingType to;

    printf("--- the table as shipped ---\n");

    /* SUPPLY_CHAIN Phase 3 rooted two SEPARATE lines rather than a
     * ladder: a Wright's House is built, not upgraded into. Phase 4
     * gives the first of them somewhere to go; the Wrights line waits
     * for Engineers in Phase 6. */
    CHECK(tier_def_for(BUILDING_HOUSE) != NULL &&
          tier_def_for(BUILDING_HOUSE)->next_tier == BUILDING_HOUSE_ARTISAN,
          "a Marsh Cottage climbs to Artisans");
    CHECK(tier_def_for(BUILDING_HOUSE_WORKER) != NULL &&
          tier_def_for(BUILDING_HOUSE_WORKER)->next_tier ==
              BUILDING_HOUSE_ENGINEER,
          "a Wright's House climbs to Engineers (Phase 6)");
    CHECK(tier_def_for(BUILDING_HOUSE_ARTISAN) != NULL &&
          tier_def_for(BUILDING_HOUSE_ARTISAN)->next_tier == BUILDING_NONE,
          "and Artisans is the top of the first line");
    CHECK(tier_def_for(BUILDING_HOUSE_ENGINEER) != NULL &&
          tier_def_for(BUILDING_HOUSE_ENGINEER)->next_tier == BUILDING_NONE,
          "as Engineers is of the second");
    /* Both lines are now two deep and BOTH bases are still built
     * rather than upgraded into — which is the three-line model's
     * whole shape, and what a ladder would have quietly undone. */
    CHECK(BUILDING_DEFS[BUILDING_HOUSE].hud_placeable &&
          BUILDING_DEFS[BUILDING_HOUSE_WORKER].hud_placeable,
          "and both base tiers are still things you build");
    CHECK(!BUILDING_DEFS[BUILDING_HOUSE_ARTISAN].hud_placeable &&
          !BUILDING_DEFS[BUILDING_HOUSE_ENGINEER].hud_placeable,
          "while both upper tiers are only ever upgraded into");
    CHECK(BUILDING_DEFS[BUILDING_HOUSE_WORKER].hud_placeable,
          "which is why a Wright's House is something you build");
    CHECK(tier_def_for(BUILDING_WAREHOUSE) == NULL,
          "a warehouse is not a tier");
    CHECK(tier_upgrade_requires(BUILDING_HOUSE, TIER_BRANCH_LINE) == BUILDING_NONE,
          "no tier needs a building yet — the Academy is Phase 8");

    /* The table NEEDS_PLAN Phase 1 gives them. */
    {
        const TierDef *m = tier_def_for(BUILDING_HOUSE);
        const TierDef *w = tier_def_for(BUILDING_HOUSE_WORKER);
        int i, mb = 0, ml = 0, wb = 0, wl = 0;
        for (i = 0; i < MAX_TIER_GOODS; i++) {
            if (m->basic[i]  != RES_COUNT) mb++;
            if (m->luxury[i] != RES_COUNT) ml++;
            if (w->basic[i]  != RES_COUNT) wb++;
            if (w->luxury[i] != RES_COUNT) wl++;
        }
        CHECK(mb == 2 && ml == 2, "Marshfolk: two basics, two luxuries");
        CHECK(m->basic[0] == RES_FISH && m->basic[1] == RES_GRAIN,
              "and the basics are a fisher's hut and a farm — the "
              "opening a player actually builds");
        CHECK(m->luxury[0] == RES_OILSKINS && m->luxury[1] == RES_MARSH_GIN,
              "with Oilskins and Marsh Gin bought comfort, not survival");
        CHECK(wb == 2 && wl == 3, "Wrights: two basics, three luxuries");
        CHECK(w->basic[0] == RES_SAUSAGES && w->basic[1] == RES_BREAD,
              "Wrights live on Sausages and Bread");

        /* Inheritance is the property that keeps a first island worth
         * having after it succeeds. */
        {
            const TierDef *a = tier_def_for(BUILDING_HOUSE_ARTISAN);
            CHECK(a->basic[0] == RES_FISH && a->basic[1] == RES_GRAIN,
                  "and Artisans still eat what Marshfolk ate");
        }
    }

    /* A Scholar's House wants the basics of wherever its people came
     * from — the first needs in this game that depend on a building's
     * history rather than its kind. */
    {
        const TierDef *sc = tier_def_for(BUILDING_HOUSE_SCHOLAR);
        ResourceType   got[MAX_TIER_GOODS];
        int            n, i, fish = 0, sausages = 0, books = 0;

        n = tier_basic_needs(sc, BUILDING_HOUSE, got);
        for (i = 0; i < n; i++) {
            if (got[i] == RES_FISH)     fish = 1;
            if (got[i] == RES_BOOKS)    books = 1;
            if (got[i] == RES_SAUSAGES) sausages = 1;
        }
        CHECK(books && fish && !sausages,
              "a scholar out of a Marsh Cottage wants Books, Fish and Grain");

        fish = sausages = books = 0;
        n = tier_basic_needs(sc, BUILDING_HOUSE_WORKER, got);
        for (i = 0; i < n; i++) {
            if (got[i] == RES_FISH)     fish = 1;
            if (got[i] == RES_BOOKS)    books = 1;
            if (got[i] == RES_SAUSAGES) sausages = 1;
        }
        CHECK(books && sausages && !fish,
              "and one out of a Wright's House wants Books, Sausages and Bread");

        /* A house with no recorded origin — every house in a save
         * written before this field existed — must still want
         * something real. */
        n = tier_basic_needs(sc, BUILDING_NONE, got);
        CHECK(n >= 2, "an origin nobody recorded falls back to the base tier");
    }

    /* Gold is not the gate, and this is the phase where that stops
     * being a claim about a synthetic table and becomes one about the
     * shipped one: the way to promote a neighbourhood is to build the
     * chains that will keep feeding it. */
    for (r = 0; r < RES_COUNT; r++) stock[r] = 0;
    stock[RES_GOLD] = 10000;
    CHECK(tier_upgrade_check(BUILDING_HOUSE, TIER_BRANCH_LINE, stock, 1, &to) == REJ_NEEDS_GOODS,
          "however rich the island, an unsupplied cottage cannot climb");

    /* The bar for moving in is the tier's BASICS. You may take a house
     * in a neighbourhood you cannot yet keep in spectacles — and then
     * go and build the spectacle shop. Demanding the luxuries too would
     * make every upgrade wait on the whole chain above it. */
    stock[RES_FISH]  = 1;
    stock[RES_GRAIN] = 1;
    CHECK(tier_upgrade_check(BUILDING_HOUSE, TIER_BRANCH_LINE, stock, 1, &to) == REJ_NEEDS_GOODS,
          "a cottage's own food is not enough to become Artisans");
    stock[RES_PRESERVES] = 1;
    CHECK(tier_upgrade_check(BUILDING_HOUSE, TIER_BRANCH_LINE, stock, 1, &to) == REJ_OK,
          "with the basics of the tier above, it may climb");
    CHECK(to == BUILDING_HOUSE_ARTISAN, "and Artisans is where it goes");

    stock[RES_GOLD] = 0;
    CHECK(tier_upgrade_check(BUILDING_HOUSE, TIER_BRANCH_LINE, stock, 1, &to) == REJ_CANT_AFFORD,
          "the goods are necessary but the Gold is still required too");
}

/* ---- 4b. the Academy: a second future for every house ------ */
static void test_academy(void)
{
    static const BuildingType HOUSES[] = {
        BUILDING_HOUSE, BUILDING_HOUSE_WORKER, BUILDING_HOUSE_ARTISAN,
        BUILDING_HOUSE_ENGINEER, BUILDING_HOUSE_MERCHANT,
        BUILDING_HOUSE_INVESTOR
    };
    int          stock[RES_COUNT], r;
    size_t       h;
    BuildingType to;

    printf("--- the Academy ---\n");

    for (r = 0; r < RES_COUNT; r++) stock[r] = 0;
    stock[RES_GOLD]       = 100000;
    stock[RES_BOOKS]      = 1;
    stock[RES_CHARTS]     = 1;
    stock[RES_COFFEE]     = 1;
    stock[RES_SPECTACLES] = 1;

    for (h = 0; h < sizeof(HOUSES) / sizeof(HOUSES[0]); h++) {
        char msg[96];

        CHECK(tier_upgrade_requires(HOUSES[h], TIER_BRANCH_ACADEMY) ==
              BUILDING_ACADEMY,
              "every house type names the Academy as what it waits on");

        /* Without one: refused, and refused for the RIGHT reason. A
         * plain "no" would not tell a player to go and build it. */
        snprintf(msg, sizeof(msg), "%s cannot reach Scholars with no Academy",
                 BUILDING_DEFS[HOUSES[h]].name);
        CHECK(tier_upgrade_check(HOUSES[h], TIER_BRANCH_ACADEMY, stock, 0,
                                 &to) == REJ_NEEDS_BUILDING, msg);

        snprintf(msg, sizeof(msg), "...and can with one");
        CHECK(tier_upgrade_check(HOUSES[h], TIER_BRANCH_ACADEMY, stock, 1,
                                 &to) == REJ_OK, msg);
        CHECK(to == BUILDING_HOUSE_SCHOLAR, "...arriving at Scholars");
    }

    /* A Scholar's House is the end of that road, not a loop. */
    CHECK(tier_branch_target(BUILDING_HOUSE_SCHOLAR, TIER_BRANCH_ACADEMY) ==
          BUILDING_NONE,
          "a Scholar's House is not offered a promotion to itself");

    /* The goods still matter: the Academy is a prerequisite, not a
     * shortcut past the tier's needs. */
    stock[RES_BOOKS] = 0;
    CHECK(tier_upgrade_check(BUILDING_HOUSE, TIER_BRANCH_ACADEMY, stock, 1,
                             &to) == REJ_NEEDS_GOODS,
          "an Academy does not excuse a household from wanting Books");

    /* And the two branches are genuinely different roads: a Marsh
     * Cottage with an Academy has two futures, and the popup's button
     * order is the shared list both sides read. */
    {
        int br[2], n = tier_branches(BUILDING_HOUSE, br);
        CHECK(n == 2, "a Marsh Cottage has two possible futures");
        CHECK(br[0] == TIER_BRANCH_LINE && br[1] == TIER_BRANCH_ACADEMY,
              "its own line first, the Academy second");
        n = tier_branches(BUILDING_HOUSE_INVESTOR, br);
        CHECK(n == 1 && br[0] == TIER_BRANCH_ACADEMY,
              "a terminal tier offers only the Academy");
    }
}

/* ---- 4c. an Academy is a prerequisite, not a patron --------- */
static void test_academy_demolition(void)
{
    GameState *gs = game_init();
    Island    *isl;
    int        r, c, house = -1, academy = -1;

    printf("--- an Academy is a prerequisite, not a patron ---\n");
    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 4242u);
    isl = game_cur_island(gs);
    isl->stockpile.amount[RES_GOLD] = 500000;

    /* A Warehouse, an Academy, a house, and roads over everything. */
    for (r = 0; r < MAP_ROWS; r++)
        for (c = 0; c < MAP_COLS; c++)
            if (building_can_place(&isl->map, BUILDING_WAREHOUSE, r, c)) {
                building_place(isl->buildings, &isl->building_count,
                               &isl->map, BUILDING_WAREHOUSE, r, c);
                r = MAP_ROWS;
                break;
            }
    for (r = 0; r < MAP_ROWS && academy < 0; r++)
        for (c = 0; c < MAP_COLS && academy < 0; c++)
            if (building_can_place(&isl->map, BUILDING_ACADEMY, r, c))
                academy = building_place(isl->buildings,
                                         &isl->building_count, &isl->map,
                                         BUILDING_ACADEMY, r, c);
    for (r = 0; r < MAP_ROWS && house < 0; r++)
        for (c = 0; c < MAP_COLS && house < 0; c++)
            if (building_can_place(&isl->map, BUILDING_HOUSE, r, c))
                house = building_place(isl->buildings, &isl->building_count,
                                       &isl->map, BUILDING_HOUSE, r, c);
    for (r = 0; r < MAP_ROWS; r++)
        for (c = 0; c < MAP_COLS; c++)
            if (building_can_place(&isl->map, BUILDING_ROAD, r, c))
                building_place(isl->buildings, &isl->building_count,
                               &isl->map, BUILDING_ROAD, r, c);

    if (house < 0 || academy < 0) {
        printf("  FAIL: could not place a house and an Academy\n");
        failures++;
        game_free(gs);
        return;
    }
    pop_init(&isl->pop_data[house]);
    sim_run_one_tick(gs);            /* connectivity_update runs here */
    CHECK(isl->buildings[academy].connected,
          "the Academy is road-connected, which the rule requires");

    stockpile_add(&isl->stockpile, RES_BOOKS, 2);
    stockpile_add(&isl->stockpile, RES_CHARTS, 2);
    stockpile_add(&isl->stockpile, RES_COFFEE, 2);
    stockpile_add(&isl->stockpile, RES_SPECTACLES, 2);

    game_upgrade_house(gs, house, TIER_BRANCH_ACADEMY);
    while (gs->cmd_applied < gs->cmd_count) sim_run_one_tick(gs);
    CHECK(isl->buildings[house].type == BUILDING_HOUSE_SCHOLAR,
          "a Marsh Cottage climbs straight to Scholars beside an Academy");

    /* Now take it away. */
    game_demolish_building(gs, academy);
    while (gs->cmd_applied < gs->cmd_count) sim_run_one_tick(gs);
    CHECK(!isl->buildings[academy].active, "the Academy is demolished");

    for (r = 0; r < 200; r++) sim_run_one_tick(gs);
    CHECK(isl->buildings[house].type == BUILDING_HOUSE_SCHOLAR,
          "and the household it made is still a Scholar's, two hundred "
          "ticks later");

    game_free(gs);
}

/* ---- 5. sim and UI answer identically ---------------------- */

static int find_place(GameState *gs, BuildingType type, int *out_r, int *out_c)
{
    Island *isl = game_cur_island(gs);
    int     r, c;

    for (r = 0; r < MAP_ROWS; r++)
        for (c = 0; c < MAP_COLS; c++)
            if (building_can_place(&isl->map, type, r, c)) {
                *out_r = r; *out_c = c; return 1;
            }
    return 0;
}

static void test_sim_and_ui_agree(void)
{
    static UiSnapshot snap;
    GameState        *gs = game_init();
    Island           *isl;
    ConfirmView       view;
    int               r, c, idx, before;
    BuildingType      to_ui;

    printf("--- one rule, both sides ---\n");

    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    isl = game_cur_island(gs);

    if (!find_place(gs, BUILDING_HOUSE, &r, &c)) {
        printf("  FAIL: nowhere to put a house\n");
        failures++;
        game_free(gs);
        return;
    }
    idx = building_place(isl->buildings, &isl->building_count,
                         &isl->map, BUILDING_HOUSE, r, c);
    if (idx < 0) {
        printf("  FAIL: could not place a house\n");
        failures++;
        game_free(gs);
        return;
    }
    pop_init(&isl->pop_data[idx]);

    /* SUPPLY_CHAIN Phase 4 gives this test its accept path back. Until */
    isl->stockpile.amount[RES_GOLD] = 5000;
    stockpile_add(&isl->stockpile, RES_FISH, 3);
    stockpile_add(&isl->stockpile, RES_OILSKINS, 3);
    stockpile_add(&isl->stockpile, RES_MARSH_GIN, 3);
    ui_snapshot_build(&snap, gs);
    CHECK(snapshot_upgrade_check(&snap.islands[snap.current_island], idx,
                                 TIER_BRANCH_LINE, &to_ui) == REJ_NEEDS_GOODS,
          "the UI refuses a cottage that has none of what Artisans want");
    CHECK(to_ui == BUILDING_NONE, "and names no destination while refusing");

    /* The popup says so too, rather than offering a button. */
    gs->confirm.open = 1;
    gs->confirm.kind = CONFIRM_UPGRADE;
    gs->confirm.cmd.kind = CMD_UPGRADE_HOUSE;
    gs->confirm.cmd.a = gs->current_island;
    gs->confirm.cmd.b = idx;
    ui_snapshot_build(&snap, gs);
    confirm_view_build(&view, &snap);
    CHECK(view.refusal == REJ_NEEDS_GOODS, "the popup records the refusal");
    CHECK(!view.options[0].affordable,
          "and cannot be accepted, however much Gold is in the store");

    /* The sim refuses identically, and changes nothing when it does. */
    before = isl->stockpile.amount[RES_GOLD];
    gs->confirm.open = 0;
    game_upgrade_house(gs, idx, TIER_BRANCH_LINE);
    while (gs->cmd_applied < gs->cmd_count) sim_run_one_tick(gs);

    CHECK(isl->buildings[idx].type == BUILDING_HOUSE,
          "the sim refuses the same upgrade the UI refused");
    CHECK(isl->stockpile.amount[RES_GOLD] == before,
          "and a refused upgrade costs nothing");

    /* ---- and now the accept path, on the same house ----
     * The Artisans' BASICS: Fish, Grain and Preserves. Their luxuries
     * are not the price of moving in (NEEDS_PLAN Phase 1). */
    stockpile_add(&isl->stockpile, RES_FISH, 2);
    stockpile_add(&isl->stockpile, RES_GRAIN, 2);
    stockpile_add(&isl->stockpile, RES_PRESERVES, 2);

    ui_snapshot_build(&snap, gs);
    CHECK(snapshot_upgrade_check(&snap.islands[snap.current_island], idx,
                                 TIER_BRANCH_LINE, &to_ui) == REJ_OK,
          "supply the tier's basics and the UI says it may climb");
    CHECK(to_ui == BUILDING_HOUSE_ARTISAN, "and names Artisans as where");

    gs->confirm.open = 1;
    gs->confirm.kind = CONFIRM_UPGRADE;
    gs->confirm.cmd.kind = CMD_UPGRADE_HOUSE;
    gs->confirm.cmd.a = gs->current_island;
    gs->confirm.cmd.b = idx;
    ui_snapshot_build(&snap, gs);
    confirm_view_build(&view, &snap);
    CHECK(view.refusal == REJ_OK, "the popup offers it rather than refusing");
    CHECK(view.options[0].affordable, "and the button can be pressed");

    before = isl->stockpile.amount[RES_GOLD];
    gs->confirm.open = 0;
    game_upgrade_house(gs, idx, TIER_BRANCH_LINE);
    while (gs->cmd_applied < gs->cmd_count) sim_run_one_tick(gs);

    CHECK(isl->buildings[idx].type == BUILDING_HOUSE_ARTISAN,
          "the sim promotes the house the UI said it would");
    CHECK(isl->stockpile.amount[RES_GOLD] < before,
          "and the upgrade is paid for");
    /* The needs are a standing requirement, not a price: they must be
     * PRESENT to climb, and they stay on the shelf afterwards to feed
     * the tier that now wants them every needs tick. */
    CHECK(isl->stockpile.amount[RES_PRESERVES] == 2,
          "the goods that qualified it are not consumed by the upgrade");

    /* island_has_building and its snapshot twin must not drift. */
    {
        int t, disagree = 0;
        ui_snapshot_build(&snap, gs);
        for (t = 0; t < BUILDING_TYPE_COUNT; t++)
            if (island_has_building(isl, (BuildingType)t) !=
                snapshot_has_building(&snap.islands[snap.current_island],
                                      (BuildingType)t))
                disagree++;
        CHECK(disagree == 0,
              "sim and snapshot agree about what is standing on the island");
    }

    game_free(gs);
}

int main(void)
{
    printf("=== tiers: the upgrade graph ===\n");

    test_limits();
    test_three_inputs();
    test_upgrade_rule();
    test_real_table();
    test_academy();
    test_academy_demolition();
    test_sim_and_ui_agree();

    if (failures) {
        printf("\n%d FAILED\n", failures);
        return 1;
    }
    printf("\nPASSED\n");
    return 0;
}
