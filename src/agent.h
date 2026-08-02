#ifndef AGENT_H
#define AGENT_H

/* agent.h  --  Population agents and real labor supply  (Phase 5) */

#include "building.h"
#include "population.h"
#include "connectivity.h"   /* Pt */

/* Agent is ~1060 bytes (path[] below is 1KB of it), so this cap
 * dominates memory: at 2000 it was 2.1MB, i.e. ~96% of GameState. */
#define MAX_AGENTS         512

/* Do NOT shrink this to save memory: build_commute_path() passes */
#define MAX_AGENT_PATH     128

#define AGENT_SPEED_ROAD     3.0f   /* tiles/sec while on a road waypoint */
#define AGENT_SPEED_OFFROAD  1.0f   /* tiles/sec for the home/work "last mile" */
/* ---- one work cycle is one month (LIFE_PLAN Phase 4) ------ */
#define AGENT_SHIFT_DURATION 24.0f  /* seconds spent AGENT_WORKING per shift */
#define AGENT_REST_DURATION   6.0f  /* seconds spent AGENT_IDLE_HOME before recommuting */
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
                              * residents grow/shrink every NEEDS_INTERVAL
                              * (and a House can be demolished outright),
                              * so agent churn is frequent. */
    int        home_idx;    /* buildings[] index of the House */
    int        work_idx;    /* buildings[] index of workplace, -1 if none */
    AgentState state;
    float      row, col;    /* fractional tile position (iso_to_screen input) */
    float      state_timer; /* seconds spent in the current state */
    Pt         path[MAX_AGENT_PATH];
    int        path_len;
    int        path_pos;    /* index of the next waypoint to walk toward */
} Agent;

/* Reconciles agents[] against every active House's pop_data.residents: */
/* `adults_at(ctx, house_idx)` answers how many people in that house. */
/* `workers[h]` is how many agents house h should have; `live_agents[h]`
 * how many it has. Both are per-house tallies the caller builds in one
 * pass, rather than this scanning every resident and agent per house. */
void agents_sync(Agent agents[], int *agent_count,
                 const Building buildings[], const PopData pop_data[],
                 int building_count, const int workers[],
                 int live_agents[]);

/* Counts active agents per house into `out`, one pass. */
void agents_tally(const Agent agents[], int agent_count,
                  int building_count, int out[]);

/* Periodic (see AGENT_ASSIGN_INTERVAL): assigns every. */
void agents_assign_jobs(Agent agents[], int agent_count,
                        const Building buildings[], int building_count);

/* Called every frame: advances each agent's state machine and
 * position, and tallies Building.worker_count (zeroed here, then +1
 * per AGENT_WORKING agent's work_idx) for game_tick_buildings()'s
 * labor-supply gate. */
void agents_update(Agent agents[], int agent_count,
                   Building buildings[], int building_count, float dt);

#endif /* AGENT_H */
