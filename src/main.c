#include "compositor.h"
#include "diagnostics.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char *argv[])
{
    const char *backend_env = getenv("PLAYOS_BACKEND");
    enum playos_backend backend = PLAYOS_BACKEND_HEADLESS;

    if (backend_env) {
        if (strcmp(backend_env, "wayland") == 0)
            backend = PLAYOS_BACKEND_WAYLAND;
        else if (strcmp(backend_env, "drm") == 0)
            backend = PLAYOS_BACKEND_DRM;
    }

    (void)argc;
    (void)argv;

    /* Initialize diagnostics early */
    playos_diag_init();

    wlr_log_init(WLR_INFO, NULL);

    struct playos_compositor compositor;
    playos_compositor_init(&compositor, backend);

    if (playos_compositor_start(&compositor) != 0) {
        fprintf(stderr, "playos-compositor: failed to start\n");
        playos_diag_fini();
        return EXIT_FAILURE;
    }

    playos_compositor_run(&compositor);
    playos_compositor_destroy(&compositor);
    playos_diag_fini();

    return EXIT_SUCCESS;
}
