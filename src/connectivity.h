#ifndef CONNECTIVITY_H
#define CONNECTIVITY_H

/* connectivity.h -- road-network reachability. A building is connected
 * when a road 4-adjacent to its footprint reaches a warehouse. */

#include "building.h"

/* A tile coordinate — public so callers (Phase 5's agent.c) can store
 * path waypoints in the same type connectivity itself uses. */
typedef struct { int r, c; } Pt;

/* Recomputes Building.connected for every active building in
 * `buildings[0..count)`. Called once per frame from game_update(),
 * before anything reads the field. */
/* `sig` caches a signature of the inputs; when nothing that decides */
void connectivity_update(Building buildings[], int count, uint32_t *sig);

/* ---- Phase 5: point-to-point pathfinding ---------------- */

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
