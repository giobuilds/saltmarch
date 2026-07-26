/*  test_tier.c  --  the tier graph and the upgrade rule
 *                   (SUPPLY_CHAIN Phase 2)
 *
 * Phase 2 raises ceilings before the content hits them, and turns the
 * tier model from a ladder into a graph. Neither change has content
 * behind it yet — MAX_BUILDING_INPUTS is 3 and no building takes three
 * inputs; MAX_TIER_GOODS is 5 and no tier lists five; TierDef has a
 * requires_building field and no tier requires one. So the widening is
 * unproven unless something drives it with data of its own, which is
 * what these tests are for: they write their own defs and tiers and
 * push them through the real rules.
 *
 * The rule under test is the one a player meets as "why can't I
 * upgrade this house": tier_upgrade_check(). It is deliberately the
 * SAME function on both sides — sim_upgrade_house calls it to decide
 * and the confirm popup calls it to predict — so the last section
 * checks the two agree over a real world rather than trusting that
 * they share a name.
 *
 * Linked WITHOUT SDL, against libsaltmarch_ui and libsaltmarch_sim.
 */

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
    { RES_FISH, RES_COUNT, RES_COUNT, RES_COUNT, RES_COUNT },
    BUILDING_HOUSE_WORKER, 250, BUILDING_NONE
};
static const TierDef T_FIVE = {
    BUILDING_HOUSE_WORKER,
    { RES_WOOD, RES_FISH, RES_GRAIN, RES_HOPS, RES_BEER },
    BUILDING_NONE, 0, BUILDING_NONE
};
static const TierDef T_GATED = {
    BUILDING_HOUSE_WORKER,
    { RES_FISH, RES_COUNT, RES_COUNT, RES_COUNT, RES_COUNT },
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
     * ladder: a Wright's House is built, not upgraded into. Both base
     * tiers are therefore terminal until Artisans (Phase 4) and
     * Engineers (Phase 6) give them somewhere to go. */
    CHECK(tier_def_for(BUILDING_HOUSE) != NULL &&
          tier_def_for(BUILDING_HOUSE)->next_tier == BUILDING_NONE,
          "a Marsh Cottage has nowhere to climb yet — Artisans is Phase 4");
    CHECK(tier_def_for(BUILDING_HOUSE_WORKER) != NULL &&
          tier_def_for(BUILDING_HOUSE_WORKER)->next_tier == BUILDING_NONE,
          "nor a Wright's House — Engineers is Phase 6");
    CHECK(BUILDING_DEFS[BUILDING_HOUSE_WORKER].hud_placeable,
          "which is why a Wright's House is something you build");
    CHECK(tier_def_for(BUILDING_WAREHOUSE) == NULL,
          "a warehouse is not a tier");
    CHECK(tier_upgrade_requires(BUILDING_HOUSE) == BUILDING_NONE,
          "no tier needs a building yet — the Academy is Phase 8");

    /* The needs the plan gives them. Grain is conspicuously absent: it
     * is milled and baked now, which is what makes the Windmill and
     * the Bakehouse worth building. */
    {
        const TierDef *m = tier_def_for(BUILDING_HOUSE);
        const TierDef *w = tier_def_for(BUILDING_HOUSE_WORKER);
        int i, mn = 0, wn = 0, grain = 0;
        for (i = 0; i < MAX_TIER_GOODS; i++) {
            if (m->needs[i] != RES_COUNT) mn++;
            if (w->needs[i] != RES_COUNT) wn++;
            if (m->needs[i] == RES_GRAIN || w->needs[i] == RES_GRAIN) grain++;
        }
        CHECK(mn == 3 && wn == 4, "Marshfolk want three things, Wrights four");
        CHECK(grain == 0, "and nobody eats raw Grain any more");
        CHECK(m->needs[1] == RES_OILSKINS && m->needs[2] == RES_MARSH_GIN,
              "Marshfolk want Fish, Oilskins and Marsh Gin");
        CHECK(w->needs[0] == RES_SAUSAGES && w->needs[1] == RES_BREAD &&
              w->needs[2] == RES_SOAP && w->needs[3] == RES_BEER,
              "Wrights want Sausages, Bread, Soap and Beer");
    }

    for (r = 0; r < RES_COUNT; r++) stock[r] = 0;
    stock[RES_GOLD] = 10000;
    CHECK(tier_upgrade_check(BUILDING_HOUSE, stock, 1, &to) == REJ_UNAVAILABLE,
          "so however rich the island, there is nothing to promote into");
    CHECK(to == BUILDING_NONE, "and no destination is named");
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

    /* Phase 3 leaves both base tiers terminal, so the only verdict
     * either side can reach on the SHIPPED table is "nowhere to go".
     * That is still worth asserting jointly: it is the wiring — sim
     * reading Island, UI reading a snapshot — rather than the rule,
     * which section 3 proves at five needs and behind a building. The
     * accept path comes back under test in Phase 4 with Artisans. */
    isl->stockpile.amount[RES_GOLD] = 5000;
    stockpile_add(&isl->stockpile, RES_FISH, 3);
    stockpile_add(&isl->stockpile, RES_OILSKINS, 3);
    stockpile_add(&isl->stockpile, RES_MARSH_GIN, 3);
    ui_snapshot_build(&snap, gs);
    CHECK(snapshot_upgrade_check(&snap.islands[snap.current_island], idx,
                                 &to_ui) == REJ_UNAVAILABLE,
          "the UI reports a fully supplied cottage as having nowhere to go");
    CHECK(to_ui == BUILDING_NONE, "and names no destination");

    /* The popup says so too, rather than offering a button. */
    gs->confirm.open = 1;
    gs->confirm.kind = CONFIRM_UPGRADE;
    gs->confirm.cmd.kind = CMD_UPGRADE_HOUSE;
    gs->confirm.cmd.a = gs->current_island;
    gs->confirm.cmd.b = idx;
    ui_snapshot_build(&snap, gs);
    confirm_view_build(&view, &snap);
    CHECK(view.refusal == REJ_UNAVAILABLE, "the popup records the refusal");
    CHECK(!view.options[0].affordable,
          "and cannot be accepted, however much Gold is in the store");

    /* The sim refuses identically, and changes nothing when it does. */
    before = isl->stockpile.amount[RES_GOLD];
    gs->confirm.open = 0;
    game_upgrade_house(gs, idx);
    while (gs->cmd_applied < gs->cmd_count) sim_run_one_tick(gs);

    CHECK(isl->buildings[idx].type == BUILDING_HOUSE,
          "the sim refuses the same upgrade the UI refused");
    CHECK(isl->stockpile.amount[RES_GOLD] == before,
          "and a refused upgrade costs nothing");

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
    test_sim_and_ui_agree();

    if (failures) {
        printf("\n%d FAILED\n", failures);
        return 1;
    }
    printf("\nPASSED\n");
    return 0;
}
