#include "compositor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

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

    /* Create Wayland display */
    c->display = wl_display_create();
    if (!c->display) {
        fprintf(stderr, "playos-compositor: failed to create wl_display\n");
        return -1;
    }
    c->event_loop = wl_display_get_event_loop(c->display);

    /* Autostart backend */
    if (c->backend_type == PLAYOS_BACKEND_HEADLESS)
        setenv("WLR_BACKENDS", "headless", 1);
    else
        setenv("WLR_BACKENDS", "wayland", 1);

    c->backend = wlr_backend_autocreate(c->display, NULL);
    if (!c->backend) {
        fprintf(stderr, "playos-compositor: failed to create backend\n");
        wl_display_destroy(c->display);
        return -1;
    }

    /* Renderer and allocator */
    c->renderer = wlr_renderer_autocreate(c->backend);
    if (!c->renderer) {
        fprintf(stderr, "playos-compositor: failed to create renderer\n");
        wl_display_destroy(c->display);
        return -1;
    }
    wlr_renderer_init_wl_display(c->renderer, c->display);

    c->allocator = wlr_allocator_autocreate(c->backend, c->renderer);
    if (!c->allocator) {
        fprintf(stderr, "playos-compositor: failed to create allocator\n");
        wl_display_destroy(c->display);
        return -1;
    }

    /* Scene graph */
    c->scene = wlr_scene_create();
    if (!c->scene) {
        fprintf(stderr, "playos-compositor: failed to create scene\n");
        wl_display_destroy(c->display);
        return -1;
    }

    /* Output layout */
    c->output_layout = wlr_output_layout_create();
    if (!c->output_layout) {
        fprintf(stderr, "playos-compositor: failed to create output layout\n");
        wl_display_destroy(c->display);
        return -1;
    }

    /* Compositor (wl_compositor global) */
    wlr_compositor_create(c->display, 6, c->renderer);

    /* XDG shell */
    c->xdg_shell = wlr_xdg_shell_create(c->display, 3);
    if (!c->xdg_shell) {
        fprintf(stderr, "playos-compositor: failed to create xdg_shell\n");
        wl_display_destroy(c->display);
        return -1;
    }

    /* Minimal seat */
    c->seat = wlr_seat_create(c->display, "seat0");
    if (!c->seat) {
        fprintf(stderr, "playos-compositor: failed to create seat\n");
        wl_display_destroy(c->display);
        return -1;
    }

    /* Setup listeners */
    c->new_output.notify = handle_new_output;
    wl_signal_add(&c->backend->events.new_output, &c->new_output);

    c->new_xdg_surface.notify = handle_new_xdg_surface;
    wl_signal_add(&c->xdg_shell->events.new_surface, &c->new_xdg_surface);

    /* Create Wayland socket */
    const char *socket = wl_display_add_socket_auto(c->display);
    if (!socket) {
        fprintf(stderr, "playos-compositor: failed to add Wayland socket\n");
        wl_display_destroy(c->display);
        return -1;
    }
    c->socket_name = socket;

    /* Start backend */
    if (!wlr_backend_start(c->backend)) {
        fprintf(stderr, "playos-compositor: failed to start backend\n");
        wl_display_destroy(c->display);
        return -1;
    }

    /* Signal readiness: write a file to /run/playos/compositor-ready */
    FILE *ready = fopen("/run/playos/compositor-ready", "w");
    if (ready) {
        fprintf(ready, "pid=%d\nsocket=%s\nbackend=%s\n",
                getpid(), c->socket_name,
                c->backend_type == PLAYOS_BACKEND_HEADLESS ? "headless" : "wayland");
        fclose(ready);
    }

    c->state   = PLAYOS_STATE_READY;
    c->running = true;

    wlr_log(WLR_INFO, "playos-compositor: ready, socket=%s, backend=%s",
            c->socket_name,
            c->backend_type == PLAYOS_BACKEND_HEADLESS ? "headless" : "wayland");

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

    wlr_log(WLR_INFO, "playos-compositor: new output '%s'", output->name);

    /* Set output mode if needed for headless */
    if (c->backend_type == PLAYOS_BACKEND_HEADLESS) {
        wlr_output_set_custom_mode(output, 1920, 1080, 60000);
        wlr_output_enable(output, true);
        if (!wlr_output_commit(output))
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
    if (!scene_output)
        return;

    wlr_log(WLR_INFO, "playos-compositor: output '%s' added to layout", output->name);
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

        /* Add surface to scene tree for fullscreen layout */
        struct wlr_scene_tree *tree =
            wlr_scene_xdg_surface_create(&c->scene->tree, xdg_surface);
        if (tree) {
            wlr_scene_node_set_position(&tree->node, 0, 0);
            wlr_log(WLR_INFO, "playos-compositor: surface mapped as fullscreen");
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
