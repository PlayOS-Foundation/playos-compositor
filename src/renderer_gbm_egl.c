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

    /* Get the EGL display and context through wlroots */
    /* wlroots 0.20: wlr_renderer_get_*_for_display */
    /* For now, use a diagnostic approach — query GL directly
     * since wlroots has already created the EGL context. */

    /* Check if we have an EGL context bound */
    EGLDisplay egl_display = eglGetCurrentDisplay();
    EGLContext egl_context = eglGetCurrentContext();

    if (egl_display == EGL_NO_DISPLAY || egl_context == EGL_NO_CONTEXT) {
        /* No current EGL context — try to create a temporary one */
        EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (dpy == EGL_NO_DISPLAY) {
            wlr_log(WLR_ERROR, "renderer_query: no EGL display available");
            info->egl_available = false;
            info->gbm_available = false;
            snprintf(info->renderer_name, sizeof(info->renderer_name),
                     "unknown (no EGL)");
            return -1;
        }

        EGLint major, minor;
        if (!eglInitialize(dpy, &major, &minor)) {
            wlr_log(WLR_ERROR, "renderer_query: eglInitialize failed");
            info->egl_available = false;
            goto done;
        }
        info->egl_available = true;

        /* Create a pbuffer surface and bind context temporarily */
        EGLint config_attrs[] = {
            EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_NONE
        };
        EGLConfig config;
        EGLint num_configs;
        if (!eglChooseConfig(dpy, config_attrs, &config, 1, &num_configs) ||
            num_configs == 0) {
            wlr_log(WLR_ERROR, "renderer_query: no suitable EGL config");
            eglTerminate(dpy);
            return -1;
        }

        EGLint ctx_attrs[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2,
            EGL_NONE
        };
        EGLContext ctx = eglCreateContext(dpy, config, EGL_NO_CONTEXT, ctx_attrs);
        if (ctx == EGL_NO_CONTEXT) {
            wlr_log(WLR_ERROR, "renderer_query: eglCreateContext failed");
            eglTerminate(dpy);
            return -1;
        }

        EGLint pb_attrs[] = {
            EGL_WIDTH, 1,
            EGL_HEIGHT, 1,
            EGL_NONE
        };
        EGLSurface pb = eglCreatePbufferSurface(dpy, config, pb_attrs);
        if (pb == EGL_NO_SURFACE) {
            eglDestroyContext(dpy, ctx);
            eglTerminate(dpy);
            return -1;
        }

        eglMakeCurrent(dpy, pb, pb, ctx);

        /* Now query GL */
        const GLubyte *renderer_str = glGetString(GL_RENDERER);
        const GLubyte *vendor_str   = glGetString(GL_VENDOR);
        const GLubyte *version_str  = glGetString(GL_VERSION);

        if (renderer_str) {
            strncpy(info->renderer_name, (const char *)renderer_str,
                    sizeof(info->renderer_name) - 1);
        } else {
            snprintf(info->renderer_name, sizeof(info->renderer_name),
                     "unknown");
        }

        if (vendor_str) {
            strncpy(info->vendor_name, (const char *)vendor_str,
                    sizeof(info->vendor_name) - 1);
        } else {
            snprintf(info->vendor_name, sizeof(info->vendor_name),
                     "unknown");
        }

        if (version_str) {
            strncpy(info->gles_version, (const char *)version_str,
                    sizeof(info->gles_version) - 1);
        } else {
            snprintf(info->gles_version, sizeof(info->gles_version),
                     "unknown");
        }

        info->is_hardware = !playos_renderer_is_software(info->renderer_name);
        info->gbm_available = eglQueryString(dpy, EGL_EXTENSIONS) &&
            strstr(eglQueryString(dpy, EGL_EXTENSIONS), "EGL_KHR_platform_gbm");

        /* Cleanup */
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(dpy, pb);
        eglDestroyContext(dpy, ctx);

        eglTerminate(dpy);
    } else {
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
        info->gbm_available = true; /* wlroots DRM backend implies GBM */
    }

    /* Log the result */
    playos_diag_log_renderer(info);

    if (!info->is_hardware) {
        wlr_log(WLR_ERROR, "playos-compositor: WARNING: software rendering "
                "detected (%s)", info->renderer_name);
        return -1;
    }

    return 0;

done:
    return -1;
}
