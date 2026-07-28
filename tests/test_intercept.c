/*  test_intercept.c  --  tide-time PvP (MMO_PLAN later phases)
 *
 * An intercept is a Command naming a voyage. The engagement is computed
 * from the ordered log plus a seeded hash, so both players' clients and
 * the server reach the same outcome without exchanging a shot — the
 * feed never becomes a real-time arbiter, which is the property the
 * whole architecture is protecting.
 *
 * The interesting cases are the refusals: an intercept must not be able
 * to land on a voyage the attacker never saw.
 */

#include "game.h"
#include "pirate.h"
#include "knowledge.h"
#include "ship.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }    \
        else         { printf("  ok:   %s\n", (msg)); }                \
    } while (0)

/* Two ships at sea, owned by different players. Written directly: this
 * is about the engagement, not about how ships come to be — so the
 * world is marked unreplayable and the determinism check below builds
 * its own through the funnel. */
static void two_at_sea(GameState *gs, int cargo_a, int cargo_b)
{
    gs->ship_count = 2;
    memset(gs->ships, 0, sizeof(gs->ships[0]) * 2);

    gs->ships[0].active         = 1;
    gs->ships[0].owner          = 1u;
    gs->ships[0].at_island      = -1;
    gs->ships[0].from_island    = 0;
    gs->ships[0].to_island      = 1;
    gs->ships[0].departure_tick = 10;
    gs->ships[0].cargo[RES_WOOD] = cargo_a;

    gs->ships[1].active         = 1;
    gs->ships[1].owner          = 2u;
    gs->ships[1].at_island      = -1;
    gs->ships[1].from_island    = 1;
    gs->ships[1].to_island      = 0;
    gs->ships[1].departure_tick = 12;
    gs->ships[1].cargo[RES_FISH] = cargo_b;

    gs->replay_valid = 0;
}

static void test_pirates_can_be_hunted(void)
{
    /* The point of Phase 5b. Piracy used to be a hash: cargo vanished
     * and there was nothing to go after. Now the fleet is somewhere,
     * it is sitting on what it took, and a hull with guns can go and
     * get it back — which is also the only reason to arm that is not
     * aimed at another player. */
    GameState *gs = game_init();
    Ship      *sh;
    Pirate    *pr;
    int        i;

    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 7u);
    gs->islands[0].stockpile.amount[RES_GOLD] = 100000;

    CHECK(gs->pirates.count > 0, "the world has fleets in it");

    pr = &gs->pirates.fleet[0];
    pr->plunder[RES_FISH] = 30;
    pr->chart             = 4;      /* a passage they took off somebody */

    game_build_ship_class(gs, SHIP_WARSHIP);
    sim_run_one_tick(gs);
    sh = &gs->ships[0];

    /* You have to be there. Attacking from harbour is a menu item, not
     * a place. */
    {
        Command c;
        memset(&c, 0, sizeof(c));
        c.kind      = CMD_ATTACK_PIRATE;
        c.a         = 0;
        c.b         = 0;
        c.player_id = 1u;
        CHECK(sim_apply_reason(gs, &c) == REJ_NO_TARGET ||
              sim_apply_reason(gs, &c) == REJ_UNAVAILABLE,
              "a fleet cannot be attacked from your own harbour");
    }

    /* Put the warship exactly on the lair. A ship's position is
     * derived from its voyage and cannot be assigned, so the way to
     * place one somewhere is to pick a route THROUGH that somewhere
     * and wind its departure back by the leg that reaches it. The
     * public lane always threads a waypoint, which is what makes this
     * possible at all rather than a search.
     *
     * The earlier version of this test looked for a lair near the
     * harbour and printed "nothing to test here" when it found none —
     * which it always did. Every assertion below it was skipped, and
     * the test reported success. */
    {
        const Route *r = sea_route_between(&gs->sea, 0, 1);

        if (!r || r->waypoint_count < 1) {
            printf("  FAIL: the lane threads no waypoint to meet at\n");
            failures++;
            game_free(gs);
            return;
        }
        pr->waypoint = r->waypoint[0];

        sh->at_island      = -1;
        sh->from_island    = 0;
        sh->to_island      = 1;
        sh->departure_tick = gs->sim_tick_no - r->leg_ticks[0];

        {
            SeaPos me = sea_route_point(&gs->sea, r, r->leg_ticks[0]);
            CHECK(sea_distance(me, pirate_pos(&gs->pirates, &gs->sea, 0)) <=
                  (uint32_t)PIRATE_STRIKE_RADIUS,
                  "the hunter is on top of the lair");
        }
    }

    /* A warship against a fleet wins most of the time; give it as many
     * attempts as it needs, which is what a hunt is. */
    for (i = 0; i < 40 && pr->active; i++) {
        Command c;
        memset(&c, 0, sizeof(c));
        c.kind      = CMD_ATTACK_PIRATE;
        c.a         = 0;
        c.b         = 0;
        c.player_id = 1u;
        (void)sim_apply(gs, &c);
        sh->departure_tick++;      /* a fresh engagement, not the same one */
    }

    CHECK(!pr->active, "a warship clears the lair");
    CHECK(sh->cargo[RES_FISH] > 0,
          "and comes home with what the fleet was sitting on — which was "
          "somebody else's, so clearing it is a service to the lane");
    CHECK(knowledge_charts(&gs->knowledge, 1u, 4) > 0,
          "and with the chart they carried, so hunting yields geography "
          "as well as goods");

    game_free(gs);
}

