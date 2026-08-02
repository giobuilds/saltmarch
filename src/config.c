/* config.c  --  The client's preferences (AUTH_PLAN Phase 2) */

#include "config.h"
#include "account.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#define CONFIG_FILE "saltmarch.cfg"

static void config_path(char *out, size_t cap)
{
    char *pref = SDL_GetPrefPath("saltmarch", "saltmarch");

    out[0] = '\0';
    if (!pref) return;
    SDL_snprintf(out, cap, "%s%s", pref, CONFIG_FILE);
    SDL_free(pref);
}

static void make_key(char *out, size_t cap, const char *host, uint16_t port)
{
    SDL_snprintf(out, cap, "%s:%u", host ? host : "", (unsigned)port);
}

void config_load(Config *c)
{
    SDL_IOStream *io;
    char         *text;
    size_t        len = 0;
    char         *line, *save = NULL;

    memset(c, 0, sizeof(*c));
    config_path(c->path, sizeof(c->path));
    if (c->path[0] == '\0') return;

    io = SDL_IOFromFile(c->path, "rb");
    if (!io) return;                    /* first run: nothing to read */

    text = (char *)SDL_LoadFile_IO(io, &len, 1);
    if (!text) return;

    /* One record per line: `server <host:port> <account> <token-hex>`.
     * Anything unparseable is skipped rather than fatal — unlike the
     * server's account file, a bad line here costs one re-registration
     * and must never stop the game starting. */
    for (line = SDL_strtok_r(text, "\r\n", &save); line;
         line = SDL_strtok_r(NULL, "\r\n", &save)) {
        ConfigServer *s;
        char          key[CONFIG_HOST_LEN];
        char          hex[ACCOUNT_TOKEN_BYTES * 2 + 2];
        unsigned      acct = 0u;

        if (line[0] == '#') continue;
        if (SDL_sscanf(line, "server %63s %u %64s", key, &acct, hex) != 3)
            continue;
        if (c->count >= CONFIG_MAX_SERVERS) break;

        s = &c->server[c->count];
        memset(s, 0, sizeof(*s));
        SDL_strlcpy(s->key, key, sizeof(s->key));
        s->cred.account_id = (uint32_t)acct;
        if (!account_unhex(s->cred.token, ACCOUNT_TOKEN_BYTES, hex)) continue;
        c->count++;
    }

    SDL_free(text);
}

int config_save(const Config *c)
{
    SDL_IOStream *io;
    int           i, ok = 1;

    if (c->path[0] == '\0') return 0;

    io = SDL_IOFromFile(c->path, "wb");
    if (!io) {
        SDL_Log("config: cannot write %s", c->path);
        return 0;
    }

    {
        const char *hdr = "# saltmarch client config\n"
                          "# server <host:port> <account> <token>\n"
                          "# This file holds credentials. Treat it like a "
                          "password file.\n";
        ok &= SDL_WriteIO(io, hdr, SDL_strlen(hdr)) == SDL_strlen(hdr);
    }

    for (i = 0; i < c->count && ok; i++) {
        char line[256];
        char hex[ACCOUNT_TOKEN_BYTES * 2 + 1];
        size_t n;

        if (c->server[i].cred.account_id == 0u) continue;
        account_hex(hex, c->server[i].cred.token, ACCOUNT_TOKEN_BYTES);
        n = (size_t)SDL_snprintf(line, sizeof(line), "server %s %u %s\n",
                                 c->server[i].key,
                                 (unsigned)c->server[i].cred.account_id, hex);
        ok &= SDL_WriteIO(io, line, n) == n;
    }

    SDL_CloseIO(io);
    return ok;
}

const NetCredential *config_credential(const Config *c, const char *host,
                                       uint16_t port)
{
    char key[CONFIG_HOST_LEN];
    int  i;

    make_key(key, sizeof(key), host, port);
    for (i = 0; i < c->count; i++)
        if (SDL_strcmp(c->server[i].key, key) == 0)
            return &c->server[i].cred;
    return NULL;
}

void config_set_credential(Config *c, const char *host, uint16_t port,
                           const NetCredential *cred)
{
    char key[CONFIG_HOST_LEN];
    int  i;

    if (!cred) return;
    make_key(key, sizeof(key), host, port);

    for (i = 0; i < c->count; i++)
        if (SDL_strcmp(c->server[i].key, key) == 0) {
            c->server[i].cred = *cred;
            return;
        }

    if (c->count >= CONFIG_MAX_SERVERS) {
        /* Sixteen servers is more than a person plays on, and dropping
         * the oldest silently would lose an identity. Say so. */
        SDL_Log("config: no room to remember %s", key);
        return;
    }
    SDL_strlcpy(c->server[c->count].key, key, sizeof(c->server[0].key));
    c->server[c->count].cred = *cred;
    c->count++;
}
