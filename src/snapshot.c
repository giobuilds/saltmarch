/*  snapshot.c  --  Full world state as bytes
 *                  (SERVER.md, "Log truncation: the snapshot format")
 *
 *  See snapshot.h for what is captured and why the encoding is
 *  explicit rather than a struct dump.
 *
 *  The two halves below are written to be read side by side: every
 *  put_* in the writer has a get_* at the same position in the reader,
 *  in the same order. That symmetry IS the format specification —
 *  there is no schema anywhere else — so a field added to one half and
 *  forgotten in the other is the failure mode to watch for. The
 *  hash check at the end of decode is the backstop that turns such a
 *  mistake into a loud refusal rather than a world that quietly drifts.
 */

#include "snapshot.h"
#include "island.h"
#include "map.h"
#include "simlog.h"
#include "orderbook.h"

#include <stdlib.h>
#include <string.h>

#define SNAP_MAGIC 0x504E5300u   /* "\0SNP" */

/* magic(4) + version(2) + reserved(2) + checksum(4). Everything past
 * this is what the checksum covers. */
#define SNAP_HDR_BYTES 12u

/* ---- writer ------------------------------------------------
 * Grow-by-doubling byte sink. `bad` latches an allocation failure so
 * the caller checks once at the end rather than at every field. */
typedef struct {
    unsigned char *b;
    size_t         len, cap;
    int            bad;
} W;

static void w_bytes(W *w, const void *p, size_t n)
{
    if (w->bad) return;
    if (w->len + n > w->cap) {
        size_t         ncap = w->cap ? w->cap : 4096;
        unsigned char *nb;
        while (ncap < w->len + n) ncap *= 2;
        nb = (unsigned char *)realloc(w->b, ncap);
        if (!nb) { w->bad = 1; return; }
        w->b = nb;
        w->cap = ncap;
    }
    memcpy(w->b + w->len, p, n);
    w->len += n;
}

static void w_u8(W *w, uint8_t v) { w_bytes(w, &v, 1); }

static void w_u16(W *w, uint16_t v)
{
    unsigned char t[2];
    t[0] = (unsigned char)(v);
    t[1] = (unsigned char)(v >> 8);
    w_bytes(w, t, 2);
}

static void w_u32(W *w, uint32_t v)
{
    unsigned char t[4];
    t[0] = (unsigned char)(v);
    t[1] = (unsigned char)(v >> 8);
    t[2] = (unsigned char)(v >> 16);
    t[3] = (unsigned char)(v >> 24);
    w_bytes(w, t, 4);
}

static void w_u64(W *w, uint64_t v)
{
    w_u32(w, (uint32_t)(v & 0xFFFFFFFFu));
    w_u32(w, (uint32_t)(v >> 32));
}

static void w_i16(W *w, int16_t v) { w_u16(w, (uint16_t)v); }
static void w_i32(W *w, int32_t v) { w_u32(w, (uint32_t)v); }

/* IEEE-754 bits. The sim's floats are agent positions and state timers;
 * storing the bit pattern rather than a decimal rendering is what makes
 * a reloaded agent stand exactly where it stood, which integer-only
 * hashing would otherwise catch as a desync. */
static void w_f32(W *w, float v)
{
    uint32_t bits;
    memcpy(&bits, &v, 4);
    w_u32(w, bits);
}

/* ---- reader ------------------------------------------------
 * Bounds-checked mirror of the writer. `bad` latches an overrun; every
 * getter past that point returns zero, so a truncated buffer produces
 * a zeroed tail and one failed check rather than a read off the end. */
typedef struct {
    const unsigned char *b;
    size_t               len, off;
    int                  bad;
} R;

static void r_bytes(R *r, void *p, size_t n)
{
    if (r->bad || r->off + n > r->len) { r->bad = 1; memset(p, 0, n); return; }
    memcpy(p, r->b + r->off, n);
    r->off += n;
}

static uint8_t r_u8(R *r) { uint8_t v; r_bytes(r, &v, 1); return v; }

static uint16_t r_u16(R *r)
{
    unsigned char t[2];
    r_bytes(r, t, 2);
    return (uint16_t)((uint16_t)t[0] | ((uint16_t)t[1] << 8));
}

static uint32_t r_u32(R *r)
{
    unsigned char t[4];
    r_bytes(r, t, 4);
    return (uint32_t)t[0] | ((uint32_t)t[1] << 8)
         | ((uint32_t)t[2] << 16) | ((uint32_t)t[3] << 24);
}