static void test_a_convoy_defends_itself(void)
{
    /* The reason a warship exists. A merchantman carries the cargo and
     * cannot protect it; a warship protects and carries nothing. The
     * answer is not a compromise hull, it is a second ship — which is
     * a decision about a fleet rather than about a ship. */
    GameState *gs = game_init();
    Command    c;
    int        i;

    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 7u);
    gs->islands[0].stockpile.amount[RES_GOLD] = 100000;

    game_build_ship_class(gs, SHIP_MERCHANTMAN);   /* 0: the cargo   */
    game_build_ship_class(gs, SHIP_WARSHIP);       /* 1: the guard   */
    sim_run_one_tick(gs);
    sim_run_one_tick(gs);

    CHECK(gs->ship_count >= 2, "the yard laid down two different hulls");
    CHECK(gs->ships[0].guns == 0 && gs->ships[1].guns > 0,
          "and they are not the same kind of thing");
    CHECK(ship_hold_capacity(&gs->ships[0]) >
          ship_hold_capacity(&gs->ships[1]),
          "guns cost hold, which is what makes the choice a choice");

    /* Form the convoy and sail. */
    game_set_escort(gs, 1, 0);
    sim_run_one_tick(gs);
    CHECK(gs->ships[1].escorting == 0, "the warship is assigned to guard");

    game_ship_depart(gs, 0, 1);
    sim_run_one_tick(gs);
    CHECK(gs->ships[1].at_island < 0 &&
          gs->ships[1].departure_tick == gs->ships[0].departure_tick &&
          gs->ships[1].to_island == gs->ships[0].to_island,
          "and sails WITH its charge — an escort ordered out separately "
          "would arrive on a different tick and defend nobody");

    /* Now a raider attacks the merchantman. The escort's guns are in
     * the defence even though the raider never named it. */
    {
        int j = gs->ship_count++;
        memset(&gs->ships[j], 0, sizeof(Ship));
        gs->ships[j].active         = 1;
        gs->ships[j].owner          = 9u;
        gs->ships[j].at_island      = -1;
        gs->ships[j].from_island    = 2;
        gs->ships[j].to_island      = 1;
        gs->ships[j].klass          = SHIP_CUTTER;
        gs->ships[j].guns           = SHIP_CLASSES[SHIP_CUTTER].guns;
        gs->ships[j].hull           = SHIP_CLASSES[SHIP_CUTTER].hull;
        gs->ships[j].escorting      = -1;
        gs->ships[j].departure_tick = gs->sim_tick_no;

        CHECK(intercept_odds(gs->ships[j].guns, gs->ships[0].guns) >
              intercept_odds(gs->ships[j].guns,
                             gs->ships[0].guns + gs->ships[1].guns),
              "a raider's odds against the convoy are worse than against "
              "the merchantman alone — which is the whole point of it");

        memset(&c, 0, sizeof(c));
        c.kind      = CMD_INTERCEPT;
        c.a         = j;
        c.b         = 0;
        c.c         = (int32_t)gs->ships[0].departure_tick;
        c.player_id = 9u;
        (void)sim_apply(gs, &c);
    }

    for (i = 0; i < 2; i++) sim_run_one_tick(gs);
    game_free(gs);
}

