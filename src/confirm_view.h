#ifndef CONFIRM_VIEW_H
#define CONFIRM_VIEW_H

/* confirm_view.h  --  One confirmation, showing the command
 * (UI_PLAN Phase 6) */

#include <stdint.h>
#include "game.h"
#include "ui_kit.h"
#include "ui_snapshot.h"

#define CONFIRM_LINE_LEN   72
#define CONFIRM_MAX_LINES   3

typedef struct {
    char     title[40];
    char     lines[CONFIRM_MAX_LINES][CONFIRM_LINE_LEN];
    int32_t  line_count;

    /* The two payment options, or one when the action has no choice.
     * `label` is the human offer ("Pay 20 Wood, 150 Gold"); `preview`
     * is the command itself, decoded. */
    struct {
        char    label[40];
        char    preview[CONFIRM_LINE_LEN];
        uint8_t affordable;
    } options[2];
    int32_t  option_count;
    int32_t  chosen;

    /* The needs checklist (SUPPLY_CHAIN Phase 2). A house upgrades */
    struct {
        char    label[24];
        uint8_t met;
        /* Whether this is something the tier lives on or something. */
        uint8_t luxury;
    } needs[MAX_TIER_GOODS * 2];
    int32_t  need_count;
    /* Why the upgrade is refused, for the line under the list.
     * REJ_OK when it is not. */
    int32_t  refusal;

    uint64_t apply_tick;        /* when the sim would act on it        */
    uint8_t  destructive;       /* colour the accept button as a risk  */
    uint8_t  hue_r, hue_g, hue_b;
} ConfirmView;

/* Build the view for whatever confirmation is pending in the snapshot.
 * Produces an empty view (option_count 0) when nothing is open. */
void confirm_view_build(ConfirmView *out, const UiSnapshot *snap);

/* ---- geometry -------------------------------------------- */
#define CONFIRM_W        520.0f
#define CONFIRM_MARGIN    18.0f
#define CONFIRM_TITLE_H   38.0f
#define CONFIRM_LINE_H    22.0f
#define CONFIRM_OPTION_H  34.0f
#define CONFIRM_NEED_H    20.0f
#define CONFIRM_BTN_H     36.0f

void confirm_build(UiList *out, const ConfirmView *view,
                   float screen_w, float screen_h);

typedef enum {
    CONFIRM_HIT_NONE = 0,
    CONFIRM_HIT_OUTSIDE,     /* dismisses, like every other overlay */
    CONFIRM_HIT_CHOOSE,      /* `option` selects a payment          */
    CONFIRM_HIT_ACCEPT,
    CONFIRM_HIT_CANCEL
} ConfirmHitKind;

typedef struct {
    ConfirmHitKind kind;
    int            option;
} ConfirmHit;

ConfirmHit confirm_hit(const UiList *list, float x, float y);

#endif /* CONFIRM_VIEW_H */
