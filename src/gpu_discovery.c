#include "gpu_discovery.h"
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

/**
 * gpu_discovery.c — Discover and select the correct GPU
 *
 * Algorithm (ADR-0008):
 *   1. Enumerate all DRM devices via drmGetDevices2()
 *   2. For each: resolve PCI vendor/device, check for active connector
 *   3. Select: connected+AMD > connected+Intel > first AMD > first Intel > first valid
 *
 * Preference for internal panels: eDP (DRM_MODE_CONNECTOR_eDP) or LVDS.
 */

/* ── Internal helpers ───────────────────────────────────── */

static bool
device_has_connector(int fd, const char *card_path,
                     char *connector_name, size_t conn_name_len,
                     bool *out_is_edp)
{
    (void)card_path;

    drmModeResPtr res = drmModeGetResources(fd);
    if (!res)
        return false;

    bool found = false;
    *out_is_edp = false;

    for (int i = 0; i < res->count_connectors && !found; i++) {
        drmModeConnectorPtr conn =
            drmModeGetConnectorCurrent(fd, res->connectors[i]);
        if (!conn)
            continue;

        /* Prefer eDP or LVDS (built-in panels) */
        if (conn->connection == DRM_MODE_CONNECTED) {
            if (conn->connector_type == DRM_MODE_CONNECTOR_eDP ||
                conn->connector_type == DRM_MODE_CONNECTOR_LVDS) {
                *out_is_edp = true;
            }
            if (connector_name && conn_name_len > 0) {
                snprintf(connector_name, conn_name_len,
                         "%s-%u",
                         drmModeGetConnectorTypeName(conn->connector_type),
                         conn->connector_type_id);
            }
            found = true;
        }
        drmModeFreeConnector(conn);
    }

    drmModeFreeResources(res);
    return found;
}

/* ── Candidate scoring ──────────────────────────────────── */
struct gpu_candidate {
    int         index;
    char        primary_path[256];
    char        render_path[256];
    uint16_t    vendor_id;
    uint16_t    device_id;
    bool        has_connected_output;
    bool        is_edp;
    char        connector_name[64];
    int         score;
};

/* Scoring: eDP+connected+AMD = best. Scale: */
#define SCORE_EDP           1000
#define SCORE_CONNECTED      500
#define SCORE_AMD            300
#define SCORE_INTEL          100
#define SCORE_VALID            1

static int
calculate_score(const struct gpu_candidate *c)
{
    int s = 0;
    if (c->is_edp)               s += SCORE_EDP;
    else if (c->has_connected_output) s += SCORE_CONNECTED;
    if (c->vendor_id == PCI_VENDOR_AMD)   s += SCORE_AMD;
    else if (c->vendor_id == PCI_VENDOR_INTEL) s += SCORE_INTEL;
    else s += SCORE_VALID;
    return s;
}

/* ── Public API ─────────────────────────────────────────── */

