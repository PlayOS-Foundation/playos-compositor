#ifndef PLAYOS_DRM_BACKEND_H
#define PLAYOS_DRM_BACKEND_H

#include <wayland-server-core.h>
#include <wlr/backend.h>
#include "compositor.h"

/**
 * drm_backend.h — Native DRM/KMS backend initialization via wlroots
 *
 * Creates the DRM backend, sets up the renderer and allocator,
 * and starts the backend. Used when PLAYOS_BACKEND=drm.
 */

/**
 * Create and start the native DRM backend.
 *
 * Uses wlr_backend_autocreate() with WLR_BACKENDS=drm.
 * Initializes renderer via wlr_renderer_autocreate() with EGL/GBM context.
 *
 * On success, fills compositor->backend, ->renderer, ->allocator.
 * Returns 0 on success, -1 on failure (logs via diagnostics).
 */
int playos_drm_backend_start(struct playos_compositor *c,
                             struct wl_event_loop *event_loop,
                             struct wl_display *display);

#endif /* PLAYOS_DRM_BACKEND_H */
