#ifndef DOT11P_H
#define DOT11P_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Wraps a GeoNetworking-layer payload in an 802.11 OCB frame: QoS Data
// (subtype 8, 26-byte header), matching real ITS-G5 hardware, broadcast, no
// BSS (Addr1=Addr3=broadcast), LLC/SNAP with Ethertype 0x8947
// (GeoNetworking's registered Ethertype). Output is ready to hand straight
// to esp_wifi_80211_tx_custom() (tx_custom.c) - NOT esp_wifi_80211_tx(),
// which rejects this frame type outright. `src_mac` is used as Addr2 - pass
// the same 6 bytes you gave geonet_wrap_shb, since GN_ADDR's MID field is
// defined to be this same link-layer address. Returns bytes written, or -1
// if out buffer too small.
//
// History: this used to be downgraded to non-QoS Data (subtype 0) because
// esp_wifi_80211_tx() rejects QoS Data ("unsupport QoS frame type" / esp_err
// 258) and an attempted linker-override bypass (old main/wifi_patches.c)
// didn't work. Restored to QoS Data now that main.c transmits via
// esp_wifi_80211_tx_custom() instead, which bypasses that gate entirely
// (see tx_custom.c) - so there's no longer a reason to deviate from the
// real frame format.
// qos=true  -> QoS Data (subtype 8, 26-byte header) for esp_wifi_80211_tx_custom()
// qos=false -> plain Data (subtype 0, 24-byte header) which the STANDARD
//              esp_wifi_80211_tx() accepts (used for the standard-TX isolation test)
int dot11p_build_frame(const uint8_t *gn_payload, int gn_len,
                        const uint8_t src_mac[6],
                        uint8_t *out, size_t out_len, bool qos);

#endif
