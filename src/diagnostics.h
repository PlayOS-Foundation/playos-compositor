#ifndef PLAYOS_DIAGNOSTICS_H
#define PLAYOS_DIAGNOSTICS_H

#include <stdbool.h>
#include <stdint.h>

/**
 * diagnostics.h — PlayOS compositor diagnostics and logging
 *
 * Writes structured diagnostic messages to /run/playos/log/compositor.log.
 * Supports phase-specific failure logging, simpledrm fallback detection,
 * and structured GPU/renderer reporting.
 */

/* Log phases for failure tracing */
enum playos_diag_phase {
    PLAYOS_DIAG_PHASE_INIT,
    PLAYOS_DIAG_PHASE_GPU_DISCOVERY,
    PLAYOS_DIAG_PHASE_BACKEND_START,
    PLAYOS_DIAG_PHASE_RENDERER_INIT,
    PLAYOS_DIAG_PHASE_OUTPUT_SETUP,
    PLAYOS_DIAG_PHASE_FALLBACK,
    PLAYOS_DIAG_PHASE_RUNNING,
};

/* GPU discovery diagnostics */
struct playos_gpu_info {
    char    card_path[256];
    char    render_path[256];
    char    connector_name[64];
    uint16_t pci_vendor_id;
    uint16_t pci_device_id;
    int     mode_width;
    int     mode_height;
    int     mode_refresh_mhz;    /* mHz */
    bool    is_edp;
};

/* Renderer diagnostics */
struct playos_renderer_info {
    char renderer_name[256];
    char vendor_name[256];
    char gles_version[32];
    bool is_hardware;            /* false if llvmpipe/softpipe/swrast */
    bool gbm_available;
    bool egl_available;
};

/* ── API ──────────────────────────────────────────────── */

/**
 * Initialize diagnostics: create /run/playos/log directory,
 * open compositor.log for appending.
 * Returns 0 on success, -1 on failure (non-fatal — logs to stderr fallback).
 */
int  playos_diag_init(void);

/**
 * Log a phase entry with optional message.
 */
void playos_diag_log_phase(enum playos_diag_phase phase, const char *msg);

/**
 * Log GPU discovery results.
 */
void playos_diag_log_gpu(const struct playos_gpu_info *info);

/**
 * Log renderer initialization results.
 */
void playos_diag_log_renderer(const struct playos_renderer_info *info);

/**
 * Log a fatal error with phase context and an optional errno string.
 * Returns -1 so it can be used in return statements:
 *   return playos_diag_fatal(PHASE, "message");
 */
int  playos_diag_fatal(enum playos_diag_phase phase, const char *msg);

/**
 * Log a fallback path entry (e.g., simpledrm).
 */
void playos_diag_log_fallback(const char *fallback_name, const char *reason);

/**
 * Close the log file. Safe to call on uninitialized state.
 */
void playos_diag_fini(void);

#endif /* PLAYOS_DIAGNOSTICS_H */
