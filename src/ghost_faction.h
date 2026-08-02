#ifndef GHOST_FACTION_H
#define GHOST_FACTION_H

/* ghost_faction.h  --  Neighbours with no AI
 * (MMO_PLAN later phases) */

#include <stdint.h>
#include "game.h"

/* Seed `island` from the recorded session at `path`, as `npc_player`,
 * with its first command landing `delay_ticks` from now. */
int ghost_faction_seed(GameState *gs, const char *path, int island,
                       uint32_t npc_player, uint64_t delay_ticks);

#endif /* GHOST_FACTION_H */
