#ifndef PLAYOS_GPU_DISCOVERY_H
#define PLAYOS_GPU_DISCOVERY_H

#include <stdbool.h>
#include <stdint.h>
#include "diagnostics.h"

/**
 * gpu_discovery.h — Deterministic GPU discovery via DRM enumeration
 *
 * Implements ADR-0008: enumerate DRM devices with drmGetDevices2(),
 * resolve PCI vendor/device identity, detect active connectors, and
 * select the correct GPU. Never hardcodes /dev/dri/card0.
 */

/* Known PCI vendor IDs */
#define PCI_VENDOR_AMD   0x1002
#define PCI_VENDOR_INTEL 0x8086

/* Maximum DRM devices to enumerate */
#define PLAYOS_MAX_DRM_DEVICES 8

/**
 * Result of GPU discovery — the selected device.
 */
struct playos_gpu {
    int      card_fd;            /* open fd to primary node, or -1 */
    int      render_fd;          /* open fd to render node, or -1 */
    char     card_path[256];
    char     render_path[256];
    uint16_t pci_vendor_id;
    uint16_t pci_device_id;
    bool     valid;              /* false if no suitable GPU found */
};

/**
 * Discover and select the best GPU.
 *
 * Selection priority (ADR-0008):
 *   1. Device with an active connected display (eDP/LVDS preferred)
 *   2. First AMD device
 *   3. First Intel device
 *   4. First valid DRM device
 *
 * On success, fills *gpu and returns 0.
 * On failure, sets gpu->valid = false and returns -1, logging the reason.
 */
int playos_gpu_discover(struct playos_gpu *gpu);

/**
 * Close any open file descriptors held by the gpu struct.
 */
void playos_gpu_close(struct playos_gpu *gpu);

#endif /* PLAYOS_GPU_DISCOVERY_H */
