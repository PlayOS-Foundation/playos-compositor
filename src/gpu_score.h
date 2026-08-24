#ifndef PLAYOS_GPU_SCORE_H
#define PLAYOS_GPU_SCORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * gpu_score.h — Pure GPU-selection scoring (ADR-0008, Sprint 13)
 *
 * The scoring model is kept free of DRM/Wayland dependencies so it can be
 * unit-tested with synthetic vendor/connector data:
 *
 *     eDP            +1000
 *     connected      +500  (only when not eDP)
 *     AMD            +300
 *     Intel          +100
 *     other vendor   +1    (NVIDIA falls here — effectively last resort)
 *
 * Highest total wins. Candidates must score at least 1 to be selectable.
 */

/* Known PCI vendor IDs */
#define PCI_VENDOR_AMD   0x1002
#define PCI_VENDOR_INTEL 0x8086
#define PCI_VENDOR_NVIDIA 0x10de

/* Scoring weights */
#define PLAYOS_GPU_SCORE_EDP       1000
#define PLAYOS_GPU_SCORE_CONNECTED  500
#define PLAYOS_GPU_SCORE_AMD        300
#define PLAYOS_GPU_SCORE_INTEL      100
#define PLAYOS_GPU_SCORE_OTHER        1

/* Minimal candidate shape for selection without any DRM types. */
struct playos_gpu_score_input {
    uint16_t vendor_id;
    bool     has_connected_output;
    bool     is_edp;
};

/* Score a single candidate. Always returns >= 1. */
int playos_gpu_score(uint16_t vendor_id, bool has_connected_output, bool is_edp);

/* Return the index of the highest-scoring candidate, or -1 if there are no
 * candidates. Ties resolve in favour of the earlier candidate. */
int playos_gpu_select_index(const struct playos_gpu_score_input *cands,
                            size_t count);

/* Human-readable vendor name for logs: "AMD", "Intel", "NVIDIA", "other". */
const char *playos_gpu_vendor_name(uint16_t vendor_id);

#endif /* PLAYOS_GPU_SCORE_H */
