/*  agent.c  --  Population agents and real labor supply  (Phase 5)  */

#include "agent.h"
#include <math.h>
#include <string.h>

/* ---- Sync: keep agents[] matching pop_data[].residents -------- */

static int count_live_agents_for_home(const Agent agents[], int agent_count,
                                      int home_idx)
{
    int i, n = 0;
    for (i = 0; i < agent_count; i++)
        if (agents[i].active && agents[i].home_idx == home_idx)
            n++;
    return n;
}

static int find_free_agent_slot(Agent agents[], int *agent_count)
{
    int i;

    /* Same find-inactive-or-append reuse building_place() does now:
     * a house's residents legitimately grows and shrinks every
     * NEEDS_INTERVAL (and a demolished House deactivates its agents
     * outright), so churn is frequent, not append-only. */
    for (i = 0; i < *agent_count; i++)
        if (!agents[i].active) return i;

    if (*agent_count < MAX_AGENTS) {
        int idx = *agent_count;
        (*agent_count)++;
        return idx;
    }

    return -1;   /* full — drop silently, shouldn't happen at MAX_AGENTS */
}

static void spawn_agent(Agent agents[], int *agent_count, int home_idx,
                        const Building buildings[])
{
    int             idx = find_free_agent_slot(agents, agent_count);
    Agent          *a;
    const Building *home_b;

    if (idx < 0) return;

    a      = &agents[idx];
    home_b = &buildings[home_idx];

    a->active      = 1;
    a->home_idx    = home_idx;
    a->work_idx    = -1;
    a->state       = AGENT_IDLE_HOME;
    a->row         = (float)home_b->row;
    a->col         = (float)home_b->col;
    a->state_timer = 0.0f;
    a->path_len    = 0;
    a->path_pos    = 0;
}

/* Removes one agent belonging to `home_idx`, preferring one currently
 * AGENT_IDLE_HOME so a walking agent is never cut off mid-stride. */
static void despawn_one_agent_for_home(Agent agents[], int agent_count,
                                       int home_idx)
{
    int i, fallback = -1;

    for (i = 0; i < agent_count; i++) {
        if (!agents[i].active || agents[i].home_idx != home_idx) continue;
        if (agents[i].state == AGENT_IDLE_HOME) {
            agents[i].active = 0;
            return;
        }
        fallback = i;
    }

    if (fallback >= 0) agents[fallback].active = 0;
}

void agents_sync(Agent agents[], int *agent_count,
                 const Building buildings[], const PopData pop_data[],
                 int building_count)
{
    int i;

    for (i = 0; i < building_count; i++) {
        int live, target, k;

        /* Any residential tier, not just BUILDING_HOUSE — testing the
         * concrete type here meant an upgraded Worker's House silently
         * stopped having its agents spawned/despawned. */
        if (!buildings[i].active || !pop_is_house_type(buildings[i].type)) continue;
        if (!pop_data[i].active) continue;

        live   = count_live_agents_for_home(agents, *agent_count, i);
        target = pop_data[i].residents;

        for (k = live; k < target; k++)
            spawn_agent(agents, agent_count, i, buildings);
        for (k = live; k > target; k--)
            despawn_one_agent_for_home(agents, *agent_count, i);
    }
}

/* ---- Job assignment: nearest open job, periodic --------------- */

void agents_assign_jobs(Agent agents[], int agent_count,
                        const Building buildings[], int building_count)
{
    static int claimed[MAX_BUILDINGS];
    int i, j;

    memset(claimed, 0, sizeof(claimed));
    for (i = 0; i < agent_count; i++)
        if (agents[i].active && agents[i].work_idx >= 0)
            claimed[agents[i].work_idx] = 1;

    for (i = 0; i < agent_count; i++) {
        Agent *a = &agents[i];
        int    best_job = -1, best_dist = -1;

        if (!a->active || a->state != AGENT_IDLE_HOME || a->work_idx >= 0)
            continue;

        /* One BFS answers "how far" for every candidate job below —
         * far cheaper than a fresh BFS per (agent, job) pair. */
        connectivity_bfs_from(buildings, building_count, a->home_idx);

        for (j = 0; j < building_count; j++) {
            const Building    *b = &buildings[j];
            const BuildingDef *def;
            int                d;

            if (!b->active || !b->connected || claimed[j]) continue;
            def = &BUILDING_DEFS[b->type];
            if (def->tick_seconds <= 0.0f) continue;   /* not a producer */

            d = connectivity_dist_to(buildings, j);
            if (d >= 0 && (best_dist < 0 || d < best_dist)) {
                best_dist = d;
                best_job  = j;
            }
        }

        if (best_job >= 0) {
            a->work_idx      = best_job;
            claimed[best_job] = 1;   /* so a later agent this pass can't also grab it */
        }
    }
}

