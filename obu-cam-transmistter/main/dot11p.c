#include "dot11p.h"
#include <string.h>

int dot11p_build_frame(const uint8_t *gn_payload, int gn_len,
                        const uint8_t src_mac[6],
                        uint8_t *out, size_t out_len, bool qos)
{
    static const uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    static const uint8_t llc_snap[8] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x89, 0x47};

    int hdr_len = qos ? 26 : 24; // QoS Data adds a 2-byte QoS Control field
    int total = hdr_len + 8 /* LLC/SNAP */ + gn_len;
    if ((size_t)total > out_len) {
        return -1;
    }

    uint8_t *p = out;

    // Frame Control: version=0, type=Data(2), subtype=QoS Data(8) -> bytes
    // 0x88 0x00. This is what real ITS-G5 hardware actually transmits.
    //
    // Back on QoS Data again (previously downgraded to non-QoS, subtype 0,
    // as a working-but-nonstandard fallback - see git history / old comments
    // here for that whole detour). What changed: main.c no longer calls
    // esp_wifi_80211_tx() at all - it now goes through
    // esp_wifi_80211_tx_custom() (tx_custom.c, pulled from
    // opentrafficmap/its-g5-receiver-firmware_txenabled), which bypasses the
    // frame-type sanity check entirely by never calling the code path that
    // contains it. Frame subtype is no longer gated, so there's no reason
    // left to avoid matching real hardware here.
    // Frame Control byte 0: version=0, type=Data(2). Subtype: QoS Data(8)=0x88
    // for the tx_custom path, or plain Data(0)=0x08 for the standard
    // esp_wifi_80211_tx() path (which rejects QoS Data outright).
    *p++ = qos ? 0x88 : 0x08; *p++ = 0x00;
    // Duration
    *p++ = 0x00; *p++ = 0x00;
    // Addr1 = destination = broadcast
    memcpy(p, broadcast, 6); p += 6;
    // Addr2 = source (our pseudonym)
    memcpy(p, src_mac, 6); p += 6;
    // Addr3 = BSSID = broadcast (no BSS exists in OCB mode)
    memcpy(p, broadcast, 6); p += 6;
    // Sequence control - left at 0; en_sys_seq=true fills this in for us
    *p++ = 0x00; *p++ = 0x00;
    // QoS Control field - only present in QoS Data frames
    if (qos) {
        *p++ = 0x00; *p++ = 0x00; // best-effort access category
    }

    // LLC/SNAP (Ethertype 0x8947 = GeoNetworking)
    memcpy(p, llc_snap, 8); p += 8;

    // GeoNetworking + BTP + DENM payload
    memcpy(p, gn_payload, gn_len); p += gn_len;

    return (int)(p - out);
}
