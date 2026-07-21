#ifndef CONNECTIVITY_H
#define CONNECTIVITY_H

/* =========================================================
 * connectivity.h  --  Road-network reachability  (Phase 3)
 *
 * A building only produces (see game_tick_buildings, game.c) or
 * has its population needs served (see pop_update, population.c)
 * if it is "connected": reachable via 4-connected BUILDING_ROAD
 * tiles from an active Warehouse. Warehouses and roads themselves
 * are always considered connected — a Warehouse doesn't need a
 * route to itself, and a road segment has no production to gate.
 * ========================================================= */

#include "building.h"

/* A tile coordinate — public so callers (Phase 5's agent.c) can store
 * path waypoints in the same type connectivity itself uses. */
typedef struct { int r, c; } Pt;

/* Recomputes Building.connected for every active building in
 * `buildings[0..count)`. Called once per frame from game_update(),
 * before anything reads the field. */
void connectivity_update(Building buildings[], int count);

/* ---- Phase 5: point-to-point pathfinding ----------------
 * Reuses the same road_grid connectivity_update() just built. A
 * single-source BFS answers both "how far" and "what's the route"
 * for any number of candidate destinations without re-running the
 * search per candidate — call connectivity_bfs_from() once per
 * agent (from its home), then connectivity_dist_to()/
 * connectivity_path_to() as many times as needed against it. */

/* Single-source BFS (with parent tracking) from the road tile(s)
 * adjacent to from_idx's footprint. Must be called after
 * connectivity_update() this frame so road_grid is current. */
void connectivity_bfs_from(const Building buildings[], int count, int from_idx);

/* Distance in road-tile hops from the last connectivity_bfs_from()
 * source to to_idx's footprint, or -1 if unreached. */
int connectivity_dist_to(const Building buildings[], int to_idx);

/* Backtracks the parent chain to fill out_path[] with the road-tile
 * route (ordered from the connectivity_bfs_from() source toward
 * to_idx) to to_idx's footprint. Returns the route length, or 0 if
 * unreached or the route would exceed max_path. */
int connectivity_path_to(const Building buildings[], int to_idx,
                         Pt out_path[], int max_path);

#endif /* CONNECTIVITY_H */
