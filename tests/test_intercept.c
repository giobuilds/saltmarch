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

static void test_outcome_is_a_function(void)
{
    int a = intercept_attacker_wins(7u, 0, 10, 1, 12);
    int b = intercept_attacker_wins(7u, 0, 10, 1, 12);
    int i, wins = 0;

    CHECK(a == b, "the same engagement always resolves the same way");

    for (i = 0; i < 200; i++)
        if (intercept_attacker_wins(7u, 0, (uint64_t)i, 1, 12)) wins++;
    CHECK(wins > 0 && wins < 200, "both sides win sometimes");
}

static void test_cargo_changes_hands(void)
{
    GameState *gs = game_init();
    int        won;

    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 7u);
    two_at_sea(gs, 10, 20);

    won = intercept_attacker_wins(gs->world_seed, 0, 10, 1, 12);

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
    test_outcome_is_a_function();
    test_cargo_changes_hands();
    test_refusals();
    test_replays();

    if (failures == 0) { printf("\nPASSED\n"); return 0; }
    printf("\nFAILED (%d)\n", failures);
    return 1;
}
