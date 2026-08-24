#include "gpu_score.h"

int
playos_gpu_score(uint16_t vendor_id, bool has_connected_output, bool is_edp)
{
    int s = 0;

    if (is_edp) {
        s += PLAYOS_GPU_SCORE_EDP;
    } else if (has_connected_output) {
        s += PLAYOS_GPU_SCORE_CONNECTED;
    }

    if (vendor_id == PCI_VENDOR_AMD) {
        s += PLAYOS_GPU_SCORE_AMD;
    } else if (vendor_id == PCI_VENDOR_INTEL) {
        s += PLAYOS_GPU_SCORE_INTEL;
    } else {
        s += PLAYOS_GPU_SCORE_OTHER;
    }

    return s;
}

int
playos_gpu_select_index(const struct playos_gpu_score_input *cands, size_t count)
{
    int best = -1;
    int best_score = -1;

    for (size_t i = 0; i < count; i++) {
        int s = playos_gpu_score(cands[i].vendor_id,
                                 cands[i].has_connected_output,
                                 cands[i].is_edp);
        if (s > best_score) {
            best_score = s;
            best = (int)i;
        }
    }

    return best;
}

const char *
playos_gpu_vendor_name(uint16_t vendor_id)
{
    switch (vendor_id) {
    case PCI_VENDOR_AMD:    return "AMD";
    case PCI_VENDOR_INTEL:  return "Intel";
    case PCI_VENDOR_NVIDIA: return "NVIDIA";
    default:                return "other";
    }
}
