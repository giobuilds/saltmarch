/*  net.c  --  Lockstep co-op over raw TCP (MMO_PLAN Phase 5 / Phase 6)
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
 * SOCKETS. Non-blocking recv into a per-peer accumulation buffer,
 * frames parsed out as they complete — a frame split across TCP
 * segments is just "not ready yet". Sends are queued into a per-peer
 * outbound buffer and flushed non-blocking from net_pump. Winsock and
 * BSD sockets differ only behind the small shim at the top.
 *
 * NOTHING HERE MAY BLOCK, and nothing here may grow without a bound.
 * Both halves of that are hard-won (SERVER.md, "Transport hardening
 * plan"). Sends used to retry a full socket buffer forever, and
 * broadcast() runs from the tick loop, so one peer that stopped reading
 * froze the world for everyone on the server. Receives used to drain
 * until the socket ran dry, so one peer writing at line rate held the
 * pump indefinitely and grew memory without limit. Every send path is
 * therefore a queue-and-return, every receive path is budgeted, and
 * every way a peer can go quiet or stop draining ends in a timeout.
 * A new message type or code path that waits on a peer reintroduces
 * the same class of bug.
 *
 * MANY PEERS (Phase 6). The host is an array of connections rather than
 * one: the same code now backs both "a player hosting a friend" and
 * saltmarch_host, the dedicated server. Everything host-side that used
 * to write to THE peer now broadcasts, and identity is per connection
 * (assigned at join, resumable across reconnects) instead of the single
 * hardcoded guest id. A persistent session (net_set_persistent) outlives
 * its peers; a co-op host still tears down with its guest, which is what
 * gives single-player continuation.
 *
 * NO SDL. This file is transport, not simulation, but it links into the
 * dedicated server, which has no client — hence sim_log in place of
 * SDL_Log, and net_now_ms in place of SDL_GetTicks. That clock drives
 * timeouts only: no part of the world is a function of it, so it never
 * reaches a hash and cannot make a replay disagree with the run that
 * recorded it.
 */

/* Before the platform shim, because the shim itself uses uint64_t (the
 * monotonic clock) and nothing below is guaranteed to provide it. The
 * POSIX socket headers happen to drag the fixed-width types in, which
 * is exactly why this was invisible on Linux and a hard syntax error
 * on MSVC — winsock2.h has no such side effect. */
#include <stdint.h>
#include <stddef.h>

/* ---- platform shim ----------------------------------------
 * Included BEFORE net.h: winsock2.h must be seen before anything that
 * might drag in windows.h (which would pull the incompatible winsock 1
 * headers). */
#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
   /* Explicitly, and only AFTER winsock2.h: this file used to reach
    * windows.h through SDL, and Sleep() below comes from there. Relying
    * on winsock2.h to drag it in is exactly the include-order trap the
    * comment above warns about. */
#  include <windows.h>
   typedef SOCKET sock_t;
#  define BAD_SOCK        INVALID_SOCKET
#  define sock_close(s)   closesocket(s)
#  define sock_errno()    WSAGetLastError()
#  define SOCK_EWOULDBLOCK WSAEWOULDBLOCK
#  define SOCK_EINPROGRESS WSAEWOULDBLOCK
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
/* Monotonic milliseconds. Drives the timeouts below and nothing else —
 * no part of the world is a function of it, so it never enters a hash
 * and cannot make a replay disagree with the run it recorded. */
static uint64_t net_now_ms(void)
{
    static LARGE_INTEGER freq;
    LARGE_INTEGER        c;
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&c);
    return (uint64_t)((double)c.QuadPart / (double)freq.QuadPart * 1000.0);
}
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
#  include <time.h>
#  include <sys/select.h>
   typedef int sock_t;
#  define BAD_SOCK        (-1)
#  define sock_close(s)   close(s)
#  define sock_errno()    errno
#  define SOCK_EWOULDBLOCK EWOULDBLOCK
#  define SOCK_EINPROGRESS EINPROGRESS
static int sock_set_nonblock(sock_t s)
{
    int fl = fcntl(s, F_GETFL, 0);
    return fl >= 0 && fcntl(s, F_SETFL, fl | O_NONBLOCK) == 0;
}
static int net_platform_init(void) { return 1; }
static void net_platform_quit(void) { }
/* Monotonic milliseconds. Drives the timeouts below and nothing else —
 * no part of the world is a function of it, so it never enters a hash
 * and cannot make a replay disagree with the run it recorded. */
static uint64_t net_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}
#  define SOCK_IOLEN(n)   (n)
#  define SOCK_ADDRLEN(n) ((socklen_t)(n))
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

/* Everything a connected socket wants set on it, in one place so the
 * accept path and the connect path cannot drift apart.
 *
 * TCP_NODELAY matters more here than the byte counts suggest: the
 * frames that carry the lockstep clock are 13 bytes (MSG_TICK_AUTH) and
 * 37 (MSG_CMD), which is exactly the shape Nagle holds back waiting for
 * company. Up to ~40 ms of delay on the one path where latency is the
 * product.
 *
 * SO_KEEPALIVE is the backstop under the idle timeout below: a peer
 * whose machine vanished without closing (a yanked cable, a suspended
 * laptop) leaves a socket that looks perfectly healthy until something
 * probes it. */
static void sock_tune(sock_t s)
{
    int yes = 1;

    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&yes, sizeof(yes));
    setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, (const char *)&yes, sizeof(yes));
#ifdef SO_NOSIGPIPE
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, (const char *)&yes, sizeof(yes));
#endif
}

/* Connect without handing the game's frame loop to the network stack.
 * A blocking connect() to a host that is simply not there returns when
 * the OS gives up — over a minute on Linux — and that whole time the
 * window is frozen with no indication why. The socket is made
 * non-blocking first, so the wait is ours to bound.
 *
 * Returns 1 on a connected socket. `ms` is the whole budget. */
