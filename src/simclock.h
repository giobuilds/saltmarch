#ifndef SIMCLOCK_H
#define SIMCLOCK_H

/* simclock.h  --  The fixed-timestep clock constants
 * (MMO_PLAN Phase 1b) */

#define SIM_TICK_MS        100
#define SIM_TICKS_PER_SEC  10
#define SIM_TICK_NS        (SIM_TICK_MS * 1000000ULL)
#define SIM_TICK_SECONDS   (1.0f / (float)SIM_TICKS_PER_SEC)

#endif /* SIMCLOCK_H */
