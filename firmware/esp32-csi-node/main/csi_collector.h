/**
 * @file csi_collector.h
 * @brief CSI data collection and ADR-018 binary frame serialization.
 */

#ifndef CSI_COLLECTOR_H
#define CSI_COLLECTOR_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_wifi_types.h"

/** ADR-018 magic number. */
#define CSI_MAGIC 0xC5110001

/** ADR-018 header size in bytes. */
#define CSI_HEADER_SIZE 20

/** Maximum frame buffer size (header + 4 antennas * 256 subcarriers * 2 bytes). */
#define CSI_MAX_FRAME_SIZE (CSI_HEADER_SIZE + 4 * 256 * 2)

/** Maximum number of channels in the hop table (ADR-029). */
#define CSI_HOP_CHANNELS_MAX 6

/**
 * Initialize CSI collection.
 * Registers the WiFi CSI callback.
 */
void csi_collector_init(void);

/**
 * Capture node_id BEFORE wifi_init_sta() or any other heavy init.
 *
 * Must be called from app_main() immediately after nvs_config_load().
 * WiFi driver initialization can corrupt g_nvs_config.node_id (confirmed
 * on device 80:b5:4e:c1:be:b8, NVS=3 but post-WiFi reads as 1).
 * This early capture shields s_node_id from that corruption window.
 *
 * @param node_id Value from g_nvs_config.node_id, read right after NVS load.
 */
void csi_collector_set_node_id(uint8_t node_id);

/**
 * Get the runtime node_id (early capture if available, otherwise init-time).
 *
 * Other modules (edge_processing, wasm_runtime, display_ui) should prefer
 * this accessor over reading g_nvs_config.node_id directly.
 *
 * @return Node ID (0-255) as loaded from NVS at boot.
 */
uint8_t csi_collector_get_node_id(void);

/**
 * Serialize CSI data into ADR-018 binary frame format.
 *
 * @param info   WiFi CSI info from the ESP-IDF callback.
 * @param buf    Output buffer (must be at least CSI_MAX_FRAME_SIZE bytes).
 * @param buf_len Size of the output buffer.
 * @return Number of bytes written, or 0 on error.
 */
size_t csi_serialize_frame(const wifi_csi_info_t *info, uint8_t *buf, size_t buf_len);

/** Guarded DATA fallback runtime policy. */
typedef struct {
    bool     enabled;             /**< Enable automatic MGMT-only -> MGMT+DATA fallback. */
    uint16_t low_yield_pps;       /**< Enable DATA when processed CSI yield is <= this. */
    uint16_t raw_cb_max_pps;      /**< Disable DATA when raw callback pressure exceeds this. */
    uint32_t enable_after_ms;     /**< Startup grace period before DATA fallback may enable. */
    uint32_t min_data_dwell_ms;   /**< Minimum time to stay in DATA mode before disabling. */
    uint32_t cooldown_ms;         /**< Cooldown after DATA overload before re-enabling DATA. */
} csi_data_fallback_policy_t;

/** Action emitted by csi_data_fallback_decide(). */
typedef enum {
    CSI_DATA_FALLBACK_KEEP = 0,
    CSI_DATA_FALLBACK_ENABLE_DATA,
    CSI_DATA_FALLBACK_DISABLE_DATA,
} csi_data_fallback_action_t;

/**
 * Pure decision function for guarded DATA fallback.
 *
 * @param policy               Policy thresholds.
 * @param data_mode_enabled    Current promiscuous filter includes DATA.
 * @param processed_yield_pps  CSI callbacks that survived software gates.
 * @param raw_cb_pps           CSI callback entries before software rate gates.
 * @param now_ms               Monotonic time in milliseconds.
 * @param last_switch_ms       Last promiscuous filter switch time in ms.
 * @param last_data_disable_ms Last DATA-disable time in ms, or 0 if never.
 * @return Filter action to apply.
 */
csi_data_fallback_action_t csi_data_fallback_decide(
    const csi_data_fallback_policy_t *policy,
    bool data_mode_enabled,
    uint16_t processed_yield_pps,
    uint16_t raw_cb_pps,
    uint32_t now_ms,
    uint32_t last_switch_ms,
    uint32_t last_data_disable_ms);

/**
 * Configure the channel-hop table for multi-band sensing (ADR-029).
 *
 * When hop_count == 1 the collector stays on the single configured channel
 * (backward-compatible with the original single-channel mode).
 *
 * @param channels  Array of WiFi channel numbers (1-14 for 2.4 GHz, 36-177 for 5 GHz).
 * @param hop_count Number of entries in the channels array (1..CSI_HOP_CHANNELS_MAX).
 * @param dwell_ms  Dwell time per channel in milliseconds (>= 10).
 */
void csi_collector_set_hop_table(const uint8_t *channels, uint8_t hop_count, uint32_t dwell_ms);

/**
 * Advance to the next channel in the hop table.
 *
 * Called by the hop timer callback. If hop_count <= 1 this is a no-op.
 * Calls esp_wifi_set_channel() internally.
 */
void csi_hop_next_channel(void);

/**
 * Start the channel-hop timer.
 *
 * Creates an esp_timer periodic callback that fires every dwell_ms
 * milliseconds, calling csi_hop_next_channel(). If hop_count <= 1
 * the timer is not started (single-channel backward-compatible mode).
 */
void csi_collector_start_hop_timer(void);

/**
 * Inject an NDP (Null Data Packet) frame for sensing.
 *
 * Uses esp_wifi_80211_tx() to send a preamble-only frame (~24 us airtime)
 * that triggers CSI measurement at all receivers. This is the "sensing-first"
 * TX mechanism described in ADR-029.
 *
 * @return ESP_OK on success, or an error code.
 *
 * @note TODO: Full NDP frame construction. Currently sends a minimal
 *       null-data frame as a placeholder.
 */
esp_err_t csi_inject_ndp_frame(void);

/**
 * Get the recent CSI callback rate (per second).
 *
 * Computed as a sliding 1-second window over the internal s_cb_count
 * counter. Used by the ADR-081 radio abstraction layer to fill the
 * pkt_yield_per_sec field of rv_radio_health_t.
 *
 * @return Callbacks observed in the trailing ~1 second.
 */
uint16_t csi_collector_get_pkt_yield_per_sec(void);

/**
 * Get the cumulative UDP send-failure counter since boot.
 *
 * @return Number of stream_sender_send() failures recorded by the
 *         CSI callback path.
 */
uint16_t csi_collector_get_send_fail_count(void);

#endif /* CSI_COLLECTOR_H */