static void test_wear_and_refit(void)
{
    /* Losing costs more than the hold, but never the ship. Damage is a
     * reason to go home, not the end of an evening. */
    GameState *gs = game_init();
    int        b, full;

    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 7u);
    gs->islands[0].stockpile.amount[RES_GOLD] = 100000;

    game_build_ship_class(gs, SHIP_WARSHIP);
    sim_run_one_tick(gs);
    full = SHIP_CLASSES[SHIP_WARSHIP].hull;
    CHECK(gs->ships[0].hull == full, "a new hull is whole");

    gs->ships[0].hull = 1;
    CHECK(ship_fighting_strength(&gs->ships[0]) <
          SHIP_CLASSES[SHIP_WARSHIP].guns,
          "a worn hull fights worse than a fresh one");

    /* No yard, no refit. */
    for (b = 0; b < SHIP_REFIT_TICKS_PER_HULL * 3; b++) sim_run_one_tick(gs);
    CHECK(gs->ships[0].hull == 1,
          "an island with no Shipyard cannot put it right");

    {
        Island *isl = &gs->islands[0];
        int     i   = isl->building_count++;
        isl->buildings[i].active = 1;
        isl->buildings[i].type   = BUILDING_SHIPYARD;
    }
    for (b = 0; b < SHIP_REFIT_TICKS_PER_HULL * 3; b++) sim_run_one_tick(gs);
    CHECK(gs->ships[0].hull > 1, "a yard refits it");
    CHECK(gs->ships[0].hull <= full, "and never past what it was");

    game_free(gs);
}

static void test_guns_decide_it(void)
{
    /* The whole point of Phase 5: an engagement stopped being a coin
     * flip and became a consequence of what the ships are. */
    CHECK(intercept_odds(0, 0) == INTERCEPT_ATTACKER_ODDS,
          "two unarmed hulls is a scuffle, and the attacker chose the "
          "moment");
    CHECK(intercept_odds(8, 3) > intercept_odds(3, 3),
          "more guns is better odds");
    CHECK(intercept_odds(8, 0) == intercept_odds(3, 0),
          "though against an unarmed hull any guns at all are already "
          "as good as it gets");
    CHECK(intercept_odds(8, 0) > intercept_odds(8, 3),
          "and a defender with guns is worse odds for the attacker");
    CHECK(intercept_odds(3, 3) > 40 && intercept_odds(3, 3) < 60,
          "evenly matched is near enough a coin flip");

    /* Nothing at sea is ever certain. A convoy that could not be taken
     * would make escorting a solved problem rather than a judgement. */
    CHECK(intercept_odds(0, 100) >= INTERCEPT_MIN_ODDS,
          "even a hopeless attack sometimes lands");
    CHECK(intercept_odds(100, 0) <= INTERCEPT_MAX_ODDS,
          "and even a hopeless defence sometimes holds");
}

static void test_the_roll_is_not_lumpy(void)
{
    /* Whether the roll favours some ships over others.
     *
     * THE SAMPLE SIZE IS THE TEST. At 200 draws a Bernoulli(0.55)
     * count has a standard deviation of 7, so anything from about 96
     * to 124 is ordinary noise and a test with that band cannot tell a
     * biased generator from a fair one — it just passes. (The first
     * version of this test did exactly that, and I briefly believed it
     * had found a defect in the old hash when what it had found was
     * sampling variation.) At 4000 draws sigma is 31, and a systematic
     * few percent has somewhere to show up.
     *
     * This matters here because survey.c DID have a degenerate mix —
     * one route at 0% where 14% was expected, which is five sigma and
     * not luck. The lesson was not "hash bad", it was "measure with
     * enough samples to tell". */
    int a, worst = 1000000, best = 0;

    for (a = 0; a < MAX_SHIPS; a++) {
        int t, wins = 0;
        for (t = 0; t < 4000; t++)
            if (intercept_attacker_wins(4242u, a, (uint64_t)t,
                                        (a + 1) % MAX_SHIPS, (uint64_t)t,
                                        0, 0)) wins++;
        if (wins < worst) worst = wins;
        if (wins > best)  best  = wins;
    }
    /* 55% of 4000 is 2200; +-4 sigma is +-125. */
    CHECK(worst >= 2075 && best <= 2325,
          "no ship is luckier than any other at the same odds");
}

static void test_outcome_is_a_function(void)
{
    int a = intercept_attacker_wins(7u, 0, 10, 1, 12, 0, 0);
    int b = intercept_attacker_wins(7u, 0, 10, 1, 12, 0, 0);
    int i, wins = 0;

    CHECK(a == b, "the same engagement always resolves the same way");

    for (i = 0; i < 200; i++)
        if (intercept_attacker_wins(7u, 0, (uint64_t)i, 1, 12, 0, 0)) wins++;
    CHECK(wins > 0 && wins < 200, "both sides win sometimes");
}

