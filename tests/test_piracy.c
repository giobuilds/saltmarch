/*  test_piracy.c  --  loss mechanics and marine insurance
 *                     (MMO_PLAN later phases)
 *
 * MMO_PLAN is emphatic about the order: piracy first, insurance second,
 * because insuring against a loss that cannot happen is theatre. This
 * covers both, and the property that makes them safe to have at all —
 * a raid is DERIVED from the voyage's identity rather than rolled, so
 * every client, replay and server computes the same one without the
 * feed carrying a word about it.
 */

#include "game.h"
#include "sea.h"
#include "ship.h"
#include "faction.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }    \
        else         { printf("  ok:   %s\n", (msg)); }                \
    } while (0)

/* ---- 1. the raid is a function, not a roll ---------------- */
static void test_deterministic(void)
{
    int a = voyage_is_raided(1234u, 0, 100, 0, 1);
    int b = voyage_is_raided(1234u, 0, 100, 0, 1);
    int raided = 0, i;

    CHECK(a == b, "the same voyage is always the same voyage");

    /* Different voyages differ, or the whole mechanic is a constant. */
    for (i = 0; i < 200; i++)
        if (voyage_is_raided(1234u, 0, (uint64_t)i, 0, 1)) raided++;
    CHECK(raided > 0 && raided < 200,
          "some voyages are raided and some are not");

    /* Roughly the advertised rate — this is the tuning knob, and a
     * test that pins the exact count would break on every re-tune. */
    CHECK(raided < 200 * PIRACY_CHANCE_PER_MILLE / 1000 * 3 + 5,
          "the raid rate is in the neighbourhood of what it claims");

    /* The world seed matters: two worlds do not share a pirate. */
    {
        int differs = 0;
        for (i = 0; i < 100; i++)
            if (voyage_is_raided(1u, 0, (uint64_t)i, 0, 1) !=
                voyage_is_raided(2u, 0, (uint64_t)i, 0, 1)) differs = 1;
        CHECK(differs, "a different world rolls different voyages");
    }
}

/* Find a departure tick whose voyage from 0 to 1 is (or is not) raided,
 * so the tests below can arrange the outcome they want to check. */
static uint64_t find_tick(uint32_t seed, int ship, int want_raid)
{
    uint64_t t;
    for (t = 0; t < 5000; t++)
        if (voyage_is_raided(seed, ship, t, 0, 1) == want_raid) return t;
    return 0;
}

/* ---- 2. a raid actually takes cargo ---------------------- */
static void test_raid_takes_cargo(void)
{
    Ship   ships[1];
    Island islands[MAX_ISLANDS];
    Sea    sea;
    uint64_t dep;
    uint32_t crossing;
    int    i;

    memset(ships, 0, sizeof(ships));
    memset(islands, 0, sizeof(islands));
    /* A crossing is now the route's length (MARITIME_PLAN Phase 1), so
     * this drives the updater over the real duration rather than the
     * constant it used to be. */
    sea_init(&sea, 99u, MAX_ISLANDS);
    crossing = sea_crossing_ticks(&sea, 0, 1);

    dep = find_tick(99u, 0, 1);

    ships[0].active         = 1;
    ships[0].at_island      = -1;
    ships[0].from_island    = 0;
    ships[0].to_island      = 1;
    ships[0].departure_tick = dep;
    ships[0].cargo[RES_WOOD] = 20;

    for (i = 0; i <= (int)crossing; i++)
        ships_update(&sea, ships, 1, islands, MAX_ISLANDS,
                     dep + (uint64_t)i, 99u);

    CHECK(ships[0].cargo[RES_WOOD] == 10,
          "pirates take half the hold, once");
    CHECK(ships[0].at_island == 1, "the ship still arrives");
}

static void test_safe_voyage_keeps_cargo(void)
{
    Ship   ships[1];
    Island islands[MAX_ISLANDS];
    Sea    sea;
    uint64_t dep;
    uint32_t crossing;
    int    i;

    memset(ships, 0, sizeof(ships));
    memset(islands, 0, sizeof(islands));
    /* A crossing is now the route's length (MARITIME_PLAN Phase 1), so
     * this drives the updater over the real duration rather than the
     * constant it used to be. */
    sea_init(&sea, 99u, MAX_ISLANDS);
    crossing = sea_crossing_ticks(&sea, 0, 1);

    dep = find_tick(99u, 0, 0);

    ships[0].active          = 1;
    ships[0].at_island       = -1;
    ships[0].from_island     = 0;
    ships[0].to_island       = 1;
    ships[0].departure_tick  = dep;
    ships[0].cargo[RES_WOOD] = 20;

    for (i = 0; i <= (int)crossing; i++)
        ships_update(&sea, ships, 1, islands, MAX_ISLANDS,
                     dep + (uint64_t)i, 99u);

    CHECK(ships[0].cargo[RES_WOOD] == 20,
          "an unraided voyage arrives with everything it left with");
}

