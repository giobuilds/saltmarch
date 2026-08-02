#ifndef SURVEY_H
#define SURVEY_H

/* survey.h  --  Sending a scholar to find the fast water
 * (MARITIME_PLAN Phase 3d: the survey mission) */

#include <stdint.h>

/* Missions one world may have in progress. Bounded like everything
 * else here; a player's own limit is their scholars and their boats. */
#define MAX_SURVEYS 32

/* How long an expedition takes, in sim ticks. Deliberately long
 * against a mean crossing of ~200: this is a season's work, not an
 * errand, and the scholar and the boat are committed for all of it. */
#define SURVEY_TICKS 900

/* How often it comes back with nothing, and how often "nothing"
 * means nobody came back at all. Both per mille, both derived. */
#define SURVEY_FAIL_PER_MILLE 350   /* 35% find no passage            */
#define SURVEY_LOSS_PER_MILLE 400   /* of those, 40% are lost at sea  */

typedef struct {
    int32_t  active;
    uint32_t owner;
    int32_t  from_island;    /* where it sailed from, and returns to  */
    int32_t  to_island;      /* the island it is looking for a way to */
    int32_t  route_id;       /* the passage it will chart, if it does */
    uint64_t finish_tick;
    int32_t  succeeds;       /* fixed at dispatch, applied at finish  */
    int32_t  lost;           /* the crew does not come home           */
} Survey;

typedef struct {
    Survey mission[MAX_SURVEYS];
    int32_t count;           /* high-water slot count, like buildings */
} SurveyBoard;

void survey_init(SurveyBoard *b);

/* Live missions belonging to `player`. */
int  survey_active_count(const SurveyBoard *b, uint32_t player);

/* Does this expedition find its passage, and if not, does it come
 * home? Pure functions of the mission's identity: the same arguments
 * always give the same answer, which is what makes a replay agree.
 * Exposed for the tests and for a UI that wants to explain the odds. */
int  survey_succeeds(uint32_t world_seed, int route_id, uint64_t start_tick,
                     uint32_t owner);
int  survey_is_lost(uint32_t world_seed, int route_id, uint64_t start_tick,
                    uint32_t owner);

#endif /* SURVEY_H */
