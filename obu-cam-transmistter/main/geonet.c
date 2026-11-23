#include "geonet.h"
#include <string.h>

int geonet_wrap_shb(const uint8_t *its_payload, int its_len,
                     const uint8_t mac[6], uint8_t station_type,
                     int32_t latitude_tenmicrodeg, int32_t longitude_tenmicrodeg,
                     uint16_t btp_dest_port,
                     uint8_t *out, size_t out_len)
{
    // GN Basic Header (4) + GN Common Header (8) + SHB source LPV (24)
    // + BTP-B header (4) + ITS payload
    int total = 4 + 8 + 24 + 4 + its_len;
    if ((size_t)total > out_len) {
        return -1;
    }

    uint8_t *p = out;

    // ---- GN Basic Header (4 bytes) ---- (EN 302 636-4-1 clause 9.6)
    *p++ = (uint8_t)((1 << 4) | 1); // version=1, NextHeader=1 (Common Header, unsecured)
    *p++ = 0x00;                    // reserved
    *p++ = 0x83;                    // lifetime (~60s in the base/multiplier encoding) - tune if needed
    *p++ = 1;                       // remaining hop limit = 1 (SHB single-hop; matches CAM in the Rust reference)

    // ---- GN Common Header (8 bytes) ---- (clause 9.7)
    *p++ = (uint8_t)((2 << 4) | 0); // NextHeader=2 (BTP-B), reserved nibble
    // HeaderType=5 (TSB), HeaderSubtype=0 (SINGLE_HOP) per table 9 - this is
    // the actual encoding for single-hop broadcast. An earlier version of
    // this code used (2,0), which is GEOUNICAST - wrong header type entirely
    // for a broadcast frame; real receivers would try to match the
    // destination-address extended header GeoUnicast expects and mishandle
    // or reject the packet.
    *p++ = (uint8_t)((5 << 4) | 0);
    *p++ = 0x02;                    // traffic class: SCF=0, ChannelOffload=0, TC-ID=2 (clause 9.7.5)
    *p++ = 0x80;                    // flags: bit0 = "is mobile" station (clause 9.7.2)
    // Payload length = what follows the WHOLE GeoNetworking header
    // (Basic+Common+Extended), i.e. BTP-B header + ITS payload only - does
    // NOT include the 24-byte extended header itself. An earlier version of
    // this code wrongly added the 24 bytes in here too.
    uint16_t payload_len = (uint16_t)(4 + its_len);
    *p++ = (uint8_t)(payload_len >> 8);
    *p++ = (uint8_t)(payload_len & 0xFF);
    *p++ = 1;                       // max hop limit = 1, matches basic header RHL (SHB single-hop)
    *p++ = 0x00;                    // reserved

    // ---- SHB extended header: Source Long Position Vector (24 bytes) ----
    // (clause 9.5.2). GN_ADDR (8 bytes) is itself structured, not a raw
    // pseudonym (clause 9.5.1): bit0 M-flag(0=auto-derived), bits1-5 ITS-S
    // type (5-bit), bits6-15 reserved(=0), then octets2-7 = MID, which is
    // defined to BE the link-layer (802.11) address - so this must match
    // the source address dot11p_build_frame uses, not just "look similar."
    uint8_t gn_addr[8];
    gn_addr[0] = (uint8_t)((0 << 7) | ((station_type & 0x1F) << 2)); // M=0, ST=station_type, top 2 reserved bits=0
    gn_addr[1] = 0x00; // remaining 8 reserved bits
    memcpy(&gn_addr[2], mac, 6);    // MID = link-layer address
    memcpy(p, gn_addr, 8); p += 8;
    // Timestamp (4 bytes, ms since 2004-01-01 mod 2^32) - placeholder 0,
    // same caveat as detectionTime in denm.c.
    memset(p, 0, 4); p += 4;
    // Latitude/Longitude (4+4 bytes, signed, big-endian, 1/10 microdegree) -
    // fixed-width binary fields, not UPER bit-packed.
    uint32_t lat_u = (uint32_t)latitude_tenmicrodeg;
    *p++ = (uint8_t)(lat_u >> 24); *p++ = (uint8_t)(lat_u >> 16);
    *p++ = (uint8_t)(lat_u >> 8);  *p++ = (uint8_t)(lat_u);
    uint32_t lon_u = (uint32_t)longitude_tenmicrodeg;
    *p++ = (uint8_t)(lon_u >> 24); *p++ = (uint8_t)(lon_u >> 16);
    *p++ = (uint8_t)(lon_u >> 8);  *p++ = (uint8_t)(lon_u);
    // PAI(1 bit) + Speed(15 bits), packed into 2 bytes: 0 = PAI false,
    // speed 0 - which is actually correct semantics for a STATIONARY
    // vehicle beacon, not just a placeholder.
    *p++ = 0x00; *p++ = 0x00;
    // Heading (16 bits, 0.1 degree units): 0 = due north / unavailable
    *p++ = 0x00; *p++ = 0x00;

    // ---- BTP-B header (4 bytes) ----
    *p++ = (uint8_t)(btp_dest_port >> 8);
    *p++ = (uint8_t)(btp_dest_port & 0xFF);
    *p++ = 0x00; *p++ = 0x00; // destination port info, unused for BTP-B

    // ---- ITS payload (DENM UPER bytes) ----
    memcpy(p, its_payload, its_len);
    p += its_len;

    return (int)(p - out);
}