/* ---- 3. insurance ---------------------------------------- */
static void test_insurance(void)
{
    GameState *gs = game_init();
    int        quote, gold_before, i;

    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 4242u);

    game_build_ship(gs);
    sim_run_one_tick(gs);
    game_ship_transfer(gs, 0, RES_GOLD, 200);
    sim_run_one_tick(gs);

    quote = game_insurance_quote(gs, 0, 1);
    CHECK(quote > 0, "a laden ship can be quoted a premium");

    gold_before = gs->islands[0].stockpile.amount[RES_GOLD];
    game_ship_depart_insured(gs, 0, 1);
    sim_run_one_tick(gs);

    CHECK(gs->islands[0].stockpile.amount[RES_GOLD] == gold_before - quote,
          "the premium is paid up front, from the port it sails from");
    CHECK(gs->ships[0].insured && gs->ships[0].insured_value > 0,
          "the policy records the value declared at departure");

    for (i = 0; i < (int)sea_crossing_ticks(&gs->sea,
                                            gs->ships[0].from_island,
                                            gs->ships[0].to_island) + 2; i++)
        sim_run_one_tick(gs);
    CHECK(!gs->ships[0].insured,
          "the policy is settled and closed on arrival");

    game_free(gs);
}

/* ---- 4. the premium is the information layer -------------- */
static void test_route_premium_moves(void)
{
    Faction f;
    Sea     sea;
    int     start, after_losses, after_calm, i;

    sea_init(&sea, 4242u, MAX_ISLANDS);
    faction_init(&f);
    faction_init_routes(&f, &sea);

    start = faction_route_premium(&f, 0);
    CHECK(start == INSURANCE_PREMIUM_START,
          "the patrolled lane starts at the base rate");

    /* The point of pricing per route rather than per island pair
     * (MARITIME_PLAN Phase 3c): the same water between the same two
     * islands is not one risk. A private passage is fast because it
     * runs outside patrolled water, and the underwriter knows it. */
    {
        int priv = -1, v;
        for (v = 0; v < SEA_ROUTES_PER_PAIR; v++) {
            const Route *r = sea_route_variant(&sea, 0, 1, v);
            if (r && r->is_private) { priv = sea_route_id(&sea, r); break; }
        }
        CHECK(priv >= 0, "the pair has a private passage");
        CHECK(faction_route_premium(&f, priv) > start,
              "which costs more to insure than the lane beside it");
    }

    for (i = 0; i < 20; i++) faction_route_experience(&f, 0, 1);
    after_losses = faction_route_premium(&f, 0);
    CHECK(after_losses > start, "a route that loses cargo gets expensive");

    for (i = 0; i < 60; i++) faction_route_experience(&f, 0, 0);
    after_calm = faction_route_premium(&f, 0);
    CHECK(after_calm < after_losses,
          "and grows cheap again when the losses stop");

    CHECK(faction_route_premium(&f, 3) == INSURANCE_PREMIUM_START ||
          faction_route_premium(&f, 3) == INSURANCE_PREMIUM_PRIVATE,
          "one route's troubles do not price another's");

    /* Bounded either way: a route can be dangerous without becoming
     * uninsurable, and safe without becoming free. */
    for (i = 0; i < 500; i++) faction_route_experience(&f, 0, 1);
    CHECK(faction_route_premium(&f, 0) <= INSURANCE_PREMIUM_MAX,
          "the premium is capped");
    for (i = 0; i < 500; i++) faction_route_experience(&f, 0, 0);
    CHECK(faction_route_premium(&f, 0) >= INSURANCE_PREMIUM_MIN,
          "and floored");
}

/* ---- 5. all of it replays -------------------------------- */
static void test_replays(void)
{
    GameState *gs = game_init();
    int        i;

    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 31337u);

    game_build_ship(gs);
    sim_run_one_tick(gs);
    game_ship_transfer(gs, 0, RES_GOLD, 150);
    sim_run_one_tick(gs);
    game_ship_depart_insured(gs, 0, 1);
    for (i = 0; i < SHIP_VOYAGE_TICKS + 20; i++) sim_run_one_tick(gs);

    CHECK(game_verify_determinism(gs),
          "raids, premiums and payouts all replay from the log alone");

    game_free(gs);
}

int main(void)
{
    printf("== piracy and marine insurance ==\n");
    test_deterministic();
    test_raid_takes_cargo();
    test_safe_voyage_keeps_cargo();
    test_insurance();
    test_route_premium_moves();
    test_replays();

    if (failures == 0) { printf("\nPASSED\n"); return 0; }
    printf("\nFAILED (%d)\n", failures);
    return 1;
}
