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
 * Foreground state machine (Sprint 7).
 *
 * Tracks which surface is in the foreground: shell, game, or the PlayOS
 * overlay UI. This is orthogonal to the process lifecycle states above.
 */
enum playos_foreground_state {
    PLAYOS_FG_SHELL_FOREGROUND,
    PLAYOS_FG_GAME_STARTING,
    PLAYOS_FG_GAME_FOREGROUND,
    PLAYOS_FG_PLAYOS_UI_FOREGROUND_WITH_GAME_BACKGROUND,
    PLAYOS_FG_TERMINATING_GAME,
};

/**
 * Trusted client roles.
 */
enum playos_trusted_role {
    PLAYOS_ROLE_NONE,
    PLAYOS_ROLE_SHELL,
    PLAYOS_ROLE_OVERLAY,
    PLAYOS_ROLE_GAME,
};

/* ── compositor.sock IPC message types (Sprint 7) ──────────────── */
/* init → compositor */
#define PLAYOS_COMPOSITOR_MSG_SET_EXPECTED_GAME    "SetExpectedGame"
#define PLAYOS_COMPOSITOR_MSG_CLEAR_EXPECTED_GAME  "ClearExpectedGame"
#define PLAYOS_COMPOSITOR_MSG_FORCE_TERMINATE_GAME "ForceTerminateGame"
#define PLAYOS_COMPOSITOR_MSG_SHOW_OVERLAY         "ShowOverlay"
#define PLAYOS_COMPOSITOR_MSG_HIDE_OVERLAY         "HideOverlay"
/* compositor → init */
#define PLAYOS_COMPOSITOR_MSG_GAME_SURFACE_READY   "GameSurfaceReady"
#define PLAYOS_COMPOSITOR_MSG_STATE_CHANGED        "CompositorStateChanged"

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
    struct wl_resource       *overlay_resource;
    struct wl_list            overlay_resources;  /* bound playos_overlay_v1 clients */
    enum playos_trusted_role  pending_role;

    /* Sprint 7 foreground state machine */
    enum playos_foreground_state fg_state;
    char                       expected_launch_token[64];
    char                       expected_game_id[256];
    struct wlr_scene_tree     *shell_tree;
    struct wlr_scene_tree     *game_tree;
    struct wlr_scene_tree     *overlay_tree;
    struct wl_list             toplevels;   /* tracked xdg toplevels */
    bool                       game_surface_ready;
    bool                       overlay_visible;

    /* compositor.sock IPC (client of playos-init) */
    int                        compositor_sock_fd;
    struct wl_event_source    *ipc_source;
    struct wl_event_source    *ipc_reconnect_timer;
    int                        ipc_reconnect_delay_ms;

    /* Output info */
    int                       output_width;
    int                       output_height;
    int                       output_refresh_mhz;
    int                       output_scale_100;

    /* Signal listeners */
    struct wl_listener        new_output;
    struct wl_listener        new_xdg_surface;
    struct wl_listener        new_input;
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
void playos_compositor_reclassify_toplevels(struct playos_compositor *c);

/* ── Trusted protocol (S5) ────────────────────────────── */
int  playos_manager_create(struct playos_compositor *c);

/* ── Overlay protocol (Sprint 7) ─────────────────────── */
int  playos_overlay_create(struct playos_compositor *c);
void playos_overlay_send_about_to_show(struct playos_compositor *c);
void playos_overlay_send_about_to_hide(struct playos_compositor *c);
void playos_overlay_send_output_info(struct playos_compositor *c);

/* ── Readiness signaling (S2-T7) ──────────────────────── */
void playos_readiness_signal(struct playos_compositor *c);

/* ── Foreground state machine (Sprint 7) ──────────────── */
void playos_state_init(struct playos_compositor *c);
void playos_state_refresh(struct playos_compositor *c);
void playos_state_transition(struct playos_compositor *c,
                             enum playos_foreground_state state);
void playos_state_handle_set_expected_game(struct playos_compositor *c,
                                           const char *launch_token,
                                           const char *game_id);
void playos_state_handle_clear_expected(struct playos_compositor *c);
void playos_state_handle_force_terminate(struct playos_compositor *c);
void playos_state_game_surface_ready(struct playos_compositor *c);
void playos_state_game_exited(struct playos_compositor *c, bool crashed);

/* ── compositor.sock IPC client (Sprint 7) ────────────── */
int  playos_compositor_ipc_start(struct playos_compositor *c);
void playos_compositor_ipc_stop(struct playos_compositor *c);
int  playos_compositor_ipc_send(struct playos_compositor *c,
                                const char *type,
                                const char *extra_json);

/* ── Overlay visibility manager (Sprint 7) ────────────── */
void playos_overlay_manager_show(struct playos_compositor *c);
void playos_overlay_manager_hide(struct playos_compositor *c);

/* ── System button intercept (Sprint 7) ───────────────── */
void playos_system_button_init(struct playos_compositor *c);

#endif /* PLAYOS_COMPOSITOR_H */
