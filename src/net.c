/*  net.c  --  Lockstep two-player co-op over raw TCP (MMO_PLAN Phase 5)
 *
 * See net.h for the model. Implementation notes:
 *
 * FRAMING. Every message is [1 byte type][4 bytes payload length LE]
 * [payload]. Payloads are the raw C structs/values named per type
 * below. Both peers must be same-endian, same-struct-layout builds —
 * true for every platform this game targets (little-endian, and the
 * Command struct is all fixed-width fields); a portable serialiser can
 * replace the memcpys if that ever changes.
 *
 * SOCKETS. Non-blocking recv into a per-session accumulation buffer,
 * frames parsed out as they complete — a frame split across TCP
 * segments is just "not ready yet". Sends use a retry loop; at co-op
 * traffic volumes (bytes per second, one MSG_WORLD burst at join) a
 * full send buffer is momentary. Winsock and BSD sockets differ only
 * behind the small shim at the top.
 */

/* ---- platform shim ----------------------------------------
 * Included BEFORE net.h/SDL: winsock2.h must be seen before anything
 * that might drag in windows.h (which would pull the incompatible
 * winsock 1 headers). */
#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
   typedef SOCKET sock_t;
#  define BAD_SOCK        INVALID_SOCKET
#  define sock_close(s)   closesocket(s)
#  define sock_errno()    WSAGetLastError()
#  define SOCK_EWOULDBLOCK WSAEWOULDBLOCK
static int sock_set_nonblock(sock_t s)
{
    u_long on = 1;
    return ioctlsocket(s, FIONBIO, &on) == 0;
}
static int net_platform_init(void)
{
    WSADATA w;
    return WSAStartup(MAKEWORD(2, 2), &w) == 0;
}
static void net_platform_quit(void) { WSACleanup(); }
#  define SOCK_IOLEN(n)   ((int)(n))
#  define SOCK_ADDRLEN(n) ((int)(n))
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <errno.h>
   typedef int sock_t;
#  define BAD_SOCK        (-1)
#  define sock_close(s)   close(s)
#  define sock_errno()    errno
#  define SOCK_EWOULDBLOCK EWOULDBLOCK
static int sock_set_nonblock(sock_t s)
{
    int fl = fcntl(s, F_GETFL, 0);
    return fl >= 0 && fcntl(s, F_SETFL, fl | O_NONBLOCK) == 0;
}
static int net_platform_init(void) { return 1; }
static void net_platform_quit(void) { }
#  define SOCK_IOLEN(n)   (n)
#  define SOCK_ADDRLEN(n) (n)
#endif

/* A send() to a peer that vanished must report an error, not raise
 * SIGPIPE and kill the process — disconnect is exactly when we promise
 * graceful single-player continuation. Linux has the per-call flag;
 * macOS uses a per-socket option (applied in setup); Windows has
 * neither problem. */
#ifdef MSG_NOSIGNAL
#  define SEND_FLAGS MSG_NOSIGNAL
#else
#  define SEND_FLAGS 0
#endif

static void sock_no_sigpipe(sock_t s)
{
#ifdef SO_NOSIGPIPE
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, (const char *)&yes, sizeof(yes));
#else
    (void)s;
#endif
}

#include "net.h"
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

/* ---- protocol -------------------------------------------- */
enum {
    MSG_HELLO     = 1,  /* guest->host: {uint32 proto_version}          */
    MSG_WELCOME   = 2,  /* host->guest: {uint32 player_id}              */
    MSG_WORLD     = 3,  /* host->guest: {uint32 seed, uint64 tick,
                         *               int32 n, Command[n]}           */
    MSG_CMD       = 4,  /* guest->host: unstamped Command (identity and
                         * tick ignored); host->guest: stamped Command  */
    MSG_TICK_AUTH = 5,  /* host->guest: {uint64 tick} run through here  */
    MSG_HASH      = 6,  /* guest->host: {uint64 tick, uint64 hash}      */
    MSG_BYE       = 7   /* polite shutdown either way                   */
};

#define RECV_BUF_CAP   (256 * 1024)
#define HASH_RING      16
#define GUEST_PLAYER_ID 2u

