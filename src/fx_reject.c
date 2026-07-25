/*  fx_reject.c  --  Matching results to clicks (UI_PLAN M1)  */

#include "fx_reject.h"
#include <string.h>

void fx_reject_init(FxReject *fx)
{
    memset(fx, 0, sizeof(*fx));
}

FxAnchor fx_anchor_tile(int row, int col)
{
    FxAnchor a;
    memset(&a, 0, sizeof(a));
    a.kind = FX_ANCHOR_TILE;
    a.row  = row;
    a.col  = col;
    return a;
}

FxAnchor fx_anchor_rect(UiRect r)
{
    FxAnchor a;
    memset(&a, 0, sizeof(a));
    a.kind = FX_ANCHOR_SCREEN;
    a.rect = r;
    return a;
}

void fx_reject_expect(FxReject *fx, uint32_t seq, FxAnchor anchor)
{
    int i;

    if (seq == 0) return;   /* not a submitted command */

    /* Full: drop the oldest. A pending entry that never gets an answer
     * costs one missed flash; refusing to record the newest would cost
     * feedback on the click the player just made. */
    if (fx->pending_count >= FX_MAX_PENDING) {
        for (i = 1; i < fx->pending_count; i++)
            fx->pending[i - 1] = fx->pending[i];
        fx->pending_count--;
    }

    fx->pending[fx->pending_count].seq    = seq;
    fx->pending[fx->pending_count].anchor = anchor;
    fx->pending_count++;
}

static void raise_flash(FxReject *fx, FxAnchor anchor, RejectReason reason)
{
    FxFlash *f;
    int      i;

    if (fx->flash_count >= FX_MAX_FLASHES) {
        for (i = 1; i < fx->flash_count; i++)
            fx->flashes[i - 1] = fx->flashes[i];
        fx->flash_count--;
    }

    f = &fx->flashes[fx->flash_count++];
    memset(f, 0, sizeof(*f));
    f->anchor = anchor;
    f->life   = FX_FLASH_SECONDS;
    f->reason = (uint8_t)reason;
    {
        const char *t = ui_reject_text(reason);
        size_t      n = strlen(t);
        if (n >= FX_TEXT_LEN) n = FX_TEXT_LEN - 1;
        memcpy(f->text, t, n);
        f->text[n] = '\0';
    }
}

int fx_reject_drain(FxReject *fx, GameState *gs)
{
    SimResult results[SIM_RESULT_RING];
    int       n, i, j, raised = 0;

    n = sim_results_drain(gs, results, SIM_RESULT_RING);

    for (i = 0; i < n; i++) {
        const SimResult *r = &results[i];

        /* Ours only. Another client's sequence numbers occupy the same
         * small integers as ours, so the player id is half the key. */
        if (r->player_id != gs->local_player_id) continue;

        for (j = 0; j < fx->pending_count; j++) {
            if (fx->pending[j].seq != r->seq) continue;

            if (r->reason != REJ_OK)
                raised += (raise_flash(fx, fx->pending[j].anchor,
                                       (RejectReason)r->reason), 1);

            /* Answered either way: remove it. A success needs no flash
             * — the world changing IS the feedback. */
            for (; j + 1 < fx->pending_count; j++)
                fx->pending[j] = fx->pending[j + 1];
            fx->pending_count--;
            break;
        }
    }
    return raised;
}

void fx_reject_update(FxReject *fx, float dt)
{
    int i = 0;

    while (i < fx->flash_count) {
        fx->flashes[i].life -= dt;
        if (fx->flashes[i].life <= 0.0f) {
            int j;
            for (j = i + 1; j < fx->flash_count; j++)
                fx->flashes[j - 1] = fx->flashes[j];
            fx->flash_count--;
            continue;    /* same index now holds the next flash */
        }
        i++;
    }
}
