#ifndef ISLAND_H
#define ISLAND_H

/* =========================================================
 * island.h  --  One island: its map, economy and population
 *
 * Everything that used to be world-scoped in GameState and is
 * actually *per-landmass* lives here. GameState keeps only what is
 * genuinely global: input, timing, the current view, and the UI
 * overlay flags.
 *
 * The split is what makes ships and trade routes mean anything: each
 * island has its OWN Stockpile, so goods produced on one island are
 * not available on another until something physically carries them.
 *
 * Note `settled`: an island exists (and is generated, and can be
 * looked at) long before the player can build on it. Only a settled
 * island is simulated by island_update() or accepts placement.
 * ========================================================= */

#include "map.h"
#include "camera.h"
#include "resource.h"
#include "building.h"
#include "population.h"
#include "agent.h"
#include "resident.h"
#include "simclock.h"
#include "faction.h"   /* faction_bid: what a good is worth (Phase 7) */

/* Job assignment runs every AGENT_ASSIGN_INTERVAL seconds, expressed in
 * whole sim ticks (see agent.h and simclock.h). */
#define AGENT_ASSIGN_INTERVAL_TICKS \
    ((int)(AGENT_ASSIGN_INTERVAL * SIM_TICKS_PER_SEC))

/* Four is plenty for the intended archipelago and keeps GameState at
 * roughly its historical size — see MAX_AGENTS in agent.h, which was
 * reduced specifically so this multiplication stays affordable. */
/* SUPPLY_CHAIN Phase 5 doubled this for the southern climates. It is a
 * wider change than it looks: SAVE_VERSION (a v11 log describes a
 * four-island world), NET_PROTO_VERSION (a four-island client and an
 * eight-island server disagree about what the world IS), faction.h's
 * lane table, world_ui.c's map positions, and every per-island array
 * the snapshot writes. */
#define MAX_ISLANDS 8

#define ISLAND_NAME_LEN 16

/* ---- marine insurance (MMO_PLAN later phases) --------------
 * A voyage can be insured at departure: a premium paid to the faction
 * now, a payout from the faction if pirates take the cargo. The
 * premium is per LANE and moves with experience — every insured
 * voyage that arrives safely nudges it down, every one that is raided
 * nudges it up, as an exponential moving average.
 *
 * That EMA is the point. It turns the faction's books into the game's
 * information layer: a lane whose premium has crept up is a lane that
 * has been losing ships, and that is knowledge a player can act on
 * before losing one of their own. Insurance is a price signal wearing
 * a mechanic's clothes.
 *
 * Premiums are stored in tenths of a percent of cargo value so the
 * whole thing stays integer, and are clamped to a sane band.
 */
#define INSURANCE_PREMIUM_START   80    /* 8.0% of declared value      */
/* A private passage starts dearer to insure, because it is dearer to
 * sail: it is fast precisely because it runs outside patrolled water
 * (MARITIME_PLAN Phase 3c). Experience moves both from here. */
#define INSURANCE_PREMIUM_PRIVATE 220   /* 22.0%                       */
#define INSURANCE_PREMIUM_MIN     20
#define INSURANCE_PREMIUM_MAX    400
#define INSURANCE_EMA_SHIFT        3    /* how fast experience moves it */
#define INSURANCE_MIN_PREMIUM_GOLD 5

/* ---- charter terms ----------------------------------------
 * Deliberately gentle: a working colony pays for itself many times
 * over, so upkeep is a reason to keep an island productive rather than
 * a countdown. An island left completely idle takes about ten minutes
 * of world time to lapse. */
#define CHARTER_BID_GOLD        150   /* paid to the faction to claim  */
#define CHARTER_UPKEEP_GOLD      25   /* per payment                   */
#define CHARTER_UPKEEP_TICKS   1200   /* two minutes of world time     */
#define CHARTER_GRACE_PAYMENTS    3   /* missed payments before lapse  */

/* Houses an island may found by IMMIGRATION before it has to grow its
 * own people (LIFE_PLAN Phase 6c, EXPERIMENT). A hundred is deliberately
 * more than a player will lay early and far fewer than a mature island
 * wants, so the allowance is invisible for the first hour and then
 * becomes the thing the whole settlement runs on. */
#define FOUNDER_ALLOWANCE 100

