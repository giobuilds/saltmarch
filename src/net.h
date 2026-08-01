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
 *     MSG_WORLD (a snapshot of the world plus the commands stamped for
 *     ticks that have not run yet), then the host grants the joiner a starting island
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
 *    SERVER.md's transport hardening plan).
 * 5: MSG_WORLD carries a SNAPSHOT and the pending tail instead of the
 *    seed and the whole command log, so joining costs what the world
 *    weighs rather than how long it has existed (SERVER.md, "Log
 *    truncation"). Same message, entirely different payload.
 * 6: MAX_ISLANDS 4 -> 8 (SUPPLY_CHAIN Phase 5). No message changed
 *    shape, but a four-island client and an eight-island server
 *    disagree about what the world IS — the snapshot's island count is
 *    checked on decode, so they would fail to talk anyway; this makes
 *    them say why at the handshake instead.
 * 7: nine more goods (SUPPLY_CHAIN Phase 6). A resource-vocabulary
 *    change is a protocol change for the same reason it is a save
 *    change: Command is the same size and parses fine, but a peer
 *    built before it reads "sell 5 of resource 44" as a different
 *    good. Phase 4 bumped only SAVE_VERSION for this and should have
 *    bumped both; Phase 5's island-count bump closed that window by
 *    accident rather than by design.
 * 8: twenty-five more goods (SUPPLY_CHAIN Phase 7), same reasoning
 *    as 7.
 * 9: four more goods, and CMD_UPGRADE_HOUSE's `c` now selects which
 *    way up (SUPPLY_CHAIN Phase 8). A peer that ignores it would
 *    apply every Scholars upgrade as a line upgrade.
 * 10: the order book (MARITIME_PLAN Phase 2). MSG_WORLD's snapshot
 *    grew two sections, so an older peer would read the book as the
 *    start of the faction and reject the checkpoint. Worse if it
 *    didn't: the book is hashed and matched every tick, so a peer that
 *    did not run the matcher would disagree with the host about the
 *    world one tick after the first trade crossed.
 * 11: trade capacity (MARITIME_PLAN Phase 2, merchants). Two more
 *    fields per island and two per booking in the snapshot, and a
 *    matching rule that now depends on them — so a peer on 10 would
 *    both misread the checkpoint and fill trades this one refuses.
 * 12: the faction's home ports and standing quotes (MARITIME_PLAN
 *    Phase 2). The snapshot carries its quote table, and the world it
 *    describes has two islands nobody may colonise.
 * 13: three routes per island pair (MARITIME_PLAN Phase 3). No message
 *    changed shape and the Sea is in none of them — it is regenerated
 *    from the seed at both ends, which is the point: a peer on 12
 *    rebuilds a DIFFERENT sea from the same seed and disagrees about
 *    when every ship arrives. Nothing in the wire format would catch
 *    that, so the handshake has to.
 * 14: route knowledge and charts (MARITIME_PLAN Phase 3b). The
 *    snapshot carries per-player knowledge and the market's chart
 *    offers, and a booking's route depends on the seller's charts —
 *    so a peer on 13 both misreads the checkpoint and sails cargoes
 *    down the wrong passage.
 * 15: per-route insurance and shipment raids (MARITIME_PLAN Phase 3c).
 *    A new command kind, a differently-shaped premium table in the
 *    snapshot, and cargo that can now fail to arrive — a peer on 14
 *    would deliver what this one lost.
 * 16: the survey mission (MARITIME_PLAN Phase 3d). Expeditions in the
 *    snapshot, two more command kinds, and charts that now appear
 *    from a mission rather than only from the market.
 * 17: chart expiry (MARITIME_PLAN Phase 3e). A Sea now has one field
 *    that is world state rather than generated — the per-pair cursor
 *    saying which private passages are in play — and the snapshot
 *    carries it. A peer on 16 regenerates the pool and then disagrees
 *    about which two of it are real.
 * 20: pirates as entities (MARITIME_PLAN Phase 5b). The fleets are in
 *    the snapshot and raids are caused by where a cargo sailed rather
 *    than derived at dispatch, so a peer on 19 loses different ships.
 * 19: ship classes and escorts (MARITIME_PLAN Phase 5). Four more
 *    fields per ship in the snapshot, a new command kind, and
 *    interception resolved from guns rather than a constant.
 * 18: server authority (SERVER_AUTHORITY.md Phase 1). MSG_STATE, and
 *    MSG_WELCOME grew a flag saying whether the server's word is
 *    final. A peer on 17 would read the flag as absent, keep waiting
 *    for tick authorisation that an authoritative server never sends,
 *    and sit still. */
