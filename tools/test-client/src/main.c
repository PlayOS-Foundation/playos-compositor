/**
 * tools/test-client/src/main.c — PlayOS Wayland hardware-accelerated test client
 *
 * Connects to playos-0, creates an xdg_toplevel, renders an
 * animated PlayOS-branded frame using EGL/GLES2 at ~60fps,
 * and displays GPU diagnostics in the window title.
 *
 * Sprint 4 — replaces the wl_shm software client with EGL/GLES2.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <sys/time.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include "xdg-shell-client-protocol.h"

static struct wl_compositor *compositor = NULL;
static struct xdg_wm_base *xdg_wm_base = NULL;
static struct wl_surface *surface = NULL;
static struct xdg_surface *xdg_surface = NULL;
static struct xdg_toplevel *xdg_toplevel = NULL;
static struct wl_egl_window *egl_window = NULL;
static EGLDisplay egl_display = EGL_NO_DISPLAY;
static EGLSurface egl_surface = EGL_NO_SURFACE;
static EGLContext egl_context = EGL_NO_CONTEXT;
static EGLConfig egl_config = NULL;
static int running = 1;
static int width = 640, height = 480;
static struct timeval start_time;

/* Forward declarations */
static int init_shaders(void);

static void
registry_handle_global(void *data, struct wl_registry *registry,
                       uint32_t name, const char *interface, uint32_t version)
{
    (void)data;
    if (strcmp(interface, wl_compositor_interface.name) == 0)
        compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
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
init_egl(struct wl_display *wayland_display)
{
    /* On bare DRM/KMS without X11/Wayland platform, eglGetDisplay(NULL)
     * fails. Pass the wl_display* so the EGL implementation knows to
     * go through the Wayland protocol. */
    egl_display = eglGetDisplay((EGLNativeDisplayType)wayland_display);
    if (egl_display == EGL_NO_DISPLAY) {
        fprintf(stderr, "test-client: eglGetDisplay failed\n");
        return -1;
    }

    EGLint major, minor;
    if (!eglInitialize(egl_display, &major, &minor)) {
        fprintf(stderr, "test-client: eglInitialize failed\n");
        return -1;
    }

    /* We are a GLES2 client — without this the default EGL_OPENGL_API
     * is used and the context/surface end up mismatched (EGL_BAD_MATCH
     * at eglMakeCurrent) */
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        fprintf(stderr, "test-client: eglBindAPI(ES) failed: 0x%x\n",
                eglGetError());
        return -1;
    }

    EGLint config_attrs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };
    EGLConfig config;
    EGLint num_configs;
    if (!eglChooseConfig(egl_display, config_attrs, &config, 1, &num_configs) ||
        num_configs == 0) {
        fprintf(stderr, "test-client: no suitable EGL config\n");
        return -1;
    }
    egl_config = config;

    EGLint ctx_attrs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    egl_context = eglCreateContext(egl_display, config,
                                   EGL_NO_CONTEXT, ctx_attrs);
    if (egl_context == EGL_NO_CONTEXT) {
        fprintf(stderr, "test-client: eglCreateContext failed\n");
        return -1;
    }

    return 0;
}

static void
create_egl_window(void)
{
    egl_window = wl_egl_window_create(surface, width, height);
    if (!egl_window) {
        fprintf(stderr, "test-client: wl_egl_window_create failed\n");
        return;
    }

    /* Reuse the SAME EGLConfig the context was created with — Mesa
     * requires surface and context configs to be identical, otherwise
     * eglMakeCurrent fails with EGL_BAD_MATCH. */
    if (!egl_config) {
        fprintf(stderr, "test-client: no EGL config from init\n");
        return;
    }

    egl_surface = eglCreateWindowSurface(egl_display, egl_config,
                                         (EGLNativeWindowType)egl_window, NULL);
    if (egl_surface == EGL_NO_SURFACE) {
        fprintf(stderr, "test-client: eglCreateWindowSurface failed: 0x%x\n",
                eglGetError());
    }
}

