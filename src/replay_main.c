/* replay_main.c  --  saltmarch_replay: the headless twin's front door
 * (MMO_PLAN Phase 6) */

#include "replay.h"
#include <stdio.h>

int main(int argc, char *argv[])
{
    if (!replay_cli_requested(argc, argv)) {
        fprintf(stderr, "usage: %s %s\n",
                argc > 0 ? argv[0] : "saltmarch_replay", replay_cli_usage());
        return 2;
    }
    return replay_cli_run(argc, argv);
}
