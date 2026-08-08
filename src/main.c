#include "compositor.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    const char *backend_env = getenv("PLAYOS_BACKEND");
    enum playos_backend backend = PLAYOS_BACKEND_HEADLESS;

    if (backend_env && strcmp(backend_env, "wayland") == 0)
        backend = PLAYOS_BACKEND_WAYLAND;

    (void)argc;
    (void)argv;

    wlr_log_init(WLR_INFO, NULL);

    struct playos_compositor compositor;
    playos_compositor_init(&compositor, backend);

    if (playos_compositor_start(&compositor) != 0) {
        fprintf(stderr, "playos-compositor: failed to start\n");
        return EXIT_FAILURE;
    }

    playos_compositor_run(&compositor);
    playos_compositor_destroy(&compositor);

    return EXIT_SUCCESS;
}
