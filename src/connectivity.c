/*  connectivity.c  --  Road-network reachability  (Phase 3)
 *
 *  Two-step algorithm, both driven off `buildings[]` directly
 *  (roads are BUILDING_ROAD entries, not a Tile flag — see the
 *  Phase 2 note in building.c):
 *
 *  1. Mark every tile currently covered by an active road in a
 *     scratch MAP_ROWS x MAP_COLS grid (roads are 1x1, so this is
 *     one cell per road building).
 *  2. Multi-source BFS over that grid, seeded from every road tile
 *     adjacent to an active Warehouse's footprint. Reachable road
 *     tiles are "connected."
 *
 *  A building is then connected if it IS a Warehouse or Road
 *  (neither needs a route to itself/isn't gated), or if any tile
 *  adjacent to its footprint is a reached road tile.
 */

#include "connectivity.h"
#include "map.h"
#include <string.h>

typedef struct { int r, c; } Pt;

static int road_grid[MAP_ROWS][MAP_COLS];
static int reached[MAP_ROWS][MAP_COLS];
static Pt  queue[MAP_ROWS * MAP_COLS];

static const int DR[4] = { -1, 1,  0, 0 };
static const int DC[4] = {  0, 0,  1,-1 };

static void mark_road_tiles(const Building buildings[], int count)
{
    int i;
    memset(road_grid, 0, sizeof(road_grid));
    for (i = 0; i < count; i++) {
        const Building *b = &buildings[i];
        if (b->active && b->type == BUILDING_ROAD)
            road_grid[b->row][b->col] = 1;
    }
}

/* Marks reached[] for every road tile 4-connected-reachable from a
 * road tile adjacent to any active Warehouse's footprint. */
static void bfs_from_warehouses(const Building buildings[], int count)
{
    int head = 0, tail = 0;
    int i;

    memset(reached, 0, sizeof(reached));

    for (i = 0; i < count; i++) {
        const Building *b = &buildings[i];
        const BuildingDef *def;
        int r, c, d;

        if (!b->active || b->type != BUILDING_WAREHOUSE) continue;
        def = &BUILDING_DEFS[BUILDING_WAREHOUSE];

        for (r = b->row; r < b->row + def->tile_h; r++) {
            for (c = b->col; c < b->col + def->tile_w; c++) {
                for (d = 0; d < 4; d++) {
                    int nr = r + DR[d], nc = c + DC[d];
                    if (nr < 0 || nr >= MAP_ROWS || nc < 0 || nc >= MAP_COLS)
                        continue;
                    if (road_grid[nr][nc] && !reached[nr][nc]) {
                        reached[nr][nc] = 1;
                        queue[tail].r = nr;
                        queue[tail].c = nc;
                        tail++;
                    }
                }
            }
        }
    }

    while (head < tail) {
        Pt p = queue[head++];
        int d;
        for (d = 0; d < 4; d++) {
            int nr = p.r + DR[d], nc = p.c + DC[d];
            if (nr < 0 || nr >= MAP_ROWS || nc < 0 || nc >= MAP_COLS)
                continue;
            if (road_grid[nr][nc] && !reached[nr][nc]) {
                reached[nr][nc] = 1;
                queue[tail].r = nr;
                queue[tail].c = nc;
                tail++;
            }
        }
    }
}

static int footprint_adjacent_to_reached(int row, int col, int fw, int fh)
{
    int r, c, d;
    for (r = row; r < row + fh; r++) {
        for (c = col; c < col + fw; c++) {
            for (d = 0; d < 4; d++) {
                int nr = r + DR[d], nc = c + DC[d];
                if (nr < 0 || nr >= MAP_ROWS || nc < 0 || nc >= MAP_COLS)
                    continue;
                if (reached[nr][nc]) return 1;
            }
        }
    }
    return 0;
}

void connectivity_update(Building buildings[], int count)
{
    int i;

    mark_road_tiles(buildings, count);
    bfs_from_warehouses(buildings, count);

    for (i = 0; i < count; i++) {
        Building *b = &buildings[i];

        if (!b->active) {
            b->connected = 0;
        } else if (b->type == BUILDING_WAREHOUSE || b->type == BUILDING_ROAD) {
            b->connected = 1;
        } else {
            const BuildingDef *def = &BUILDING_DEFS[b->type];
            b->connected = footprint_adjacent_to_reached(
                b->row, b->col, def->tile_w, def->tile_h);
        }
    }
}
