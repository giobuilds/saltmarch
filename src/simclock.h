#ifndef SIMCLOCK_H
#define SIMCLOCK_H

/* =========================================================
 * simclock.h  --  The fixed-timestep clock constants
 *                 (MMO_PLAN Phase 1b)
 *
 * The world advances in fixed integer ticks, decoupled from frame rate:
 * SIM_TICK_MS wall-milliseconds per tick, SIM_TICKS_PER_SEC ticks per
 * real second. Wall clock exists only at the frame edge (game_update's
 * accumulator turning elapsed real time into a whole number of ticks);
 * inside sim_run_one_tick nothing reads a clock, so the same command
 * log always produces the same world.
 *
 * SIM_TICK_SECONDS is the fixed per-tick step handed to the few sim
 * pieces still advanced as floats (agent movement, ship voyage
 * progress) — deterministic on one machine because it is a constant.
 * Discrete sim timers that the F9 hash reads (building production,
 * population needs) are integer tick counts instead.
 *
 * Its own header, SDL-free, so the sim subsystems (island, population,
 * ...) can share it without depending on game.h.
 * ========================================================= */

#define SIM_TICK_MS        100
#define SIM_TICKS_PER_SEC  10
#define SIM_TICK_NS        (SIM_TICK_MS * 1000000ULL)
#define SIM_TICK_SECONDS   (1.0f / (float)SIM_TICKS_PER_SEC)

#endif /* SIMCLOCK_H */
