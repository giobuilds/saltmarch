/*  feed.c  --  The shared voyage feed (MMO_PLAN Phase 4)
 *
 * Plain C stdio throughout: the feed is append-only text, the parser
 * must tolerate a file another process is writing this instant, and
 * none of it may ever block the frame for long (files are tiny).
 */

#include "feed.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* FNV-1a over the name: a stable cosmetic id, no RNG involved. */
static uint32_t name_hash(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
    return h ? h : 1u;   /* 0 is reserved for "unknown" */
}

void feed_init(Feed *f, const char *name)
{
    memset(f, 0, sizeof(*f));
    snprintf(f->my_name, sizeof(f->my_name), "%s",
             (name && name[0]) ? name : "captain");
    f->my_id = name_hash(f->my_name);
}

/* ---- publishing ------------------------------------------ */

static void publish_line(Feed *f, const VoyageRecord *v, uint64_t unix_ms)
{
    FILE *fp = fopen(FEED_OUT_PATH, "a");
    char  rec[512];

    if (!fp) return;   /* an unwritable feed must never hurt the game */

    if (!f->handshake_written) {
        fprintf(fp, "{\"hello\":%u,\"name\":\"%s\"}\n", f->my_id, f->my_name);
        f->handshake_written = 1;
    }

    /* The canonical record serialiser (ship.c) writes {...}; the feed
     * envelope extends it: strip the closing brace and append the
     * wall-time field. One serialiser, one shape, no drift. */
    if (voyage_record_to_json(v, rec, sizeof(rec)) > 0) {
        size_t len = strlen(rec);
        rec[len - 1] = '\0';   /* drop '}' */
        fprintf(fp, "%s,\"departure_unix_ms\":%llu}\n",
                rec, (unsigned long long)unix_ms);
    }
    fclose(fp);
}

void feed_track_departures(Feed *f, const Ship ships[], int ship_count,
                           uint64_t unix_ms)
{
    int i;

    for (i = 0; i < ship_count && i < MAX_SHIPS; i++) {
        const Ship *sh     = &ships[i];
        int         at_sea = sh->active && sh->at_island < 0;

        /* A new voyage is "now at sea" with a departure_tick we have
         * not seen — the tick check also catches back-to-back route
         * legs, where a ship docks and re-departs between two frames
         * that both see it at sea. */
        if (at_sea && (!f->seen_at_sea[i] ||
                       f->seen_departure_tick[i] != sh->departure_tick)) {
            VoyageRecord v = voyage_record_make(sh, i, f->my_id);
            publish_line(f, &v, unix_ms);
            f->seen_departure_tick[i] = sh->departure_tick;
        }
        f->seen_at_sea[i] = at_sea;
    }
}

/* ---- parsing --------------------------------------------- */

/* Find "\"key\":" in `line` and parse the integer after it. Returns 1
 * on success. Tolerates any field order and unknown extra fields. */
static int get_i64(const char *line, const char *key, long long *out)
{
    char  pat[48];
    const char *p;

    snprintf(pat, sizeof(pat), "\"%s\":", key);
    p = strstr(line, pat);
    if (!p) return 0;
    *out = strtoll(p + strlen(pat), NULL, 10);
    return 1;
}

/* Extract "\"key\":\"...\"", clamped into out[n]. Returns 1 on success. */
static int get_str(const char *line, const char *key, char *out, size_t n)
{
    char  pat[48];
    const char *p, *e;
    size_t len;

    snprintf(pat, sizeof(pat), "\"%s\":\"", key);
    p = strstr(line, pat);
    if (!p) return 0;
    p += strlen(pat);
    e = strchr(p, '"');
    if (!e) return 0;
    len = (size_t)(e - p);
    if (len >= n) len = n - 1;   /* clamp hostile long names */
    memcpy(out, p, len);
    out[len] = '\0';
    return 1;
}

int feed_reload(Feed *f, const char *path)
{
    FILE *fp = fopen(path, "r");
    char  line[1024];

    struct { uint32_t id; char name[FEED_NAME_LEN]; } peers[FEED_MAX_PEERS];
    int peer_count = 0;

    f->ghost_count     = 0;
    f->malformed_count = 0;

    if (!fp) return 0;   /* no feed yet: zero ghosts, no error */

    while (fgets(line, sizeof(line), fp)) {
        long long id, from, to, unix_ms;
        size_t    len = strlen(line);

        /* A line without a newline is a partial trailing write from the
         * sync script — silently ignore it; next poll will see it whole. */
        if (len == 0 || line[len - 1] != '\n') break;

        /* Handshake line: remember the peer's display name. */
        if (get_i64(line, "hello", &id)) {
            if (peer_count < FEED_MAX_PEERS) {
                peers[peer_count].id = (uint32_t)id;
                if (!get_str(line, "name", peers[peer_count].name,
                             FEED_NAME_LEN))
                    snprintf(peers[peer_count].name, FEED_NAME_LEN, "peer");
                peer_count++;
            }
            continue;
        }

        /* Voyage line. */
        if (get_i64(line, "player", &id) &&
            get_i64(line, "from", &from) &&
            get_i64(line, "to", &to) &&
            get_i64(line, "departure_unix_ms", &unix_ms)) {
            GhostVoyage *g;
            int          j;

            /* Our own published lines come back through the merge;
             * showing yourself as a ghost would double every ship. */
            if ((uint32_t)id == f->my_id) continue;
            if (from < 0 || from >= MAX_ISLANDS ||
                to   < 0 || to   >= MAX_ISLANDS || from == to) {
                f->malformed_count++;
                continue;
            }
            if (f->ghost_count >= FEED_MAX_GHOSTS) continue;   /* capped */

            g = &f->ghosts[f->ghost_count++];
            g->player_id         = (uint32_t)id;
            g->from              = (int32_t)from;
            g->to                = (int32_t)to;
            g->departure_unix_ms = (uint64_t)unix_ms;
            snprintf(g->name, FEED_NAME_LEN, "peer-%04x",
                     (unsigned)(id & 0xffff));
            for (j = 0; j < peer_count; j++)
                if (peers[j].id == (uint32_t)id) {
                    snprintf(g->name, FEED_NAME_LEN, "%s", peers[j].name);
                    break;
                }
            continue;
        }

        f->malformed_count++;
    }

    fclose(fp);
    return f->ghost_count;
}

void feed_poll(Feed *f, uint64_t now_ns)
{
    if (now_ns < f->next_poll_ns) return;
    f->next_poll_ns = now_ns + (uint64_t)FEED_POLL_SECONDS * 1000000000ULL;
    feed_reload(f, FEED_IN_PATH);
}

float ghost_progress(const GhostVoyage *g, uint64_t unix_ms)
{
    uint64_t dur_ms = (uint64_t)(SHIP_VOYAGE_SECONDS * 1000.0f);
    uint64_t elapsed;

    if (unix_ms < g->departure_unix_ms) return -1.0f;   /* clock skew */
    elapsed = unix_ms - g->departure_unix_ms;
    if (elapsed >= dur_ms) return -1.0f;                /* long arrived */
    return (float)elapsed / (float)dur_ms;
}
