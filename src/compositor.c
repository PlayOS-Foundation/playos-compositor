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

static void handle_new_xdg_toplevel(struct wl_listener *listener, void *data);
static void handle_toplevel_commit(struct wl_listener *listener, void *data);
static void handle_toplevel_destroy(struct wl_listener *listener, void *data);
static void handle_new_output(struct wl_listener *listener, void *data);
static void handle_frame(struct wl_listener *listener, void *data);
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

    /* Pre-init listener links so wl_list_remove() in destroy is a safe
     * no-op for listeners that were never attached */
    wl_list_init(&c->new_output.link);
    wl_list_init(&c->new_xdg_surface.link);
    wl_list_init(&c->toplevel_commit.link);
    wl_list_init(&c->toplevel_destroy.link);
    wl_list_init(&c->frame.link);
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

        /* GPU discovery (ADR-0008)
         * Retry up to 10 times (5s) — GPU driver may not be ready yet */
        struct playos_gpu gpu;
        int gpu_attempts = 0;
        while (gpu_attempts < 10) {
            if (playos_gpu_discover(&gpu) == 0 && gpu.valid)
                break;
            if (gpu_attempts == 0)
                wlr_log(WLR_INFO, "GPU discovery: waiting for DRM devices...");
            usleep(500000);  /* 500ms poll */
            gpu_attempts++;
        }

        if (!gpu.valid) {
            playos_diag_log_fallback("simpledrm",
                                     "GPU discovery failed after 5s, attempting headless fallback");
            /* No GPU found — headless fallback */
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

            /* Guide wlroots to the correct device via WLR_DRM_DEVICES */
            setenv("WLR_DRM_DEVICES", gpu.card_path, 1);

            /* Close the discovery fd — wlroots will open its own fd
             * to the same device when the DRM backend starts. */
            playos_gpu_close(&gpu);

            /* DRM backend init — uses the discovered GPU */
            if (playos_drm_backend_start(c, c->event_loop, c->display) != 0) {
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
    fprintf(stderr, "compositor: renderer created, querying...\n");
    if (c->renderer) {
        struct playos_renderer_info rinfo;
        playos_renderer_query(c->renderer, &rinfo);
    }
    fprintf(stderr, "compositor: renderer query done\n");

    /* ── Scene graph ─────────────────────────────────── */
    fprintf(stderr, "compositor: creating scene...\n");
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

    c->new_xdg_surface.notify = handle_new_xdg_toplevel;
    wl_signal_add(&c->xdg_shell->events.new_toplevel, &c->new_xdg_surface);

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
    fprintf(stderr, "compositor: starting backend...\n");
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

    /* Detach listeners before wl_display_destroy — wlroots 0.20 asserts
     * that signal listener lists are empty when objects are freed */
    wl_list_remove(&c->new_output.link);
    wl_list_remove(&c->new_xdg_surface.link);
    wl_list_remove(&c->frame.link);

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

    fprintf(stderr, "compositor: new output '%s' (%dx%d)\n",
            output->name, output->width, output->height);
    wlr_log(WLR_INFO, "playos-compositor: new output '%s'", output->name);

    /* Attach renderer/allocator to the output — required by wlroots
     * 0.20 before any rendering (wlr_scene_output_commit) on it */
    if (!wlr_output_init_render(output, c->allocator, c->renderer)) {
        wlr_log(WLR_ERROR, "playos-compositor: failed to attach renderer "
                "to output '%s'", output->name);
        return;
    }

    /* Set output mode if needed for headless */
    if (c->backend_type == PLAYOS_BACKEND_HEADLESS) {
        if (!compositor_configure_output(output))
            return;
    }

    /* For DRM backend, explicitly set the preferred or discovered mode.
     * This prevents wlroots from picking a suboptimal fallback mode. */
    if (c->backend_type == PLAYOS_BACKEND_DRM) {
        struct wlr_output_mode *preferred = NULL;

        /* Try to match the discovered output config */
        if (c->output_width > 0 && c->output_height > 0 && c->output_refresh_mhz > 0) {
            struct wlr_output_mode *mode;
            wl_list_for_each(mode, &output->modes, link) {
                if (mode->width == c->output_width &&
                    mode->height == c->output_height &&
                    (int)(mode->refresh / 1000) == (c->output_refresh_mhz / 1000)) {
                    preferred = mode;
                    break;
                }
            }
        }

        /* Fall back to the connector's preferred mode */
        if (!preferred) {
            struct wlr_output_mode *mode;
            wl_list_for_each(mode, &output->modes, link) {
                if (mode->preferred) {
                    preferred = mode;
                    break;
                }
            }
        }

        /* If we found a mode, set it; otherwise let wlroots pick */
        if (preferred) {
            struct wlr_output_state state;
            wlr_output_state_init(&state);
            wlr_output_state_set_mode(&state, preferred);
            wlr_output_state_set_enabled(&state, true);
            wlr_output_commit_state(output, &state);
            wlr_output_state_finish(&state);
            wlr_log(WLR_INFO, "playos-compositor: output '%s' set to %dx%d@%dHz",
                    output->name,
                    preferred->width, preferred->height,
                    (int)(preferred->refresh / 1000));
        }
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

    /* Register frame listener for diagnostic logging */
    c->frame.notify = handle_frame;
    wl_signal_add(&output->events.frame, &c->frame);

    /* Kick off the render loop — guarantees a first frame even if the
     * backend doesn't emit one spontaneously after the modeset */
    wlr_output_schedule_frame(output);

    wlr_log(WLR_INFO, "playos-compositor: output '%s' added to layout "
            "(%dx%d), scene output created",
            output->name, output->width, output->height);
    c->state = PLAYOS_STATE_RUNNING;
}

static void
handle_new_xdg_toplevel(struct wl_listener *listener, void *data)
{
    struct playos_compositor *c = g_compositor;
    struct wlr_xdg_toplevel *toplevel = data;
    struct wlr_xdg_surface *xdg_surface = toplevel->base;

    (void)listener;

    wlr_log(WLR_INFO, "playos-compositor: new xdg_toplevel surface");

    /* Add surface to scene tree for fullscreen layout */
    struct wlr_scene_tree *tree =
        wlr_scene_xdg_surface_create(&c->scene->tree, xdg_surface);
    if (tree) {
        wlr_scene_node_set_position(&tree->node, 0, 0);
        wlr_scene_node_raise_to_top(&tree->node);

        /* The initial configure is sent from the commit handler once
         * the client performs its first commit (see handle_toplevel_commit) */
        c->toplevel_commit.notify = handle_toplevel_commit;
        wl_signal_add(&xdg_surface->surface->events.commit,
                      &c->toplevel_commit);

        c->toplevel_destroy.notify = handle_toplevel_destroy;
        wl_signal_add(&xdg_surface->events.destroy,
                      &c->toplevel_destroy);

        wlr_log(WLR_INFO, "playos-compositor: surface added to scene");
    } else {
        wlr_log(WLR_ERROR, "playos-compositor: failed to add surface to scene");
    }
}

static void
handle_toplevel_commit(struct wl_listener *listener, void *data)
{
    struct playos_compositor *c = g_compositor;
    struct wlr_surface *surface = data;

    (void)listener;

    struct wlr_xdg_surface *xdg_surface =
        wlr_xdg_surface_try_from_wlr_surface(surface);
    if (!xdg_surface || xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL)
        return;

    /* On the initial commit the compositor must reply with a configure
     * so the client can map. PlayOS surfaces are fullscreen: size them
     * to the output (0,0 lets the client pick, e.g. on headless). */
    if (xdg_surface->initial_commit) {
        int w = c->output_width > 0 ? c->output_width : 0;
        int h = c->output_height > 0 ? c->output_height : 0;
        wlr_xdg_toplevel_set_size(xdg_surface->toplevel, w, h);
    }
}

static void
handle_toplevel_destroy(struct wl_listener *listener, void *data)
{
    struct playos_compositor *c = g_compositor;

    (void)listener;
    (void)data;

    /* wlroots 0.20 asserts all listeners are detached before the
     * surface/signal is freed */
    wl_list_remove(&c->toplevel_commit.link);
    wl_list_remove(&c->toplevel_destroy.link);
}

static void
handle_frame(struct wl_listener *listener, void *data)
{
    (void)listener;
    struct playos_compositor *c = g_compositor;
    struct wlr_output *output = data;

    /* Render the scene onto this output. wlr_scene_output_commit() is
     * what actually draws and queues a buffer for scanout — without it
     * no frame is ever presented and, since DRM frame events are driven
     * by completed page flips, the frame event loop never starts either
     * (output stays black). */
    struct wlr_scene_output *scene_output =
        wlr_scene_get_scene_output(c->scene, output);
    if (!scene_output)
        return;

    wlr_scene_output_commit(scene_output, NULL);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
}

static void
handle_signal(int sig)
{
    if (g_compositor) {
        wlr_log(WLR_INFO, "playos-compositor: received signal %d, shutting down", sig);
        wl_display_terminate(g_compositor->display);
    }
}
