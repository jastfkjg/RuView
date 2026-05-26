#include "csi_collector.h"

csi_data_fallback_action_t csi_data_fallback_decide(
    const csi_data_fallback_policy_t *policy,
    bool data_mode_enabled,
    uint16_t processed_yield_pps,
    uint16_t raw_cb_pps,
    uint32_t now_ms,
    uint32_t last_switch_ms,
    uint32_t last_data_disable_ms)
{
    if (policy == NULL || !policy->enabled) {
        return CSI_DATA_FALLBACK_KEEP;
    }

    if (data_mode_enabled) {
        uint32_t dwell_ms = now_ms - last_switch_ms;
        if (raw_cb_pps > policy->raw_cb_max_pps &&
            dwell_ms >= policy->min_data_dwell_ms) {
            return CSI_DATA_FALLBACK_DISABLE_DATA;
        }
        return CSI_DATA_FALLBACK_KEEP;
    }

    if (now_ms < policy->enable_after_ms) {
        return CSI_DATA_FALLBACK_KEEP;
    }
    if (last_data_disable_ms != 0 &&
        (now_ms - last_data_disable_ms) < policy->cooldown_ms) {
        return CSI_DATA_FALLBACK_KEEP;
    }
    if (processed_yield_pps <= policy->low_yield_pps) {
        return CSI_DATA_FALLBACK_ENABLE_DATA;
    }
    return CSI_DATA_FALLBACK_KEEP;
}