struct NetSession {
    int      is_host;
    sock_t   listen_fd;       /* host only, BAD_SOCK once accepted      */
    sock_t   peer_fd;         /* the one connection (BAD_SOCK = none)   */
    int      alive;

    /* guest: how far the host has authorised us to simulate. */
    uint64_t authorized_tick;
    int      world_installed; /* guest: MSG_WORLD processed             */

    /* host: my hash at recent NET_HASH_INTERVAL boundaries, to compare
     * against the guest's reports (the guest runs behind us). */
    struct { uint64_t tick, hash; } hash_ring[HASH_RING];
    int      hash_ring_n;
    uint64_t last_hash_tick;  /* both: last boundary hashed/reported    */

    unsigned char rbuf[RECV_BUF_CAP];
    size_t        rlen;

    /* In-memory transport (net_pair_mem): when is_mem, sends append to
     * the peer's mem_q and recv drains our own; no sockets exist. A
     * closed peer sets mem_severed — we drain what's queued, then the
     * pump reports the disconnect exactly as TCP EOF would. */
    int           is_mem;
    NetSession   *mem_peer;
    unsigned char mem_q[RECV_BUF_CAP];
    size_t        mem_qlen;
    int           mem_severed;

    char     status[64];
};

static int session_connected(const NetSession *ns)
{
    return ns->is_mem ? 1 : ns->peer_fd != BAD_SOCK;
}

/* ---- low-level send/recv ---------------------------------- */

static int send_all(NetSession *ns, const void *data, size_t n)
{
    const char *p = (const char *)data;

    if (ns->is_mem) {
        NetSession *peer = ns->mem_peer;
        if (!peer) { ns->alive = 0; return 0; }   /* peer closed */
        if (peer->mem_qlen + n > RECV_BUF_CAP) { ns->alive = 0; return 0; }
        memcpy(peer->mem_q + peer->mem_qlen, p, n);
        peer->mem_qlen += n;
        return 1;
    }

    while (n > 0) {
        long w = (long)send(ns->peer_fd, p, SOCK_IOLEN(n), SEND_FLAGS);
        if (w > 0) { p += w; n -= (size_t)w; continue; }
        if (w < 0 && sock_errno() == SOCK_EWOULDBLOCK) {
            SDL_Delay(1);   /* momentary full buffer at these volumes */
            continue;
        }
        ns->alive = 0;
        return 0;
    }
    return 1;
}

static int send_msg(NetSession *ns, unsigned char type,
                    const void *payload, uint32_t len)
{
    unsigned char hdr[5];
    if (!session_connected(ns) || !ns->alive) return 0;
    hdr[0] = type;
    hdr[1] = (unsigned char)(len);
    hdr[2] = (unsigned char)(len >> 8);
    hdr[3] = (unsigned char)(len >> 16);
    hdr[4] = (unsigned char)(len >> 24);
    return send_all(ns, hdr, 5) &&
           (len == 0 || send_all(ns, payload, len));
}

/* Drain the socket into rbuf. Returns 0 on a dead connection. */
static int recv_into_buf(NetSession *ns)
{
    if (ns->is_mem) {
        size_t space = RECV_BUF_CAP - ns->rlen;
        size_t take  = ns->mem_qlen < space ? ns->mem_qlen : space;
        if (take > 0) {
            memcpy(ns->rbuf + ns->rlen, ns->mem_q, take);
            ns->rlen += take;
            memmove(ns->mem_q, ns->mem_q + take, ns->mem_qlen - take);
            ns->mem_qlen -= take;
        }
        /* Severed and fully drained = EOF, same as a TCP close. */
        return !(ns->mem_severed && ns->mem_qlen == 0 && take == 0);
    }

    for (;;) {
        long r;
        if (ns->rlen == RECV_BUF_CAP) return 1;   /* parse first */
        r = (long)recv(ns->peer_fd, (char *)ns->rbuf + ns->rlen,
                       SOCK_IOLEN(RECV_BUF_CAP - ns->rlen), 0);
        if (r > 0) { ns->rlen += (size_t)r; continue; }
        if (r == 0) return 0;                     /* orderly close */
        if (sock_errno() == SOCK_EWOULDBLOCK) return 1;
        return 0;                                 /* hard error    */
    }
}

