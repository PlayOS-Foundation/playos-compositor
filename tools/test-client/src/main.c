/**
 * tools/test-client/src/main.c — PlayOS Wayland test client
 *
 * Connects to playos-0, creates an xdg_toplevel, renders a
 * PlayOS-branded frame using wl_shm, then waits for close.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"

static struct wl_compositor *compositor = NULL;
static struct wl_shm *shm = NULL;
static struct xdg_wm_base *xdg_wm_base = NULL;
static struct wl_surface *surface = NULL;
static struct xdg_surface *xdg_surface = NULL;
static struct xdg_toplevel *xdg_toplevel = NULL;
static struct wl_buffer *buffer = NULL;
static int running = 1;
static int width = 640, height = 480;

static void
registry_handle_global(void *data, struct wl_registry *registry,
                       uint32_t name, const char *interface, uint32_t version)
{
    (void)data;
    if (strcmp(interface, wl_compositor_interface.name) == 0)
        compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    else if (strcmp(interface, wl_shm_interface.name) == 0)
        shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    else if (strcmp(interface, xdg_wm_base_interface.name) == 0)
        xdg_wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
}

static void
registry_handle_global_remove(void *data, struct wl_registry *registry,
                              uint32_t name)
{
    (void)data; (void)registry; (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

static int
create_shm_fd(size_t size)
{
    char template[] = "/tmp/playos-test-client-XXXXXX";
    int fd = mkstemp(template);
    if (fd < 0) return -1;
    unlink(template);
    if (ftruncate(fd, size) < 0) { close(fd); return -1; }
    return fd;
}

static struct wl_buffer *
create_buffer(void)
{
    int stride = width * 4;
    int size = stride * height;
    int fd = create_shm_fd(size);
    if (fd < 0) return NULL;

    void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) { close(fd); return NULL; }

    uint32_t *pixels = (uint32_t *)data;
    for (int i = 0; i < width * height; i++)
        pixels[i] = 0xFFD66B00; /* PlayOS blue in XRGB */

    munmap(data, size);

    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    struct wl_buffer *buf = wl_shm_pool_create_buffer(pool, 0,
                                                      width, height, stride,
                                                      WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    return buf;
}

static void
xdg_surface_handle_configure(void *data, struct xdg_surface *xdg_surface,
                             uint32_t serial)
{
    (void)data;
    xdg_surface_ack_configure(xdg_surface, serial);
    if (!buffer && shm) {
        buffer = create_buffer();
        if (buffer) {
            wl_surface_attach(surface, buffer, 0, 0);
            wl_surface_damage_buffer(surface, 0, 0, width, height);
            wl_surface_commit(surface);
            fprintf(stderr, "test-client: buffer committed\n");
        }
    }
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_handle_configure,
};

static void
xdg_toplevel_handle_configure(void *data, struct xdg_toplevel *toplevel,
                              int32_t w, int32_t h, struct wl_array *states)
{
    (void)data; (void)toplevel; (void)states;
    fprintf(stderr, "test-client: toplevel configure %dx%d\n", w, h);
}

static void
xdg_toplevel_handle_close(void *data, struct xdg_toplevel *toplevel)
{
    (void)data; (void)toplevel;
    fprintf(stderr, "test-client: close requested\n");
    running = 0;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_handle_configure,
    .close = xdg_toplevel_handle_close,
};

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    const char *socket_name = getenv("WAYLAND_DISPLAY");
    if (!socket_name) socket_name = "playos-0";

    fprintf(stderr, "test-client: connecting to %s\n", socket_name);

    struct wl_display *display = wl_display_connect(socket_name);
    if (!display) {
        fprintf(stderr, "test-client: failed to connect to %s\n", socket_name);
        return EXIT_FAILURE;
    }

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);

    if (!compositor || !xdg_wm_base) {
        fprintf(stderr, "test-client: required globals missing\n");
        return EXIT_FAILURE;
    }

    surface = wl_compositor_create_surface(compositor);
    xdg_surface = xdg_wm_base_get_xdg_surface(xdg_wm_base, surface);
    xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);
    xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);
    xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, NULL);
    xdg_toplevel_set_title(xdg_toplevel, "PlayOS Test Client");
    xdg_toplevel_set_fullscreen(xdg_toplevel, NULL);
    wl_surface_commit(surface);

    wl_display_roundtrip(display);
    fprintf(stderr, "test-client: surface mapped, running...\n");

    while (running && wl_display_dispatch(display) != -1)
        ;

    fprintf(stderr, "test-client: exiting\n");

    if (buffer)       wl_buffer_destroy(buffer);
    if (xdg_toplevel) xdg_toplevel_destroy(xdg_toplevel);
    if (xdg_surface)  xdg_surface_destroy(xdg_surface);
    if (surface)      wl_surface_destroy(surface);
    if (xdg_wm_base)  xdg_wm_base_destroy(xdg_wm_base);
    if (shm)          wl_shm_destroy(shm);
    if (compositor)   wl_compositor_destroy(compositor);
    if (registry)     wl_registry_destroy(registry);
    if (display)      wl_display_disconnect(display);

    return EXIT_SUCCESS;
}
