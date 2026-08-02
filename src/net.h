#ifndef NET_H
#define NET_H

/* net.h  --  Lockstep co-op and the server's transport
 * (MMO_PLAN Phase 5, generalised in Phase 6) */

#include "game.h"   /* GameState (game.h only forward-declares us)     */
#include <stdint.h>

typedef struct NetSession NetSession;

#define NET_DEFAULT_PORT      7777
/* 2: HELLO carries a resume id after the version (MMO Phase 6). */
/* 21: authentication (AUTH_PLAN Phase 1). MSG_HELLO carries an account */
/* 22: a house's origin tier joins the snapshot (NEEDS_PLAN Phase 1). */
/* 25: marriage and birth (LIFE_PLAN Phase 6). NOTHING ON THE WIRE */
/* 26: a resident carries a sex, a pregnancy and the house they were
 *    born in (LIFE_PLAN Phase 6b). This one IS a format change as well
 *    as a rule change — three int32s per resident — so an older peer
 *    would decode every resident after the first at the wrong offset. */
/* 27: households, the reserve and the treasury (LIFE_PLAN Phase 7). */
/* 28: productivity (LIFE_PLAN Phase 8). No field changed shape, but
 *    Building.timer changed scale and production now depends on who is
 *    standing in the building — so a peer on 27 decodes every frame
 *    perfectly and disagrees about how much was made. */
#define NET_PROTO_VERSION     28u
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
/* What a client presents to say who it is (AUTH_PLAN Phase 1). Zeroed */
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

/* Create a connected host+guest pair over an IN-MEMORY transport —. */
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
 * authorisations, hash reports, world transfers), and reacts to them. */
int net_pump(NetSession *ns, GameState *gs);

/* Call once per frame AFTER client_update: the host broadcasts the new
 * tick authorisation. */
void net_after_update(NetSession *ns, GameState *gs);

/* Call once per COMPLETED sim tick, from whichever loop is running them
 * (client.c's fixed-timestep pump, the server's clock). Two jobs, both
 * of which have to happen AT a tick rather than once a frame: */
void net_on_tick(NetSession *ns, GameState *gs);

/* Point `gs` at a session (or, with ns == NULL / net_detach, at none): */
void net_attach(GameState *gs, NetSession *ns);
void net_detach(GameState *gs);

/* command_submit's routing hook. Returns 1 if the session handled the
 * submission (host: stamped+logged+broadcast; guest: sent to host), 0
 * if the caller should fall back to local single-player stamping. */
int net_submit_local(NetSession *ns, GameState *gs, const Command *c);

/* May the sim run tick `tick` right now? Hosts always may; guests only
 * up to the last authorised tick. */
int net_tick_allowed(const NetSession *ns, uint64_t tick);

/* Declare this host the authority (SERVER_AUTHORITY.md Phase 1):. */
/* Install the account store this host authenticates against, or NULL */
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
