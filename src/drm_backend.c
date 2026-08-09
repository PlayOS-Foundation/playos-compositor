#include "drm_backend.h"
#include "diagnostics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * drm_backend.c — Native DRM/KMS backend initialization
 *
 * Creates the wlroots DRM backend, sets up renderer/allocator.
 * Output selection is handled by the compositor's new_output listener.
 */

int
playos_drm_backend_start(struct playos_compositor *c,
                         struct wl_event_loop *event_loop,
                         struct wl_display *display)
{
    playos_diag_log_phase(PLAYOS_DIAG_PHASE_BACKEND_START,
                          "initializing native DRM/KMS backend");

    /* Set wlroots to use DRM backend */
    setenv("WLR_BACKENDS", "drm", 1);

    /* Create DRM backend */
    c->backend = wlr_backend_autocreate(event_loop, NULL);
    if (!c->backend) {
        playos_diag_log_fallback("simpledrm",
                                 "DRM backend autocreate failed");
        return playos_diag_fatal(PLAYOS_DIAG_PHASE_BACKEND_START,
                                 "failed to create DRM backend");
    }

    playos_diag_log_phase(PLAYOS_DIAG_PHASE_BACKEND_START,
                          "DRM backend created");

    /* Create renderer with GBM/EGL context */
    fprintf(stderr, "compositor: creating renderer...\n");
    c->renderer = wlr_renderer_autocreate(c->backend);
    if (!c->renderer) {
        return playos_diag_fatal(PLAYOS_DIAG_PHASE_BACKEND_START,
                                 "failed to create renderer");
    }
    fprintf(stderr, "compositor: renderer created, init wl_display...\n");
    wlr_renderer_init_wl_display(c->renderer, display);

    /* Create allocator */
    c->allocator = wlr_allocator_autocreate(c->backend, c->renderer);
    if (!c->allocator) {
        return playos_diag_fatal(PLAYOS_DIAG_PHASE_BACKEND_START,
                                 "failed to create allocator");
    }

    playos_diag_log_phase(PLAYOS_DIAG_PHASE_RENDERER_INIT,
                          "DRM/KMS backend initialized successfully");

    return 0;
}
