/*  map.c  --  Tile map implementation
 *
 *  For now we fill the map with a simple hand-crafted pattern so
 *  we have something interesting to look at without needing image
 *  assets yet.  Later we will replace this with a noise-based or
 *  file-loaded map.
 */

#include "map.h"
#include <stddef.h>

/* ---- map_init ------------------------------------------
 * Walks every cell in the grid and assigns a TileType.
 * The rule here is very simple:
 *   - outer 3-tile border  → TILE_WATER  (the sea around the island)
 *   - a sandy beach ring   → TILE_SAND
 *   - a diagonal band      → TILE_FOREST
 *   - everything else      → TILE_GRASS
 * This gives us a recognisable island shape from day one.
 * -------------------------------------------------------- */
void map_init(Map *map)
{
    int r, c;

    map->rows = MAP_ROWS;
    map->cols = MAP_COLS;

    for (r = 0; r < MAP_ROWS; r++) {
        for (c = 0; c < MAP_COLS; c++) {
            Tile *t = &map->tiles[r][c];
            t->elevation = 0;

            /* Sea border */
            if (r < 3 || r >= MAP_ROWS - 3 ||
                c < 3 || c >= MAP_COLS - 3) {
                t->type = TILE_WATER;

            /* Sandy beach: one tile inside the water border */
            } else if (r < 5 || r >= MAP_ROWS - 5 ||
                       c < 5 || c >= MAP_COLS - 5) {
                t->type = TILE_SAND;

            /* A diagonal forest belt across the island interior */
            } else if ((r + c) % 9 == 0 || (r + c) % 9 == 1) {
                t->type = TILE_FOREST;

            /* Everything else is buildable grass */
            } else {
                t->type = TILE_GRASS;
            }
        }
    }
}

/* ---- map_get_tile --------------------------------------
 * Bounds-checked tile accessor.  Returns NULL when the
 * caller asks for a tile outside the grid.
 * -------------------------------------------------------- */
Tile *map_get_tile(Map *map, int row, int col)
{
    if (row < 0 || row >= map->rows ||
        col < 0 || col >= map->cols) {
        return NULL;
    }
    return &map->tiles[row][col];
}