typedef struct {
    Map        map;
    Camera     camera;          /* per-island, so returning to an island
                                 * restores the view you left it at    */
    Stockpile  stockpile;       /* per-island: goods do NOT teleport   */

    Building   buildings[MAX_BUILDINGS];
    int        building_count;
    PopData    pop_data[MAX_BUILDINGS];   /* parallel to buildings[]   */

    Agent      agents[MAX_AGENTS];
    int        agent_count;
    int        agent_assign_timer;   /* sim ticks since last assign pass */

    /* ---- who those agents actually are (LIFE_PLAN Phase 3) ----
     * Agents are motion: derived, never saved, rebuilt every session.
     * Residents are IDENTITY: hashed, saved, snapshotted. next_resident_id
     * is world state because it decides what everybody is called — names
     * are a pure function of (world_seed, id) rather than stored bytes. */
    Resident   residents[MAX_RESIDENTS];
    int        resident_count;
    uint32_t   next_resident_id;
    /* How many more households may be founded by IMMIGRATION (LIFE_PLAN
     * Phase 6c, EXPERIMENT). Every house laid while this is positive
     * arrives with a couple off a boat and spends one; after that a
     * house is filled from the reserve or stands empty until the
     * reserve can fill it. World state: hashed, saved, snapshotted. */
    int        founder_allowance;

    /* Where somebody goes when this island cannot roof them.
     *
     * A FUNCTION POINTER THE WORLD INSTALLS, not a direct call, for the
     * same reason GameState.net_submit is one: an island cannot see
     * another island, and island.c must not learn what a world is.
     * game.c sets these so a departing resident is offered the player's
     * OTHER islands first and another player's second; left unset — as
     * in every unit test — nobody is relocated and they simply leave.
     *
     * Derived wiring, not world state: never hashed, never saved. */
    int      (*emigrate)(void *ctx, int resident_idx);
    void      *emigrate_ctx;

    /* How many left last month. Read by the vitals strip so "people are
     * leaving" is a fact rather than an inference. Derived. */
    int        left_last_month;

    /* ---- the treasury (LIFE_PLAN Phase 7) -----------------
     * What the player takes from wages and from business profit, in
     * per mille, set through CMD_SET_TAX_RATE and clamped to
     * TAX_RATE_MAX_PERMILLE.
     *
     * `compliance_permille` is how much of it is actually paid.
     * Sustained unhappiness erodes it and contentment restores it, with
     * a floor and a patience period so the loop
     * docs/new-happiness-design.md warns about cannot run away. Both are
     * world state: hashed, saved, snapshotted. */
    int32_t    tax_rate_permille;
    int32_t    compliance_permille;
    /* Consecutive needs ticks the island has been unhappy. The
     * hysteresis: compliance does not move until this passes
     * COMPLIANCE_PATIENCE_TICKS, so one bad quarter costs nothing. */
    int32_t    unhappy_streak;
    /* Wages plus profit earned since the last levy. World state: it is
     * carried across a save and it decides what the next levy is worth.
     *
     * TAX IS LEVIED MONTHLY, NOT PER TICK, and the reason is integer
     * arithmetic rather than flavour. A single production cycle is a
     * few coins; a few coins times a tenth, in integers, is zero. An
     * island of ten Fisher's Huts collected NOTHING until the base was
     * accumulated over a month and divided once — which is what the
     * first two runs of this model measured. */
    int32_t    tax_base;
    /* What the last levy brought in, for the UI to show. Derived. */
    int32_t    tax_last_month;

    int        settled;         /* 0 = generated but not colonised     */
    MapProfile profile;
    char       name[ISLAND_NAME_LEN];

    /* ---- Ownership & the harbor airlock (MMO_PLAN Phase 5) ----
     * owner is a player id (PLAYER_NONE = unowned); recorded at
     * colonisation/grant and enforced by sim_apply — privacy by
     * validation, not by hiding state. docking_allowed gates whether a
     * FOREIGN player's ship may transfer here at all (a ship that can't
     * dock can't deliver: blockade for free). escrow[] is the harbor's
     * neutral airlock: foreign ships may move goods only ship<->escrow,
     * and only the owner moves goods escrow<->stockpile. Uncapped (it
     * is a quay, not a warehouse). All three are sim state: hashed,
     * replayed, mutated only through commands. */
    uint32_t   owner;
    int        docking_allowed;

    /* ---- the port charter (MMO_PLAN later phases) ----------
     * An island is not owned outright: it is HELD, under a charter
     * bought from the faction and kept current by an upkeep payment
     * every CHARTER_UPKEEP_TICKS. Miss CHARTER_GRACE_PAYMENTS of them
     * and the charter lapses — the island is relisted, unowned and
     * dormant, its buildings still standing for whoever charters it
     * next.
     *
     * This is what gives a persistent world a way to hand islands to
     * new players without an administrator: an abandoned colony
     * eventually becomes available again on its own. It is also the
     * first gold SINK the economy has had — until now gold only ever
     * moved between the player and the faction.
     *
     * All three are sim state: hashed, replayed, integer. */
    uint32_t   charter_timer;    /* ticks toward the next payment     */
    int32_t    charter_arrears;  /* consecutive payments missed       */
    int32_t    escrow[RES_COUNT];

    /* ---- trade capacity (MARITIME_PLAN Phase 2) ------------
     * A booking does not merely cost goods and time: it takes a
     * merchant and a hull out of this island's hands for the whole
     * round trip, and gives them back when they get home. They are
     * CAPITAL, NOT FUEL — nothing is consumed, but nothing else can
     * use them meanwhile, so how many trades you can run at once is a
     * standing build decision rather than a running cost.
     *
     * Only the "out" counts are stored. Capacity is derived from the
     * buildings standing (island_merchant_capacity /
     * island_hull_capacity) because it must follow demolition and
     * depopulation for free; storing it would be a second copy of the
     * building list, wrong the moment a Merchant House burns down.
     *
     * Sim state: hashed, replayed, integer. */
    int32_t    merchants_out;
    int32_t    hulls_out;

    /* Standing marine policy (MARITIME_PLAN Phase 3c). When set, every
     * shipment dispatched from this harbour is insured at the route's
     * current premium, paid to the market when the booking is made.
     *
     * A standing policy rather than a per-order flag because Command
     * has four payload slots and an order already uses all of them —
     * and because it reads better as what it is: a decision a port
     * makes about how it does business, like docking_allowed, not a
     * box ticked on every consignment. */
    int32_t    insure_shipments;

    /* ---- expeditions (MARITIME_PLAN Phase 3d) --------------
     * Research boats are built here (at a Shipyard) and kept here.
     * Unlike a trade hull, a research boat is a THING THAT CAN BE
     * LOST: a survey that goes wrong does not give it back, which is
     * why it is a stock that goes down rather than a capacity that
     * frees up.
     *
     * Scholars are the other half, and they are not stored at all —
     * capacity comes from the populated Scholars' Houses standing
     * here, and `scholars_out` counts the ones away on a mission. A
     * lost expedition takes a resident with it. */
    int32_t    research_boats;
    int32_t    research_boats_out;
    int32_t    scholars_out;
} Island;