/* ---- host: stamping authority ------------------------------ */

/* Stamp `c` as `player` for the delay-buffered future, append to the
 * authoritative log, and broadcast. The one place order is decided. */
static int host_stamp_log_send(NetSession *ns, GameState *gs,
                               const Command *c, uint32_t player)
{
    Command stamped = *c;
    stamped.tick      = gs->sim_tick_no + NET_CMD_DELAY_TICKS;
    stamped.player_id = player;

    if (!command_log_append(gs, &stamped)) return 0;
    send_msg(ns, MSG_CMD, &stamped, (uint32_t)sizeof(stamped));
    return 1;
}

static void host_send_world(NetSession *ns, const GameState *gs)
{
    /* {seed, tick, n, Command[n]} — the same information as a v6 save. */
    size_t  fixed = sizeof(uint32_t) + sizeof(uint64_t) + sizeof(int32_t);
    size_t  total = fixed + sizeof(Command) * (size_t)gs->cmd_count;
    unsigned char *buf = (unsigned char *)malloc(total);
    size_t  off = 0;
    int32_t n = gs->cmd_count;

    if (!buf) return;
    memcpy(buf + off, &gs->world_seed, sizeof(uint32_t));  off += sizeof(uint32_t);
    memcpy(buf + off, &gs->sim_tick_no, sizeof(uint64_t)); off += sizeof(uint64_t);
    memcpy(buf + off, &n, sizeof(int32_t));                off += sizeof(int32_t);
    if (n > 0)
        memcpy(buf + off, gs->cmd_log, sizeof(Command) * (size_t)n);

    send_msg(ns, MSG_WORLD, buf, (uint32_t)total);
    free(buf);
    SDL_Log("net: world sent (seed %u, tick %llu, %d commands)",
            gs->world_seed, (unsigned long long)gs->sim_tick_no, n);
}

/* A freshly joined guest that owns nothing gets a starting island —
 * expressed as a logged command like everything else, so replay and
 * the guest's own copy of history agree that the join happened. */
static void host_grant_if_landless(NetSession *ns, GameState *gs)
{
    Command c = {0};
    int     i, target = -1;

    for (i = 0; i < MAX_ISLANDS; i++)
        if (gs->islands[i].owner == GUEST_PLAYER_ID) return;
    for (i = 0; i < MAX_ISLANDS; i++)
        if (!gs->islands[i].settled &&
            gs->islands[i].owner == PLAYER_NONE) { target = i; break; }
    if (target < 0) { SDL_Log("net: no island left to grant"); return; }

    c.kind = CMD_GRANT_START;
    c.a    = target;
    host_stamp_log_send(ns, gs, &c, GUEST_PLAYER_ID);
    SDL_Log("net: granted island %d to the guest", target);
}

static void host_note_hash_mismatch(NetSession *ns, GameState *gs,
                                    uint64_t tick, uint64_t theirs)
{
    int i;
    for (i = 0; i < ns->hash_ring_n; i++) {
        if (ns->hash_ring[i].tick != tick) continue;
        if (ns->hash_ring[i].hash == theirs) return;   /* in sync */
        SDL_Log("net: DESYNC at tick %llu (host %016llx guest %016llx) "
                "— resyncing guest by full replay",
                (unsigned long long)tick,
                (unsigned long long)ns->hash_ring[i].hash,
                (unsigned long long)theirs);
        host_send_world(ns, gs);
        return;
    }
    /* Tick already left the ring: harmless — the next report will hit. */
}

/* ---- message dispatch -------------------------------------- */