static int sock_connect_timeout(sock_t s, const struct sockaddr *addr,
                                size_t addrlen, unsigned ms)
{
    struct timeval tv;
    fd_set         wfds;
    int            err = 0, rc;
#ifdef _WIN32
    int            elen = (int)sizeof(err);
#else
    socklen_t      elen = (socklen_t)sizeof(err);
#endif

    if (!sock_set_nonblock(s)) return 0;

    if (connect(s, addr, SOCK_ADDRLEN(addrlen)) == 0) return 1;
    if (sock_errno() != SOCK_EINPROGRESS) return 0;

    FD_ZERO(&wfds);
    FD_SET(s, &wfds);
    /* tv_usec is `int` on macOS and `long` on Linux and Windows, so the
     * only assignment that narrows nowhere is one from a type narrower
     * than all three. The value is under a million by construction. */
    tv.tv_sec  = (long)(ms / 1000u);
    tv.tv_usec = (int)((ms % 1000u) * 1000u);

#ifdef _WIN32
    /* Winsock ignores nfds entirely, and a SOCKET is a pointer-width
     * handle — computing `s + 1` for it would be inventing a narrowing
     * conversion to satisfy an argument nobody reads. */
    rc = select(0, NULL, &wfds, NULL, &tv);
#else
    rc = select(s + 1, NULL, &wfds, NULL, &tv);
#endif
    if (rc <= 0) return 0;                       /* timed out or failed */

    /* Writable only means the attempt finished; SO_ERROR says how. */
    if (getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&err, &elen) != 0)
        return 0;
    return err == 0;
}

#include "net.h"
#include "simlog.h"
#include "snapshot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- protocol -------------------------------------------- */
enum {
    MSG_HELLO     = 1,  /* guest->host: {uint32 proto, uint32 resume_id} */
    MSG_WELCOME   = 2,  /* host->guest: {uint32 player_id}              */
    MSG_WORLD     = 3,  /* host->guest: {uint32 seed, uint64 tick,
                         *               int32 n, Command[n]}           */
    MSG_CMD       = 4,  /* guest->host: unstamped Command (identity and
                         * tick ignored); host->guest: stamped Command  */
    MSG_TICK_AUTH = 5,  /* host->guest: {uint64 tick} run through here  */
    MSG_HASH      = 6,  /* guest->host: {uint64 tick, uint64 hash}      */
    MSG_BYE       = 7,  /* polite shutdown either way                   */
    MSG_PING      = 8   /* either way: no payload, means "still here"   */
};

#define HASH_RING       16
#define RECV_CHUNK      (64 * 1024)
/* Sanity ceiling on one frame. A joining client's MSG_WORLD carries the
 * whole command log, which on a long-lived server is the biggest thing
 * that ever crosses the wire — at 32 bytes per Command this allows a
 * couple of million of them, well past the point where log truncation
 * has to happen anyway. Anything larger is a hostile or corrupt length
 * field and kills the connection rather than the allocator. */
#define MAX_FRAME_BYTES (64u * 1024u * 1024u)

/* ---- flow control and timeouts ----------------------------
 * None of this existed before SERVER.md's hardening plan, and its
 * absence was the whole of that plan's Phase A: with an unbounded
 * blocking send and an unbounded receive drain, ONE peer could stop the
 * world — either by not reading (the tick loop blocked inside send) or
 * by writing forever (the pump never left the recv loop). Every limit
 * below exists to keep one connection's behaviour to itself.
 *
 * The numbers are deliberately loose. Co-op traffic is bytes per
 * second; anything that trips these is broken or hostile, not busy. */

/* Bytes accepted from one peer in one pump before we move on. The
 * remainder is still in the socket and arrives next pump — which is
 * only what a frame split across TCP segments already did. */
#define RECV_BUDGET     (1u * 1024u * 1024u)

/* Hard ceiling on a peer's unparsed backlog. One maximum frame must fit
 * whole or a legitimate MSG_WORLD could never be assembled, so this is
 * that plus a chunk of slack. */
#define MAX_RECV_BUF    ((size_t)MAX_FRAME_BYTES + 5u + RECV_CHUNK)

/* How long a peer's outbound queue may sit without a single byte moving
 * before we call it dead. This is a stall timer rather than a byte cap
 * on purpose: the MSG_WORLD sent at join is legitimately large, and
 * "slow but draining" must not be confused with "gone". */
#define SEND_STALL_MS   15000u

/* Say nothing for this long and a peer sends MSG_PING; say nothing for
 * that long and it is dropped. Liveness deliberately does NOT ride on
 * MSG_HASH: a guest sitting in the F8 scrubber has a frozen sim and
 * reports no hashes, and is not dead. */
#define PING_INTERVAL_MS  5000u
#define IDLE_TIMEOUT_MS  30000u

/* A connection that has not introduced itself is holding a slot that a
 * real player wants; NET_MAX_PEERS of them is the whole server. */
#define HANDSHAKE_MS    10000u

/* How long net_join waits for a TCP handshake before reporting that the
 * host is not there. (Name resolution ahead of it is still the
 * resolver's to bound — getaddrinfo has no portable timeout, and
 * putting it on a thread would drag a threading dependency into a file
 * that deliberately has none.) */
#define CONNECT_TIMEOUT_MS 5000u

/* Command budget per peer: refilled every tick, allowed to bank up to a
 * burst so that a player who clicks fast, or whose connection delivered
 * a frame's worth at once, is never the one this catches. A peer that
 * keeps overrunning an allowance this generous is not playing. */
#define CMDS_PER_TICK    8
#define CMD_BURST       64
#define MAX_CMD_OVERRUNS 256

/* ---- growable byte buffer ---------------------------------
 * Peers used to hold two fixed 256 KB arrays. That capped MSG_WORLD at
 * ~8000 commands, which a persistent server passes in an afternoon —
 * and the failure mode was a dropped connection at join, the worst
 * possible time to discover it. */
typedef struct {
    unsigned char *b;
    size_t         len;
    size_t         cap;
} Buf;

static int buf_reserve(Buf *buf, size_t extra)
{
    size_t need = buf->len + extra;
    unsigned char *nb;
    size_t ncap;

    if (need <= buf->cap) return 1;

    ncap = buf->cap ? buf->cap : RECV_CHUNK;
    while (ncap < need) ncap *= 2;

    nb = (unsigned char *)realloc(buf->b, ncap);
    if (!nb) return 0;
    buf->b   = nb;
    buf->cap = ncap;
    return 1;
}

static int buf_append(Buf *buf, const void *data, size_t n)
{
    if (n == 0) return 1;
    if (!buf_reserve(buf, n)) return 0;
    memcpy(buf->b + buf->len, data, n);
    buf->len += n;
    return 1;
}

static void buf_consume(Buf *buf, size_t n)
{
    if (n >= buf->len) { buf->len = 0; return; }
    memmove(buf->b, buf->b + n, buf->len - n);
    buf->len -= n;
}

/* Hand back the capacity a one-off burst grew. A join pushes a peer's
 * buffers into the megabytes for as long as MSG_WORLD takes to cross;
 * without this they stay that size for the rest of the session, times
 * NET_MAX_PEERS. Only shrinks a buffer that has gone quiet again. */
