#include "renderer_gbm_egl.h"
#include <stdio.h>
#include <string.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <wlr/render/egl.h>
#include <wlr/render/gles2.h>
#include <wlr/util/log.h>

/**
 * renderer_gbm_egl.c — GBM/EGL/Mesa rendering path diagnostics
 *
 * Uses wlroots' internal wlr_egl (via wlr_gles2_renderer_get_egl)
 * to query the GPU driver and GLES version. Avoids standalone
 * eglGetDisplay(EGL_DEFAULT_DISPLAY) which fails on DRM/GBM.
 */

/* Known software renderer substrings */
static const char *software_renderers[] = {
    "llvmpipe",
    "softpipe",
    "swrast",
    NULL
};

bool
playos_renderer_is_software(const char *renderer_name)
{
    if (!renderer_name)
        return true;  /* can't determine — assume software */

    for (int i = 0; software_renderers[i]; i++) {
        if (strstr(renderer_name, software_renderers[i]))
            return true;
    }
    return false;
}

bool
playos_renderer_is_mesa_gbm(const struct playos_renderer_info *info)
{
    if (!info)
        return false;

    /* Mesa renderers typically contain "Mesa" or "radeonsi" */
    if (strstr(info->renderer_name, "Mesa") ||
        strstr(info->renderer_name, "radeonsi") ||
        strstr(info->renderer_name, "iris") ||
        strstr(info->vendor_name, "Mesa")) {
        return info->gbm_available;
    }
    return false;
}

int
playos_renderer_query(struct wlr_renderer *renderer,
                      struct playos_renderer_info *info)
{
    memset(info, 0, sizeof(*info));

    if (!renderer) {
        wlr_log(WLR_ERROR, "renderer_query: NULL renderer");
        return -1;
    }

    /* Get the EGL display through wlroots' GLES2 renderer.
     * On DRM/GBM backends, wlroots already created the EGL context
     * with a proper GBM device — we must use that, not EGL_DEFAULT_DISPLAY
     * which doesn't work without an X11/Wayland platform. */
    struct wlr_egl *egl = wlr_gles2_renderer_get_egl(renderer);
    if (!egl) {
        /* Not a GLES2 renderer (e.g., pixman software renderer) */
        wlr_log(WLR_INFO, "renderer_query: not a GLES2 renderer "
                "(pixman/software)");
        info->egl_available = false;
        info->gbm_available = false;
        info->is_hardware   = false;
        snprintf(info->renderer_name, sizeof(info->renderer_name),
                 "pixman (software)");
        snprintf(info->vendor_name, sizeof(info->vendor_name),
                 "N/A");
        goto done;
    }

    EGLDisplay dpy = wlr_egl_get_display(egl);
    EGLContext ctx = wlr_egl_get_context(egl);

    if (dpy == EGL_NO_DISPLAY || ctx == EGL_NO_CONTEXT) {
        wlr_log(WLR_ERROR, "renderer_query: wlr_egl has no display/context");
        info->egl_available = false;
        info->gbm_available = false;
        snprintf(info->renderer_name, sizeof(info->renderer_name),
                 "unknown (no EGL)");
        return -1;
    }

    info->egl_available = true;
    info->gbm_available = true; /* wlroots GLES2 renderer implies GBM */

    /* Create a tiny pbuffer surface so we can make the context current
     * and query GL strings without disrupting the compositor's rendering */
    EGLint config_attrs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 1,
        EGL_GREEN_SIZE, 1,
        EGL_BLUE_SIZE, 1,
        EGL_NONE
    };
    EGLConfig config;
    EGLint num_configs;
    if (!eglChooseConfig(dpy, config_attrs, &config, 1, &num_configs) ||
        num_configs == 0) {
        wlr_log(WLR_ERROR, "renderer_query: no suitable EGL config");
        return -1;
    }

    EGLint pb_attrs[] = {
        EGL_WIDTH, 1,
        EGL_HEIGHT, 1,
        EGL_NONE
    };
    EGLSurface pb = eglCreatePbufferSurface(dpy, config, pb_attrs);
    if (pb == EGL_NO_SURFACE) {
        wlr_log(WLR_ERROR, "renderer_query: pbuffer creation failed");
        return -1;
    }

    /* Make wlroots' context current with our temporary surface */
    eglMakeCurrent(dpy, pb, pb, ctx);

    /* Query GL strings */
    const GLubyte *renderer_str = glGetString(GL_RENDERER);
    const GLubyte *vendor_str   = glGetString(GL_VENDOR);
    const GLubyte *version_str  = glGetString(GL_VERSION);

    if (renderer_str)
        strncpy(info->renderer_name, (const char *)renderer_str,
                sizeof(info->renderer_name) - 1);
    else
        snprintf(info->renderer_name, sizeof(info->renderer_name),
                 "unknown");

    if (vendor_str)
        strncpy(info->vendor_name, (const char *)vendor_str,
                sizeof(info->vendor_name) - 1);
    else
        snprintf(info->vendor_name, sizeof(info->vendor_name),
                 "unknown");

    if (version_str)
        strncpy(info->gles_version, (const char *)version_str,
                sizeof(info->gles_version) - 1);
    else
        snprintf(info->gles_version, sizeof(info->gles_version),
                 "unknown");

    info->is_hardware = !playos_renderer_is_software(info->renderer_name);

    /* Restore — unbind our surface without destroying wlroots' context */
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(dpy, pb);

    /* Log the result */
    playos_diag_log_renderer(info);

    if (!info->is_hardware) {
        wlr_log(WLR_ERROR, "playos-compositor: WARNING: software rendering "
                "detected (%s)", info->renderer_name);
        return -1;
    }

    return 0;

done:
    playos_diag_log_renderer(info);
    return -1;
}
