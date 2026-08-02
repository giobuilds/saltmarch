#ifndef ISLAND_H
#define ISLAND_H

/* ========================================================= */

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
/* SUPPLY_CHAIN Phase 5 doubled this for the southern climates. It is. */
#define MAX_ISLANDS 8

#define ISLAND_NAME_LEN 16

/* ---- marine insurance (MMO_PLAN later phases) -------------- */
#define INSURANCE_PREMIUM_START   80    /* 8.0% of declared value      */
/* A private passage starts dearer to insure, because it is dearer to
 * sail: it is fast precisely because it runs outside patrolled water
 * (MARITIME_PLAN Phase 3c). Experience moves both from here. */
#define INSURANCE_PREMIUM_PRIVATE 220   /* 22.0%                       */
#define INSURANCE_PREMIUM_MIN     20
#define INSURANCE_PREMIUM_MAX    400
#define INSURANCE_EMA_SHIFT        3    /* how fast experience moves it */
#define INSURANCE_MIN_PREMIUM_GOLD 5

/* ---- charter terms ---------------------------------------- */
#define CHARTER_BID_GOLD        150   /* paid to the faction to claim  */
#define CHARTER_UPKEEP_GOLD      25   /* per payment                   */
#define CHARTER_UPKEEP_TICKS   1200   /* two minutes of world time     */
#define CHARTER_GRACE_PAYMENTS    3   /* missed payments before lapse  */

/* Houses an island may found by IMMIGRATION before it has to grow. */
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
     * Agents are motion: derived, never saved, rebuilt every session. */
    Resident   residents[MAX_RESIDENTS];
    int        resident_count;
    uint32_t   next_resident_id;
    /* How many more households may be founded by IMMIGRATION (LIFE_PLAN */
    int        founder_allowance;

    /* Where somebody goes when this island cannot roof them. */
    int      (*emigrate)(void *ctx, int resident_idx);
    void      *emigrate_ctx;

    /* How many left last month. Read by the vitals strip so "people are
     * leaving" is a fact rather than an inference. Derived. */
    int        left_last_month;

    /* Cached signature of the building layout, so connectivity is
     * recomputed only when something that decides it has moved.
     * Derived: never hashed, never saved. */
    uint32_t   conn_sig;

    /* ---- the treasury (LIFE_PLAN Phase 7) ----------------- */
    int32_t    tax_rate_permille;
    int32_t    compliance_permille;
    /* Consecutive needs ticks the island has been unhappy. The
     * hysteresis: compliance does not move until this passes
     * COMPLIANCE_PATIENCE_TICKS, so one bad quarter costs nothing. */
    int32_t    unhappy_streak;
    /* Wages plus profit earned since the last levy. World state: it is
     * carried across a save and it decides what the next levy is worth. */
    int32_t    tax_base;
    /* What the last levy brought in, for the UI to show. Derived. */
    int32_t    tax_last_month;

    int        settled;         /* 0 = generated but not colonised     */
    MapProfile profile;
    char       name[ISLAND_NAME_LEN];

    /* ---- Ownership & the harbor airlock (MMO_PLAN Phase 5) ---- */
    uint32_t   owner;
    int        docking_allowed;

    /* ---- the port charter (MMO_PLAN later phases) ---------- */
    uint32_t   charter_timer;    /* ticks toward the next payment     */
    int32_t    charter_arrears;  /* consecutive payments missed       */
    int32_t    escrow[RES_COUNT];

    /* ---- trade capacity (MARITIME_PLAN Phase 2) ------------ */
    int32_t    merchants_out;
    int32_t    hulls_out;

    /* Standing marine policy (MARITIME_PLAN Phase 3c). When set, every
     * shipment dispatched from this harbour is insured at the route's
     * current premium, paid to the market when the booking is made. */
    int32_t    insure_shipments;

    /* ---- expeditions (MARITIME_PLAN Phase 3d) --------------
     * Research boats are built here (at a Shipyard) and kept here. */
    int32_t    research_boats;
    int32_t    research_boats_out;
    int32_t    scholars_out;
} Island;

/* The NPC market's own player id (MARITIME_PLAN Phase 2). It owns. */
#define PLAYER_FACTION 0xFFFFFFFFu

/* How many merchants and trade hulls this island can have committed at */
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
 * production, population needs, and agent spawn/assignment/movement. */
/* `world_seed` is threaded in for LIFE_PLAN Phase 3: a resident's name
 * is a pure function of (world_seed, id) rather than stored bytes, so
 * the island needs the seed at the moment somebody is born. It is the
 * only thing here that reaches outside the island. */
/* `market` prices what this island's businesses produce, so wages. */
void island_update(Island *isl, uint32_t world_seed, uint64_t tick,
                   const Faction *market);

/* Tries to put a household into the empty house at `idx` — a founding
 * couple while the allowance lasts, a pair out of the reserve after
 * that. Returns how many moved in (0 or 2); 0 leaves the house empty
 * and it is asked again next month. */
int island_settle_house(Island *isl, int idx, uint32_t world_seed);

/* Moves compliance one step toward what this island's mood deserves,
 * once a month (LIFE_PLAN Phase 7). */
void island_update_compliance(Island *isl);

/* How many rungs of happiness this island's tax rate is worth: 0 at a
 * modest rate, falling to -TAX_HAPPINESS_MAX at the maximum. */
int  island_tax_happiness(const Island *isl);

/* Recompute this island's per-resource storage cap from the number of
 * active Warehouses ON THIS ISLAND. Per-island by necessity: otherwise
 * a Warehouse built on one island would raise another's caps. */
void island_recompute_storage_capacity(Island *isl);

/* Is an active, ROAD-CONNECTED building of `type` standing here?
 * BUILDING_NONE answers 1 ("nothing required"), so a caller with an
 * optional prerequisite need not special-case it. */
int island_has_building(const Island *isl, BuildingType type);

/* A stamp over the harbour quay's contents and its docking flag */
uint32_t island_escrow_nonce(const Island *isl);

#endif /* ISLAND_H */