static void buf_trim(Buf *buf)
{
    unsigned char *nb;

    if (buf->cap <= RECV_CHUNK || buf->len > RECV_CHUNK / 2) return;

    nb = (unsigned char *)realloc(buf->b, RECV_CHUNK);
    if (!nb) return;            /* keeping the larger block is harmless */
    buf->b   = nb;
    buf->cap = RECV_CHUNK;
}

static void buf_free(Buf *buf)
{
    free(buf->b);
    buf->b   = NULL;
    buf->len = 0;
    buf->cap = 0;
}

/* ---- one connection ---------------------------------------
 * A guest session has exactly one peer (the host); a host session has
 * up to NET_MAX_PEERS. Everything that differs between "the wire" and
 * "the in-memory test transport" is confined to this struct. */
typedef struct NetPeer {
    int      in_use;
    sock_t   fd;
    uint32_t player_id;      /* host side: who this connection speaks as */
    int      said_hello;

    Buf      rbuf;           /* inbound bytes awaiting frame parsing     */
    Buf      wbuf;           /* outbound bytes awaiting a writable socket*/

    /* Wall-clock bookkeeping for the timeouts. Milliseconds from
     * net_now_ms; monotonic, and never part of world state. */
    uint64_t created_ms;
    uint64_t last_recv_ms;
    uint64_t last_send_ms;
    uint64_t stall_since_ms; /* 0 while the outbound queue is moving     */

    /* Submissions this peer may still make before the next tick refills
     * it. Every accepted command is appended to the authoritative log
     * forever and broadcast to everyone, so an unmetered client is a
     * client that can permanently inflate the world. */
    int      cmd_budget;
    int      cmd_overruns;

    /* In-memory transport (net_pair_mem): sends append to mem_peer's
     * queue and recv drains our own; no sockets exist. A closed peer
     * sets mem_severed — we drain what is queued, then report the
     * disconnect exactly as TCP EOF would. */
    int             is_mem;
    struct NetPeer *mem_peer;
    int             mem_severed;
} NetPeer;

struct NetSession {
    int      is_host;
    sock_t   listen_fd;       /* host only                              */
    int      alive;
    int      persistent;      /* host: outlive peers (the server does)  */
    int      plat_init;       /* this session owns a platform startup   */

    NetPeer  peers[NET_MAX_PEERS];

    /* guest: how far the host has authorised us to simulate. */
    uint64_t authorized_tick;
    int      world_installed;
    uint32_t resume_id;       /* identity to ask for at join, 0 = any   */

    /* host: my hash at recent NET_HASH_INTERVAL boundaries, to compare
     * against guests' reports (guests run behind us).
     *
     * The write cursor is separate from the fill count on purpose: they
     * were one field, and since the count stopped at HASH_RING the slot
     * index stopped with it — every boundary after the sixteenth
     * overwrote slot 0 while slots 1-15 kept ticks from the world's
     * first eighty seconds forever. A ring of sixteen that remembers
     * one is not a ring, and the reports that missed it were discarded
     * as "too old" in silence. */
    struct { uint64_t tick, hash; } hash_ring[HASH_RING];
    int      hash_ring_n;     /* how many slots hold anything yet       */
    uint64_t hash_ring_w;     /* monotonic; the next slot to write      */

    char     status[64];
};

static int peer_live(const NetPeer *p)
{
    return p->in_use && (p->is_mem || p->fd != BAD_SOCK);
}

/* A guest's one connection: the host. Was written as `&ns->peers[0]`,
 * which is true only because a fresh session allocates slot 0 first —
 * a fact nothing enforced and a reconnect need not preserve. */
static NetPeer *guest_peer(NetSession *ns)
{
    int i;
    for (i = 0; i < NET_MAX_PEERS; i++)
        if (peer_live(&ns->peers[i])) return &ns->peers[i];
    return NULL;
}

static int session_connected(const NetSession *ns)
{
    int i;
    for (i = 0; i < NET_MAX_PEERS; i++)
        if (peer_live(&ns->peers[i])) return 1;
    return 0;
}

int net_peer_count(const NetSession *ns)
{
    int i, n = 0;
    if (!ns) return 0;
    for (i = 0; i < NET_MAX_PEERS; i++)
        if (peer_live(&ns->peers[i])) n++;
    return n;
}

static NetPeer *peer_alloc(NetSession *ns)
{
    int i;
    for (i = 0; i < NET_MAX_PEERS; i++)
        if (!ns->peers[i].in_use) {
            NetPeer *p   = &ns->peers[i];
            uint64_t now = net_now_ms();
            memset(p, 0, sizeof(*p));
            p->in_use       = 1;
            p->fd           = BAD_SOCK;
            p->created_ms   = now;
            p->last_recv_ms = now;   /* the clocks start on arrival, not */
            p->last_send_ms = now;   /* at zero — see peer_timed_out     */
            p->cmd_budget   = CMD_BURST;
            return p;
        }
    return NULL;
}

/* Drop one connection. The session itself survives unless it is a
 * co-op host/guest pair, where losing the peer IS the end of the
 * session (main.c then continues single-player). */
static void peer_drop(NetSession *ns, NetPeer *p, const char *why)
{
    if (!p->in_use) return;

    if (p->is_mem) {
        if (p->mem_peer) {
            p->mem_peer->mem_severed = 1;
            p->mem_peer->mem_peer    = NULL;
        }
    } else if (p->fd != BAD_SOCK) {
        sock_close(p->fd);
    }
    buf_free(&p->rbuf);
    buf_free(&p->wbuf);
    memset(p, 0, sizeof(*p));
    p->fd = BAD_SOCK;

    if (why) {
        if (ns->persistent)
            sim_log("net: %s", why);
        else
            sim_log("net: %s — continuing single-player", why);
    }
    if (!ns->persistent) ns->alive = 0;
}

/* ---- low-level send/recv ---------------------------------- */

/* Queue one framed message for `p`. NEVER touches the socket: sending
 * used to retry a full buffer forever with 1 ms sleeps, and broadcast()
 * runs from the tick loop, so one peer that stopped reading froze the
 * whole world — including, on the dedicated server, every other
 * player's. The bytes wait in wbuf and peer_flush moves what the socket
 * will take, each pump.
 *
 * Header and payload are reserved together so a failed allocation
 * cannot leave half a frame in the queue: the stream is a sequence of
 * complete frames or it is nothing. */
