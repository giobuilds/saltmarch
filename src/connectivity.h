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

/* Recomputes Building.connected for every active building in
 * `buildings[0..count)`. Called once per frame from game_update(),
 * before anything reads the field. */
void connectivity_update(Building buildings[], int count);

#endif /* CONNECTIVITY_H */
