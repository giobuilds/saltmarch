#ifndef SNAPSHOT_H
#define SNAPSHOT_H

/* =========================================================
 * snapshot.h  --  Full world state as bytes (SERVER.md, "Log
 *                 truncation: the snapshot format")
 *
 * A world was a pure function of (seed, ordered command log), and
 * reaching any tick meant replaying every tick before it. That is what
 * made joining a long-lived server expensive: the cost is REPLAY TIME,
 * proportional to the world's age, and it accrues whether or not anyone
 * played. A snapshot is the escape — the state at tick T, written
 * directly, so a client can begin at T instead of walking to it.
 *
 * WHAT IS AND IS NOT IN HERE. Everything the sim reads when it runs a
 * tick, and nothing else. Islands (map, stockpile, buildings, pop,
 * agents, charter, ownership, escrow), ships, the faction, the world
 * clock and seed. Not the camera, not hover or overlay flags, not the
 * net session or local player id, not the F9/scrub bookkeeping — those
 * are client state, and a snapshot that carried them would make one
 * machine's view part of the world.
 *
 * EXPLICIT ENCODING, NOT A STRUCT DUMP. Every field is written
 * little-endian by hand. fwrite(&state) would have been a tenth of the
 * code and was rejected twice over: struct padding is uninitialised
 * bytes (the same class of bug that once leaked four bytes of stack
 * into every save and made two recordings of one session differ), and a
 * reordered or resized field would silently load as a DIFFERENT WORLD
 * rather than failing. Floats are stored as their IEEE-754 bits, which
 * every platform this targets uses.
 *
 * LIVE DATA ONLY. A dump of the structs is 2.6 MB, of which ~2.1 MB is
 * Agent.path[] arrays that are usually empty and 328 KB is four Maps.
 * At 40 bytes per Command that would only pay for itself past ~65,000
 * commands — i.e. it would make every ordinary world bigger. So this
 * writes agent_count agents each with path_len waypoints,
 * building_count buildings, Tiles packed 20 bytes into 8, and a map
 * only for islands that are SETTLED. An unsettled island's map cannot
 * have been touched (sim_place_building refuses unsettled islands and
 * every other mutation is ownership-gated), so it is regenerated from
 * its seed and profile on load.
 *
 * SELF-VERIFYING. The snapshot carries the sim_hash of the world it
 * captured. Decode recomputes it and refuses a mismatch, so a corrupt
 * or truncated checkpoint fails loudly instead of quietly becoming a
 * different world that then diverges from everyone else's.
 * ========================================================= */

#include "game.h"
#include <stddef.h>
#include <stdint.h>

/* Bumped whenever the encoding changes. Unlike SAVE_VERSION this is not
 * about the meaning of a log — a snapshot has no history to
 * reinterpret — it is purely "these bytes are laid out differently". */
#define SNAPSHOT_VERSION 15u   /* 2: order book; 3: trade capacity;
                               * 4: the faction's standing quotes;
                               * 5: route knowledge and charts;
                               * 6: per-route premiums, raids, policies;
                               * 7: expeditions, boats and scholars;
                               * 8: which passages are in play;
                               * 9: ship class, guns, hull, escort;
                               * 10: the pirate fleets;
                               * 11: a house's origin tier;
                               * 12: happiness as a 0..10 ladder;
                               * 13: residents as named people;
                               * 14: sex, pregnancy and the birth house;
                               * 15: the reserve, and the treasury */

/* Encode `gs`'s world state into a freshly malloc'd buffer. On success
 * returns 1 and stores the buffer and its length; the caller owns the
 * buffer and frees it. Returns 0 on allocation failure. */
int snapshot_encode(const GameState *gs, unsigned char **out, size_t *out_len);

/* The same, but redacted to what `viewer` is entitled to know
 * (SERVER_AUTHORITY.md Phase 3). Own islands in full; foreign ones by
 * their public face; nobody else's charts, expeditions or ship
 * manifests; and a shipment on a private passage reported as though it
 * took the public lane.
 *
 * This is the ONE place concealment happens. It is a filter on the way
 * out rather than a rule inside the sim, because the sim has to be able
 * to compute the whole world — every other design collapses into
 * writing the game twice.
 *
 * `viewer` of PLAYER_NONE redacts nothing, which is what a checkpoint
 * and the replay tools want: a server's own record of the world is not
 * a view of it. */
int snapshot_encode_for(const GameState *gs, uint32_t viewer,
                        unsigned char **out, size_t *out_len);

/* Replace `gs`'s world with the one in `buf`. Returns 1 on success; 0
 * if the buffer is not a snapshot this build understands, is truncated,
 * carries out-of-range counts, or does not hash to the value it claims
 * — in every one of those cases `gs` is left untouched rather than
 * half-loaded, because a partially applied world is worse than no world
 * at all.
 *
 * Restores the sim only. The caller re-establishes its own view state
 * (current island, cameras) exactly as it does after game_load. */
int snapshot_decode(GameState *gs, const unsigned char *buf, size_t len);

/* The tick a snapshot buffer claims, without decoding it. Used to
 * decide whether a snapshot is worth installing at all. Returns 0 and
 * leaves *out_tick alone if the buffer is not a readable snapshot. */
int snapshot_peek_tick(const unsigned char *buf, size_t len,
                       uint64_t *out_tick);

#endif /* SNAPSHOT_H */
