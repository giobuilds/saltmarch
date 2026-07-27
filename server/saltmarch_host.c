/*  saltmarch_host.c  --  the persistent server (MMO_PLAN Phase 6)
 *
 *  A thin process around libsaltmarch_sim. It owns the canonical command
 *  log, stamps ticks in real time — including while nobody is connected,
 *  which is where "offline progression" actually lives — accepts Command
 *  submissions from clients, broadcasts the ordered stream, and
 *  checkpoints the world so a restart resumes where it left off.
 *
 *  There is deliberately no server-side game logic here. Everything this
 *  file does is clock, sockets and files; the world is the same
 *  sim_run_one_tick() the client runs, and the protocol is the same
 *  net.c the co-op host uses. That is the whole Phase 6 claim: the
 *  server is not a second implementation of the game.
 *
 *  Usage:
 *    saltmarch_host [--port N] [--world FILE] [--seed N]
 *                   [--checkpoint-seconds N] [--ticks N] [--quiet]
 *
 *    --world FILE      load this checkpoint if it exists, else create a
 *                      world and checkpoint to it (default world.smlog)
 *    --seed N          seed for a NEW world (ignored when loading)
 *    --checkpoint-seconds N   how often to write it (default 60, 0 = only
 *                      at shutdown)
 *    --ticks N         run N ticks as fast as real time allows, then exit
 *                      — how the tests and CI drive it
 *    --ghost FILE:N    seed island N with the recorded session in FILE
 *                      as an NPC neighbour (repeatable). The behaviour
 *                      is somebody's actual play, replayed — there is
 *                      no AI in this program.
 *    --quiet           silence the sim's own narration
 *
 *  Ctrl-C (SIGINT) or SIGTERM writes a final checkpoint and exits 0.
 */

#include "game.h"
#include "ghost_faction.h"
#include "net.h"
#include "simclock.h"
#include "simlog.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <time.h>
#endif

/* ---- the one clock -----------------------------------------
 * Wall time enters the world here and nowhere else: this counter is
 * converted into whole ticks by the loop below, exactly as the client's
 * frame does. Monotonic, so a clock adjustment cannot rewind the world
 * or spend an hour of accumulator in one iteration. */
static uint64_t now_ns(void)
{
#ifdef _WIN32
    static LARGE_INTEGER freq;
    LARGE_INTEGER        c;
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&c);
    return (uint64_t)((double)c.QuadPart / (double)freq.QuadPart * 1e9);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
#endif
}