/* The NPC market's own player id (MARITIME_PLAN Phase 2). It owns its
 * home ports and posts orders in the book like anybody else, so it
 * needs an identity — but one no human can ever be assigned, which is
 * why it sits at the top of the range rather than after the humans.
 * Ownership checks reject a command claiming it for the same reason
 * they reject one player commanding another's ships. */
#define PLAYER_FACTION 0xFFFFFFFFu

/* How many merchants and trade hulls this island can have committed at
 * once. Every settled island has a base of each — a colony is a trading
 * post before it is anything else, and a market that only opened once
 * the third house line was up would be dead for most of a game — and
 * buildings raise it from there. */
#define TRADE_BASE_MERCHANTS      1
#define TRADE_BASE_HULLS          1

/* A faction home port is a trading house, not a colony: it starts with
 * the capacity a player would need a developed island to match. This is
 * what keeps the NPC market liquid without making it infinite — it can
 * still be saturated, and its ports can still be blockaded. */
#define FACTION_PORT_MERCHANTS    6
#define FACTION_PORT_HULLS        6
#define TRADE_MERCHANTS_PER_HOUSE 1   /* a populated Merchant House    */
#define TRADE_MERCHANTS_PER_INVESTOR 2/* its upgrade is worth more     */
#define TRADE_HULLS_PER_SHIPYARD  2

/* What a research boat costs to build, at a Shipyard. Dear next to a
 * trade hull because it is not cargo capacity — it is the thing that
 * turns a blank chart into a passage nobody else has. */
#define RESEARCH_BOAT_GOLD    600
#define RESEARCH_BOAT_PLANKS   30

int island_merchant_capacity(const Island *isl);

/* Scholars this island can have away at once: one per populated
 * Scholars' House. No base — unlike trade, an expedition needs someone
 * qualified to send, and that is the whole point of the Academy. */
int island_scholar_capacity(const Island *isl);

/* Whether a survey could set out from here right now: a scholar free,
 * a research boat idle, and a blank chart in store. */
int island_can_survey(const Island *isl);
int island_hull_capacity(const Island *isl);

