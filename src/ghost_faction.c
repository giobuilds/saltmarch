/*  ghost_faction.c  --  Replaying a human as a neighbour
 *                       (MMO_PLAN later phases)
 */

#include "ghost_faction.h"
#include "building.h"
#include "simlog.h"
#include <stdlib.h>
#include <string.h>

/* Which command kinds re-address cleanly to another island. The test is
 * "does this command name anything world-scoped?" — a building on an
 * island is safe, a ship index is not. */
static int kind_is_island_scoped(CommandKind k)
{
    switch (k) {
    case CMD_PLACE_BUILDING:
    case CMD_PLACE_ROAD:
    case CMD_DEMOLISH:
    case CMD_SELL_RESOURCE:
    case CMD_BUY_RESOURCE:
    case CMD_UPGRADE_HOUSE:
        return 1;
    default:
        return 0;
    }
}

/* Tiles this seeding pass has already promised to an earlier command.
 * Without it two snapped placements would race for the same spot and
 * the second would be refused at apply time — the neighbour would build
 * a thinner town than it recorded for no visible reason. */
typedef struct {
    unsigned char taken[MAP_ROWS][MAP_COLS];
} PlannedTiles;

static int footprint_free(const PlannedTiles *p, const BuildingDef *def,
                          int row, int col)
{
    int r, c;
    for (r = row; r < row + def->tile_h; r++)
        for (c = col; c < col + def->tile_w; c++) {
            if (r < 0 || c < 0 || r >= MAP_ROWS || c >= MAP_COLS) return 0;
            if (p->taken[r][c]) return 0;
        }
    return 1;
}

static void footprint_claim(PlannedTiles *p, const BuildingDef *def,
                            int row, int col)
{
    int r, c;
    for (r = row; r < row + def->tile_h; r++)
        for (c = col; c < col + def->tile_w; c++)
            if (r >= 0 && c >= 0 && r < MAP_ROWS && c < MAP_COLS)
                p->taken[r][c] = 1;
}

/* The nearest tile to (row, col) where `type` can legally stand. */
static int snap_placement(const Island *isl, PlannedTiles *planned,
                          BuildingType type, int *row, int *col)
{
    const BuildingDef *def = &BUILDING_DEFS[type];
    int radius;

    for (radius = 0; radius < MAP_ROWS; radius++) {
        int dr, dc;
        for (dr = -radius; dr <= radius; dr++)
            for (dc = -radius; dc <= radius; dc++) {
                int r, c;
                /* Ring only: the interior was covered by smaller radii. */
                if (radius > 0 && dr > -radius && dr < radius &&
                    dc > -radius && dc < radius) continue;
                r = *row + dr;
                c = *col + dc;
                if (building_place_check(&isl->map, type, r, c) != REJ_OK)
                    continue;
                if (!footprint_free(planned, def, r, c)) continue;
                footprint_claim(planned, def, r, c);
                *row = r;
                *col = c;
                return 1;
            }
    }
    return 0;
}

int ghost_faction_seed(GameState *gs, const char *path, int island,
                       uint32_t npc_player, uint64_t delay_ticks)
{
    PlannedTiles planned;
    Command *cmds = NULL;
    int      count = 0, i, seeded = 0;
    uint64_t base, first_tick = 0;
    int      have_first = 0;

    memset(&planned, 0, sizeof(planned));

    if (island < 0 || island >= MAX_ISLANDS) return -1;
    if (npc_player == PLAYER_NONE)           return -1;
    if (!game_load_commands(path, &cmds, &count)) return -1;

    base = gs->sim_tick_no + delay_ticks;

    /* The charter first, or every command below is refused for want of
     * ownership. A neighbour holds its island exactly as a player does,
     * upkeep included — which means a neighbour can go bankrupt and
     * relist, and that is a feature. */
    {
        Command grant;
        memset(&grant, 0, sizeof(grant));
        grant.kind      = CMD_GRANT_START;
        grant.a         = island;
        grant.player_id = npc_player;
        grant.tick      = base;
        if (!command_log_append(gs, &grant)) {
            free(cmds);
            return -1;
        }
    }

    for (i = 0; i < count; i++) {
        Command c = cmds[i];

        if (!kind_is_island_scoped(c.kind)) continue;

        /* Re-address: their island becomes ours, their identity becomes
         * the neighbour's, and their clock is shifted to start now. */
        if (!have_first) {
            first_tick = c.tick;
            have_first = 1;
        }
        c.a         = island;
        c.player_id = npc_player;
        c.seq       = 0;
        c.tick      = base + (c.tick - first_tick) + 1;

        /* Snap placements onto ground that exists here. A command that
         * cannot be placed anywhere at all is dropped rather than
         * queued to be refused. */
        if (c.kind == CMD_PLACE_BUILDING || c.kind == CMD_PLACE_ROAD) {
            BuildingType type = (c.kind == CMD_PLACE_ROAD)
                                ? BUILDING_ROAD
                                : (BuildingType)(c.d / 2);
            int row = c.b, col = c.c;

            if (type <= BUILDING_NONE || type >= BUILDING_TYPE_COUNT) continue;
            if (!snap_placement(&gs->islands[island], &planned, type,
                                &row, &col)) continue;
            c.b = row;
            c.c = col;
        }

        if (!command_log_append(gs, &c)) break;
        seeded++;
    }

    free(cmds);
    sim_log("Ghost faction seeded on island %d as player %u: %d commands "
            "from %s", island, npc_player, seeded, path);
    return seeded;
}
