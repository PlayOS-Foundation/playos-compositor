/**
 * tests/nested/test_nested.c — Compositor validation in nested Wayland mode
 *
 * Starts the compositor using the Wayland backend (nested inside an
 * existing Wayland session). Requires WAYLAND_DISPLAY to be set.
 *
 * This is the developer iteration path — run with:
 *   PLAYOS_BACKEND=wayland ./compositor-nested-test
 * from inside a running Wayland desktop (weston, gnome, etc.).
 */

#include "compositor.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void)
{
    const char *wayland_display = getenv("WAYLAND_DISPLAY");

    if (!wayland_display) {
        fprintf(stderr, "SKIP: WAYLAND_DISPLAY not set — not running under a Wayland session\n");
        return 0;
    }

    setenv("PLAYOS_BACKEND", "wayland", 1);
    wlr_log_init(WLR_ERROR, NULL);

    struct playos_compositor compositor;
    playos_compositor_init(&compositor, PLAYOS_BACKEND_WAYLAND);

    if (playos_compositor_start(&compositor) != 0) {
        fprintf(stderr, "FAIL: nested compositor failed to start\n");
        return EXIT_FAILURE;
    }

    fprintf(stderr, "OK: nested compositor started, socket=%s, backend=wayland, parent=%s\n",
            compositor.socket_name, wayland_display);

    /* Run briefly then terminate */
    sleep(2);
    wl_display_terminate(compositor.display);

    playos_compositor_destroy(&compositor);
    fprintf(stderr, "OK: nested compositor shutdown cleanly\n");

    return EXIT_SUCCESS;
}
