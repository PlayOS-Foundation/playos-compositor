#ifndef PLAYOS_OUTPUT_MODES_H
#define PLAYOS_OUTPUT_MODES_H

#include <stdbool.h>
#include <stdint.h>

/**
 * output_modes.h — Output enumeration and mode selection
 *
 * Directly inspects DRM connectors via the provided file descriptor
 * to find the preferred output (eDP > LVDS > DSI > other connected)
 * and its preferred mode.
 */

struct playos_output_config {
    int  width;
    int  height;
    int  refresh_mhz;  /* mHz */
    int  scale_100;    /* output scale × 100 */
    char connector_name[64];
    bool is_edp;
    bool valid;
};

/**
 * Select the best output by inspecting DRM connectors.
 *
 * @param drm_fd  Open DRM device file descriptor
 * @param config  Output configuration (filled on success)
 * @return 0 on success, -1 if no usable output found
 */
int playos_output_select_from_fd(int drm_fd,
                                 struct playos_output_config *config);

#endif /* PLAYOS_OUTPUT_MODES_H */
