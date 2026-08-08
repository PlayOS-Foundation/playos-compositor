#include "output_modes.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <libdrm/drm_mode.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

/**
 * output_modes.c — Output enumeration and mode selection
 *
 * Directly inspects DRM connectors via the provided file descriptor
 * to find the preferred output (eDP > LVDS > DSI > other connected)
 * and its preferred mode.
 */

static bool
is_internal_panel(uint32_t connector_type)
{
    return (connector_type == DRM_MODE_CONNECTOR_eDP ||
            connector_type == DRM_MODE_CONNECTOR_LVDS ||
            connector_type == DRM_MODE_CONNECTOR_DSI);
}

static const char *
connector_type_name(uint32_t type)
{
    switch (type) {
    case DRM_MODE_CONNECTOR_eDP:         return "eDP";
    case DRM_MODE_CONNECTOR_LVDS:        return "LVDS";
    case DRM_MODE_CONNECTOR_DSI:         return "DSI";
    case DRM_MODE_CONNECTOR_DisplayPort: return "DP";
    case DRM_MODE_CONNECTOR_HDMIA:       return "HDMI-A";
    case DRM_MODE_CONNECTOR_HDMIB:       return "HDMI-B";
    case DRM_MODE_CONNECTOR_VGA:         return "VGA";
    case DRM_MODE_CONNECTOR_DVII:        return "DVI-I";
    default:                             return "Other";
    }
}

int
playos_output_select_from_fd(int drm_fd,
                             struct playos_output_config *config)
{
    memset(config, 0, sizeof(*config));

    drmModeResPtr res = drmModeGetResources(drm_fd);
    if (!res) {
        fprintf(stderr, "output_modes: drmModeGetResources failed\n");
        return -1;
    }

    int best_score = -1;
    int best_conn_idx = -1;
    int best_mode_idx = -1;

    /* Score connectors: internal panels get higher scores */
    for (int i = 0; i < res->count_connectors; i++) {
        drmModeConnectorPtr conn = drmModeGetConnectorCurrent(drm_fd,
                                                               res->connectors[i]);
        if (!conn)
            continue;

        /* Skip disconnected connectors */
        if (conn->connection != DRM_MODE_CONNECTED || conn->count_modes == 0) {
            drmModeFreeConnector(conn);
            continue;
        }

        int score = 0;
        if (conn->connector_type == DRM_MODE_CONNECTOR_eDP)
            score = 1000;
        else if (conn->connector_type == DRM_MODE_CONNECTOR_LVDS)
            score = 900;
        else if (conn->connector_type == DRM_MODE_CONNECTOR_DSI)
            score = 850;
        else
            score = 100;  /* External display */

        /* Prefer higher resolution */
        int max_w = 0;
        int max_h = 0;
        int best_m = 0;
        for (int m = 0; m < conn->count_modes; m++) {
            int pixels = conn->modes[m].hdisplay * conn->modes[m].vdisplay;
            if (pixels > max_w * max_h) {
                max_w = conn->modes[m].hdisplay;
                max_h = conn->modes[m].vdisplay;
                best_m = m;
            }
        }
        score += max_w * max_h / 100000;  /* resolution bonus */

        if (score > best_score) {
            best_score = score;
            best_conn_idx = i;
            best_mode_idx = best_m;
        }

        drmModeFreeConnector(conn);
    }

    if (best_conn_idx < 0) {
        drmModeFreeResources(res);
        return -1;
    }

    /* Re-read the best connector to get details */
    drmModeConnectorPtr conn = drmModeGetConnectorCurrent(drm_fd,
                                                           res->connectors[best_conn_idx]);
    if (!conn || best_mode_idx >= conn->count_modes) {
        if (conn) drmModeFreeConnector(conn);
        drmModeFreeResources(res);
        return -1;
    }

    config->width       = conn->modes[best_mode_idx].hdisplay;
    config->height      = conn->modes[best_mode_idx].vdisplay;
    config->refresh_mhz = conn->modes[best_mode_idx].vrefresh * 1000;
    config->scale_100   = 100;
    config->is_edp      = is_internal_panel(conn->connector_type);
    config->valid       = true;
    snprintf(config->connector_name, sizeof(config->connector_name),
             "%s-%u", connector_type_name(conn->connector_type),
             conn->connector_type_id);

    drmModeFreeConnector(conn);
    drmModeFreeResources(res);

    fprintf(stderr, "output_modes: selected %s %dx%d@%dHz (internal=%s)\n",
            config->connector_name,
            config->width, config->height,
            config->refresh_mhz / 1000,
            config->is_edp ? "yes" : "no");

    return 0;
}