static void
xdg_surface_handle_configure(void *data, struct xdg_surface *xdg_surface,
                             uint32_t serial)
{
    (void)data;
    xdg_surface_ack_configure(xdg_surface, serial);

    if (!egl_surface || egl_surface == EGL_NO_SURFACE) {
        create_egl_window();
        if (egl_surface != EGL_NO_SURFACE) {
            if (!eglMakeCurrent(egl_display, egl_surface, egl_surface,
                                egl_context)) {
                fprintf(stderr, "test-client: eglMakeCurrent failed: 0x%x\n",
                        eglGetError());
            }

            /* Init shaders once */
            if (init_shaders() != 0) {
                fprintf(stderr, "test-client: shader init failed\n");
            }

            /* Set window title with GPU diagnostics */
            const GLubyte *renderer_str = glGetString(GL_RENDERER);
            const GLubyte *vendor_str   = glGetString(GL_VENDOR);
            const GLubyte *version_str  = glGetString(GL_VERSION);
            char title[256];
            snprintf(title, sizeof(title),
                     "PlayOS Sprint 4 | %s | %s | GLES %s",
                     vendor_str ? (const char *)vendor_str : "?",
                     renderer_str ? (const char *)renderer_str : "?",
                     version_str ? (const char *)version_str : "?");
            xdg_toplevel_set_title(xdg_toplevel, title);

            fprintf(stderr, "test-client: GPU: %s / %s / GLES %s\n",
                    vendor_str ? (const char *)vendor_str : "?",
                    renderer_str ? (const char *)renderer_str : "?",
                    version_str ? (const char *)version_str : "?");
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
    if (w > 0 && h > 0) {
        width = w;
        height = h;
        if (egl_window) {
            wl_egl_window_resize(egl_window, width, height, 0, 0);
            glViewport(0, 0, width, height);
        }
    }
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

/* ── Animated rendering (EGL/GLES2) ────────────────────── */

static GLuint shader_program = 0;
static GLint  u_color_loc = -1;

static const char *vertex_shader_src =
    "attribute vec3 a_pos;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos, 1.0);\n"
    "}\n";

static const char *fragment_shader_src =
    "precision mediump float;\n"
    "uniform vec4 u_color;\n"
    "void main() {\n"
    "    gl_FragColor = u_color;\n"
    "}\n";

static GLuint
compile_shader(GLenum type, const char *src)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);

    GLint compiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint log_len;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_len);
        char *log = malloc(log_len + 1);
        glGetShaderInfoLog(shader, log_len, NULL, log);
        fprintf(stderr, "test-client: shader compile error: %s\n", log);
        free(log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static int
init_shaders(void)
{
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vertex_shader_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_src);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return -1;
    }

    shader_program = glCreateProgram();
    glAttachShader(shader_program, vs);
    glAttachShader(shader_program, fs);
    glLinkProgram(shader_program);

    GLint linked;
    glGetProgramiv(shader_program, GL_LINK_STATUS, &linked);
    if (!linked) {
        fprintf(stderr, "test-client: shader link error\n");
        glDeleteProgram(shader_program);
        shader_program = 0;
        return -1;
    }

    u_color_loc = glGetUniformLocation(shader_program, "u_color");
    glDeleteShader(vs);
    glDeleteShader(fs);
    return 0;
}

