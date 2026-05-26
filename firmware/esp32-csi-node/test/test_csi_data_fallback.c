/*
 * Host unit tests for the guarded CSI DATA fallback policy.
 *
 * The policy is pure so we can validate the S3 crash-avoidance guard without
 * real WiFi hardware: low processed CSI yield should enable DATA capture, but
 * excessive raw callback pressure should disable it and enter cooldown.
 */

#include "esp_stubs.h"

#include "csi_collector.h"

#include <stdio.h>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg) do {                                           \
    if (cond) {                                                         \
        g_pass++;                                                       \
    } else {                                                            \
        g_fail++;                                                       \
        printf("  FAIL: %s (line %d)\n", msg, __LINE__);                \
    }                                                                   \
} while (0)

static csi_data_fallback_policy_t default_policy(void)
{
    csi_data_fallback_policy_t p = {
        .enabled = true,
        .low_yield_pps = 2,
        .raw_cb_max_pps = 80,
        .enable_after_ms = 5000,
        .min_data_dwell_ms = 10000,
        .cooldown_ms = 30000,
    };
    return p;
}

static void test_low_mgmt_yield_enables_data_after_startup_delay(void)
{
    printf("test: low MGMT yield enables DATA after startup delay\n");
    csi_data_fallback_policy_t p = default_policy();

    csi_data_fallback_action_t early = csi_data_fallback_decide(
        &p, false, 0, 0, 4000, 0, 0);
    CHECK(early == CSI_DATA_FALLBACK_KEEP,
          "startup delay should suppress early DATA enable");

    csi_data_fallback_action_t after_delay = csi_data_fallback_decide(
        &p, false, 1, 1, 6000, 0, 0);
    CHECK(after_delay == CSI_DATA_FALLBACK_ENABLE_DATA,
          "low yield after startup delay should enable DATA");
}

static void test_high_raw_callback_rate_disables_data_after_dwell(void)
{
    printf("test: high raw callback pressure disables DATA after dwell\n");
    csi_data_fallback_policy_t p = default_policy();

    csi_data_fallback_action_t during_dwell = csi_data_fallback_decide(
        &p, true, 40, 200, 12000, 5000, 0);
    CHECK(during_dwell == CSI_DATA_FALLBACK_KEEP,
          "minimum dwell should prevent immediate DATA disable");

    csi_data_fallback_action_t after_dwell = csi_data_fallback_decide(
        &p, true, 40, 200, 16000, 5000, 0);
    CHECK(after_dwell == CSI_DATA_FALLBACK_DISABLE_DATA,
          "high raw callback pressure after dwell should disable DATA");
}

static void test_cooldown_blocks_reenable_after_data_disable(void)
{
    printf("test: cooldown blocks DATA re-enable after overload\n");
    csi_data_fallback_policy_t p = default_policy();

    csi_data_fallback_action_t cooling = csi_data_fallback_decide(
        &p, false, 0, 0, 20000, 16000, 16000);
    CHECK(cooling == CSI_DATA_FALLBACK_KEEP,
          "cooldown should block DATA re-enable");

    csi_data_fallback_action_t cooled = csi_data_fallback_decide(
        &p, false, 0, 0, 47000, 16000, 16000);
    CHECK(cooled == CSI_DATA_FALLBACK_ENABLE_DATA,
          "DATA can re-enable after cooldown expires");
}

static void test_disabled_policy_never_changes_filter(void)
{
    printf("test: disabled policy keeps current filter\n");
    csi_data_fallback_policy_t p = default_policy();
    p.enabled = false;

    CHECK(csi_data_fallback_decide(&p, false, 0, 0, 6000, 0, 0)
              == CSI_DATA_FALLBACK_KEEP,
          "disabled policy should not enable DATA");
    CHECK(csi_data_fallback_decide(&p, true, 30, 500, 60000, 0, 0)
              == CSI_DATA_FALLBACK_KEEP,
          "disabled policy should not disable DATA");
}

int main(void)
{
    printf("=== csi DATA fallback policy host tests ===\n\n");

    test_low_mgmt_yield_enables_data_after_startup_delay();
    test_high_raw_callback_rate_disables_data_after_dwell();
    test_cooldown_blocks_reenable_after_data_disable();
    test_disabled_policy_never_changes_filter();

    printf("\n=== result: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
