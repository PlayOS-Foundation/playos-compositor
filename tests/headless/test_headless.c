/**
 * headless/test_headless.c — Minimal headless compositor test
 *
 * Starts the compositor in headless mode, runs for 2 seconds
 * to verify backend/socket creation, then exits.
 */

#include "compositor.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void)
{
    setenv("PLAYOS_BACKEND", "headless", 1);
    wlr_log_init(WLR_ERROR, NULL);

    struct playos_compositor compositor;
    playos_compositor_init(&compositor, PLAYOS_BACKEND_HEADLESS);

    if (playos_compositor_start(&compositor) != 0) {
        fprintf(stderr, "FAIL: compositor failed to start\n");
        return EXIT_FAILURE;
    }

    fprintf(stderr, "OK: compositor started, socket=%s\n",
            compositor.socket_name);

    /* Run for a short time then terminate */
    sleep(2);
    wl_display_terminate(compositor.display);

    playos_compositor_destroy(&compositor);
    fprintf(stderr, "OK: compositor shutdown cleanly\n");

    return EXIT_SUCCESS;
}