static void
draw_frame(void)
{
    struct timeval now;
    gettimeofday(&now, NULL);
    double elapsed = (now.tv_sec - start_time.tv_sec) +
                     (now.tv_usec - start_time.tv_usec) / 1000000.0;

    /* PlayOS blue background */
    float r = 0.08f, g = 0.16f, b = 0.30f;

    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(shader_program);

    /* Animated accent bars */
    float bar_speed = 0.3f;
    float bar_width = 0.15f;

    for (int i = 0; i < 3; i++) {
        float offset = fmodf((float)(elapsed * bar_speed + i * 0.33f), 2.0f) - 1.0f;

        /* Pulsing playos orange accent */
        float brightness = 0.6f + 0.4f * sinf((float)(elapsed * 3.0 + i));
        float cr = 0.84f * brightness;
        float cg = 0.42f * brightness;
        float cb = 0.0f;

        /* Vertical bar */
        float x1 = offset - bar_width * 0.5f;
        float x2 = offset + bar_width * 0.5f;

        GLfloat vertices[] = {
            x1, -1.0f, 0.0f,
            x2, -1.0f, 0.0f,
            x1,  1.0f, 0.0f,
            x2,  1.0f, 0.0f,
        };

        GLint pos_loc = glGetAttribLocation(shader_program, "a_pos");
        glEnableVertexAttribArray((GLuint)pos_loc);
        glVertexAttribPointer((GLuint)pos_loc, 3, GL_FLOAT, GL_FALSE, 0, vertices);

        glUniform4f(u_color_loc, cr, cg, cb, 0.3f + 0.5f * brightness);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDisableVertexAttribArray((GLuint)pos_loc);
    }

    eglSwapBuffers(egl_display, egl_surface);
}

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    gettimeofday(&start_time, NULL);

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

    /* Initialize EGL before creating the Wayland surface */
    if (init_egl(display) != 0) {
        fprintf(stderr, "test-client: EGL initialization failed\n");
        return EXIT_FAILURE;
    }

    /* Never throttle inside eglSwapBuffers on frame callbacks — a test
     * client must make forward progress even if frame-done delivery
     * stalls; the busy loop below draws as fast as possible. */
    eglSwapInterval(egl_display, 0);

    fprintf(stderr, "test-client: EGL initialized, creating surface\n");

    surface = wl_compositor_create_surface(compositor);
    xdg_surface = xdg_wm_base_get_xdg_surface(xdg_wm_base, surface);
    xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);
    xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);
    xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, NULL);
    xdg_toplevel_set_title(xdg_toplevel, "PlayOS Sprint 4 — Hardware-Accelerated Test Client");
    xdg_toplevel_set_fullscreen(xdg_toplevel, NULL);
    wl_surface_commit(surface);

    wl_display_roundtrip(display);
    fprintf(stderr, "test-client: surface mapped, rendering...\n");

    /* Main render loop */
    int frame_count = 0;
    struct timeval last_fps_time = start_time;

    while (running) {
        /* Non-blocking event pump — never wait on the socket */
        wl_display_dispatch_pending(display);
        wl_display_flush(display);

        if (egl_surface != EGL_NO_SURFACE) {
            draw_frame();
            frame_count++;

            /* FPS counter every 5 seconds */
            struct timeval now;
            gettimeofday(&now, NULL);
            double fps_elapsed = (now.tv_sec - last_fps_time.tv_sec) +
                                 (now.tv_usec - last_fps_time.tv_usec) / 1000000.0;
            if (fps_elapsed >= 5.0) {
                double fps = frame_count / fps_elapsed;
                fprintf(stderr, "test-client: %.1f fps (%d frames)\n",
                        fps, frame_count);
                frame_count = 0;
                last_fps_time = now;
            }
        }
    }

    fprintf(stderr, "test-client: exiting\n");

    /* Cleanup */
    if (egl_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
        if (egl_surface != EGL_NO_SURFACE)
            eglDestroySurface(egl_display, egl_surface);
        if (egl_context != EGL_NO_CONTEXT)
            eglDestroyContext(egl_display, egl_context);
        eglTerminate(egl_display);
    }
    if (egl_window) wl_egl_window_destroy(egl_window);
    if (xdg_toplevel) xdg_toplevel_destroy(xdg_toplevel);
    if (xdg_surface)  xdg_surface_destroy(xdg_surface);
    if (surface)      wl_surface_destroy(surface);
    if (xdg_wm_base)  xdg_wm_base_destroy(xdg_wm_base);
    if (compositor)   wl_compositor_destroy(compositor);
    if (registry)     wl_registry_destroy(registry);
    if (display)      wl_display_disconnect(display);

    return EXIT_SUCCESS;
}