int
playos_gpu_discover(struct playos_gpu *gpu)
{
    memset(gpu, 0, sizeof(*gpu));
    gpu->card_fd   = -1;
    gpu->render_fd = -1;
    gpu->valid     = false;

    playos_diag_log_phase(PLAYOS_DIAG_PHASE_GPU_DISCOVERY,
                          "enumerating DRM devices");

    /* Enumerate all DRM devices */
    drmDevicePtr devices[PLAYOS_MAX_DRM_DEVICES];
    int count = drmGetDevices2(0, devices, PLAYOS_MAX_DRM_DEVICES);
    if (count < 0) {
        /* drmGetDevices2 might not be available; try drmGetDevices */
        count = drmGetDevices(devices, PLAYOS_MAX_DRM_DEVICES);
    }
    if (count <= 0) {
        playos_diag_log_phase(PLAYOS_DIAG_PHASE_GPU_DISCOVERY,
                              "no DRM devices found");
        return -1;
    }

    struct gpu_candidate best;
    int best_score = -1;
    memset(&best, 0, sizeof(best));

    /* Evaluate each device */
    for (int i = 0; i < count; i++) {
        drmDevicePtr dev = devices[i];

        /* Must have a primary node */
        if (!(dev->available_nodes & (1 << DRM_NODE_PRIMARY)))
            continue;

        struct gpu_candidate c;
        memset(&c, 0, sizeof(c));
        c.index = i;

        /* dev->nodes[] is a NULL-terminated string array of paths */
        if (dev->nodes[DRM_NODE_PRIMARY]) {
            snprintf(c.primary_path, sizeof(c.primary_path),
                     "%s", dev->nodes[DRM_NODE_PRIMARY]);
        }

        if ((dev->available_nodes & (1 << DRM_NODE_RENDER)) &&
            dev->nodes[DRM_NODE_RENDER]) {
            snprintf(c.render_path, sizeof(c.render_path),
                     "%s", dev->nodes[DRM_NODE_RENDER]);
        }

        /* Get PCI IDs */
        if (dev->bustype == DRM_BUS_PCI) {
            c.vendor_id = dev->deviceinfo.pci->vendor_id;
            c.device_id = dev->deviceinfo.pci->device_id;
        }

        /* Check for connected outputs */
        int fd = open(c.primary_path, O_RDWR | O_CLOEXEC);
        if (fd < 0)
            fd = open(c.primary_path, O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            c.has_connected_output = device_has_connector(
                fd, c.primary_path,
                c.connector_name, sizeof(c.connector_name),
                &c.is_edp);
            close(fd);
        }

        c.score = calculate_score(&c);

        if (c.score > best_score) {
            best = c;
            best_score = c.score;
        }
    }

    /* Clean up enumeration */
    for (int i = 0; i < count; i++)
        drmFreeDevice(&devices[i]);

    if (best_score < 0) {
        playos_diag_log_phase(PLAYOS_DIAG_PHASE_GPU_DISCOVERY,
                              "no suitable GPU found");
        return -1;
    }

    /* Open the selected device */
    gpu->card_fd = open(best.primary_path, O_RDWR | O_CLOEXEC);
    if (gpu->card_fd < 0) {
        playos_diag_log_fallback("simpledrm",
                                 "cannot open primary node for selected GPU");
        return playos_diag_fatal(PLAYOS_DIAG_PHASE_GPU_DISCOVERY,
                                 "cannot open primary DRM node");
    }

    if (best.render_path[0]) {
        gpu->render_fd = open(best.render_path, O_RDWR | O_CLOEXEC);
        if (gpu->render_fd < 0)
            gpu->render_fd = open(best.render_path, O_RDONLY | O_CLOEXEC);
    }

    strncpy(gpu->card_path, best.primary_path, sizeof(gpu->card_path) - 1);
    strncpy(gpu->render_path, best.render_path, sizeof(gpu->render_path) - 1);
    gpu->pci_vendor_id = best.vendor_id;
    gpu->pci_device_id = best.device_id;
    gpu->valid = true;

    /* Log the selected GPU */
    struct playos_gpu_info info;
    memset(&info, 0, sizeof(info));
    strncpy(info.card_path, gpu->card_path, sizeof(info.card_path) - 1);
    strncpy(info.render_path, gpu->render_path, sizeof(info.render_path) - 1);
    strncpy(info.connector_name, best.connector_name,
            sizeof(info.connector_name) - 1);
    info.pci_vendor_id = gpu->pci_vendor_id;
    info.pci_device_id = gpu->pci_device_id;
    info.mode_width = 0;    /* filled by output_modes later */
    info.mode_height = 0;
    info.mode_refresh_mhz = 0;
    info.is_edp = best.is_edp;
    playos_diag_log_gpu(&info);

    return 0;
}

void
playos_gpu_close(struct playos_gpu *gpu)
{
    if (!gpu)
        return;
    if (gpu->card_fd >= 0) {
        close(gpu->card_fd);
        gpu->card_fd = -1;
    }
    if (gpu->render_fd >= 0) {
        close(gpu->render_fd);
        gpu->render_fd = -1;
    }
    gpu->valid = false;
}