/* ---- State machine + movement ---------------------------------- */

/* Builds the road route from `from_idx`'s footprint to `to_idx`'s
 * footprint into a->path[]/path_len, followed by to_idx's own tile
 * as the final "last mile" waypoint. Sets path_len to 0 on failure
 * (no route) — can genuinely happen now if a road the route depended
 * on was demolished since the job was assigned (see agents_update()'s
 * callers: both just retry next frame rather than assuming success).
 * Note this only affects starting a *new* leg of the commute — an
 * agent already mid-walk doesn't re-validate its stored path, so
 * destroying a road out from under one just means its dot crosses a
 * tile that's no longer a road on its way, not a stuck/crashed agent. */
static void build_commute_path(const Building buildings[], int count,
                               int from_idx, int to_idx, Agent *a)
{
    const Building *to_b = &buildings[to_idx];
    int             road_len;

    connectivity_bfs_from(buildings, count, from_idx);
    road_len = connectivity_path_to(buildings, to_idx, a->path, MAX_AGENT_PATH - 1);

    a->path_pos = 0;
    if (road_len <= 0) {
        a->path_len = 0;
        return;
    }

    a->path_len = road_len;
    a->path[a->path_len].r = to_b->row;
    a->path[a->path_len].c = to_b->col;
    a->path_len++;
}

/* Advances a->row/col toward path[path_pos] at a speed depending on
 * whether this hop is the off-road "last mile" (the first hop, start
 * tile -> first road tile, and the last hop, last road tile ->
 * destination tile) or an on-road hop between two road waypoints. */
static void move_along_path(Agent *a, float dt)
{
    float target_r, target_c, speed, dr, dc, dist, step;

    if (a->path_pos >= a->path_len) return;

    target_r = (float)a->path[a->path_pos].r;
    target_c = (float)a->path[a->path_pos].c;

    speed = (a->path_pos == 0 || a->path_pos == a->path_len - 1)
          ? AGENT_SPEED_OFFROAD : AGENT_SPEED_ROAD;

    dr   = target_r - a->row;
    dc   = target_c - a->col;
    dist = sqrtf(dr * dr + dc * dc);
    step = speed * dt;

    if (step >= dist || dist < 0.0001f) {
        a->row = target_r;
        a->col = target_c;
        a->path_pos++;
    } else {
        a->row += dr / dist * step;
        a->col += dc / dist * step;
    }
}

void agents_update(Agent agents[], int agent_count,
                   Building buildings[], int building_count, float dt)
{
    int i;

    for (i = 0; i < building_count; i++)
        buildings[i].worker_count = 0;

    for (i = 0; i < agent_count; i++) {
        Agent *a = &agents[i];
        if (!a->active) continue;

        a->state_timer += dt;

        switch (a->state) {
        case AGENT_IDLE_HOME:
            if (a->work_idx >= 0 && a->state_timer >= AGENT_REST_DURATION) {
                build_commute_path(buildings, building_count,
                                   a->home_idx, a->work_idx, a);
                if (a->path_len > 0) {
                    a->state       = AGENT_COMMUTING_WORK;
                    a->state_timer = 0.0f;
                }
                /* else: route vanished (shouldn't happen — see
                 * build_commute_path's doc comment); stay idle and
                 * retry next frame. */
            }
            break;

        case AGENT_COMMUTING_WORK:
        case AGENT_COMMUTING_HOME:
            move_along_path(a, dt);
            if (a->path_pos >= a->path_len) {
                a->state       = (a->state == AGENT_COMMUTING_WORK)
                                ? AGENT_WORKING : AGENT_IDLE_HOME;
                a->state_timer = 0.0f;
            }
            break;

        case AGENT_WORKING:
            buildings[a->work_idx].worker_count++;
            if (a->state_timer >= AGENT_SHIFT_DURATION) {
                build_commute_path(buildings, building_count,
                                   a->work_idx, a->home_idx, a);
                if (a->path_len > 0) {
                    a->state       = AGENT_COMMUTING_HOME;
                    a->state_timer = 0.0f;
                }
                /* else: stay working, retry next frame (defensive). */
            }
            break;
        }
    }
}
