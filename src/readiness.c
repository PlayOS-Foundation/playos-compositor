#include "compositor.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/**
 * Signal compositor readiness to playos-init (PID 1).
 *
 * In Sprint 2, readiness is signaled by writing a status file
 * to /run/playos/compositor-ready. PID 1 polls for this file.
 *
 * In later sprints, this may be replaced by a one-shot message
 * over the trusted control IPC.
 */

void
playos_readiness_signal(struct playos_compositor *c)
{
    FILE *f = fopen("/run/playos/compositor-ready", "w");
    if (!f) {
        fprintf(stderr, "playos-compositor: WARNING: failed to write "
                "readiness file at /run/playos/compositor-ready\n");
        return;
    }

    fprintf(f, "pid=%d\nsocket=%s\nbackend=%s\nready=true\n",
            getpid(), c->socket_name,
            c->backend_type == PLAYOS_BACKEND_HEADLESS ? "headless" :
            c->backend_type == PLAYOS_BACKEND_WAYLAND ? "wayland" : "drm");
    fclose(f);
}
