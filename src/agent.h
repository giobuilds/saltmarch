#ifndef AGENT_H
#define AGENT_H

/* =========================================================
 * agent.h  --  Population agents and real labor supply  (Phase 5)
 *
 * One Agent per resident (PopData.residents), synced every frame
 * by agents_sync(). Agents are purely visible/derived state — not
 * saved (see agents_sync()'s doc comment) — but they DO gate
 * production: a Fisher's Hut/Farm/Lumberjack only ticks while at
 * least one assigned agent is physically AGENT_WORKING there (see
 * Building.worker_count, tallied by agent_update()).
 *
 * Lifecycle, one agent at a time:
 *   AGENT_IDLE_HOME       -- at home; unemployed, or resting between shifts
 *   AGENT_COMMUTING_WORK  -- walking home -> workplace
 *   AGENT_WORKING         -- at workplace; counts toward worker_count
 *   AGENT_COMMUTING_HOME  -- walking workplace -> home
 *
 * Job assignment (agent_assign_jobs) is periodic, not per-frame:
 * buildings are never removed in this codebase (no demolish tool),
 * so open jobs only change when a new producer is placed or a
 * house's population grows -- there is nothing to reassign otherwise.
 * ========================================================= */

#include "building.h"
#include "population.h"
#include "connectivity.h"   /* Pt */

#define MAX_AGENTS        2000
#define MAX_AGENT_PATH     128

#define AGENT_SPEED_ROAD     3.0f   /* tiles/sec while on a road waypoint */
#define AGENT_SPEED_OFFROAD  1.0f   /* tiles/sec for the home/work "last mile" */
#define AGENT_SHIFT_DURATION 60.0f  /* seconds spent AGENT_WORKING per shift */
#define AGENT_REST_DURATION  15.0f  /* seconds spent AGENT_IDLE_HOME before recommuting */
#define AGENT_ASSIGN_INTERVAL 3.0f  /* seconds between job-assignment passes */

typedef enum {
    AGENT_IDLE_HOME = 0,
    AGENT_COMMUTING_WORK,
    AGENT_WORKING,
    AGENT_COMMUTING_HOME
} AgentState;

typedef struct {
    int        active;      /* like Building/PopData: reused via
                              * find-inactive-or-append, NOT append-only --
                              * residents grow/shrink every NEEDS_INTERVAL,
                              * so agent churn is frequent (unlike buildings,
                              * which are never removed). */
    int        home_idx;    /* buildings[] index of the House */
    int        work_idx;    /* buildings[] index of workplace, -1 if none */
    AgentState state;
    float      row, col;    /* fractional tile position (iso_to_screen input) */
    float      state_timer; /* seconds spent in the current state */
    Pt         path[MAX_AGENT_PATH];
    int        path_len;
    int        path_pos;    /* index of the next waypoint to walk toward */
} Agent;

/* Reconciles agents[] against every active House's pop_data.residents:
 * spawns new AGENT_IDLE_HOME agents (reusing inactive slots first) if
 * residents grew since the last sync, despawns surplus agents
 * (preferring an AGENT_IDLE_HOME one, so a walking agent is never cut
 * off mid-stride) if residents shrank. Called once per frame after
 * pop_update(), and once after a successful game_load() to rebuild the
 * agent population from the freshly-restored pop_data instead of
 * trying to serialize agents at all. */
void agents_sync(Agent agents[], int *agent_count,
                 const Building buildings[], const PopData pop_data[],
                 int building_count);

/* Periodic (see AGENT_ASSIGN_INTERVAL): assigns every unemployed,
 * AGENT_IDLE_HOME agent to the nearest still-open job (by road-network
 * distance) among active, connected buildings with tick_seconds > 0
 * (the same generic "this building type produces on a tick" signal
 * game_tick_buildings already uses) that no other agent has already
 * claimed. */
void agents_assign_jobs(Agent agents[], int agent_count,
                        const Building buildings[], int building_count);

/* Called every frame: advances each agent's state machine and
 * position, and tallies Building.worker_count (zeroed here, then +1
 * per AGENT_WORKING agent's work_idx) for game_tick_buildings()'s
 * labor-supply gate. */
void agents_update(Agent agents[], int agent_count,
                   Building buildings[], int building_count, float dt);

#endif /* AGENT_H */