static void handle_msg(NetSession *ns, GameState *gs, unsigned char type,
                       const unsigned char *p, uint32_t len)
{
    if (ns->is_host) {
        switch (type) {
        case MSG_HELLO: {
            uint32_t ver = 0, id = GUEST_PLAYER_ID;
            if (len >= 4) memcpy(&ver, p, 4);
            if (ver != NET_PROTO_VERSION) {
                SDL_Log("net: guest speaks proto %u, we speak %u — bye",
                        ver, NET_PROTO_VERSION);
                ns->alive = 0;
                return;
            }
            send_msg(ns, MSG_WELCOME, &id, sizeof(id));
            host_send_world(ns, gs);
            host_grant_if_landless(ns, gs);
            SDL_Log("net: guest joined as player %u", id);
            break;
        }
        case MSG_CMD:
            if (len == sizeof(Command)) {
                Command c;
                memcpy(&c, p, sizeof(c));
                /* Identity comes from the CONNECTION, never the wire. */
                host_stamp_log_send(ns, gs, &c, GUEST_PLAYER_ID);
            }
            break;
        case MSG_HASH:
            if (len == 16) {
                uint64_t tick, hash;
                memcpy(&tick, p, 8);
                memcpy(&hash, p + 8, 8);
                host_note_hash_mismatch(ns, gs, tick, hash);
            }
            break;
        case MSG_BYE:
            ns->alive = 0;
            break;
        default: break;
        }
        return;
    }

    /* guest */
    switch (type) {
    case MSG_WELCOME:
        if (len >= 4) {
            uint32_t id;
            memcpy(&id, p, 4);
            gs->local_player_id = id;
            SDL_Log("net: we are player %u", id);
        }
        break;
    case MSG_WORLD: {
        uint32_t seed;
        uint64_t tick;
        int32_t  n;
        size_t   fixed = sizeof(uint32_t) + sizeof(uint64_t) + sizeof(int32_t);
        if (len < fixed) break;
        memcpy(&seed, p, 4);
        memcpy(&tick, p + 4, 8);
        memcpy(&n,    p + 12, 4);
        if (n < 0 || fixed + sizeof(Command) * (size_t)n != len) break;
        if (!game_install_world(gs, seed, tick,
                                (const Command *)(p + fixed), n)) {
            SDL_Log("net: failed to install world");
            ns->alive = 0;
            break;
        }
        /* Everything at or before the install point is authorised by
         * construction; later ticks wait for MSG_TICK_AUTH. */
        ns->authorized_tick = tick;
        ns->world_installed = 1;
        ns->last_hash_tick  = tick;
        SDL_Log("net: world installed at tick %llu",
                (unsigned long long)tick);
        break;
    }
    case MSG_CMD:
        if (len == sizeof(Command) && ns->world_installed) {
            Command c;
            memcpy(&c, p, sizeof(c));
            command_log_append(gs, &c);   /* applies at its stamped tick */
        }
        break;
    case MSG_TICK_AUTH:
        if (len == 8) {
            uint64_t t;
            memcpy(&t, p, 8);
            if (t > ns->authorized_tick) ns->authorized_tick = t;
        }
        break;
    case MSG_BYE:
        ns->alive = 0;
        break;
    default: break;
    }
}

/* Parse complete frames out of rbuf. */
static void parse_frames(NetSession *ns, GameState *gs)
{
    size_t off = 0;
    while (ns->rlen - off >= 5) {
        unsigned char type = ns->rbuf[off];
        uint32_t len =  (uint32_t)ns->rbuf[off + 1]
                     | ((uint32_t)ns->rbuf[off + 2] << 8)
                     | ((uint32_t)ns->rbuf[off + 3] << 16)
                     | ((uint32_t)ns->rbuf[off + 4] << 24);
        if (len > RECV_BUF_CAP - 5) { ns->alive = 0; break; }  /* hostile */
        if (ns->rlen - off - 5 < len) break;                   /* partial */
        handle_msg(ns, gs, type, ns->rbuf + off + 5, len);
        off += 5 + (size_t)len;
        if (!ns->alive) break;
    }
    if (off > 0) {
        memmove(ns->rbuf, ns->rbuf + off, ns->rlen - off);
        ns->rlen -= off;
    }
}

/* ---- lifecycle --------------------------------------------- */

static NetSession *session_new(int is_host)
{
    NetSession *ns = (NetSession *)calloc(1, sizeof(NetSession));
    if (!ns) return NULL;
    ns->is_host   = is_host;
    ns->listen_fd = BAD_SOCK;
    ns->peer_fd   = BAD_SOCK;
    ns->alive     = 1;
    return ns;
}