static int send_msg(NetSession *ns, NetPeer *p, unsigned char type,
                    const void *payload, uint32_t len)
{
    unsigned char hdr[5];
    Buf          *out;

    if (!peer_live(p) || !ns->alive) return 0;

    hdr[0] = type;
    hdr[1] = (unsigned char)(len);
    hdr[2] = (unsigned char)(len >> 8);
    hdr[3] = (unsigned char)(len >> 16);
    hdr[4] = (unsigned char)(len >> 24);

    /* The in-memory transport has no socket to be busy: a send is an
     * append straight onto the far side's inbound queue. */
    out = p->is_mem ? (p->mem_peer ? &p->mem_peer->rbuf : NULL) : &p->wbuf;
    if (!out) return 0;                                /* peer closed   */

    if (!buf_reserve(out, 5u + (size_t)len)) return 0;
    buf_append(out, hdr, 5);
    if (len) buf_append(out, payload, len);

    p->last_send_ms = net_now_ms();
    return 1;
}

/* Move what the socket will take. Returns 0 on a dead connection.
 *
 * Progress, or an empty queue, clears the stall clock; a queue that
 * holds bytes the socket refused starts it. peer_timed_out turns a
 * clock that has been running too long into a disconnect — which is the
 * bounded version of what the old retry loop did forever. */
static int peer_flush(NetPeer *p)
{
    size_t sent = 0;

    if (p->is_mem || p->fd == BAD_SOCK) return 1;

    while (sent < p->wbuf.len) {
        long w = (long)send(p->fd, (const char *)p->wbuf.b + sent,
                            SOCK_IOLEN(p->wbuf.len - sent), SEND_FLAGS);
        if (w > 0) { sent += (size_t)w; continue; }
        if (w < 0 && sock_errno() == SOCK_EWOULDBLOCK) break;
        return 0;
    }

    if (sent > 0) buf_consume(&p->wbuf, sent);

    if (p->wbuf.len == 0 || sent > 0) p->stall_since_ms = 0;
    else if (p->stall_since_ms == 0)  p->stall_since_ms = net_now_ms();

    if (p->wbuf.len == 0) buf_trim(&p->wbuf);
    return 1;
}

/* Every peer, one message. Send failures are not fatal here: the next
 * pump sees the dead socket and drops that peer alone. */
static void broadcast(NetSession *ns, unsigned char type,
                      const void *payload, uint32_t len)
{
    int i;
    for (i = 0; i < NET_MAX_PEERS; i++)
        if (peer_live(&ns->peers[i]))
            send_msg(ns, &ns->peers[i], type, payload, len);
}

/* Drain the socket into the peer's buffer. Returns 0 on a dead
 * connection (or an allocation failure, which we treat the same way —
 * a peer we cannot buffer is a peer we cannot serve).
 *
 * Bounded twice. RECV_BUDGET caps how long we stay here, because this
 * used to loop until the socket ran dry and a peer writing at line rate
 * could hold the pump — and therefore the tick loop — indefinitely.
 * MAX_RECV_BUF caps how much we will hold for a peer that never
 * completes a frame. Hitting the budget is not an error: the rest of
 * the bytes are still in the socket, and waiting a pump for them is
 * exactly what a frame split across TCP segments already does. */
static int recv_into_buf(NetPeer *p)
{
    size_t taken = 0;

    if (p->is_mem) {
        /* Sends land directly in rbuf; nothing to drain. EOF is the
         * severed flag once everything queued has been parsed. */
        return !(p->mem_severed && p->rbuf.len == 0);
    }

    while (taken < RECV_BUDGET) {
        long   r;
        size_t room;

        if (p->rbuf.len >= MAX_RECV_BUF) return 0;   /* hostile backlog */
        room = MAX_RECV_BUF - p->rbuf.len;
        if (room > RECV_CHUNK) room = RECV_CHUNK;

        if (!buf_reserve(&p->rbuf, room)) return 0;
        r = (long)recv(p->fd, (char *)p->rbuf.b + p->rbuf.len,
                       SOCK_IOLEN(room), 0);
        if (r > 0) {
            p->rbuf.len += (size_t)r;
            taken       += (size_t)r;
            continue;
        }
        if (r == 0) return 0;                     /* orderly close */
        if (sock_errno() == SOCK_EWOULDBLOCK) break;
        return 0;                                 /* hard error    */
    }

    if (taken > 0) p->last_recv_ms = net_now_ms();
    return 1;
}

/* Has this connection run out of the patience we extend it? Every
 * clause is a way a peer can occupy a slot while giving nothing back.
 *
 * The in-memory transport is exempt: it has no sockets, no latency and
 * no way to stall, and making the protocol tests depend on wall time
 * would trade a deterministic suite for a flaky one. */
/* Elapsed milliseconds, saturating at zero when `then` is AFTER `now`.
 *
 * That is not a hypothetical ordering. net_pump samples `now` once at
 * the top and then accepts connections, so a peer created later in the
 * same call carries a timestamp a millisecond or two in the FUTURE
 * relative to it. These are uint64_t: the plain subtraction wraps to
 * something near 2^64, every timeout fires at once, and a peer is
 * dropped in the same breath as it connects.
 *
 * It is a race on whether a millisecond boundary falls between two
 * calls, so it hides completely on a fast machine and shows up on a
 * loaded CI runner as a connection that "went silent" two seconds
 * after saying hello. */
static uint64_t ms_since(uint64_t now, uint64_t then)
{
    return now > then ? now - then : 0;
}

static int peer_timed_out(const NetSession *ns, const NetPeer *p,
                          uint64_t now, const char **why)
{
    if (p->is_mem) return 0;

    if (ns->is_host && !p->said_hello &&
        ms_since(now, p->created_ms) > HANDSHAKE_MS) {
        *why = "peer connected but never introduced itself";
        return 1;
    }
    if (ms_since(now, p->last_recv_ms) > IDLE_TIMEOUT_MS) {
        *why = "peer went silent";
        return 1;
    }
    if (p->stall_since_ms &&
        ms_since(now, p->stall_since_ms) > SEND_STALL_MS) {
        *why = "peer stopped reading";
        return 1;
    }
    return 0;
}

/* ---- host: identity ---------------------------------------- */

static int id_connected(const NetSession *ns, uint32_t id)
{
    int i;
    for (i = 0; i < NET_MAX_PEERS; i++)
        if (peer_live(&ns->peers[i]) && ns->peers[i].said_hello &&
            ns->peers[i].player_id == id)
            return 1;
    return 0;
}

static int id_owns_island(const GameState *gs, uint32_t id)
{
    int i;
    for (i = 0; i < MAX_ISLANDS; i++)
        if (gs->islands[i].owner == id) return 1;
    return 0;
}

