#ifndef PEOPLE_VIEW_H
#define PEOPLE_VIEW_H

/* people_view.h  --  Who lives here, and how they are doing
 * (LIFE_PLAN Phase 9) */

#include <stdint.h>
#include "ui_kit.h"
#include "ui_snapshot.h"
#include "wellbeing.h"

/* The longest sentence wellbeing_describe() can form: a 31-character
 * name, a 23-character workplace, an age, a tenure and the fixed words
 * between them. Sized to hold it whole — a description cut mid-word
 * reads as a fault rather than as a limit. */
#define PEOPLE_LINE_LEN 112

/* The island's own score sits in UI_GROUP_FACTOR one past the last real
 * factor, so the drawer picks it out by identity like every other row. */
#define PEOPLE_FACTOR_TOTAL WB_FACTOR_COUNT

/* One member of the cast: the sentence describing them, and the six
 * factors behind their score. */
typedef struct {
    uint32_t  id;
    char      line[PEOPLE_LINE_LEN];
    Wellbeing wb;
} PeopleRow;

typedef struct {
    char      title[48];
    uint8_t   hue_r, hue_g, hue_b;
    int32_t   island;
    uint8_t   settled;
    uint8_t   detail_known;
    /* 1 when somebody lives here and we are told about them, which is
     * what makes `island_wb` a score rather than a row of zeroes. */
    uint8_t   scored;

    Wellbeing island_wb;

    int32_t   residents;
    int32_t   reserve;
    int32_t   homes_empty;
    int32_t   founder_allowance;
    int32_t   left_last_month;

    PeopleRow rows[UI_CAST_MAX];
    int32_t   row_count;
} PeopleView;

/* Build from one island's snapshot. Rebuilt each frame: the cast is
 * chosen by notability in ui_snapshot_build, and nothing on this screen
 * is clickable, so a row that changes place under the cursor costs
 * nothing. */
void people_view_build(PeopleView *v, const UiSnapshot *snap, int island);

/* ---- geometry ---------------------------------------------- */
#define PEOPLE_W          900.0f
#define PEOPLE_MARGIN      16.0f
#define PEOPLE_TITLE_H     40.0f
#define PEOPLE_SCORE_H     54.0f
#define PEOPLE_FACTOR_H    24.0f
#define PEOPLE_HEAD_H      22.0f
#define PEOPLE_ROW_H       28.0f
#define PEOPLE_ROW_GAP      3.0f
#define PEOPLE_FOOTER_H    40.0f
#define PEOPLE_MAX_H      860.0f

/* Where a factor bar's track starts, measured from the left edge of its
 * row: the name and the weight sit to the left of it. */
#define PEOPLE_BAR_X      220.0f
#define PEOPLE_BAR_W      460.0f

/* A cast row's inline six-segment bar, and the gap between the end of
 * the sentence and the start of it. Layout may never consult a font
 * metric (UI_PLAN Phase 0), so the sentence column is budgeted rather
 * than measured: 628px for at most 112 characters of 11pt prose. */
#define PEOPLE_ROW_BAR_W  180.0f
#define PEOPLE_ROW_BAR_PAD 54.0f

void people_build(UiList *out, const PeopleView *view,
                  float screen_w, float screen_h);

typedef enum {
    PEOPLE_HIT_NONE = 0,
    PEOPLE_HIT_OUTSIDE,
    PEOPLE_HIT_CLOSE
} PeopleHitKind;

typedef struct {
    PeopleHitKind kind;
    UiRect        rect;
} PeopleHit;

PeopleHit people_hit(const UiList *list, float x, float y);

#endif /* PEOPLE_VIEW_H */
