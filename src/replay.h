#ifndef REPLAY_H
#define REPLAY_H

/* replay.h  --  The headless record/replay harness
 * (MMO_PLAN Phase 1d, formalised in Phase 6) */

#include <stdint.h>
#include <stdio.h>
#include "game.h"

/* A scripted session that exercises the float-sensitive paths on
 * purpose — a house (population and agents run), a purchase, a ship and
 * a voyage (progress accumulates) — then 500 ticks. Leaves gs holding
 * the finished world; --record saves it as a .smlog fixture. */
/* Record the determinism fixture into `gs`. Returns 1 if the session */
/* How long the determinism fixture runs: fifty years. */
#define DEMO_SESSION_TICKS 180000

int replay_record_demo_session(GameState *gs, uint32_t seed);

/* ---- the UI harness (UI_PLAN M1) --------------------------- */
int replay_verify_ui(GameState *gs, int verbose);

/* Serialise the widget lists at each recorded click to canonical text
 * on `out` — id, rect, label, one widget per line. Committing the
 * output makes a golden diff: pixel-free visual regression for layout
 * that moved when nobody meant it to. */
void replay_dump_ui(GameState *gs, FILE *out);

/* Record a session driven THROUGH THE UI: the trades are performed. */
void replay_record_ui_session(GameState *gs, uint32_t seed);

/* 1 if argv contains a mode flag this module handles (--record or
 * --replay), so the game binary knows to stay headless. */
int replay_cli_requested(int argc, char *argv[]);

/* Run the CLI. Returns a process exit code: 0 on success, 1 on failure
 * (unreadable file, nondeterministic replay, or a hash that does not
 * match --expect-hash). */
int replay_cli_run(int argc, char *argv[]);

/* One line of usage text for --help output, without a trailing newline. */
const char *replay_cli_usage(void);

#endif /* REPLAY_H */