NetSession *net_host(uint16_t port)
{
    NetSession        *ns;
    struct sockaddr_in a;
    int                yes = 1;

    if (!net_platform_init()) return NULL;
    ns = session_new(1);
    if (!ns) return NULL;

    ns->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (ns->listen_fd == BAD_SOCK) goto fail;
    setsockopt(ns->listen_fd, SOL_SOCKET, SO_REUSEADDR,
               (const char *)&yes, sizeof(yes));

    memset(&a, 0, sizeof(a));
    a.sin_family      = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port        = htons(port);
    if (bind(ns->listen_fd, (struct sockaddr *)&a, sizeof(a)) != 0) goto fail;
    if (listen(ns->listen_fd, 1) != 0) goto fail;
    if (!sock_set_nonblock(ns->listen_fd)) goto fail;

    SDL_Log("net: hosting on port %u", port);
    return ns;

fail:
    SDL_Log("net: failed to host on port %u", port);
    net_close(ns);
    return NULL;
}

NetSession *net_join(const char *host, uint16_t port)
{
    NetSession      *ns;
    struct addrinfo  hints, *res = NULL, *ai;
    char             portstr[8];
    uint32_t         ver = NET_PROTO_VERSION;

    if (!net_platform_init()) return NULL;
    ns = session_new(0);
    if (!ns) return NULL;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    SDL_snprintf(portstr, sizeof(portstr), "%u", port);
    if (getaddrinfo(host, portstr, &hints, &res) != 0) goto fail;

    for (ai = res; ai; ai = ai->ai_next) {
        ns->peer_fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (ns->peer_fd == BAD_SOCK) continue;
        if (connect(ns->peer_fd, ai->ai_addr, SOCK_ADDRLEN(ai->ai_addrlen)) == 0)
            break;
        sock_close(ns->peer_fd);
        ns->peer_fd = BAD_SOCK;
    }
    freeaddrinfo(res);
    if (ns->peer_fd == BAD_SOCK) goto fail;
    if (!sock_set_nonblock(ns->peer_fd)) goto fail;
    sock_no_sigpipe(ns->peer_fd);

    send_msg(ns, MSG_HELLO, &ver, sizeof(ver));
    SDL_Log("net: connected to %s:%u, awaiting world", host, port);
    return ns;

fail:
    SDL_Log("net: could not join %s:%u", host, port);
    net_close(ns);
    return NULL;
}

void net_close(NetSession *ns)
{
    if (!ns) return;
    if (ns->is_mem) {
        if (ns->mem_peer) {                 /* sever the surviving side */
            ns->mem_peer->mem_severed = 1;
            ns->mem_peer->mem_peer    = NULL;
        }
        free(ns);
        return;                             /* no sockets, no platform  */
    }
    if (ns->peer_fd != BAD_SOCK) {
        send_msg(ns, MSG_BYE, NULL, 0);
        sock_close(ns->peer_fd);
    }
    if (ns->listen_fd != BAD_SOCK) sock_close(ns->listen_fd);
    free(ns);
    net_platform_quit();
}

NetSession *net_pair_mem(NetSession **out_guest)
{
    NetSession *h = session_new(1);
    NetSession *g = session_new(0);
    uint32_t    ver = NET_PROTO_VERSION;

    if (!h || !g) { free(h); free(g); return NULL; }
    h->is_mem = 1;
    g->is_mem = 1;
    h->mem_peer = g;
    g->mem_peer = h;

    /* The same opening move a TCP guest makes; the host's next pump
     * answers with WELCOME + WORLD + the grant, all through the queues. */
    send_msg(g, MSG_HELLO, &ver, sizeof(ver));

    *out_guest = g;
    return h;
}

/* ---- per-frame driving ------------------------------------- */