/* Who is this connection? A client may ask to resume an identity it
 * held in a previous session (`--as N`): the whole point of a server
 * that keeps ticking while you are away is that you can come back to
 * the island you left. The request is honoured only if that player
 * actually exists in the world and nobody is currently connected as
 * them — thin, but this protocol has no authentication at all, which
 * is why net.h says so out loud.
 *
 * Otherwise: the lowest id that neither owns an island nor is connected,
 * so a fresh player can never inherit an absent one's holdings. */
static uint32_t host_assign_id(NetSession *ns, const GameState *gs,
                               uint32_t resume)
{
    uint32_t id;

    if (resume != PLAYER_NONE && resume != gs->local_player_id &&
        !id_connected(ns, resume) && id_owns_island(gs, resume))
        return resume;

    for (id = 1u; id < 1000u; id++)
        if (!id_owns_island(gs, id) && !id_connected(ns, id) &&
            id != gs->local_player_id)
            return id;
    return 0u;   /* absurd; caller refuses the join */
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
    broadcast(ns, MSG_CMD, &stamped, (uint32_t)sizeof(stamped));
    return 1;
}

/* MSG_WORLD is a SNAPSHOT plus the commands that follow it, not the
 * seed and the whole history (SERVER.md, "Log truncation").
 *
 * It used to be the latter, and the cost of joining was therefore
 * proportional to the world's AGE: game_install_world replayed every
 * tick from zero, inside net_pump, with the client's window frozen for
 * the duration. A month-old server was a minute of that; a year was
 * nine. It accrued whether or not anybody had played, because the
 * server ticks regardless — nothing a player did made it better and
 * nothing they could do made it stop.
 *
 * A snapshot is tens of KB and installs in the time it takes to decode:
 * the join cost stops tracking how long the world has existed and
 * starts tracking how big it is, which is bounded. */
static void host_send_world(NetSession *ns, NetPeer *p, const GameState *gs)
{
    size_t         fixed = sizeof(uint64_t) + sizeof(int32_t) + sizeof(int32_t);
    unsigned char *snap = NULL, *buf;
    size_t         snap_len = 0, total, off = 0;
    int32_t        n = gs->cmd_count - gs->cmd_applied;
    int32_t        snap32;

    if (n < 0) n = 0;

    if (!snapshot_encode(gs, &snap, &snap_len)) {
        sim_log("net: could not snapshot the world for player %u",
                p->player_id);
        return;
    }
    snap32 = (int32_t)snap_len;

    total = fixed + snap_len + sizeof(Command) * (size_t)n;
    buf   = (unsigned char *)malloc(total);
    if (!buf) { free(snap); return; }

    memcpy(buf + off, &gs->sim_tick_no, sizeof(uint64_t)); off += sizeof(uint64_t);
    memcpy(buf + off, &snap32, sizeof(int32_t));           off += sizeof(int32_t);
    memcpy(buf + off, &n, sizeof(int32_t));                off += sizeof(int32_t);
    memcpy(buf + off, snap, snap_len);                     off += snap_len;
    /* Only the unapplied tail: the snapshot already contains the effect
     * of everything before it. */
    if (n > 0)
        memcpy(buf + off, gs->cmd_log + gs->cmd_applied,
               sizeof(Command) * (size_t)n);

    send_msg(ns, p, MSG_WORLD, buf, (uint32_t)total);
    free(buf);
    free(snap);
    sim_log("net: world sent to player %u (tick %llu, %llu-byte snapshot, "
            "%d pending commands)",
            p->player_id, (unsigned long long)gs->sim_tick_no,
            (unsigned long long)snap_len, n);
}

/* A freshly joined player that owns nothing gets a starting island —
 * expressed as a logged command like everything else, so replay and
 * the client's own copy of history agree that the join happened. */
static void host_grant_if_landless(NetSession *ns, GameState *gs,
                                   uint32_t player)
{
    Command c;
    int     taken[MAX_ISLANDS];
    int     i, j, target = -1;

    if (id_owns_island(gs, player)) return;   /* welcome back */

    /* Grants are stamped NET_CMD_DELAY_TICKS into the future like every
     * other command, so two players joining inside that window would
     * both be looking at a world where nothing has been granted yet —
     * and would both be promised the SAME island, the second grant then
     * failing validation and leaving that player landless with no
     * indication why. The pending tail of the log is the missing half of
     * the picture: read it as "already spoken for". */
    for (i = 0; i < MAX_ISLANDS; i++) taken[i] = 0;
    for (j = gs->cmd_applied; j < gs->cmd_count; j++) {
        const Command *pc = &gs->cmd_log[j];
        if (pc->kind != CMD_GRANT_START) continue;
        if (pc->player_id == player) return;          /* already promised */
        if (pc->a >= 0 && pc->a < MAX_ISLANDS) taken[pc->a] = 1;
    }

    for (i = 0; i < MAX_ISLANDS; i++)
        if (!taken[i] && !gs->islands[i].settled &&
            gs->islands[i].owner == PLAYER_NONE) { target = i; break; }
    if (target < 0) { sim_log("net: no island left to grant"); return; }

    memset(&c, 0, sizeof(c));
    c.kind = CMD_GRANT_START;
    c.a    = target;
    host_stamp_log_send(ns, gs, &c, player);
    sim_log("net: granted island %d to player %u", target, player);
}

static void host_note_hash_mismatch(NetSession *ns, NetPeer *p, GameState *gs,
                                    uint64_t tick, uint64_t theirs)
{
    int i;
    for (i = 0; i < ns->hash_ring_n; i++) {
        if (ns->hash_ring[i].tick != tick) continue;
        if (ns->hash_ring[i].hash == theirs) return;   /* in sync */
        sim_log("net: DESYNC with player %u at tick %llu "
                "(host %016llx guest %016llx) — resyncing by full replay",
                p->player_id, (unsigned long long)tick,
                (unsigned long long)ns->hash_ring[i].hash,
                (unsigned long long)theirs);
        host_send_world(ns, p, gs);
        return;
    }
    /* Tick already left the ring: harmless — the next report will hit. */
}

/* ---- message dispatch -------------------------------------- */