/* Whether a booking could set out from here right now: one merchant and
 * one hull free. Asked by the matcher, which skips an ask whose island
 * cannot carry it rather than stalling the good for everyone. */
int island_can_dispatch(const Island *isl);

/* Generate/reset `isl` to a freshly created island: new map from
 * `seed`, camera centred, everything else cleared. `settled` is set
 * from the argument — island 0 starts settled, colonies do not. */
void island_reset(Island *isl, uint32_t seed, MapProfile profile,
                  const char *name, int settled);

/* One frame of this island's simulation: road connectivity, building
 * production, population needs, and agent spawn/assignment/movement.
 *
 * ORDERING CONSTRAINT — do not "optimise" this by hoisting the steps
 * into separate per-island loops (all islands' connectivity, then all
 * islands' agents, ...). connectivity.c keeps its BFS scratch in file
 * statics, and agents_assign_jobs() calls connectivity_bfs_from()
 * internally, relying on the road_grid built by connectivity_update()
 * for THIS island. Interleaving islands would silently path island B's
 * agents across island A's roads. Each island's pipeline must run to
 * completion before the next island's begins.
 *
 * Takes no dt: it advances the island by exactly one fixed sim tick. */
/* `world_seed` is threaded in for LIFE_PLAN Phase 3: a resident's name
 * is a pure function of (world_seed, id) rather than stored bytes, so
 * the island needs the seed at the moment somebody is born. It is the
 * only thing here that reaches outside the island. */
/* `market` prices what this island's businesses produce, so wages and
 * profit can be taxed (LIFE_PLAN Phase 7). It is the WORLD's faction —
 * an island has no market of its own — and is passed in rather than
 * reached for, because island.c must not learn what a world is. NULL is
 * accepted and means nothing is earned, which is what the unit tests
 * that build an Island by hand rely on. */
void island_update(Island *isl, uint32_t world_seed, uint64_t tick,
                   const Faction *market);

/* Tries to put a household into the empty house at `idx` — a founding
 * couple while the allowance lasts, a pair out of the reserve after
 * that. Returns how many moved in (0 or 2); 0 leaves the house empty
 * and it is asked again next month. */
int island_settle_house(Island *isl, int idx, uint32_t world_seed);

/* Moves compliance one step toward what this island's mood deserves,
 * once a month (LIFE_PLAN Phase 7).
 *
 * THE THREE DAMPING RULES docs/new-happiness-design.md demands, in one
 * function so they cannot drift apart:
 *   - nothing happens at all until the island has been unhappy for
 *     COMPLIANCE_PATIENCE_TICKS consecutive months (hysteresis);
 *   - recovery climbs at twice the rate decline falls;
 *   - it never drops below COMPLIANCE_MIN_PERMILLE (the floor).
 *
 * Exposed rather than static so tests/test_tax.c can drive an island
 * into sustained unhappiness and assert recovery is reachable, which is
 * the test that document asks for by name. */
void island_update_compliance(Island *isl);

/* How many rungs of happiness this island's tax rate is worth: 0 at a
 * modest rate, falling to -TAX_HAPPINESS_MAX at the maximum.
 *
 * Capped on purpose. It is ONE input among several and must never be
 * able to move a house's mood on its own, or a funding shortfall
 * cascades across every other factor at once — mitigation three. */
int  island_tax_happiness(const Island *isl);

/* Recompute this island's per-resource storage cap from the number of
 * active Warehouses ON THIS ISLAND. Per-island by necessity: otherwise
 * a Warehouse built on one island would raise another's caps. */
void island_recompute_storage_capacity(Island *isl);

/* Is an active, ROAD-CONNECTED building of `type` standing here?
 * BUILDING_NONE answers 1 ("nothing required"), so a caller with an
 * optional prerequisite need not special-case it.
 *
 * Defined in game.c rather than island.c because it is the sim half of
 * tier_upgrade_check()'s prerequisite (population.h) — the one part of
 * the upgrade rule the shared function cannot answer, since the sim
 * reads Island and the UI reads a snapshot. */
int island_has_building(const Island *isl, BuildingType type);

/* A stamp over the harbour quay's contents and its docking flag
 * (UI_PLAN M5). The escrow panel shows a state; the command it emits
 * carries this back; sim_apply refuses if the quay has changed since —
 * a visitor's ship can dock and take goods between the frame you read
 * and the button you press. Never zero, so zero can mean "unstamped"
 * on the wire (a replayed or scripted command). */
uint32_t island_escrow_nonce(const Island *isl);

#endif /* ISLAND_H */