static void test_cargo_changes_hands(void)
{
    GameState *gs = game_init();
    int        won;

    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 7u);
    two_at_sea(gs, 10, 20);

    won = intercept_attacker_wins(gs->world_seed, 0, 10, 1, 12, 0, 0);

    {
        Command c;
        memset(&c, 0, sizeof(c));
        c.kind      = CMD_INTERCEPT;
        c.a         = 0;
        c.b         = 1;
        c.c         = 12;
        c.player_id = 1u;
        CHECK(sim_apply_reason(gs, &c) == REJ_OK, "the intercept applies");
    }

    if (won) {
        CHECK(gs->ships[0].cargo[RES_FISH] == 20 &&
              gs->ships[1].cargo[RES_FISH] == 0,
              "the attacker takes the defender's hold");
    } else {
        CHECK(gs->ships[1].cargo[RES_WOOD] == 10 &&
              gs->ships[0].cargo[RES_WOOD] == 0,
              "a failed attack costs the attacker its own hold");
    }

    CHECK(gs->ships[0].active && gs->ships[1].active,
          "no ship is ever sunk — a hold is a setback, a ship is an evening");

    game_free(gs);
}

static void test_refusals(void)
{
    GameState *gs = game_init();
    Command    c;

    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 8u);
    two_at_sea(gs, 10, 20);

    /* The wrong departure tick: the attacker is naming a voyage that
     * has been and gone. This is the check that stops a stale click
     * hitting whatever voyage happens to be there now. */
    memset(&c, 0, sizeof(c));
    c.kind = CMD_INTERCEPT; c.a = 0; c.b = 1; c.c = 999; c.player_id = 1u;
    CHECK(sim_apply_reason(gs, &c) == REJ_NO_TARGET,
          "a stale voyage reference is refused, not redirected");

    /* Your own ship is not a target. */
    gs->ships[1].owner = 1u;
    memset(&c, 0, sizeof(c));
    c.kind = CMD_INTERCEPT; c.a = 0; c.b = 1; c.c = 12; c.player_id = 1u;
    CHECK(sim_apply_reason(gs, &c) == REJ_NO_TARGET,
          "you cannot intercept your own convoy");
    gs->ships[1].owner = 2u;

    /* A docked ship is in port, not at sea. */
    gs->ships[1].at_island = 1;
    memset(&c, 0, sizeof(c));
    c.kind = CMD_INTERCEPT; c.a = 0; c.b = 1; c.c = 12; c.player_id = 1u;
    CHECK(sim_apply_reason(gs, &c) == REJ_NO_TARGET,
          "a ship that has made port cannot be intercepted at sea");
    gs->ships[1].at_island = -1;

    /* Someone else's ship cannot be used to attack. */
    memset(&c, 0, sizeof(c));
    c.kind = CMD_INTERCEPT; c.a = 0; c.b = 1; c.c = 12; c.player_id = 5u;
    CHECK(sim_apply_reason(gs, &c) == REJ_NOT_OWNER,
          "attacking with somebody else's ship is refused as ownership");

    game_free(gs);
}

static void test_replays(void)
{
    GameState *gs = game_init();
    int        i;

    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 909u);

    /* Two ships, both ours, built and sailed through the funnel — the
     * intercept will be refused (own convoy), and that refusal must
     * replay identically too. A rejected command is part of the log's
     * meaning, not an absence from it. */
    game_build_ship(gs);
    sim_run_one_tick(gs);
    game_ship_depart(gs, 0, 1);
    sim_run_one_tick(gs);
    game_intercept(gs, 0, 0, gs->ships[0].departure_tick);
    for (i = 0; i < 40; i++) sim_run_one_tick(gs);

    CHECK(game_verify_determinism(gs),
          "an engagement — refused or not — replays from the log alone");

    game_free(gs);
}

int main(void)
{
    printf("== interception ==\n");
    test_pirates_can_be_hunted();
    test_a_convoy_defends_itself();
    test_wear_and_refit();
    test_guns_decide_it();
    test_the_roll_is_not_lumpy();
    test_outcome_is_a_function();
    test_cargo_changes_hands();
    test_refusals();
    test_replays();

    if (failures == 0) { printf("\nPASSED\n"); return 0; }
    printf("\nFAILED (%d)\n", failures);
    return 1;
}
