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

/* Four is plenty for the intended archipelago and keeps GameState at
 * roughly its historical size — see MAX_AGENTS in agent.h, which was
 * reduced specifically so this multiplication stays affordable. */
#define MAX_ISLANDS 4

#define ISLAND_NAME_LEN 16

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
    float      agent_assign_timer;

    int        settled;         /* 0 = generated but not colonised     */
    MapProfile profile;
    char       name[ISLAND_NAME_LEN];
} Island;

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
 * completion before the next island's begins. */
void island_update(Island *isl, float dt);

/* Recompute this island's per-resource storage cap from the number of
 * active Warehouses ON THIS ISLAND. Per-island by necessity: otherwise
 * a Warehouse built on one island would raise another's caps. */
void island_recompute_storage_capacity(Island *isl);

#endif /* ISLAND_H */