static void handle_msg(NetSession *ns, NetPeer *p, GameState *gs,
                       unsigned char type, const unsigned char *payload,
                       uint32_t len)
{
    if (ns->is_host) {
        switch (type) {
        case MSG_HELLO: {
            uint32_t ver = 0, resume = PLAYER_NONE, id;
            /* Exactly one introduction per connection. A second HELLO
             * used to re-run identity assignment — which, seeing this
             * peer's current id as connected and landed, handed out a
             * FRESH one, re-sent the whole command log, and granted
             * another starting island. Repeat it enough and one socket
             * owns the archipelago. */
            if (p->said_hello) {
                peer_drop(ns, p, "peer said hello twice");
                return;
            }
            if (len >= 4) memcpy(&ver, payload, 4);
            if (len >= 8) memcpy(&resume, payload + 4, 4);
            if (ver != NET_PROTO_VERSION) {
                sim_log("net: client speaks proto %u, we speak %u — bye",
                        ver, NET_PROTO_VERSION);
                peer_drop(ns, p, "protocol mismatch");
                return;
            }
            id = host_assign_id(ns, gs, resume);
            if (id == PLAYER_NONE) {
                peer_drop(ns, p, "no player id available");
                return;
            }
            p->player_id  = id;
            p->said_hello = 1;
            send_msg(ns, p, MSG_WELCOME, &id, sizeof(id));
            host_send_world(ns, p, gs);
            host_grant_if_landless(ns, gs, id);
            sim_log("net: client joined as player %u (%d connected)",
                    id, net_peer_count(ns));
            break;
        }
        case MSG_CMD:
            if (len == sizeof(Command) && p->said_hello) {
                Command c;

                /* Metered. Everything accepted here joins the
                 * authoritative log — which is never truncated, is
                 * replayed in full by every future joiner, and is what
                 * a checkpoint consists of — and is broadcast to every
                 * peer besides. An unmetered client is therefore a
                 * client that can permanently inflate the world for
                 * everyone, without exploiting anything: it need only
                 * submit faster than people click. */
                if (p->cmd_budget <= 0) {
                    if (++p->cmd_overruns > MAX_CMD_OVERRUNS) {
                        peer_drop(ns, p, "peer flooded the command log");
                        return;
                    }
                    break;      /* over budget: dropped, not fatal yet */
                }
                p->cmd_budget--;

                memcpy(&c, payload, sizeof(c));
                /* Identity comes from the CONNECTION, never the wire. */
                host_stamp_log_send(ns, gs, &c, p->player_id);
            }
            break;
        case MSG_HASH:
            if (len == 16) {
                uint64_t tick, hash;
                memcpy(&tick, payload, 8);
                memcpy(&hash, payload + 8, 8);
                host_note_hash_mismatch(ns, p, gs, tick, hash);
            }
            break;
        case MSG_PING:
            /* Nothing to do: simply arriving is the whole message, and
             * recv_into_buf has already reset the idle clock. */
            break;
        case MSG_BYE:
            peer_drop(ns, p, "peer said goodbye");
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
            memcpy(&id, payload, 4);
            gs->local_player_id = id;
            ns->resume_id       = id;   /* what to ask for next time */
            sim_log("net: we are player %u", id);
        }
        break;
    case MSG_WORLD: {
        uint64_t tick;
        int32_t  snap32, n;
        size_t   fixed = sizeof(uint64_t) + sizeof(int32_t) + sizeof(int32_t);
        size_t   snap_len;

        if (len < fixed) break;
        memcpy(&tick,   payload, 8);
        memcpy(&snap32, payload + 8, 4);
        memcpy(&n,      payload + 12, 4);
        if (snap32 <= 0 || n < 0) break;
        snap_len = (size_t)snap32;
        if (fixed + snap_len + sizeof(Command) * (size_t)n != len) break;

        /* No replay from zero any more, so no unbounded loop to bound:
         * the snapshot IS the starting point, and installing it costs
         * the same whatever tick it claims. The only remaining work is
         * the pending tail, which is a handful of commands by
         * construction (NET_CMD_DELAY_TICKS of them). */
        if (!game_install_from_snapshot(gs, payload + fixed, snap_len, tick,
                                        (const Command *)(payload + fixed +
                                                          snap_len), n)) {
            sim_log("net: failed to install world");
            peer_drop(ns, p, "could not install the host's world");
            break;
        }
        /* Everything at or before the install point is authorised by
         * construction; later ticks wait for MSG_TICK_AUTH. */
        ns->authorized_tick = tick;
        ns->world_installed = 1;
        sim_log("net: world installed at tick %llu",
                (unsigned long long)tick);
        break;
    }
    case MSG_CMD:
        if (len == sizeof(Command) && ns->world_installed) {
            Command c;
            memcpy(&c, payload, sizeof(c));
            command_log_append(gs, &c);   /* applies at its stamped tick */
        }
        break;
    case MSG_TICK_AUTH:
        if (len == 8) {
            uint64_t t;
            memcpy(&t, payload, 8);
            if (t > ns->authorized_tick) ns->authorized_tick = t;
        }
        break;
    case MSG_PING:
        break;                                /* liveness only, as above */
    case MSG_BYE:
        peer_drop(ns, p, "peer said goodbye");
        break;
    default: break;
    }
}

/* Parse complete frames out of a peer's buffer. */
static void parse_frames(NetSession *ns, NetPeer *p, GameState *gs)
{
    size_t off = 0;

    while (p->in_use && p->rbuf.len - off >= 5) {
        unsigned char type = p->rbuf.b[off];
        uint32_t len =  (uint32_t)p->rbuf.b[off + 1]
                     | ((uint32_t)p->rbuf.b[off + 2] << 8)
                     | ((uint32_t)p->rbuf.b[off + 3] << 16)
                     | ((uint32_t)p->rbuf.b[off + 4] << 24);
        if (len > MAX_FRAME_BYTES) {              /* hostile or corrupt */
            peer_drop(ns, p, "peer sent an impossible frame length");
            return;
        }
        if (p->rbuf.len - off - 5 < len) break;   /* partial            */
        handle_msg(ns, p, gs, type, p->rbuf.b + off + 5, len);
        off += 5 + (size_t)len;
    }
    if (off > 0 && p->in_use) buf_consume(&p->rbuf, off);
}

/* ---- lifecycle --------------------------------------------- */

static NetSession *session_new(int is_host)
{
    NetSession *ns = (NetSession *)calloc(1, sizeof(NetSession));
    int         i;

    if (!ns) return NULL;
    ns->is_host   = is_host;
    ns->listen_fd = BAD_SOCK;
    ns->alive     = 1;
    for (i = 0; i < NET_MAX_PEERS; i++) ns->peers[i].fd = BAD_SOCK;
    return ns;
}

/* A bound listening socket, dual-stack where the platform allows it.
 *
 * One AF_INET6 socket with IPV6_V6ONLY cleared accepts both families,
 * so which address a player reaches the server on stops being something
 * anyone has to think about. Some platforms default that option on and
 * refuse to clear it, which is what the IPv4 fallback is for. */
