#ifndef CONFIG_H
#define CONFIG_H

/* =========================================================
 * config.h  --  Where the client keeps what it must remember
 *               (AUTH_PLAN Phase 2)
 *
 * There was nowhere. The game reads argv and one environment variable,
 * so a token issued by a server had no home — and AUTH_PLAN is blunt
 * about what that would mean: "every player pastes a 64-character hex
 * string on the command line, which is not a design so much as a dare."
 *
 * So: one file under `SDL_GetPrefPath`, holding one line per server.
 *
 * KEYED BY host:port, because a token is a credential FOR A WORLD. One
 * player may play on a friend's server and on a public one; handing the
 * second the first's token would be the client leaking a credential
 * that the whole protocol exists to protect.
 *
 * CLIENT-SIDE ONLY, and never near the sim. Nothing here is world
 * state, nothing here is hashed, nothing here is sent anywhere except
 * to the server it belongs to. It is the mirror image of account.c:
 * that file is the server's secret at rest, this is the client's.
 *
 * Text, like the accounts file, and for the same reason: a player who
 * has to move to a new machine, or an admin talking one through a
 * reset, should be able to read and edit it.
 * ========================================================= */

#include <stdint.h>
#include "net.h"   /* NetCredential */

#define CONFIG_MAX_SERVERS 16
#define CONFIG_HOST_LEN    64

typedef struct {
    char          key[CONFIG_HOST_LEN];   /* "host:port"               */
    NetCredential cred;
} ConfigServer;

typedef struct {
    ConfigServer server[CONFIG_MAX_SERVERS];
    int          count;
    char         path[512];               /* resolved once at load     */
} Config;

/* Read the config, or start an empty one. Never fails in a way a caller
 * must handle: a player with no config is the ordinary first run, and a
 * config that cannot be read is one the game says so about and carries
 * on without — a broken preferences file must not stop the game
 * starting. */
void config_load(Config *c);

/* Write it back. Best effort, and says so in the log when it cannot:
 * losing a token means one more registration, not a lost island. */
int  config_save(const Config *c);

/* The credential for `host:port`, or NULL. */
const NetCredential *config_credential(const Config *c, const char *host,
                                       uint16_t port);

/* Remember one. Replaces whatever was there for that server, because a
 * server that issues a second token has invalidated the first. */
void config_set_credential(Config *c, const char *host, uint16_t port,
                           const NetCredential *cred);

#endif /* CONFIG_H */
