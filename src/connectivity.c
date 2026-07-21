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

/* Pt is declared in connectivity.h (Phase 5 needs it too). */

static int road_grid[MAP_ROWS][MAP_COLS];
static int reached[MAP_ROWS][MAP_COLS];
static Pt  queue[MAP_ROWS * MAP_COLS];

/* Phase 5: scratch state for connectivity_bfs_from()/dist_to()/
 * path_to(). Separate from `reached` above, which serves the
 * warehouse-seeded flood-fill connectivity_update() runs every
 * frame — this is a distinct, on-demand single-source query. */
static int bfs_dist[MAP_ROWS][MAP_COLS];
static Pt  bfs_parent[MAP_ROWS][MAP_COLS];

/* Phase 5: a SEPARATE walkability grid for connectivity_bfs_from(),
 * deliberately not reusing road_grid. Road tiles AND active
 * Warehouse footprint tiles both count as walkable here, since a
 * Warehouse is naturally a hub where multiple road spurs converge
 * without necessarily touching each other directly — e.g. one road
 * touching the Warehouse's north edge and another touching its west
 * edge are a normal layout, and an agent should be able to walk
 * through the Warehouse to get from one to the other. road_grid
 * itself stays road-only and untouched: it drives the `connected`
 * flag (bfs_from_warehouses/footprint_adjacent_to_reached below),
 * and widening ITS definition would silently change which buildings
 * count as connected — a Phase 3 behavior already shipped, not
 * something a Phase 5 pathfinding fix should touch as a side effect. */
static int walk_grid[MAP_ROWS][MAP_COLS];

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

/* =========================================================
 * Phase 5: point-to-point pathfinding
 *
 * connectivity_bfs_from() recomputes walk_grid itself (cheap: one
 * pass over buildings[]), so it's self-contained and doesn't rely
 * on connectivity_update() having just run this frame.
 * ========================================================= */

static void mark_walk_tiles(const Building buildings[], int count)
{
    int i;
    memset(walk_grid, 0, sizeof(walk_grid));
    for (i = 0; i < count; i++) {
        const Building *b = &buildings[i];
        if (!b->active) continue;
        if (b->type == BUILDING_ROAD) {
            walk_grid[b->row][b->col] = 1;
        } else if (b->type == BUILDING_WAREHOUSE) {
            const BuildingDef *def = &BUILDING_DEFS[BUILDING_WAREHOUSE];
            int r, c;
            for (r = b->row; r < b->row + def->tile_h; r++)
                for (c = b->col; c < b->col + def->tile_w; c++)
                    walk_grid[r][c] = 1;
        }
    }
}

void connectivity_bfs_from(const Building buildings[], int count, int from_idx)
{
    const Building    *b   = &buildings[from_idx];
    const BuildingDef *def = &BUILDING_DEFS[b->type];
    int head = 0, tail = 0;
    int r, c, d;

    mark_walk_tiles(buildings, count);

    /* memset with an all-ones byte pattern sets every int in these
     * arrays to -1 (two's-complement -1 is all bits set) — both
     * "unreached" (bfs_dist) and "no predecessor / seed" (bfs_parent,
     * whose {-1,-1} sentinel connectivity_path_to() backtracks to). */
    memset(bfs_dist,   0xFF, sizeof(bfs_dist));
    memset(bfs_parent, 0xFF, sizeof(bfs_parent));

    /* Seed: walkable (road or Warehouse) tiles adjacent to from_idx's
     * own footprint. */
    for (r = b->row; r < b->row + def->tile_h; r++) {
        for (c = b->col; c < b->col + def->tile_w; c++) {
            for (d = 0; d < 4; d++) {
                int nr = r + DR[d], nc = c + DC[d];
                if (nr < 0 || nr >= MAP_ROWS || nc < 0 || nc >= MAP_COLS)
                    continue;
                if (walk_grid[nr][nc] && bfs_dist[nr][nc] < 0) {
                    bfs_dist[nr][nc] = 0;
                    queue[tail].r = nr;
                    queue[tail].c = nc;
                    tail++;
                }
            }
        }
    }

    while (head < tail) {
        Pt p = queue[head++];
        for (d = 0; d < 4; d++) {
            int nr = p.r + DR[d], nc = p.c + DC[d];
            if (nr < 0 || nr >= MAP_ROWS || nc < 0 || nc >= MAP_COLS)
                continue;
            if (walk_grid[nr][nc] && bfs_dist[nr][nc] < 0) {
                bfs_dist[nr][nc]   = bfs_dist[p.r][p.c] + 1;
                bfs_parent[nr][nc] = p;
                queue[tail].r = nr;
                queue[tail].c = nc;
                tail++;
            }
        }
    }
}

/* Shared by dist_to()/path_to(): the closest road tile adjacent to
 * to_idx's footprint that the last bfs_from() reached, or {-1,-1}/-1
 * if none. */
static int nearest_reached_adjacent(const Building buildings[], int to_idx,
                                    int *out_r, int *out_c)
{
    const Building    *b   = &buildings[to_idx];
    const BuildingDef *def = &BUILDING_DEFS[b->type];
    int r, c, d, best = -1;

    *out_r = -1;
    *out_c = -1;

    for (r = b->row; r < b->row + def->tile_h; r++) {
        for (c = b->col; c < b->col + def->tile_w; c++) {
            for (d = 0; d < 4; d++) {
                int nr = r + DR[d], nc = c + DC[d];
                if (nr < 0 || nr >= MAP_ROWS || nc < 0 || nc >= MAP_COLS)
                    continue;
                if (bfs_dist[nr][nc] >= 0 &&
                    (best < 0 || bfs_dist[nr][nc] < best)) {
                    best    = bfs_dist[nr][nc];
                    *out_r  = nr;
                    *out_c  = nc;
                }
            }
        }
    }

    return best;
}

int connectivity_dist_to(const Building buildings[], int to_idx)
{
    int r, c;
    return nearest_reached_adjacent(buildings, to_idx, &r, &c);
}

int connectivity_path_to(const Building buildings[], int to_idx,
                         Pt out_path[], int max_path)
{
    static Pt temp[MAP_ROWS * MAP_COLS];
    int temp_len = 0;
    int start_r, start_c, cr, cc, i;

    if (nearest_reached_adjacent(buildings, to_idx, &start_r, &start_c) < 0)
        return 0;

    cr = start_r;
    cc = start_c;
    while (cr >= 0) {
        temp[temp_len].r = cr;
        temp[temp_len].c = cc;
        temp_len++;
        {
            Pt parent = bfs_parent[cr][cc];
            cr = parent.r;
            cc = parent.c;
        }
    }

    if (temp_len > max_path) return 0;

    /* temp[] runs target-side -> source-side; reverse it so
     * out_path[] reads source-side -> target-side. */
    for (i = 0; i < temp_len; i++)
        out_path[i] = temp[temp_len - 1 - i];

    return temp_len;
}