static sock_t listen_socket(uint16_t port)
{
    struct sockaddr_in6 a6;
    struct sockaddr_in  a4;
    sock_t              s;
    int                 yes = 1, no = 0;

    s = socket(AF_INET6, SOCK_STREAM, 0);
    if (s != BAD_SOCK) {
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
                   (const char *)&yes, sizeof(yes));
        setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY,
                   (const char *)&no, sizeof(no));
        memset(&a6, 0, sizeof(a6));
        a6.sin6_family = AF_INET6;
        a6.sin6_addr   = in6addr_any;
        a6.sin6_port   = htons(port);
        if (bind(s, (struct sockaddr *)&a6, sizeof(a6)) == 0) return s;
        sock_close(s);
    }

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == BAD_SOCK) return BAD_SOCK;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));
    memset(&a4, 0, sizeof(a4));
    a4.sin_family      = AF_INET;
    a4.sin_addr.s_addr = htonl(INADDR_ANY);
    a4.sin_port        = htons(port);
    if (bind(s, (struct sockaddr *)&a4, sizeof(a4)) == 0) return s;

    sock_close(s);
    return BAD_SOCK;
}

NetSession *net_host(uint16_t port)
{
    NetSession *ns;

    if (!net_platform_init()) return NULL;
    ns = session_new(1);
    if (!ns) { net_platform_quit(); return NULL; }
    ns->plat_init = 1;

    ns->listen_fd = listen_socket(port);
    if (ns->listen_fd == BAD_SOCK) goto fail;
    if (listen(ns->listen_fd, NET_MAX_PEERS) != 0) goto fail;
    if (!sock_set_nonblock(ns->listen_fd)) goto fail;

    sim_log("net: hosting on port %u", port);
    return ns;

fail:
    sim_log("net: failed to host on port %u", port);
    net_close(ns);
    return NULL;
}

void net_set_persistent(NetSession *ns, int persistent)
{
    if (ns) ns->persistent = persistent ? 1 : 0;
}

NetSession *net_join(const char *host, uint16_t port, uint32_t resume_id)
{
    NetSession      *ns;
    NetPeer         *p;
    struct addrinfo  hints, *res = NULL, *ai;
    char             portstr[8];
    unsigned char    hello[8];
    uint32_t         ver = NET_PROTO_VERSION;

    if (!net_platform_init()) return NULL;
    ns = session_new(0);
    if (!ns) { net_platform_quit(); return NULL; }
    ns->plat_init = 1;
    ns->resume_id = resume_id;

    p = peer_alloc(ns);
    if (!p) goto fail;

    memset(&hints, 0, sizeof(hints));
    /* AF_UNSPEC, not AF_INET: an address family is not something a
     * player should have to know they have. Whatever the name resolves
     * to — v4, v6, or both in whatever order the resolver prefers — is
     * tried in turn below. */
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(portstr, sizeof(portstr), "%u", (unsigned)port);
    if (getaddrinfo(host, portstr, &hints, &res) != 0) goto fail;

    for (ai = res; ai; ai = ai->ai_next) {
        p->fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (p->fd == BAD_SOCK) continue;
        if (sock_connect_timeout(p->fd, ai->ai_addr, ai->ai_addrlen,
                                 CONNECT_TIMEOUT_MS))
            break;
        sock_close(p->fd);
        p->fd = BAD_SOCK;
    }
    freeaddrinfo(res);
    if (p->fd == BAD_SOCK) goto fail;
    sock_tune(p->fd);          /* already non-blocking from the connect */

    memcpy(hello,     &ver,       4);
    memcpy(hello + 4, &resume_id, 4);
    send_msg(ns, p, MSG_HELLO, hello, sizeof(hello));
    sim_log("net: connected to %s:%u, awaiting world", host, port);
    return ns;

fail:
    sim_log("net: could not join %s:%u", host, port);
    net_close(ns);
    return NULL;
}

void net_close(NetSession *ns)
{
    int i;

    if (!ns) return;

    for (i = 0; i < NET_MAX_PEERS; i++) {
        NetPeer *p = &ns->peers[i];
        if (!p->in_use) continue;
        if (p->is_mem) {
            if (p->mem_peer) {              /* sever the surviving side */
                p->mem_peer->mem_severed = 1;
                p->mem_peer->mem_peer    = NULL;
            }
        } else if (p->fd != BAD_SOCK) {
            /* The goodbye is queued like everything else, so it needs
             * one flush to actually leave. Best effort by design: if
             * the socket will not take five bytes right now, the peer
             * finds out the way it would from a yanked cable, and both
             * paths are already handled. */
            send_msg(ns, p, MSG_BYE, NULL, 0);
            peer_flush(p);
            sock_close(p->fd);
        }
        buf_free(&p->rbuf);
        buf_free(&p->wbuf);
    }

    if (ns->listen_fd != BAD_SOCK) sock_close(ns->listen_fd);

    /* Paired with the net_platform_init that this session did, not with
     * whether it ever managed to open a socket: on Windows those calls
     * are refcounted, and a session that failed between startup and its
     * first socket used to leak one. */
    if (ns->plat_init) net_platform_quit();
    free(ns);
}

/* ---- the in-memory transport ------------------------------- */

static NetSession *mem_guest_for(NetSession *host, uint32_t resume_id)
{
    NetSession   *g  = session_new(0);
    NetPeer      *hp, *gp;
    unsigned char hello[8];
    uint32_t      ver = NET_PROTO_VERSION;

    if (!g) return NULL;
    g->resume_id = resume_id;

    hp = peer_alloc(host);
    gp = peer_alloc(g);
    if (!hp || !gp) { net_close(g); return NULL; }

    hp->is_mem = 1;  gp->is_mem = 1;
    hp->mem_peer = gp;
    gp->mem_peer = hp;

    /* The same opening move a TCP client makes; the host's next pump
     * answers with WELCOME + WORLD + the grant, all through the queues. */
    memcpy(hello,     &ver,       4);
    memcpy(hello + 4, &resume_id, 4);
    send_msg(g, gp, MSG_HELLO, hello, sizeof(hello));
    return g;
}

NetSession *net_pair_mem(NetSession **out_guest)
{
    NetSession *h = session_new(1);
    NetSession *g;

    if (!h) return NULL;
    g = mem_guest_for(h, PLAYER_NONE);
    if (!g) { net_close(h); return NULL; }

    *out_guest = g;
    return h;
}

NetSession *net_join_mem(NetSession *host, uint32_t resume_id)
{
    if (!host || !host->is_host) return NULL;
    return mem_guest_for(host, resume_id);
}

/* ---- per-frame driving ------------------------------------- */

