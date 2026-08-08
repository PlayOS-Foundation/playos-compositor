#ifndef PLAYOS_RENDERER_GBM_EGL_H
#define PLAYOS_RENDERER_GBM_EGL_H

#include <stdbool.h>
#include <wlr/render/wlr_renderer.h>
#include "diagnostics.h"

/**
 * renderer_gbm_egl.h — GBM/EGL/Mesa rendering path initialization
 *
 * Queries the EGL context for renderer/vendor/GLES version info,
 * detects software rendering (llvmpipe/softpipe/swrast), and fills
 * a diagnostics struct for logging.
 */

/**
 * Query the GBM/EGL/GLES rendering path.
 *
 * Checks that the renderer is hardware-accelerated (not llvmpipe,
 * softpipe, or swrast). Logs the renderer name, vendor, and GLES
 * version via diagnostics.
 *
 * @param renderer  The wlroots renderer (must be already created)
 * @param info      Output: filled with renderer details
 * @return 0 if hardware-accelerated, -1 if software or query failed
 */
int playos_renderer_query(struct wlr_renderer *renderer,
                          struct playos_renderer_info *info);

/**
 * Check if the renderer is using Mesa and GBM.
 * Simple string-based heuristic on the renderer name.
 */
bool playos_renderer_is_mesa_gbm(const struct playos_renderer_info *info);

/**
 * Detect software rendering by checking known software renderer names.
 */
bool playos_renderer_is_software(const char *renderer_name);

#endif /* PLAYOS_RENDERER_GBM_EGL_H */