static void sleep_ms(unsigned ms)
{
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec ts;
    ts.tv_sec  = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

/* Set from the signal handler; the loop notices it at the next
 * iteration and shuts down in the ordinary way (final checkpoint, then
 * close). Nothing is done in the handler itself beyond this store. */
static volatile sig_atomic_t g_stop = 0;
static void on_signal(int sig) { (void)sig; g_stop = 1; }

/* Does this file exist and look readable? Used to decide "resume" vs
 * "create", so the same command line works on first run and every run
 * after it. */
static int file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static void usage(const char *argv0)
{
    fprintf(stderr,
        "usage: %s [--port N] [--world FILE] [--seed N]\n"
        "          [--checkpoint-seconds N] [--ticks N] [--quiet]\n",
        argv0);
}

int main(int argc, char *argv[])
{
    const char *world_path   = "world.smlog";
    uint16_t    port         = NET_DEFAULT_PORT;
    uint32_t    seed         = 1u;
    uint64_t    ckpt_seconds = 60;
    uint64_t    run_ticks    = 0;      /* 0 = forever */
    int         quiet        = 0;
    int         i;
    const char *ghosts[MAX_ISLANDS];
    int         ghost_islands[MAX_ISLANDS];
    int         ghost_count  = 0;

    GameState  *gs;
    NetSession *ns;
    uint64_t    last_ns, acc_ns = 0, ckpt_ns = 0, report_ns = 0;
    uint64_t    start_tick;
    int         rc = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc)
            port = (uint16_t)strtoul(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--world") == 0 && i + 1 < argc)
            world_path = argv[++i];
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
            seed = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--checkpoint-seconds") == 0 && i + 1 < argc)
            ckpt_seconds = strtoull(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--ticks") == 0 && i + 1 < argc)
            run_ticks = strtoull(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--ghost") == 0 && i + 1 < argc &&
                 ghost_count < MAX_ISLANDS) {
            /* FILE:N — a recorded session and the island to run it on. */
            char *spec = argv[++i];
            char *colon = strrchr(spec, ':');
            if (!colon) { usage(argv[0]); return 2; }
            *colon = '\0';
            ghosts[ghost_count]        = spec;
            ghost_islands[ghost_count] = (int)strtol(colon + 1, NULL, 10);
            ghost_count++;
        }
        else if (strcmp(argv[i], "--quiet") == 0)
            quiet = 1;
        else { usage(argv[0]); return 2; }
    }

    if (quiet) sim_log_set_enabled(0);

    gs = game_init();
    if (!gs) { fprintf(stderr, "host: out of memory\n"); return 1; }

    /* The server is not a player. Leaving local_player_id at PLAYER_NONE
     * means it holds no island, submits no commands of its own, and —
     * the part that matters — never occupies an identity a returning
     * client might want to resume (see host_assign_id in net.c). */
    gs->local_player_id = PLAYER_NONE;

    if (file_exists(world_path)) {
        if (!game_load(gs, world_path)) {
            fprintf(stderr, "host: %s exists but could not be loaded; "
                            "move it aside or point --world elsewhere\n",
                    world_path);
            game_free(gs);
            return 1;
        }
        printf("host: resumed %s at tick %llu (%d commands)\n",
               world_path, (unsigned long long)gs->sim_tick_no,
               gs->cmd_count);
    } else {
        game_new_seeded(gs, seed);
        /* game_new_seeded makes the creator player 1; on a server there
         * is no creator, so hand the identity back. Island 0 stays owned
         * by player 1 — the first client to join with --as 1 inherits
         * the founding island, and anyone else is granted a fresh one. */
        gs->local_player_id = PLAYER_NONE;
        printf("host: new world seed %u\n", seed);
    }

    /* Neighbours, if any were asked for. Seeded as ordinary commands in
     * the log, so they replay, hash and desync-check like players. */
    for (i = 0; i < ghost_count; i++) {
        int n = ghost_faction_seed(gs, ghosts[i], ghost_islands[i],
                                  (uint32_t)(900 + i), 20);
        if (n < 0)
            fprintf(stderr, "host: could not seed a ghost from %s\n",
                    ghosts[i]);
        else
            printf("host: island %d seeded with %d commands from %s\n",
                   ghost_islands[i], n, ghosts[i]);
    }

    ns = net_host(port);
    if (!ns) {
        fprintf(stderr, "host: could not listen on port %u\n",
                (unsigned)port);
        game_free(gs);
        return 1;
    }
    net_set_persistent(ns, 1);
    /* The dedicated server is the authority (SERVER_AUTHORITY.md
     * Phase 1): it pushes the world on a cadence and its state wins.
     * A co-op host in the game client still runs lockstep — the
     * protocol carries both, and which one you get is a property of
     * who is hosting rather than of the wire format. */
    net_set_authoritative(ns, 1);

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    start_tick = gs->sim_tick_no;
    printf("host: listening on %u, ticking at %d ms, checkpointing to %s\n",
           (unsigned)port, SIM_TICK_MS, world_path);
    fflush(stdout);

    last_ns = now_ns();

    while (!g_stop) {
        uint64_t now   = now_ns();
        uint64_t delta = now - last_ns;
        last_ns = now;

        acc_ns    += delta;
        ckpt_ns   += delta;
        report_ns += delta;

        /* Drain the sockets BEFORE ticking, so every command that has
         * arrived is in the log before the tick it was stamped for. */
        net_pump(ns, gs);

        /* Unlike the client, the accumulator is NOT clamped: a server
         * that stalls owes its players that time, and the catch-up is
         * the same sim_run_one_tick loop as everything else. At a 64x64
         * grid this replays hours of world time in seconds — which is
         * exactly why there is no separate "offline production" path. */
        while (acc_ns >= SIM_TICK_NS) {
            sim_run_one_tick(gs);
            /* Inside the loop, not after it: catch-up here is
             * unbounded by design, and hashing after the fact meant a
             * burst of ticks recorded no desync baseline at all for the
             * boundaries it flew past. */
            net_on_tick(ns, gs);
            acc_ns -= SIM_TICK_NS;

            if (run_ticks && gs->sim_tick_no - start_tick >= run_ticks) {
                g_stop = 1;
                break;
            }
        }

        net_after_update(ns, gs);

        if (ckpt_seconds && ckpt_ns >= ckpt_seconds * 1000000000ULL) {
            ckpt_ns = 0;
            /* A checkpoint is STATE, not history (SERVER.md, "Log
             * truncation"): the file stays the size of the world
             * instead of growing with every command ever issued, and a
             * restart -- or a join -- costs what the world weighs
             * rather than how long it has been running. */
            if (!game_save_checkpoint(gs, world_path)) {
                fprintf(stderr, "host: checkpoint to %s FAILED\n", world_path);
                rc = 1;
            } else {
                /* Written, so the history behind it is now redundant.
                 * Dropping it here is what bounds the PROCESS as well
                 * as the file: a server that runs for months would
                 * otherwise hold every command of those months in
                 * memory, and hand all of them to the next joiner. */
                game_truncate_log(gs);
            }
        }

        if (report_ns >= 60ULL * 1000000000ULL) {
            report_ns = 0;
            printf("host: tick %llu | %d connected | hash %016llx\n",
                   (unsigned long long)gs->sim_tick_no,
                   net_peer_count(ns),
                   (unsigned long long)sim_hash(gs));
            fflush(stdout);
        }

        /* A tick is 100 ms; there is nothing to do in between. Sleeping
         * a couple of milliseconds keeps latency well inside one tick
         * while leaving the CPU alone — a server should idle at ~0%. */
        sleep_ms(2);
    }

    printf("host: stopping at tick %llu, %d connected\n",
           (unsigned long long)gs->sim_tick_no, net_peer_count(ns));

    /* The final checkpoint is the one that must not be skipped: it is
     * what makes "the world is still there tomorrow" true. */
    if (!game_save_checkpoint(gs, world_path)) {
        fprintf(stderr, "host: final checkpoint to %s FAILED\n", world_path);
        rc = 1;
    } else {
        printf("host: checkpointed %s at tick %llu (%d commands, hash %016llx)\n",
               world_path, (unsigned long long)gs->sim_tick_no, gs->cmd_count,
               (unsigned long long)sim_hash(gs));
    }

    net_close(ns);
    game_free(gs);
    fflush(stdout);
    return rc;
}
