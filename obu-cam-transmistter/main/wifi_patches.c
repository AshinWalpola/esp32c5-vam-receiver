#include <stdint.h>

// RETIRED - no longer built (removed from main/CMakeLists.txt SRCS), kept only
// for history. Confirmed not to work: linked cleanly with -Wl,-zmuldefs but
// the QoS-frame rejection persisted identically. Also turned out to be based
// on the wrong function signature - the real ieee80211_raw_frame_sanity_check
// takes (wifi_interface_t ifx, const void *buffer, int32_t len, bool
// en_sys_seq), confirmed from opentrafficmap/its-g5-receiver-firmware_txenabled's
// main/tx_custom.c, not the 3x int32_t guessed below. Superseded by
// tx_custom.c, which bypasses esp_wifi_80211_tx() (and the function that
// calls this check) entirely instead of trying to neutralize the check.
// See docs/04-transmit-setup.md.

// Overrides a function inside the closed-source WiFi library that gates
// which raw 802.11 frame types esp_wifi_80211_tx() will accept. By default
// it only allows beacon/probe-request/probe-response/action and non-QoS
// data frames - it explicitly rejects QoS Data (subtype 8), which is what
// real ITS-G5/802.11p hardware actually transmits and expects.
//
// This is the same technique used by ESP32 WiFi-security tools (deauther/
// injection projects) to unlock raw frame injection: define a function with
// the exact same name as the library's gate, and link with -Wl,-zmuldefs
// (see CMakeLists.txt) so the linker accepts having two definitions of the
// same symbol instead of erroring with "multiple definition of
// `ieee80211_raw_frame_sanity_check'" - and takes this one instead of the
// library's.
//
// Confirmed present for THIS target/IDF version: `nm` on
// components/esp_wifi/lib/esp32c5/libnet80211.a (IDF v5.5.4) shows
// `ieee80211_raw_frame_sanity_check` as a normal (non-weak) global text
// symbol in ieee80211_node.o. The exact argument count/meaning is
// reverse-engineered from community ESP32 (Xtensa) deauther tools, not
// confirmed byte-for-byte against esp32c5's actual implementation - if
// frames still get rejected, or this crashes, the real signature may take
// different arguments than assumed here.
//
// Real risk, not just an inconvenience: this disables ALL sanity checking
// on raw frames going through esp_wifi_80211_tx(), not just the QoS-type
// gate. Whatever else that check validates (frame length bounds, etc.) is
// now unchecked. Malformed frames from a bug elsewhere in this codebase
// could behave worse (silent corruption, crash) than they would have with
// the check in place, where they'd have just been rejected cleanly.
int ieee80211_raw_frame_sanity_check(int32_t arg1, int32_t arg2, int32_t arg3)
{
    (void)arg1;
    (void)arg2;
    (void)arg3;
    return 0; // 0 = "frame is sane" - always pass
}
