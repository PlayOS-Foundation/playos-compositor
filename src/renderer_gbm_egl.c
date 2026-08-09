#include "renderer_gbm_egl.h"
#include <stdio.h>
#include <string.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <wlr/util/log.h>

/**
 * renderer_gbm_egl.c — GBM/EGL/Mesa rendering path diagnostics
 *
 * Queries the EGL context attached to the wlroots renderer,
 * identifies the GPU driver and GLES version, and detects
 * software rendering.
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

    /* Check if we have an EGL context bound (wlroots already set one up
     * for DRM/nested backends). Do NOT create a temporary context — that
     * would conflict with wlroots' internal EGL state. */
    EGLDisplay egl_display = eglGetCurrentDisplay();
    EGLContext egl_context = eglGetCurrentContext();

    if (egl_display == EGL_NO_DISPLAY || egl_context == EGL_NO_CONTEXT) {
        /* No current EGL context — wlroots hasn't set one up yet.
         * This is expected for headless backends. Return gracefully. */
        info->egl_available = false;
        info->gbm_available = false;
        snprintf(info->renderer_name, sizeof(info->renderer_name),
                 "EGL context not current (headless or pre-init)");
        return -1;
    }

    /* EGL context already current — query directly */
    const GLubyte *renderer_str = glGetString(GL_RENDERER);
    const GLubyte *vendor_str   = glGetString(GL_VENDOR);
    const GLubyte *version_str  = glGetString(GL_VERSION);

    if (renderer_str)
        strncpy(info->renderer_name, (const char *)renderer_str,
                sizeof(info->renderer_name) - 1);
    if (vendor_str)
        strncpy(info->vendor_name, (const char *)vendor_str,
                sizeof(info->vendor_name) - 1);
    if (version_str)
        strncpy(info->gles_version, (const char *)version_str,
                sizeof(info->gles_version) - 1);

    info->is_hardware = !playos_renderer_is_software(info->renderer_name);
    info->egl_available = true;
    info->gbm_available = true; /* wlroots DRM/nested backend implies GBM */

    /* Log the result */
    playos_diag_log_renderer(info);

    if (!info->is_hardware) {
        wlr_log(WLR_ERROR, "playos-compositor: WARNING: software rendering "
                "detected (%s)", info->renderer_name);
        return -1;
    }

    return 0;
}
