#ifndef CONFIG_H
#define CONFIG_H

/* config.h  --  Where the client keeps what it must remember
 * (AUTH_PLAN Phase 2) */

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

/* Read the config, or start an empty one. Never fails in a way a caller */
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
