#include "compositor.h"
#include "diagnostics.h"
#include "gpu_discovery.h"
#include "drm_backend.h"
#include "output_modes.h"
#include "renderer_gbm_egl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

/* ── Output configuration: use the state-based API (available 0.16+) ──── */
static bool
compositor_configure_output(struct wlr_output *output)
{
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_custom_mode(&state, 1920, 1080, 60000);
    wlr_output_state_set_enabled(&state, true);
    bool ok = wlr_output_commit_state(output, &state);
    wlr_output_state_finish(&state);
    return ok;
}

static void handle_new_xdg_surface(struct wl_listener *listener, void *data);
static void handle_new_output(struct wl_listener *listener, void *data);
static void handle_signal(int sig);

static struct playos_compositor *g_compositor = NULL;

void
playos_compositor_init(struct playos_compositor *c, enum playos_backend backend)
{
    memset(c, 0, sizeof(*c));
    c->backend_type = backend;
    c->state        = PLAYOS_STATE_STARTING;
    c->running      = false;
    c->socket_name  = "playos-0";
}

int
playos_compositor_start(struct playos_compositor *c)
{
    g_compositor = c;

    playos_diag_log_phase(PLAYOS_DIAG_PHASE_INIT,
                          "compositor starting");

    /* Create Wayland display */
    c->display = wl_display_create();
    if (!c->display) {
        playos_diag_fatal(PLAYOS_DIAG_PHASE_INIT,
                          "failed to create wl_display");
        return -1;
    }
    c->event_loop = wl_display_get_event_loop(c->display);

    /* ── Backend selection ──────────────────────────── */
    if (c->backend_type == PLAYOS_BACKEND_DRM) {
        /* Native DRM/KMS path (Sprint 4) */
        playos_diag_log_phase(PLAYOS_DIAG_PHASE_BACKEND_START,
                              "using native DRM/KMS backend");

        /* GPU discovery (ADR-0008) */
        struct playos_gpu gpu;
        if (playos_gpu_discover(&gpu) != 0 || !gpu.valid) {
            playos_diag_log_fallback("simpledrm",
                                     "GPU discovery failed, attempting simpledrm fallback");
            /* Try simpledrm fallback via headless — system may have simpledrm
             * as a recovery framebuffer */
            setenv("WLR_BACKENDS", "headless", 1);
            c->backend = wlr_backend_autocreate(c->event_loop, NULL);
            if (!c->backend) {
                playos_diag_fatal(PLAYOS_DIAG_PHASE_FALLBACK,
                                  "simpledrm/headless fallback also failed");
                wl_display_destroy(c->display);
                return -1;
            }
            wlr_log(WLR_INFO, "playos-compositor: using simpledrm/headless fallback");
        } else {
            /* Select best output using the discovered GPU */
            struct playos_output_config output_cfg;
            if (playos_output_select_from_fd(gpu.card_fd, &output_cfg) == 0) {
                c->output_width       = output_cfg.width;
                c->output_height      = output_cfg.height;
                c->output_refresh_mhz = output_cfg.refresh_mhz;
                c->output_scale_100   = output_cfg.scale_100;
            }

            /* DRM backend init — uses the discovered GPU */
            if (playos_drm_backend_start(c, c->event_loop, c->display) != 0) {
                playos_gpu_close(&gpu);
                playos_diag_log_fallback("simpledrm",
                                         "DRM backend start failed");
                /* Attempt headless fallback */
                setenv("WLR_BACKENDS", "headless", 1);
                c->backend = wlr_backend_autocreate(c->event_loop, NULL);
                if (!c->backend) {
                    wl_display_destroy(c->display);
                    return -1;
                }
                /* Recreate renderer/allocator for headless */
                c->renderer = wlr_renderer_autocreate(c->backend);
                if (c->renderer)
                    wlr_renderer_init_wl_display(c->renderer, c->display);
                c->allocator = wlr_allocator_autocreate(c->backend, c->renderer);
            }
            playos_gpu_close(&gpu);
        }
    } else {
        /* Headless or nested Wayland (Sprint 2 path) */
        if (c->backend_type == PLAYOS_BACKEND_HEADLESS)
            setenv("WLR_BACKENDS", "headless", 1);
        else
            setenv("WLR_BACKENDS", "wayland", 1);

        playos_diag_log_phase(PLAYOS_DIAG_PHASE_BACKEND_START,
                              c->backend_type == PLAYOS_BACKEND_HEADLESS
                              ? "using headless backend" : "using nested Wayland backend");

        c->backend = wlr_backend_autocreate(c->event_loop, NULL);
        if (!c->backend) {
            playos_diag_fatal(PLAYOS_DIAG_PHASE_BACKEND_START,
                              "failed to create backend");
            wl_display_destroy(c->display);
            return -1;
        }

        /* Renderer and allocator */
        c->renderer = wlr_renderer_autocreate(c->backend);
        if (!c->renderer) {
            playos_diag_fatal(PLAYOS_DIAG_PHASE_BACKEND_START,
                              "failed to create renderer");
            wl_display_destroy(c->display);
            return -1;
        }
        wlr_renderer_init_wl_display(c->renderer, c->display);

        c->allocator = wlr_allocator_autocreate(c->backend, c->renderer);
        if (!c->allocator) {
            playos_diag_fatal(PLAYOS_DIAG_PHASE_BACKEND_START,
                              "failed to create allocator");
            wl_display_destroy(c->display);
            return -1;
        }
    }

    /* ── Query renderer diagnostics (Sprint 4) ───────── */
    if (c->renderer) {
        struct playos_renderer_info rinfo;
        playos_renderer_query(c->renderer, &rinfo);
    }

    /* ── Scene graph ─────────────────────────────────── */
    c->scene = wlr_scene_create();
    if (!c->scene) {
        playos_diag_fatal(PLAYOS_DIAG_PHASE_INIT,
                          "failed to create scene");
        wl_display_destroy(c->display);
        return -1;
    }

    /* ── Output layout ───────────────────────────────── */
    c->output_layout = wlr_output_layout_create(c->display);
    if (!c->output_layout) {
        playos_diag_fatal(PLAYOS_DIAG_PHASE_INIT,
                          "failed to create output layout");
        wl_display_destroy(c->display);
        return -1;
    }

    /* ── Compositor (wl_compositor global) ───────────── */
    wlr_compositor_create(c->display, 6, c->renderer);

    /* ── XDG shell ───────────────────────────────────── */
    c->xdg_shell = wlr_xdg_shell_create(c->display, 3);
    if (!c->xdg_shell) {
        playos_diag_fatal(PLAYOS_DIAG_PHASE_INIT,
                          "failed to create xdg_shell");
        wl_display_destroy(c->display);
        return -1;
    }

    /* ── Minimal seat ────────────────────────────────── */
    c->seat = wlr_seat_create(c->display, "seat0");
    if (!c->seat) {
        playos_diag_fatal(PLAYOS_DIAG_PHASE_INIT,
                          "failed to create seat");
        wl_display_destroy(c->display);
        return -1;
    }

    /* ── Setup listeners ─────────────────────────────── */
    c->new_output.notify = handle_new_output;
    wl_signal_add(&c->backend->events.new_output, &c->new_output);

    c->new_xdg_surface.notify = handle_new_xdg_surface;
    wl_signal_add(&c->xdg_shell->events.new_surface, &c->new_xdg_surface);

    /* ── Create Wayland socket ───────────────────────── */
    setenv("XDG_RUNTIME_DIR", "/run/playos", 0);

    const char *socket = wl_display_add_socket_auto(c->display);
    if (!socket) {
        playos_diag_fatal(PLAYOS_DIAG_PHASE_INIT,
                          "failed to add Wayland socket");
        wl_display_destroy(c->display);
        return -1;
    }
    c->socket_name = socket;

    /* ── Start backend ───────────────────────────────── */
    if (!wlr_backend_start(c->backend)) {
        playos_diag_fatal(PLAYOS_DIAG_PHASE_BACKEND_START,
                          "failed to start backend");
        wl_display_destroy(c->display);
        return -1;
    }

    /* ── Signal readiness ────────────────────────────── */
    playos_readiness_signal(c);

    c->state   = PLAYOS_STATE_READY;
    c->running = true;

    wlr_log(WLR_INFO, "playos-compositor: ready, socket=%s, backend=%s",
            c->socket_name,
            c->backend_type == PLAYOS_BACKEND_HEADLESS ? "headless" :
            c->backend_type == PLAYOS_BACKEND_WAYLAND ? "wayland" : "drm");

    playos_diag_log_phase(PLAYOS_DIAG_PHASE_RUNNING,
                          "compositor ready");

    /* SIGTERM handling for clean shutdown */
    signal(SIGTERM, handle_signal);
    signal(SIGINT,  handle_signal);

    return 0;
}

