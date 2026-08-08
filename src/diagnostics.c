#include "diagnostics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* ── Internal state ─────────────────────────────────────── */
static FILE  *diag_logfile = NULL;
static bool   diag_initialized = false;

/* ── Phase name strings ─────────────────────────────────── */
static const char *
phase_name(enum playos_diag_phase phase)
{
    switch (phase) {
        case PLAYOS_DIAG_PHASE_INIT:           return "INIT";
        case PLAYOS_DIAG_PHASE_GPU_DISCOVERY:  return "GPU_DISCOVERY";
        case PLAYOS_DIAG_PHASE_BACKEND_START:  return "BACKEND_START";
        case PLAYOS_DIAG_PHASE_RENDERER_INIT:  return "RENDERER_INIT";
        case PLAYOS_DIAG_PHASE_OUTPUT_SETUP:   return "OUTPUT_SETUP";
        case PLAYOS_DIAG_PHASE_FALLBACK:       return "FALLBACK";
        case PLAYOS_DIAG_PHASE_RUNNING:        return "RUNNING";
        default:                               return "UNKNOWN";
    }
}

/* ── Timestamp helper ───────────────────────────────────── */
static void
write_timestamp(FILE *f)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    fprintf(f, "[%ld.%09ld] ", (long)ts.tv_sec, (long)ts.tv_nsec);
}

/* ── Core write helper ──────────────────────────────────── */
static void
diag_write(const char *level, const char *phase_str, const char *msg)
{
    FILE *out = diag_logfile ? diag_logfile : stderr;

    write_timestamp(out);
    fprintf(out, "%-6s [%-14s] %s\n", level, phase_str, msg);
    fflush(out);
}

/* ── Public API ─────────────────────────────────────────── */

int
playos_diag_init(void)
{
    if (diag_initialized)
        return 0;

    /* Ensure log directory exists */
    if (mkdir("/run/playos", 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "playos-diag: failed to create /run/playos: %s\n",
                strerror(errno));
        /* non-fatal — continue with stderr fallback */
    }
    if (mkdir("/run/playos/log", 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "playos-diag: failed to create /run/playos/log: %s\n",
                strerror(errno));
        /* non-fatal */
    }

    diag_logfile = fopen("/run/playos/log/compositor.log", "a");
    if (!diag_logfile) {
        fprintf(stderr, "playos-diag: WARNING: cannot open compositor.log, "
                "logging to stderr only\n");
    }

    diag_initialized = true;

    /* Bootstrap entry */
    write_timestamp(diag_logfile ? diag_logfile : stderr);
    fprintf(diag_logfile ? diag_logfile : stderr,
            "playos-compositor v0.4.0 starting (pid=%d)\n", getpid());
    fflush(diag_logfile ? diag_logfile : stderr);

    return 0;
}

void
playos_diag_log_phase(enum playos_diag_phase phase, const char *msg)
{
    diag_write("PHASE", phase_name(phase), msg ? msg : "");
}

void
playos_diag_log_gpu(const struct playos_gpu_info *info)
{
    char buf[512];
    snprintf(buf, sizeof(buf),
             "card=%s render=%s vendor=0x%04x device=0x%04x "
             "connector=%s mode=%dx%d@%dHz eDP=%s",
             info->card_path,
             info->render_path,
             info->pci_vendor_id,
             info->pci_device_id,
             info->connector_name[0] ? info->connector_name : "none",
             info->mode_width,
             info->mode_height,
             info->mode_refresh_mhz / 1000,
             info->is_edp ? "yes" : "no");
    diag_write("GPU", "GPU_DISCOVERY", buf);
}

void
playos_diag_log_renderer(const struct playos_renderer_info *info)
{
    char buf[512];
    snprintf(buf, sizeof(buf),
             "renderer=%s vendor=%s GLES=%s hw=%s GBM=%s EGL=%s",
             info->renderer_name,
             info->vendor_name,
             info->gles_version,
             info->is_hardware ? "yes" : "NO (software)",
             info->gbm_available ? "yes" : "NO",
             info->egl_available ? "yes" : "NO");
    diag_write("RENDER", "RENDERER_INIT", buf);
}

int
playos_diag_fatal(enum playos_diag_phase phase, const char *msg)
{
    char buf[512];
    snprintf(buf, sizeof(buf), "FATAL: %s (errno=%d: %s)",
             msg, errno, strerror(errno));
    diag_write("FATAL", phase_name(phase), buf);
    return -1;
}

void
playos_diag_log_fallback(const char *fallback_name, const char *reason)
{
    char buf[512];
    snprintf(buf, sizeof(buf), "entering fallback: %s (reason: %s)",
             fallback_name, reason);
    diag_write("FALLBACK", "FALLBACK", buf);
}

void
playos_diag_fini(void)
{
    if (!diag_initialized)
        return;

    diag_write("INFO", "SHUTDOWN", "compositor shutting down");

    if (diag_logfile) {
        fclose(diag_logfile);
        diag_logfile = NULL;
    }

    diag_initialized = false;
}
