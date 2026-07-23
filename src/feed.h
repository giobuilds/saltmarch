#ifndef FEED_H
#define FEED_H

/* =========================================================
 * feed.h  --  The shared voyage feed: ghost multiplayer
 *             (MMO_PLAN Phase 4)
 *
 * Every client appends its own departures to feed_out.jsonl and
 * periodically re-reads feed_in.jsonl, rendering everyone else's
 * voyages as non-interactive GHOSTS on the world map. Transport
 * between the two files is deliberately out of process
 * (scripts/feedsync.sh — a shared folder, rsync, curl, anything);
 * the game itself performs zero networking.
 *
 * THE COSMETIC BOUNDARY. The Feed lives in App (main.c), NOT in
 * GameState. Nothing here is sim state: ghosts never enter sim_hash,
 * feed polling runs on wall clock, and the CLI record/replay path never
 * constructs a Feed. That containment — structural, not disciplinary —
 * is what keeps F9 and the CI determinism replay untouched by anything
 * another player (or a dead sync script) does.
 *
 * GHOST TIME. Peers' sim ticks are meaningless here (every client's
 * clock started at its own tick 0), so each published line carries
 * departure_unix_ms — wall time at publish — and ghosts are rendered
 * by wall-clock elapsed over the voyage's wall duration. A ghost whose
 * voyage has elapsed simply isn't drawn, so a stale feed fades out
 * naturally instead of freezing.
 *
 * FEED HYGIENE. The file is untrusted input: lines are parsed
 * defensively, unknown/malformed lines are counted and skipped (never
 * crash), a partial trailing line (a writer mid-append) is ignored,
 * names are length-clamped, and the ghost list is capped.
 *
 * File format: one JSON object per line.
 *   {"hello":<id>,"name":"<name>"}                       handshake
 *   {"player":<id>,"ship":..,"from":..,"to":..,
 *    "departure_tick":..,"cargo":[..],
 *    "departure_unix_ms":..}                             voyage
 * ========================================================= */

#include "ship.h"
#include <stdint.h>

#define FEED_OUT_PATH        "feed_out.jsonl"
#define FEED_IN_PATH         "feed_in.jsonl"
#define FEED_POLL_SECONDS    30
#define FEED_MAX_GHOSTS      64
#define FEED_NAME_LEN        16
#define FEED_MAX_PEERS       32

typedef struct {
    uint32_t player_id;
    char     name[FEED_NAME_LEN];   /* resolved from the peer's handshake */
    int32_t  from, to;              /* island indices                     */
    uint64_t departure_unix_ms;     /* wall time the voyage began         */
} GhostVoyage;

typedef struct {
    uint32_t my_id;
    char     my_name[FEED_NAME_LEN];
    int      handshake_written;

    /* Wall-clock poll timer (SDL_GetTicksNS domain). */
    uint64_t next_poll_ns;

    /* Departure detection: what each ship slot looked like last frame,
     * so a docked->at-sea transition (manual OR route auto-depart) is
     * caught without the sim knowing the feed exists. */
    uint64_t seen_departure_tick[MAX_SHIPS];
    int      seen_at_sea[MAX_SHIPS];

    /* The ghost list rebuilt by each poll. */
    GhostVoyage ghosts[FEED_MAX_GHOSTS];
    int         ghost_count;
    int         malformed_count;   /* skipped lines, visible for debug */
} Feed;

/* Initialise with a display name (clamped to FEED_NAME_LEN-1); the
 * feed id is derived from the name so two differently-named clients
 * never collide. Writes nothing until the first departure. */
void feed_init(Feed *f, const char *name);

/* Detect ships that departed since the last call and append a voyage
 * line (stamped with `unix_ms`) for each to FEED_OUT_PATH, preceded by
 * the one-time handshake line. Call once per frame from main.c. */
void feed_track_departures(Feed *f, const Ship ships[], int ship_count,
                           uint64_t unix_ms);

/* Re-read FEED_IN_PATH and rebuild the ghost list if the poll interval
 * has elapsed (`now_ns` is SDL_GetTicksNS). Missing file = zero ghosts,
 * not an error — killing the sync script degrades gracefully. */
void feed_poll(Feed *f, uint64_t now_ns);

/* The poll's worker, exposed for tests: parse `path` into the ghost
 * list right now. Returns the number of ghosts loaded. */
int  feed_reload(Feed *f, const char *path);

/* Where a ghost is along its lane at wall time `unix_ms`: 0..1, or -1
 * if the voyage hasn't started or has already finished (don't draw). */
float ghost_progress(const GhostVoyage *g, uint64_t unix_ms);

#endif /* FEED_H */