/* 21: authentication (AUTH_PLAN Phase 1). MSG_HELLO carries an account
 *    id and a 32-byte token after the resume id, and MSG_WELCOME can
 *    return a freshly issued one. A server with no account store
 *    ignores both and behaves exactly as before — but the frames
 *    changed size, and a peer that cannot say who it is must be turned
 *    away at the handshake rather than part way through a join. */
/* 22: a house's origin tier joins the snapshot (NEEDS_PLAN Phase 1).
 *    MSG_WORLD carries a snapshot, so a snapshot format change is a
 *    protocol change for the same reason a resource-vocabulary change
 *    is — an older peer would decode the pop records one field short
 *    and every house after the first would be wrong. */
#define NET_PROTO_VERSION     23u
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
/* What a client presents to say who it is (AUTH_PLAN Phase 1). Zeroed
 * — account_id 0 — means "I have no account", which is what every
 * client says today and what a co-op host expects: on a server with
 * registration open it is answered with a freshly minted one in
 * MSG_WELCOME, and on a closed one it is refused.
 *
 * The token never reaches the sim, a snapshot, or the command log. It
 * lives on the connection and in the client's own config file. */
typedef struct {
    uint32_t account_id;
    uint8_t  token[32];
} NetCredential;

/* `cred` may be NULL, which is identical to a zeroed one. */
NetSession *net_join(const char *host, uint16_t port, uint32_t resume_id,
                     const NetCredential *cred);

/* A token the server issued during this session, or NULL if it issued
 * none. The client saves it; there is no second chance to learn it. */
const NetCredential *net_issued_credential(const NetSession *ns);

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

/* An in-memory host with nobody attached yet, so a test can set an
 * account store up before the first HELLO arrives. */
NetSession *net_host_mem(void);

/* The same as net_join_mem, presenting a credential (AUTH_PLAN Phase 1). The in-memory
 * transport exists so the protocol can be driven deterministically, and
 * an authenticating handshake is exactly the kind of thing that must be
 * tested without a socket. */
NetSession *net_join_mem_as(NetSession *host, uint32_t resume_id,
                            const NetCredential *cred);

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
 * tick authorisation. */
void net_after_update(NetSession *ns, GameState *gs);

/* Call once per COMPLETED sim tick, from whichever loop is running them
 * (client.c's fixed-timestep pump, the server's clock). Two jobs, both
 * of which have to happen AT a tick rather than once a frame:
 *
 *   - the desync check. sim_hash describes the world as it is right
 *     now, so a hash for tick T can only be taken while the world is at
 *     T. This used to be attempted after the fact, by testing whether
 *     the frame happened to land exactly on a boundary — and any frame
 *     that ran two ticks stepped straight over one. The server's
 *     accumulator is deliberately unclamped, so its catch-up bursts
 *     skipped boundaries wholesale, which is precisely when a
 *     divergence most wants catching. Boundaries are now
 *     `tick % NET_HASH_INTERVAL == 0`: an absolute property of the
 *     tick, identical on both sides, independent of when either joined.
 *   - refilling each peer's command budget, which is denominated in
 *     ticks because that is the rate the world actually runs at.
 *
 * Safe to call with ns == NULL. Not called during a join replay: that
 * is catch-up, not live play, and a hash per 50 ticks of it would be
 * reporting on a world the host already knows the shape of. */
void net_on_tick(NetSession *ns, GameState *gs);

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

/* Declare this host the authority (SERVER_AUTHORITY.md Phase 1): it
 * pushes the whole world to every client once a second and its state
 * wins. Clients learn of it in MSG_WELCOME, stop waiting for tick
 * authorisation, and stop reporting desync hashes — under prediction
 * the two sides are meant to differ between pushes.
 *
 * Set before peers join. A co-op host may leave it off and keep the
 * lockstep behaviour; the dedicated server turns it on. */
/* Install the account store this host authenticates against, or NULL
 * for none. Authentication is OFF while this is NULL, which is what
 * keeps co-op exactly as it was: the store is a property of running a
 * dedicated server, not of hosting a friend.
 *
 * The session borrows the store; the host owns it, loads it, and is the
 * only thing that writes it to disk. net.c never opens a file. */
struct AccountStore;
void net_set_accounts(NetSession *ns, struct AccountStore *accounts);

/* Has an account been minted since this was last asked? Returns 1 and
 * clears the flag. The host polls it on its checkpoint cadence: net.c
 * knows when a registration happened, and only the host knows where the
 * file lives. */
int net_accounts_dirty(NetSession *ns);

void net_set_authoritative(NetSession *ns, int on);

/* Whether the server this client is talking to said it was the
 * authority. 0 offline, and 0 for a lockstep co-op session. */
int  net_server_authoritative(const NetSession *ns);

/* 1 if this session is the ordering authority. */
int net_is_host(const NetSession *ns);

/* Short status line for the HUD ("HOST waiting", "guest tick 1234"). */
const char *net_status(const NetSession *ns);

#endif /* NET_H */
