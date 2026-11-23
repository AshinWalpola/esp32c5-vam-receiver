// Copied from opentrafficmap/its-g5-receiver-firmware_txenabled, main/tx_custom.h.
// See tx_custom.c for what this does and why we pulled it in.
#pragma once

#include "esp_wifi.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t esp_wifi_80211_tx_custom(wifi_interface_t ifx, const void *buffer, int32_t len, bool en_sys_seq, wifi_tx_rate_config_t *tx_rate_config, wifi_band_t band, wifi_bandwidth_t bw);

#ifdef __cplusplus
}
#endif
