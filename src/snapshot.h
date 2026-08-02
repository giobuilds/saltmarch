#ifndef SNAPSHOT_H
#define SNAPSHOT_H

/* snapshot.h  --  Full world state as bytes (SERVER.md, "Log
 * truncation: the snapshot format") */

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

/* The same, but redacted to what `viewer` is entitled to know */
int snapshot_encode_for(const GameState *gs, uint32_t viewer,
                        unsigned char **out, size_t *out_len);

/* Replace `gs`'s world with the one in `buf`. Returns 1 on success; 0 */
int snapshot_decode(GameState *gs, const unsigned char *buf, size_t len);

/* The tick a snapshot buffer claims, without decoding it. Used to
 * decide whether a snapshot is worth installing at all. Returns 0 and
 * leaves *out_tick alone if the buffer is not a readable snapshot. */
int snapshot_peek_tick(const unsigned char *buf, size_t len,
                       uint64_t *out_tick);

#endif /* SNAPSHOT_H */
