#ifndef NET_H
#define NET_H

/* =========================================================
 * net.h  --  Lockstep co-op and the server's transport
 *            (MMO_PLAN Phase 5, generalised in Phase 6)
 *
 * Every client runs the identical deterministic sim; "multiplayer" is
 * nothing more than agreeing on the command log's order. The HOST is
 * the ordering authority — a player hosting a friend, or saltmarch_host,
 * the dedicated server, which is the same code with no client attached
 * and net_set_persistent(1) so it outlives its players:
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
 *     MSG_WORLD, then the host grants the joiner a starting island
 *     through the funnel (CMD_GRANT_START) if it owns nothing. A client
 *     may ask to resume an identity it held before (net_join's
 *     resume_id, the client's --as N): honoured if that player owns an
 *     island and nobody is connected as them. There is NO
 *     authentication — anyone who knows an id can claim it. That is
 *     acceptable for co-op and a friends-only server, and is the first
 *     thing to fix before a public one.
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
/* 2: HELLO carries a resume id after the version (MMO Phase 6).
 * 3: Command gained a `seq` field (UI_PLAN M1), so the struct that
 *    crosses the wire is a different size — old and new peers must not
 *    try to talk.
 * 4: MSG_PING. Liveness is no longer inferred from MSG_HASH, so a peer
 *    that does not send pings now looks idle and gets dropped (see
 *    SERVER.md's transport hardening plan). */
#define NET_PROTO_VERSION     4u
/* Connections one host session will hold. A co-op host uses one; the
 * dedicated server uses as many as it is given. Peers are cheap (a
 * growable receive buffer each), so this is a sanity bound, not a
 * tuning knob. */
#define NET_MAX_PEERS         8
/* Commands apply this many ticks after the host stamps them (400ms at
 * 10 ticks/sec): the latency-jitter absorber. Co-op tolerance is high. */
#define NET_CMD_DELAY_TICKS   4
/* The guest reports its hash every this many ticks (5s). */
#define NET_HASH_INTERVAL     50

/* Host a session: listen on `port`, then accept joiners from inside
 * net_pump. Returns NULL on failure (port busy, no sockets). */
NetSession *net_host(uint16_t port);

/* Persistent hosts survive losing every peer (the dedicated server);
 * the default, a co-op host, ends its session with its guest, which is
 * what produces single-player continuation. Host sessions only. */
void net_set_persistent(NetSession *ns, int persistent);

/* Join a session at host:port, asking to resume identity `resume_id`
 * (PLAYER_NONE for "assign me one"). Blocks briefly for the connect,
 * then returns with gs untouched — the world arrives through net_pump.
 * Returns NULL on failure. */
NetSession *net_join(const char *host, uint16_t port, uint32_t resume_id);

/* How many connections this session currently holds. */
int net_peer_count(const NetSession *ns);

/* The identity this client should ask for next time: the id the host
 * assigned, once WELCOME has arrived. Worth showing the player — it is
 * what --as takes. */
uint32_t net_resume_id(const NetSession *ns);

/* Create a connected host+guest pair over an IN-MEMORY transport — the
 * same sessions, protocol, framing and pump flow as TCP, with the socket
 * layer swapped for two byte queues. Exists so the lockstep protocol is
 * unit-testable deterministically in any environment (some sandboxes
 * emulate loopback TCP unfaithfully); real play uses net_host/net_join.
 * Returns the host session and stores the guest in *out_guest (both
 * freed individually with net_close; closing one severs the other, which
 * drains its queue and then reports the disconnect like TCP would). */
NetSession *net_pair_mem(NetSession **out_guest);

/* Attach one more in-memory guest to an existing host session, asking
 * for `resume_id` as net_join would. This is how the multi-player
 * server is tested without sockets. Returns NULL if the host is full. */
NetSession *net_join_mem(NetSession *host, uint32_t resume_id);

/* Free a session and its sockets. Never touches the world. */
void net_close(NetSession *ns);

/* Pump the session once per frame BEFORE client_update: accepts
 * pending joiners (host), drains inbound messages (commands, tick
 * authorisations, hash reports, world transfers), and reacts to them.
 * Returns 1 if the session is still alive, 0 if it ended (the peer of a
 * non-persistent session is gone) — the caller should then net_close()
 * and detach it. A persistent host stays alive through any number of
 * connects and disconnects. */
int net_pump(NetSession *ns, GameState *gs);

/* Call once per frame AFTER client_update: the host broadcasts the new
 * tick authorisation and both sides emit any due hash report. */
void net_after_update(NetSession *ns, GameState *gs);

/* Point `gs` at a session (or, with ns == NULL / net_detach, at none):
 * sets gs->net and installs the command-routing hook the sim calls
 * through. This is the ONLY supported way to attach a session — the sim
 * library does not link net.c, so setting gs->net by hand would leave
 * submissions taking the offline path (MMO_PLAN Phase 6). */
void net_attach(GameState *gs, NetSession *ns);
void net_detach(GameState *gs);

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