static uint64_t r_u64(R *r)
{
    uint64_t lo = r_u32(r);
    uint64_t hi = r_u32(r);
    return lo | (hi << 32);
}

static int16_t r_i16(R *r) { return (int16_t)r_u16(r); }
static int32_t r_i32(R *r) { return (int32_t)r_u32(r); }

static float r_f32(R *r)
{
    uint32_t bits = r_u32(r);
    float    v;
    memcpy(&v, &bits, 4);
    return v;
}

/* ---- the world, field by field -----------------------------
 * Each pair below writes and reads one struct. Keep them adjacent: the
 * only thing keeping the two in step is that they are read together. */

static void put_stockpile(W *w, const Stockpile *s)
{
    int i;
    for (i = 0; i < RES_COUNT; i++) w_i32(w, (int32_t)s->amount[i]);
    w_i32(w, (int32_t)s->capacity);
}

static void get_stockpile(R *r, Stockpile *s)
{
    int i;
    for (i = 0; i < RES_COUNT; i++) s->amount[i] = (int)r_i32(r);
    s->capacity = (int)r_i32(r);
}

/* FNV-1a over the payload. The sim_hash the snapshot carries is a
 * SEMANTIC check -- it catches an encoder that writes a field the
 * decoder skips -- but it can only see what sim_hash itself covers,
 * which is not everything in here (agents, for one, are world state
 * that no hash reads). This is the blunt one: any flipped byte
 * anywhere in the buffer fails it. */
