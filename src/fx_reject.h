#ifndef FX_REJECT_H
#define FX_REJECT_H

/* =========================================================
 * fx_reject.h  --  What happened to the thing I just clicked
 *                  (UI_PLAN M1, decision 3)
 *
 * A command applies at a tick boundary — 100ms later in single player,
 * several hundred milliseconds later through a co-op host. If it is
 * rejected, the click simply does nothing, and a player with no
 * feedback re-clicks and floods the log.
 *
 * So: when the UI submits, it remembers {seq, anchor} — the sequence
 * number command_submit stamped, and WHERE on screen the click came
 * from. When results come back from the tick, they are matched by
 * sequence and a short flash is drawn at that anchor, in the sim's own
 * rejection vocabulary.
 *
 * Two properties fall out of matching on the sequence rather than on
 * "the last thing I did":
 *
 *   - feedback is local by construction. A replayed command, or another
 *     player's, has no pending entry, so F9 and resync recompute
 *     hundreds of rejections silently — no special-casing needed.
 *   - it survives latency. The answer arrives whenever it arrives and
 *     still lands on the right tile.
 *
 * The matching and the decay live here, SDL-free and tested; the
 * drawing is render.c's (render_reject_flashes).
 * ========================================================= */

#include <stdint.h>
#include "game.h"
#include "ui_kit.h"

#define FX_MAX_PENDING  32
#define FX_MAX_FLASHES   8
#define FX_TEXT_LEN     40

/* How long a flash lives, in seconds. Long enough to read a short
 * phrase, short enough that it is gone before the next click. */
#define FX_FLASH_SECONDS 1.6f

typedef enum {
    FX_ANCHOR_NONE = 0,
    FX_ANCHOR_TILE,      /* a map tile — follows the camera            */
    FX_ANCHOR_SCREEN     /* a widget rect — fixed on screen            */
} FxAnchorKind;

typedef struct {
    FxAnchorKind kind;
    int32_t      row, col;   /* FX_ANCHOR_TILE                         */
    UiRect       rect;       /* FX_ANCHOR_SCREEN                       */
} FxAnchor;

typedef struct {
    FxAnchor anchor;
    char     text[FX_TEXT_LEN];
    float    life;           /* seconds remaining; <= 0 is dead        */
    uint8_t  reason;
} FxFlash;

typedef struct {
    struct { uint32_t seq; FxAnchor anchor; } pending[FX_MAX_PENDING];
    int      pending_count;

    FxFlash  flashes[FX_MAX_FLASHES];
    int      flash_count;
} FxReject;

void fx_reject_init(FxReject *fx);

/* Anchor helpers, so callers do not fill the struct by hand. */
FxAnchor fx_anchor_tile(int row, int col);
FxAnchor fx_anchor_rect(UiRect r);

/* "I just submitted command `seq`; if it fails, say so here." Dropping
 * the oldest when full is deliberate: an unanswered pending entry costs
 * nothing but a missed flash. */
void fx_reject_expect(FxReject *fx, uint32_t seq, FxAnchor anchor);

/* Drain the sim's results and turn the rejections among them into
 * flashes. Successes clear their pending entry silently — the world
 * visibly changing is the confirmation. Returns how many flashes were
 * raised. */
int  fx_reject_drain(FxReject *fx, GameState *gs);

/* Age the flashes. Cosmetic, so real seconds, not ticks. */
void fx_reject_update(FxReject *fx, float dt);

#endif /* FX_REJECT_H */