int net_pump(NetSession *ns, GameState *gs)
{
    if (!ns->alive) return 0;

    /* Host: accept the one guest. */
    if (ns->is_host && ns->peer_fd == BAD_SOCK && ns->listen_fd != BAD_SOCK) {
        sock_t c = accept(ns->listen_fd, NULL, NULL);
        if (c != BAD_SOCK) {
            if (!sock_set_nonblock(c)) { sock_close(c); return 1; }
            sock_no_sigpipe(c);
            ns->peer_fd = c;
            SDL_Log("net: guest connected");
        }
    }

    if (session_connected(ns)) {
        if (!recv_into_buf(ns)) {
            /* The peer is gone. Frames already buffered are real,
             * ordered data — apply them before declaring the death,
             * so nothing the peer said gets dropped on the floor. */
            parse_frames(ns, gs);
            SDL_Log("net: peer disconnected — continuing single-player");
            ns->alive = 0;
            return 0;
        }
        parse_frames(ns, gs);
    }

    return ns->alive;
}

void net_after_update(NetSession *ns, GameState *gs)
{
    if (!ns->alive || !session_connected(ns)) return;

    if (ns->is_host) {
        /* Broadcast my clock: every tick strictly below it is complete
         * (all its commands were stamped >= NET_CMD_DELAY_TICKS ago and
         * sent, in order, before this message). */
        uint64_t horizon = gs->sim_tick_no;
        send_msg(ns, MSG_TICK_AUTH, &horizon, sizeof(horizon));

        /* Record my hash at each interval boundary crossed, for
         * comparison when the (lagging) guest's report arrives. */
        while (ns->last_hash_tick + NET_HASH_INTERVAL <= gs->sim_tick_no) {
            int slot;
            ns->last_hash_tick += NET_HASH_INTERVAL;
            /* Hash only exactly AT the boundary; sim_hash covers
             * sim_tick_no, so equality of tick implies comparability. */
            if (ns->last_hash_tick != gs->sim_tick_no) continue;
            slot = ns->hash_ring_n % HASH_RING;
            ns->hash_ring[slot].tick = gs->sim_tick_no;
            ns->hash_ring[slot].hash = sim_hash(gs);
            if (ns->hash_ring_n < HASH_RING) ns->hash_ring_n++;
            else {
                /* ring full: overwrite is fine, it is a ring */
            }
        }
    } else if (ns->world_installed) {
        /* Report my hash when I complete an interval boundary. */
        while (ns->last_hash_tick + NET_HASH_INTERVAL <= gs->sim_tick_no) {
            ns->last_hash_tick += NET_HASH_INTERVAL;
            if (ns->last_hash_tick != gs->sim_tick_no) continue;
            {
                unsigned char p[16];
                uint64_t t = gs->sim_tick_no, h = sim_hash(gs);
                memcpy(p, &t, 8);
                memcpy(p + 8, &h, 8);
                send_msg(ns, MSG_HASH, p, sizeof(p));
            }
        }
    }
}

int net_submit_local(NetSession *ns, GameState *gs, const Command *c)
{
    if (!ns->alive) return 0;   /* fall back to offline stamping */

    if (ns->is_host) {
        if (!session_connected(ns)) return 0;    /* nobody joined yet */
        return host_stamp_log_send(ns, gs, c, gs->local_player_id);
    }

    /* Guest: upstream to the authority; it returns stamped. */
    return send_msg(ns, MSG_CMD, c, (uint32_t)sizeof(*c));
}

int net_tick_allowed(const NetSession *ns, uint64_t tick)
{
    if (ns->is_host || !ns->alive) return 1;
    if (!ns->world_installed) return 0;   /* no world yet: hold at join */
    /* The horizon is EXCLUSIVE: the host reports its sim_tick_no (the
     * next tick IT will run), so the guest may run strictly below it —
     * converging on the host's tick exactly, never ahead of it. */
    return tick < ns->authorized_tick;
}

int net_is_host(const NetSession *ns) { return ns->is_host; }

const char *net_status(const NetSession *ns)
{
    NetSession *m = (NetSession *)ns;   /* status[] is scratch space */
    if (!ns->alive)
        SDL_snprintf(m->status, sizeof(m->status), "NET: disconnected");
    else if (ns->is_host)
        SDL_snprintf(m->status, sizeof(m->status),
                     !session_connected(ns) ? "HOST: waiting for guest"
                                             : "HOST: guest connected");
    else
        SDL_snprintf(m->status, sizeof(m->status),
                     "GUEST: authorised to tick %llu",
                     (unsigned long long)ns->authorized_tick);
    return m->status;
}