static uint32_t fnv1a(const unsigned char *p, size_t n)
{
    uint32_t h = 2166136261u;
    size_t   i;
    for (i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static void put_building(W *w, const Building *b)
{
    w_i32(w, (int32_t)b->type);
    w_i32(w, (int32_t)b->row);
    w_i32(w, (int32_t)b->col);
    w_u8(w,  (uint8_t)(b->active ? 1 : 0));
    w_u32(w, b->timer);
    /* connected and worker_count are recomputed every frame before
     * anything reads them, so they are not load-bearing — but they ARE
     * hashed, and a snapshot whose hash did not match the world it came
     * from would fail its own verification. Cheaper to store two ints
     * than to special-case the check. */
    w_i32(w, (int32_t)b->connected);
    w_i32(w, (int32_t)b->worker_count);
}

static void get_building(R *r, Building *b)
{
    b->type         = (BuildingType)r_i32(r);
    b->row          = (int)r_i32(r);
    b->col          = (int)r_i32(r);
    b->active       = (int)r_u8(r);
    b->timer        = r_u32(r);
    b->connected    = (int)r_i32(r);
    b->worker_count = (int)r_i32(r);
}

static void put_pop(W *w, const PopData *p)
{
    w_u8(w,  (uint8_t)(p->active ? 1 : 0));
    w_i32(w, (int32_t)p->residents);
    w_u32(w, p->timer);
    w_u8(w,  (uint8_t)(p->happy ? 1 : 0));
}

static void get_pop(R *r, PopData *p)
{
    p->active    = (int)r_u8(r);
    p->residents = (int)r_i32(r);
    p->timer     = r_u32(r);
    p->happy     = (int)r_u8(r);
}

/* Only path_len waypoints, not MAX_AGENT_PATH. This one decision is
 * most of why a snapshot is tens of KB rather than megabytes: an agent
 * standing still carries a 1020-byte array of nothing. */
static void put_agent(W *w, const Agent *a)
{
    int i, n = a->path_len;

    if (n < 0) n = 0;
    if (n > MAX_AGENT_PATH) n = MAX_AGENT_PATH;

    w_u8(w,  (uint8_t)(a->active ? 1 : 0));
    w_i32(w, (int32_t)a->home_idx);
    w_i32(w, (int32_t)a->work_idx);
    w_i32(w, (int32_t)a->state);
    w_f32(w, a->row);
    w_f32(w, a->col);
    w_f32(w, a->state_timer);
    w_i32(w, (int32_t)a->path_pos);
    w_i32(w, (int32_t)n);
    for (i = 0; i < n; i++) {
        w_i32(w, (int32_t)a->path[i].r);
        w_i32(w, (int32_t)a->path[i].c);
    }
}

static int get_agent(R *r, Agent *a)
{
    int i, n;

    memset(a, 0, sizeof(*a));
    a->active      = (int)r_u8(r);
    a->home_idx    = (int)r_i32(r);
    a->work_idx    = (int)r_i32(r);
    a->state       = (AgentState)r_i32(r);
    a->row         = r_f32(r);
    a->col         = r_f32(r);
    a->state_timer = r_f32(r);
    a->path_pos    = (int)r_i32(r);
    n              = (int)r_i32(r);

    if (n < 0 || n > MAX_AGENT_PATH) return 0;
    a->path_len = n;
    for (i = 0; i < n; i++) {
        a->path[i].r = (int)r_i32(r);
        a->path[i].c = (int)r_i32(r);
    }
    return 1;
}

static void put_ship(W *w, const Ship *s)
{
    int i;
    w_u8(w,  (uint8_t)(s->active ? 1 : 0));
    w_u32(w, s->owner);
    w_i32(w, (int32_t)s->at_island);
    w_i32(w, (int32_t)s->from_island);
    w_i32(w, (int32_t)s->to_island);
    w_u64(w, s->departure_tick);
    w_f32(w, s->progress);
    for (i = 0; i < RES_COUNT; i++) w_i32(w, (int32_t)s->cargo[i]);
    w_i32(w, (int32_t)s->insured);
    w_i32(w, s->insured_value);
    w_i32(w, (int32_t)s->was_at_sea);
    w_i32(w, (int32_t)s->route_active);
    w_i32(w, (int32_t)s->route_a);
    w_i32(w, (int32_t)s->route_b);
    w_i32(w, (int32_t)s->route_res_ab);
    w_i32(w, (int32_t)s->route_res_ba);
    w_i32(w, (int32_t)s->route_qty);
    w_i32(w, (int32_t)s->route_leg);
}

static void get_ship(R *r, Ship *s)
{
    int i;
    memset(s, 0, sizeof(*s));
    s->active         = (int)r_u8(r);
    s->owner          = r_u32(r);
    s->at_island      = (int)r_i32(r);
    s->from_island    = (int)r_i32(r);
    s->to_island      = (int)r_i32(r);
    s->departure_tick = r_u64(r);
    s->progress       = r_f32(r);
    for (i = 0; i < RES_COUNT; i++) s->cargo[i] = (int)r_i32(r);
    s->insured       = (int)r_i32(r);
    s->insured_value = r_i32(r);
    s->was_at_sea    = (int)r_i32(r);
    s->route_active  = (int)r_i32(r);
    s->route_a       = (int)r_i32(r);
    s->route_b       = (int)r_i32(r);
    s->route_res_ab  = (ResourceType)r_i32(r);
    s->route_res_ba  = (ResourceType)r_i32(r);
    s->route_qty     = (int)r_i32(r);
    s->route_leg     = (int)r_i32(r);
}

/* The order book (MARITIME_PLAN Phase 2). Live entries only — an
 * inactive slot holds nothing the world needs, and writing its stale
 * bytes would put noise in a buffer whose checksum has to mean
 * something. Slot identity is not preserved across a restore, which is
 * safe because nothing refers to an order by slot: orders are found by
 * id, and the matcher re-derives its ordering from price and time. */
static void put_orderbook(W *w, const OrderBook *b)
{
    int i;

    w_i32(w, (int32_t)orderbook_open_live(b));
    w_u32(w, b->next_order_id);
    for (i = 0; i < b->order_count; i++) {
        const Order *o = &b->order[i];
        if (!o->active) continue;
        w_u32(w, o->id);
        w_u32(w, o->owner);
        w_i32(w, o->island);
        w_u16(w, o->what.kind);
        w_u16(w, o->what.id);
        w_i32(w, o->side);
        w_i32(w, o->qty);
        w_i32(w, o->limit);
        w_i32(w, o->reserved_gold);
        w_u64(w, o->placed_tick);
    }

    w_i32(w, (int32_t)orderbook_booking_live(b));
    for (i = 0; i < b->booking_count; i++) {
        const Booking *bk = &b->booking[i];
        if (!bk->active) continue;
        w_u16(w, bk->what.kind);
        w_u16(w, bk->what.id);
        w_i32(w, bk->qty);
        w_i32(w, bk->price);
        w_i32(w, bk->from_island);
        w_i32(w, bk->to_island);
        w_u32(w, bk->buyer);
        w_u32(w, bk->seller);
        w_u64(w, bk->arrive_tick);
    }
}

static int get_orderbook(R *r, OrderBook *b)
{
    int i, n;

    orderbook_init(b);

    n = (int)r_i32(r);
    b->next_order_id = r_u32(r);
    if (r->bad || n < 0 || n > ORDERBOOK_MAX_ORDERS) return 0;
    b->order_count = n;
    for (i = 0; i < n; i++) {
        Order *o = &b->order[i];
        o->active        = 1;
        o->id            = r_u32(r);
        o->owner         = r_u32(r);
        o->island        = r_i32(r);
        o->what.kind     = r_u16(r);
        o->what.id       = r_u16(r);
        o->side          = r_i32(r);
        o->qty           = r_i32(r);
        o->limit         = r_i32(r);
        o->reserved_gold = r_i32(r);
        o->placed_tick   = r_u64(r);
    }

    n = (int)r_i32(r);
    if (r->bad || n < 0 || n > ORDERBOOK_MAX_BOOKINGS) return 0;
    b->booking_count = n;
    for (i = 0; i < n; i++) {
        Booking *bk = &b->booking[i];
        bk->active      = 1;
        bk->what.kind   = r_u16(r);
        bk->what.id     = r_u16(r);
        bk->qty         = r_i32(r);
        bk->price       = r_i32(r);
        bk->from_island = r_i32(r);
        bk->to_island   = r_i32(r);
        bk->buyer       = r_u32(r);
        bk->seller      = r_u32(r);
        bk->arrive_tick = r_u64(r);
    }
    return !r->bad;
}

static void put_faction(W *w, const Faction *f)
{
    int i, j;
    w_i32(w, f->gold);
    for (i = 0; i < RES_COUNT; i++) w_i32(w, f->inventory[i]);
    w_u32(w, f->revert_timer);
    for (i = 0; i < MAX_ISLANDS_FOR_LANES; i++)
        for (j = 0; j < MAX_ISLANDS_FOR_LANES; j++)
            w_i16(w, f->lane_premium[i][j]);
    for (i = 0; i < RES_COUNT; i++)
        for (j = 0; j < FACTION_HIST_LEN; j++)
            w_i16(w, f->hist[i][j]);
    w_u16(w, f->hist_head);
    w_u16(w, f->hist_count);
    w_u32(w, f->hist_timer);
}

static void get_faction(R *r, Faction *f)
{
    int i, j;
    memset(f, 0, sizeof(*f));
    f->gold = r_i32(r);
    for (i = 0; i < RES_COUNT; i++) f->inventory[i] = r_i32(r);
    f->revert_timer = r_u32(r);
    for (i = 0; i < MAX_ISLANDS_FOR_LANES; i++)
        for (j = 0; j < MAX_ISLANDS_FOR_LANES; j++)
            f->lane_premium[i][j] = r_i16(r);
    for (i = 0; i < RES_COUNT; i++)
        for (j = 0; j < FACTION_HIST_LEN; j++)
            f->hist[i][j] = r_i16(r);
    f->hist_head  = r_u16(r);
    f->hist_count = r_u16(r);
    f->hist_timer = r_u32(r);
}

static void put_island(W *w, const Island *isl)
{
    int i;
    int nb = isl->building_count;
    int na = isl->agent_count;

    w_u8(w, (uint8_t)(isl->settled ? 1 : 0));
    w_i32(w, (int32_t)isl->profile);
    w_bytes(w, isl->name, ISLAND_NAME_LEN);
    w_u32(w, isl->owner);
    w_i32(w, (int32_t)isl->docking_allowed);
    w_u32(w, isl->charter_timer);
    w_i32(w, isl->charter_arrears);
    for (i = 0; i < RES_COUNT; i++) w_i32(w, isl->escrow[i]);
    w_i32(w, (int32_t)isl->agent_assign_timer);
    put_stockpile(w, &isl->stockpile);

    /* The map is (seed, profile) and nothing else.
     *
     * Terrain is IMMUTABLE once generated: outside map.c's own
     * generation passes nothing in the tree writes map.tiles at all.
     * Placement does not mark tiles -- occupancy is derived from
     * buildings[] -- so a road changes no terrain, and there is nothing
     * for a settled island's map to have drifted into. Storing the
     * grid would be storing a pure function of these eight bytes: it
     * was 32 KB per island of the first draft of this format, nine
     * tenths of the whole snapshot, and every byte of it recomputable.
     *
     * If terrain ever becomes mutable -- terraforming, depletion of a
     * deposit -- this is the line that has to change, and the tiles
     * come back. */
    w_u32(w, isl->map.seed);
    w_i32(w, (int32_t)isl->map.profile);

    w_i32(w, (int32_t)nb);
    for (i = 0; i < nb; i++) {
        put_building(w, &isl->buildings[i]);
        put_pop(w, &isl->pop_data[i]);
    }

    w_i32(w, (int32_t)na);
    for (i = 0; i < na; i++) put_agent(w, &isl->agents[i]);
}

static int get_island(R *r, Island *isl)
{
    int i, nb, na;

    memset(isl, 0, sizeof(*isl));

    isl->settled = (int)r_u8(r);
    isl->profile = (MapProfile)r_i32(r);
    r_bytes(r, isl->name, ISLAND_NAME_LEN);
    isl->name[ISLAND_NAME_LEN - 1] = '\0';
    isl->owner           = r_u32(r);
    isl->docking_allowed = (int)r_i32(r);
    isl->charter_timer   = r_u32(r);
    isl->charter_arrears = r_i32(r);
    for (i = 0; i < RES_COUNT; i++) isl->escrow[i] = r_i32(r);
    isl->agent_assign_timer = (int)r_i32(r);
    get_stockpile(r, &isl->stockpile);

    {
        /* Regenerated, not restored -- see put_island. Deterministic in
         * (seed, profile), and terrain is immutable, so this produces
         * the identical grid the snapshot was taken from. */
        uint32_t   mseed = r_u32(r);
        MapProfile mprof = (MapProfile)r_i32(r);

        if (r->bad) return 0;
        map_init(&isl->map, mseed, mprof);
    }

    /* The camera is view state, not world state, so it is not in the
     * buffer. Give the island a valid one rather than the zeroed
     * struct memset left behind. */
    camera_init(&isl->camera, SCREEN_W, SCREEN_H, MAP_COLS, MAP_ROWS);

    nb = (int)r_i32(r);
    if (r->bad || nb < 0 || nb > MAX_BUILDINGS) return 0;
    isl->building_count = nb;
    for (i = 0; i < nb; i++) {
        get_building(r, &isl->buildings[i]);
        get_pop(r, &isl->pop_data[i]);
    }

    na = (int)r_i32(r);
    if (r->bad || na < 0 || na > MAX_AGENTS) return 0;
    isl->agent_count = na;
    for (i = 0; i < na; i++)
        if (!get_agent(r, &isl->agents[i])) return 0;

    return !r->bad;
}

/* ---- the public halves ------------------------------------- */

int snapshot_encode(const GameState *gs, unsigned char **out, size_t *out_len)
{
    W   w;
    int i;

    memset(&w, 0, sizeof(w));

    w_u32(&w, SNAP_MAGIC);
    w_u16(&w, (uint16_t)SNAPSHOT_VERSION);
    w_u16(&w, 0);                       /* reserved: keeps the 8-align */
    w_u32(&w, 0);                       /* payload checksum, patched below */
    w_u64(&w, gs->sim_tick_no);
    w_u32(&w, gs->world_seed);
    w_i32(&w, (int32_t)gs->current_island);
    /* What this world hashes to. Decode recomputes and compares. */
    w_u64(&w, sim_hash(gs));

    put_faction(&w, &gs->faction);
    put_orderbook(&w, &gs->book);

    w_i32(&w, (int32_t)gs->ship_count);
    for (i = 0; i < gs->ship_count; i++) put_ship(&w, &gs->ships[i]);

    w_i32(&w, (int32_t)MAX_ISLANDS);
    for (i = 0; i < MAX_ISLANDS; i++) put_island(&w, &gs->islands[i]);

    if (w.bad) { free(w.b); return 0; }

    /* Patch the checksum over everything that follows it. */
    {
        uint32_t sum = fnv1a(w.b + SNAP_HDR_BYTES, w.len - SNAP_HDR_BYTES);
        w.b[8]  = (unsigned char)(sum);
        w.b[9]  = (unsigned char)(sum >> 8);
        w.b[10] = (unsigned char)(sum >> 16);
        w.b[11] = (unsigned char)(sum >> 24);
    }

    *out     = w.b;
    *out_len = w.len;
    return 1;
}

int snapshot_peek_tick(const unsigned char *buf, size_t len,
                       uint64_t *out_tick)
{
    R r;
    uint64_t tick;

    if (!buf || len < 16) return 0;
    r.b = buf; r.len = len; r.off = 0; r.bad = 0;

    if (r_u32(&r) != SNAP_MAGIC) return 0;
    if (r_u16(&r) != SNAPSHOT_VERSION) return 0;
    (void)r_u16(&r);
    (void)r_u32(&r);            /* checksum: snapshot_decode's business */
    tick = r_u64(&r);
    if (r.bad) return 0;

    *out_tick = tick;
    return 1;
}

int snapshot_decode(GameState *gs, const unsigned char *buf, size_t len)
{
    R         r;
    GameState *tmp;
    uint64_t  claimed_hash, actual;
    int       i, n_isl;

    if (!buf) return 0;
    r.b = buf; r.len = len; r.off = 0; r.bad = 0;

    if (r_u32(&r) != SNAP_MAGIC) {
        sim_log("snapshot: not a snapshot");
        return 0;
    }
    if (r_u16(&r) != SNAPSHOT_VERSION) {
        sim_log("snapshot: version this build does not read");
        return 0;
    }
    (void)r_u16(&r);

    /* Integrity before meaning. Checked first because everything after
     * it trusts the bytes: counts become loop bounds and indices, and a
     * buffer that has been damaged in transit should be rejected as
     * damaged rather than diagnosed as some subtler disagreement. */
    {
        uint32_t claimed_sum = r_u32(&r);
        if (r.bad || len < SNAP_HDR_BYTES) return 0;
        if (fnv1a(buf + SNAP_HDR_BYTES, len - SNAP_HDR_BYTES)
            != claimed_sum) {
            sim_log("snapshot: checksum mismatch — buffer is corrupt");
            return 0;
        }
    }

    /* Decoded into scratch first. A snapshot that turns out to be
     * truncated or mis-hashed must leave the caller's world exactly as
     * it was: a half-applied world is worse than a refused one, and on
     * a guest it would be a silent desync rather than a failed join. */
    tmp = (GameState *)calloc(1, sizeof(GameState));
    if (!tmp) return 0;

    tmp->sim_tick_no    = r_u64(&r);
    tmp->world_seed     = r_u32(&r);
    tmp->current_island = (int)r_i32(&r);
    claimed_hash        = r_u64(&r);

    get_faction(&r, &tmp->faction);
    if (!get_orderbook(&r, &tmp->book)) goto bad;

    tmp->ship_count = (int)r_i32(&r);
    if (r.bad || tmp->ship_count < 0 || tmp->ship_count > MAX_SHIPS)
        goto bad;
    for (i = 0; i < tmp->ship_count; i++) get_ship(&r, &tmp->ships[i]);

    n_isl = (int)r_i32(&r);
    if (r.bad || n_isl != MAX_ISLANDS) goto bad;
    for (i = 0; i < MAX_ISLANDS; i++)
        if (!get_island(&r, &tmp->islands[i])) goto bad;

    if (r.bad) goto bad;
    if (tmp->current_island < 0 || tmp->current_island >= MAX_ISLANDS)
        tmp->current_island = 0;

    /* The claim, checked. sim_hash reads only the fields restored
     * above, so this catches a field the writer emits and the reader
     * skips just as surely as it catches a corrupt byte. */
    actual = sim_hash(tmp);
    if (actual != claimed_hash) {
        sim_log("snapshot: hash mismatch at tick %llu "
                "(stored %016llx, decoded %016llx) — refusing",
                (unsigned long long)tmp->sim_tick_no,
                (unsigned long long)claimed_hash,
                (unsigned long long)actual);
        goto bad;
    }

    /* The sea is a pure function of the seed, like every Map, so it is
     * regenerated rather than carried in the buffer. */
    sea_init(&tmp->sea, tmp->world_seed, MAX_ISLANDS);

    /* Commit. The world moves across; everything the caller owns that
     * is NOT world state (its command log, its net session, its
     * identity, its UI) is deliberately left alone. */
    memcpy(gs->islands, tmp->islands, sizeof(gs->islands));
    memcpy(gs->ships,   tmp->ships,   sizeof(gs->ships));
    gs->ship_count     = tmp->ship_count;
    gs->faction        = tmp->faction;
    gs->book           = tmp->book;
    gs->sea            = tmp->sea;
    gs->sim_tick_no    = tmp->sim_tick_no;
    gs->world_seed     = tmp->world_seed;
    gs->current_island = tmp->current_island;

    free(tmp);
    return 1;

bad:
    sim_log("snapshot: buffer is truncated or out of range — refusing");
    free(tmp);
    return 0;
}