void
playos_compositor_run(struct playos_compositor *c)
{
    (void)c;
    wl_display_run(c->display);
}

void
playos_compositor_destroy(struct playos_compositor *c)
{
    if (!c)
        return;
    c->running = false;
    c->state   = PLAYOS_STATE_SHUTTING_DOWN;

    if (c->display)
        wl_display_destroy_clients(c->display);
    if (c->display)
        wl_display_destroy(c->display);

    g_compositor = NULL;
}

/* ── Listeners ──────────────────────────────────────────── */

static void
handle_new_output(struct wl_listener *listener, void *data)
{
    struct playos_compositor *c = g_compositor;
    struct wlr_output *output = data;

    (void)listener;

    wlr_log(WLR_INFO, "playos-compositor: new output '%s' %dx%d",
            output->name, output->width, output->height);

    /* Set output mode if needed for headless */
    if (c->backend_type == PLAYOS_BACKEND_HEADLESS) {
        if (!compositor_configure_output(output))
            return;
    }

    /* Add output to layout */
    struct wlr_output_layout_output *lo =
        wlr_output_layout_add_auto(c->output_layout, output);
    if (!lo)
        return;

    /* Create scene output for this output */
    struct wlr_scene_output *scene_output =
        wlr_scene_output_create(c->scene, output);
    if (!scene_output) {
        wlr_log(WLR_ERROR, "playos-compositor: failed to create scene output");
        return;
    }

    /* ── Background rect: PlayOS blue, always visible ──── */
    float bg_color[4] = { 0.08f, 0.16f, 0.30f, 1.0f }; /* #15304D */
    struct wlr_scene_rect *bg = wlr_scene_rect_create(
        &c->scene->tree, output->width, output->height, bg_color);
    if (bg) {
        wlr_scene_node_set_position(&bg->node, 0, 0);
        wlr_scene_node_lower_to_bottom(&bg->node);
    }

    /* Commit the output state to activate the CRTC immediately */
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);
    if (!wlr_output_commit_state(output, &state)) {
        wlr_log(WLR_ERROR, "playos-compositor: failed to commit output state");
    }
    wlr_output_state_finish(&state);

    wlr_log(WLR_INFO, "playos-compositor: output '%s' added to layout "
            "(%dx%d), scene output created, CRTC activated",
            output->name, output->width, output->height);
    c->state = PLAYOS_STATE_RUNNING;
}

static void
handle_new_xdg_surface(struct wl_listener *listener, void *data)
{
    struct playos_compositor *c = g_compositor;
    struct wlr_xdg_surface *xdg_surface = data;

    (void)listener;

    if (xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
        wlr_log(WLR_INFO, "playos-compositor: new xdg_toplevel surface");

        /* Add surface to scene tree, positioned above background */
        struct wlr_scene_tree *tree =
            wlr_scene_xdg_surface_create(&c->scene->tree, xdg_surface);
        if (tree) {
            /* Center the surface on the output */
            wlr_scene_node_set_position(&tree->node, 0, 0);
            wlr_scene_node_raise_to_top(&tree->node);
            wlr_log(WLR_INFO, "playos-compositor: surface added to scene");
        } else {
            wlr_log(WLR_ERROR, "playos-compositor: failed to add surface to scene");
        }
    }
}

static void
handle_signal(int sig)
{
    if (g_compositor) {
        wlr_log(WLR_INFO, "playos-compositor: received signal %d, shutting down", sig);
        wl_display_terminate(g_compositor->display);
    }
}
