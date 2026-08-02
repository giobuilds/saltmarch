#ifndef VITALS_H
#define VITALS_H

/* vitals.h  --  What is wrong right now (UI_PLAN Phase 4) */

#include <stdint.h>
#include "ui_snapshot.h"

#define VITALS_MAX_ROWS   8
#define VITALS_TEXT_LEN  48

typedef enum {
    VITAL_INFO = 0,     /* worth knowing                              */
    VITAL_WARN,         /* something is not working                   */
    VITAL_ALERT         /* something is wrong with the game itself    */
} VitalSeverity;

typedef struct {
    uint8_t severity;               /* VitalSeverity                  */
    int32_t count;                  /* how many things (0 = n/a)      */
    char    text[VITALS_TEXT_LEN];
} VitalRow;

typedef struct {
    VitalRow rows[VITALS_MAX_ROWS];
    int32_t  row_count;
    int32_t  hidden;                /* rules that fired but did not fit */
} VitalsView;

/* Run every rule against one island and the client's health readings.
 * Rows come back most severe first; within a severity, in rule order,
 * which is fixed — so a row does not jump around the strip as counts
 * change. */
void vitals_build(VitalsView *out, const UiSnapshot *snap, int island);

/* Severity as a short word ("Warning"), for drawers and tests. */
const char *vitals_severity_name(VitalSeverity s);

#endif /* VITALS_H */
