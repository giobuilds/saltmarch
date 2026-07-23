#ifndef NET_H
#define NET_H

/* =========================================================
 * net.h  --  Lockstep two-player co-op over raw TCP
 *            (MMO_PLAN Phase 5)
 *
 * Both clients run the identical deterministic sim; "multiplayer" is
 * nothing more than agreeing on the command log's order. The HOST is
 * the ordering authority:
 *
 *   - Every submitted Command (the host's own included) is stamped by
 *     the host with tick = host_tick + NET_CMD_DELAY_TICKS and its
 *     author's identity taken from the CONNECTION, never the payload.
 *     Stamped commands are appended to the host's log and broadcast;
 *     the guest appends them verbatim. Both sides apply them at the
 *     stamped tick boundary, identically.
 *   - The guest may only simulate through ticks the host has
 *     authorised (MSG_TICK_AUTH, sent as the host completes each
 *     tick). TCP ordering guarantees every command stamped <= T was
 *     sent before T's authorisation, so an authorised tick is a
 *     complete tick. The delay buffer absorbs latency jitter.
 *   - The guest reports sim_hash at fixed intervals; the host compares
 *     against its own hash at the same tick. A mismatch is answered
 *     with MSG_WORLD — the full (seed, tick, log), the same shape as a
 *     v6 save — and the guest rebuilds by replay. Never state-patching.
 *   - Joining IS a resync: HELLO -> WELCOME (your player id) ->
 *     MSG_WORLD, then the host grants the guest a starting island
 *     through the funnel (CMD_GRANT_START) if it owns nothing.
 *
 * The sim stays network-ignorant: nothing under sim_apply/sim_run_one_
 * tick knows the session exists. The two touch points are
 * command_submit (routes submissions through net_submit_local when a
 * session is attached) and game.c's tick loop (asks net_tick_allowed
 * before running a tick). A NetSession lives in App and is referenced
 * from GameState as an opaque pointer — client infrastructure, never
 * hashed, never saved.
 *
 * Disconnect (either side, including a yanked cable) degrades to
 * single-player continuation: the session is torn down, the world
 * stays, and submissions revert to local stamping.
 * ========================================================= */

#include "game.h"   /* GameState (game.h only forward-declares us)     */
#include <stdint.h>

typedef struct NetSession NetSession;

#define NET_DEFAULT_PORT      7777
#define NET_PROTO_VERSION     1u
/* Commands apply this many ticks after the host stamps them (400ms at
 * 10 ticks/sec): the latency-jitter absorber. Co-op tolerance is high. */
#define NET_CMD_DELAY_TICKS   4
/* The guest reports its hash every this many ticks (5s). */
#define NET_HASH_INTERVAL     50

/* Host a session: listen on `port`, then accept one guest from inside
 * net_pump. Returns NULL on failure (port busy, no sockets). */
NetSession *net_host(uint16_t port);

/* Join a session at host:port. Blocks briefly for the connect and the
 * world download, then returns with gs untouched — the world arrives
 * through net_pump. Returns NULL on failure. */
NetSession *net_join(const char *host, uint16_t port);

/* Create a connected host+guest pair over an IN-MEMORY transport — the
 * same sessions, protocol, framing and pump flow as TCP, with the socket
 * layer swapped for two byte queues. Exists so the lockstep protocol is
 * unit-testable deterministically in any environment (some sandboxes
 * emulate loopback TCP unfaithfully); real play uses net_host/net_join.
 * Returns the host session and stores the guest in *out_guest (both
 * freed individually with net_close; closing one severs the other, which
 * drains its queue and then reports the disconnect like TCP would). */
NetSession *net_pair_mem(NetSession **out_guest);

/* Free a session and its sockets. Never touches the world. */
void net_close(NetSession *ns);

/* Pump the session once per frame BEFORE game_update: accepts a
 * pending guest (host), drains inbound messages (commands, tick
 * authorisations, hash reports, world transfers), and reacts to them.
 * Returns 1 if the session is still alive, 0 if it ended (peer gone) —
 * the caller should then net_close() and detach it. */
int net_pump(NetSession *ns, GameState *gs);

/* Call once per frame AFTER game_update: the host broadcasts the new
 * tick authorisation and both sides emit any due hash report. */
void net_after_update(NetSession *ns, GameState *gs);

/* command_submit's routing hook. Returns 1 if the session handled the
 * submission (host: stamped+logged+broadcast; guest: sent to host), 0
 * if the caller should fall back to local single-player stamping. */
int net_submit_local(NetSession *ns, GameState *gs, const Command *c);

/* May the sim run tick `tick` right now? Hosts always may; guests only
 * up to the last authorised tick. */
int net_tick_allowed(const NetSession *ns, uint64_t tick);

/* 1 if this session is the ordering authority. */
int net_is_host(const NetSession *ns);

/* Short status line for the HUD ("HOST waiting", "guest tick 1234"). */
const char *net_status(const NetSession *ns);

#endif /* NET_H */
