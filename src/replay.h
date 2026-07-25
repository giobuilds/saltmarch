#ifndef REPLAY_H
#define REPLAY_H

/* =========================================================
 * replay.h  --  The headless record/replay harness
 *               (MMO_PLAN Phase 1d, formalised in Phase 6)
 *
 * A world is (seed, ordered command log). Replaying one and hashing the
 * result is therefore the project's determinism test — and, run on
 * three operating systems, its cross-platform float divergence fuzzer.
 *
 * This lives in the SDL-free sim library so the same code serves two
 * front ends: `saltmarch --replay ...` (the game binary short-circuits
 * before opening a window) and `saltmarch_replay`, the standalone CLI
 * that links no SDL at all and is what CI actually runs.
 * ========================================================= */

#include <stdint.h>
#include <stdio.h>
#include "game.h"

/* A scripted session that exercises the float-sensitive paths on
 * purpose — a house (population and agents run), a purchase, a ship and
 * a voyage (progress accumulates) — then 500 ticks. Leaves gs holding
 * the finished world; --record saves it as a .smlog fixture. */
void replay_record_demo_session(GameState *gs, uint32_t seed);

/* ---- the UI harness (UI_PLAN M1) ---------------------------
 * Replays a recorded session and, at every recorded click, rebuilds the
 * snapshot that frame was drawn from and drives the REAL overlay
 * builders and hit-tests against it.
 *
 * Two things are asserted. First, geometry: every widget the UI
 * produced lies inside the screen — the "the Prev button moved
 * off-page at 27 goods" class, caught without a display. Second,
 * emission: where a click produced a command, hit-testing the recorded
 * position must yield exactly that command's payload.
 *
 * The second check is currently limited to the exchange screen, which
 * is the surface whose click-to-command mapping is a pure function
 * today. Map clicks and the confirm popup route through main.c's
 * cascade, which is SDL-side; extracting it is what would widen this,
 * and is deliberately not done as a side effect of writing the harness.
 *
 * Returns 0 if everything checked out, 1 otherwise; `verbose` prints
 * each mismatch. */
int replay_verify_ui(GameState *gs, int verbose);

/* Serialise the widget lists at each recorded click to canonical text
 * on `out` — id, rect, label, one widget per line. Committing the
 * output makes a golden diff: pixel-free visual regression for layout
 * that moved when nobody meant it to. */
void replay_dump_ui(GameState *gs, FILE *out);

/* Record a session driven THROUGH THE UI: the trades are performed by
 * hit-testing the real exchange screen at real widget positions, and
 * each click is written to the intent log beside the command it
 * produced. The result is a fixture that `--replay --verify-ui` can
 * check, and one that fails if a widget ever moves out from under the
 * coordinates it was recorded at.
 *
 * Separate from replay_record_demo_session() so the determinism fixture
 * keeps its exact command stream (and its known hash) while this one is
 * free to change. */
void replay_record_ui_session(GameState *gs, uint32_t seed);

/* 1 if argv contains a mode flag this module handles (--record or
 * --replay), so the game binary knows to stay headless. */
int replay_cli_requested(int argc, char *argv[]);

/* Run the CLI. Returns a process exit code: 0 on success, 1 on failure
 * (unreadable file, nondeterministic replay, or a hash that does not
 * match --expect-hash).
 *
 *   --record FILE [--seed N]        write a fixture
 *   --replay FILE [--expect-hash H] load, self-check, optionally pin
 */
int replay_cli_run(int argc, char *argv[]);

/* One line of usage text for --help output, without a trailing newline. */
const char *replay_cli_usage(void);

#endif /* REPLAY_H */