int net_pump(NetSession *ns, GameState *gs)
{
    uint64_t now;
    int      i;

    if (!ns->alive) return 0;

    /* Host: accept everyone waiting, up to the peer limit. */
    if (ns->is_host && ns->listen_fd != BAD_SOCK) {
        for (;;) {
            sock_t   c;
            NetPeer *p;

            if (net_peer_count(ns) >= NET_MAX_PEERS) break;
            c = accept(ns->listen_fd, NULL, NULL);
            if (c == BAD_SOCK) break;
            if (!sock_set_nonblock(c)) { sock_close(c); break; }
            sock_tune(c);

            p = peer_alloc(ns);
            if (!p) { sock_close(c); break; }
            p->fd = c;
            sim_log("net: connection accepted");
        }
    }

    /* Sampled AFTER the accept loop, so no peer admitted above carries
     * a timestamp later than it. ms_since() would absorb that anyway;
     * this removes the inversion rather than tolerating it, and the two
     * together are why a peer can no longer be timed out in the same
     * pump that accepted it. */
    now = net_now_ms();

    for (i = 0; i < NET_MAX_PEERS; i++) {
        NetPeer    *p   = &ns->peers[i];
        const char *why = NULL;

        if (!peer_live(p)) continue;

        /* Push before pulling: whatever queued since the last pump —
         * this tick's authorisation, a broadcast command, a world —
         * goes out as far as the socket will take it. */
        if (!peer_flush(p)) {
            peer_drop(ns, p, "peer disconnected");
            continue;
        }

        if (!recv_into_buf(p)) {
            /* The peer is gone. Frames already buffered are real,
             * ordered data — apply them before declaring the death,
             * so nothing the peer said gets dropped on the floor. */
            parse_frames(ns, p, gs);
            if (p->in_use) peer_drop(ns, p, "peer disconnected");
            continue;
        }
        parse_frames(ns, p, gs);
        if (!ns->alive) break;
        if (!p->in_use) continue;      /* a frame cost it the connection */

        if (peer_timed_out(ns, p, now, &why)) {
            peer_drop(ns, p, why);
            continue;
        }

        /* Say something before the far side's idle clock runs out. Two
         * silent peers would otherwise each conclude the other had
         * died, which is the one failure mode a keepalive exists to
         * prevent. */
        if (!p->is_mem && ms_since(now, p->last_send_ms) >= PING_INTERVAL_MS)
            send_msg(ns, p, MSG_PING, NULL, 0);
    }

    return ns->alive;
}

void net_after_update(NetSession *ns, GameState *gs)
{
    if (!ns->alive) return;

    if (ns->is_host) {
        /* Broadcast my clock: every tick strictly below it is complete
         * (all its commands were stamped >= NET_CMD_DELAY_TICKS ago and
         * sent, in order, before this message). */
        uint64_t horizon = gs->sim_tick_no;

        if (session_connected(ns))
            broadcast(ns, MSG_TICK_AUTH, &horizon, sizeof(horizon));
    }
}

void net_on_tick(NetSession *ns, GameState *gs)
{
    int i;

    if (!ns || !ns->alive) return;

    /* Refill submission allowances. Banked up to a burst so that a
     * player clicking quickly, or a frame that delivered several
     * commands at once, is never what this catches. */
    if (ns->is_host) {
        for (i = 0; i < NET_MAX_PEERS; i++) {
            NetPeer *p = &ns->peers[i];
            if (!peer_live(p)) continue;
            p->cmd_budget += CMDS_PER_TICK;
            if (p->cmd_budget > CMD_BURST) p->cmd_budget = CMD_BURST;
        }
    }

    if (gs->sim_tick_no % NET_HASH_INTERVAL != 0) return;

    if (ns->is_host) {
        int slot = (int)(ns->hash_ring_w % (uint64_t)HASH_RING);
        ns->hash_ring[slot].tick = gs->sim_tick_no;
        ns->hash_ring[slot].hash = sim_hash(gs);
        ns->hash_ring_w++;
        if (ns->hash_ring_n < HASH_RING) ns->hash_ring_n++;
    } else if (ns->world_installed && session_connected(ns)) {
        unsigned char pl[16];
        uint64_t      t = gs->sim_tick_no, h = sim_hash(gs);
        memcpy(pl,     &t, 8);
        memcpy(pl + 8, &h, 8);
        broadcast(ns, MSG_HASH, pl, sizeof(pl));
    }
}

int net_submit_local(NetSession *ns, GameState *gs, const Command *c)
{
    NetPeer *p;

    if (!ns->alive) return 0;   /* fall back to offline stamping */

    if (ns->is_host) {
        if (!session_connected(ns)) return 0;    /* nobody joined yet */
        return host_stamp_log_send(ns, gs, c, gs->local_player_id);
    }

    /* Guest: upstream to the authority; it returns stamped.
     *
     * Claimed either way, even if the send failed. command_submit
     * treats a 0 here as "the session declined, stamp it locally" —
     * which on a guest means applying a command the host will never
     * see, i.e. quietly forking the world off the authoritative log.
     * A dropped click is a far smaller thing than a divergent world,
     * and the failed write is not lost information: the next pump sees
     * the dead socket, tears the session down, and submissions after
     * that legitimately take the offline path. */
    p = guest_peer(ns);
    if (!p) return 0;                  /* session already torn down */
    send_msg(ns, p, MSG_CMD, c, (uint32_t)sizeof(*c));
    return 1;
}

/* ---- attach / detach ---------------------------------------
 * The sim library knows nothing about net.c (MMO_PLAN Phase 6), so a
 * session reaches command_submit through a function pointer rather than
 * a link-time dependency. Attaching sets both halves together; detaching
 * clears both, which is what leaves the world running single-player
 * after a disconnect. */
void net_attach(GameState *gs, NetSession *ns)
{
    gs->net        = ns;
    gs->net_submit = ns ? net_submit_local : NULL;
}

void net_detach(GameState *gs)
{
    net_attach(gs, NULL);
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

uint32_t net_resume_id(const NetSession *ns)
{
    return ns ? ns->resume_id : PLAYER_NONE;
}

const char *net_status(const NetSession *ns)
{
    NetSession *m = (NetSession *)ns;   /* status[] is scratch space */
    int         n = net_peer_count(ns);

    if (!ns->alive)
        snprintf(m->status, sizeof(m->status), "NET: disconnected");
    else if (ns->is_host && n == 0)
        snprintf(m->status, sizeof(m->status), "HOST: waiting for players");
    else if (ns->is_host)
        snprintf(m->status, sizeof(m->status), "HOST: %d connected", n);
    else
        snprintf(m->status, sizeof(m->status),
                 "GUEST: authorised to tick %llu",
                 (unsigned long long)ns->authorized_tick);
    return m->status;
}
