#ifndef PLAYOS_COMPOSITOR_H
#define PLAYOS_COMPOSITOR_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/allocator.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>

/* Forward declarations for optional DRM/graphics modules */
#include "diagnostics.h"

/**
 * Backend selection for the compositor.
 */
enum playos_backend {
    PLAYOS_BACKEND_HEADLESS,
    PLAYOS_BACKEND_WAYLAND,
    PLAYOS_BACKEND_DRM,
};

/**
 * Compositor lifecycle states.
 */
enum playos_compositor_state {
    PLAYOS_STATE_STARTING,
    PLAYOS_STATE_READY,
    PLAYOS_STATE_RUNNING,
    PLAYOS_STATE_SHUTTING_DOWN,
};

/**
 * Trusted client roles.
 */
enum playos_trusted_role {
    PLAYOS_ROLE_NONE,
    PLAYOS_ROLE_SHELL,
    PLAYOS_ROLE_OVERLAY,
};

/**
 * Central compositor state — no global mutable variables.
 */
struct playos_compositor {
    /* Wayland core */
    struct wl_display        *display;
    struct wl_event_loop     *event_loop;

    /* wlroots */
    struct wlr_backend       *backend;
    struct wlr_renderer      *renderer;
    struct wlr_allocator     *allocator;
    struct wlr_scene         *scene;
    struct wlr_output_layout *output_layout;
    struct wlr_xdg_shell     *xdg_shell;
    struct wlr_seat          *seat;

    /* Session */
    enum playos_backend       backend_type;
    enum playos_compositor_state state;
    bool                      running;
    const char               *socket_name;

    /* Trusted client tracking */
    struct wl_client         *shell_client;
    struct wl_client         *overlay_client;
    enum playos_trusted_role  pending_role;

    /* Output info */
    int                       output_width;
    int                       output_height;
    int                       output_refresh_mhz;
    int                       output_scale_100;

    /* Signal listeners */
    struct wl_listener        new_output;
    struct wl_listener        new_xdg_surface;
    struct wl_listener        frame;
};

void playos_compositor_init(struct playos_compositor *c, enum playos_backend backend);
int  playos_compositor_start(struct playos_compositor *c);
void playos_compositor_run(struct playos_compositor *c);
void playos_compositor_destroy(struct playos_compositor *c);

/* ── Trusted client tracking (S2-T4) ──────────────────── */
void playos_trusted_client_init(struct playos_compositor *c);
bool playos_trusted_client_claim(struct playos_compositor *c,
                                 struct wl_client *client,
                                 enum playos_trusted_role role);
bool playos_trusted_client_is_trusted(struct playos_compositor *c,
                                      struct wl_client *client);

/* ── Readiness signaling (S2-T7) ──────────────────────── */
void playos_readiness_signal(struct playos_compositor *c);

#endif /* PLAYOS_COMPOSITOR_H */
