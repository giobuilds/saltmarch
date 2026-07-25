#ifndef GHOST_FACTION_H
#define GHOST_FACTION_H

/* =========================================================
 * ghost_faction.h  --  Neighbours with no AI
 *                      (MMO_PLAN later phases)
 *
 * An NPC island is seeded with a RECORDED HUMAN SESSION, replayed on
 * that island at an offset tick, under an NPC player id. The neighbour
 * builds what somebody actually built, in the order they actually built
 * it, at the pace they actually built it — and there is no behaviour
 * tree anywhere, because the behaviour is a log.
 *
 * Why this works at all: a world is a pure function of (seed, ordered
 * commands), so somebody else's commands are a portable description of
 * what they did. Re-address them to a different island and a different
 * player and they describe a different neighbour doing the same things.
 *
 * What is deliberately dropped: anything naming a ship, an escrow or a
 * colonisation. Ship indices are world-scoped, so a recorded "load ship
 * 0" would reach into whatever ship 0 happens to be in THIS world —
 * somebody else's, most likely. Island-scoped building and trading
 * commands re-address cleanly; the rest do not, and pretending
 * otherwise would corrupt live ships.
 *
 * COORDINATES ARE ADVISORY. A recorded placement names a tile that was
 * legal on the RECORDER'S island; the target island has different
 * terrain, so most of those tiles are water, rock or the wrong soil.
 * The seeder therefore snaps each placement to the nearest legal tile,
 * scanning outward from the recorded position in a fixed order (so it
 * stays deterministic) and avoiding tiles it has already promised to an
 * earlier command.
 *
 * What survives from the recording is the SEQUENCE and PACE of what
 * somebody built — which is what makes a neighbour believable — not the
 * exact layout. A recording is a script, not a blueprint.
 *
 * The seeded commands go into the ordinary command log, so an NPC
 * island replays, hashes and desync-checks exactly like a player's.
 * ========================================================= */

#include <stdint.h>
#include "game.h"

/* Seed `island` from the recorded session at `path`, as `npc_player`,
 * with its first command landing `delay_ticks` from now.
 *
 * Returns the number of commands seeded, or -1 if the file could not be
 * read. A return of 0 means the log had nothing re-addressable in it.
 *
 * The island must be unsettled and unowned: this appends the charter
 * grant too, so the neighbour holds its island the same way a player
 * would. */
int ghost_faction_seed(GameState *gs, const char *path, int island,
                       uint32_t npc_player, uint64_t delay_ticks);

#endif /* GHOST_FACTION_H */
