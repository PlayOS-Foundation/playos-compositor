/* test_gpu_select.c — Sprint 13 T1: GPU-selection scoring unit test.
 *
 * Feeds synthetic vendor/connector data through the ADR-0008 scoring model
 * without touching real DRM nodes. Covers the ZenBook UX530 hybrid shape
 * (Intel eDP iGPU + idle NVIDIA dGPU) and other multi-GPU combinations.
 */

#include <stdio.h>
#include <string.h>

#include "gpu_score.h"

static int failures = 0;

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            failures++;                                                       \
        }                                                                     \
    } while (0)

static void
test_score_basic(void)
{
    /* eDP + vendor */
    CHECK(playos_gpu_score(PCI_VENDOR_AMD,   true,  true)  == 1300);
    CHECK(playos_gpu_score(PCI_VENDOR_INTEL, true,  true)  == 1100);

    /* connected (non-eDP) + vendor */
    CHECK(playos_gpu_score(PCI_VENDOR_AMD,   true,  false) == 800);
    CHECK(playos_gpu_score(PCI_VENDOR_INTEL, true,  false) == 600);

    /* NVIDIA = "other" vendor: connected +1, idle +1 */
    CHECK(playos_gpu_score(PCI_VENDOR_NVIDIA, true,  false) == 501);
    CHECK(playos_gpu_score(PCI_VENDOR_NVIDIA, false, false) == 1);

    /* unknown vendor */
    CHECK(playos_gpu_score(0x1234, true,  false) == 501);
    CHECK(playos_gpu_score(0x1234, false, false) == 1);

    /* eDP beats connected regardless of vendor (AMD eDP > AMD connected) */
    CHECK(playos_gpu_score(PCI_VENDOR_AMD, true, true) >
          playos_gpu_score(PCI_VENDOR_AMD, true, false));
}

static void
test_select_prefers_edp_amd(void)
{
    const struct playos_gpu_score_input cands[] = {
        { PCI_VENDOR_INTEL, true,  true  },  /* 1100 */
        { PCI_VENDOR_AMD,   true,  true  },  /* 1300 -> winner */
        { PCI_VENDOR_NVIDIA, false, false }, /* 1 */
    };
    CHECK(playos_gpu_select_index(cands, 3) == 1);
}

static void
test_select_intel_over_nvidia_hybrid(void)
{
    /* ZenBook UX530 shape: Intel iGPU drives the eDP panel, NVIDIA dGPU idle */
    const struct playos_gpu_score_input cands[] = {
        { PCI_VENDOR_NVIDIA, false, false }, /* 1 */
        { PCI_VENDOR_INTEL,  true,  true  }, /* 1100 -> winner */
    };
    CHECK(playos_gpu_select_index(cands, 2) == 1);
}

static void
test_select_connected_intel_over_headless_amd(void)
{
    const struct playos_gpu_score_input cands[] = {
        { PCI_VENDOR_AMD,   false, false }, /* 300 */
        { PCI_VENDOR_INTEL, true,  false }, /* 600 -> winner */
    };
    CHECK(playos_gpu_select_index(cands, 2) == 1);
}

static void
test_select_single_other_vendor(void)
{
    /* A lone NVIDIA device with a connected output is still selectable. */
    const struct playos_gpu_score_input cands[] = {
        { PCI_VENDOR_NVIDIA, true, false }, /* 501 -> winner */
    };
    CHECK(playos_gpu_select_index(cands, 1) == 0);
}

static void
test_select_empty(void)
{
    CHECK(playos_gpu_select_index(NULL, 0) == -1);
}

static void
test_vendor_names(void)
{
    CHECK(strcmp(playos_gpu_vendor_name(PCI_VENDOR_AMD), "AMD") == 0);
    CHECK(strcmp(playos_gpu_vendor_name(PCI_VENDOR_INTEL), "Intel") == 0);
    CHECK(strcmp(playos_gpu_vendor_name(PCI_VENDOR_NVIDIA), "NVIDIA") == 0);
    CHECK(strcmp(playos_gpu_vendor_name(0x1234), "other") == 0);
}

int
main(void)
{
    test_score_basic();
    test_select_prefers_edp_amd();
    test_select_intel_over_nvidia_hybrid();
    test_select_connected_intel_over_headless_amd();
    test_select_single_other_vendor();
    test_select_empty();
    test_vendor_names();

    if (failures != 0) {
        fprintf(stderr, "test_gpu_select: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_gpu_select: all tests passed\n");
    return 0;
}
