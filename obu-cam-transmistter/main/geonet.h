#ifndef GEONET_H
#define GEONET_H
#include <stdint.h>
#include <stddef.h>

// Wraps an ITS application payload (e.g. from denm_encode) with a minimal
// GeoNetworking Basic Header + Common Header + Single-Hop-Broadcast
// extended header (HeaderType=TSB(5), HeaderSubtype=SINGLE_HOP(0), per
// ETSI EN 302 636-4-1 table 9), then prepends a BTP-B header addressed to
// the DENM service port (2002).
//
// `mac` is the 6-byte pseudonym/link-layer address - pass the SAME address
// you hand to dot11p_build_frame's src address, since GN_ADDR's MID field
// (the last 6 bytes of the 8-byte GN_ADDR) is defined to BE that
// link-layer address (EN 302 636-4-1 clause 9.5.1). `station_type` is the
// 5-bit ITS-S type from the same clause (5 = passengerCar) and gets packed
// into GN_ADDR alongside the address.
//
// `latitude_tenmicrodeg`/`longitude_tenmicrodeg` go into the Source Long
// Position Vector (clause 9.5.2) as plain 32-bit signed big-endian fields -
// NOT UPER bit-packed like the DENM payload's position fields, this is a
// fixed-width binary protocol. Pass the SAME values you gave denm_encode's
// eventPosition, so the GN-layer position and the DENM's own claimed
// position agree.
//
// Deliberate simplification: real DENM dissemination normally uses
// GeoBroadcast (GBC, HeaderType=4) so RSUs/OBUs can forward it across an
// area - that needs a sequence number + geo-area fields this skeleton
// doesn't build yet. Single-hop broadcast is simpler and is the
// best-tested decode path in the receiver firmware you already have
// working (same extended header shape as CAM). Fine for a single-vehicle
// beacon; revisit if you need real multi-hop forwarding later.
//
// `btp_dest_port` is the BTP-B destination port for the service being carried
// (ETSI TS 103 248): 2001 = CAM, 2002 = DENM, 2003 = MAPEM, 2004 = SPATEM, ...
//
// Returns bytes written, or -1 if out buffer too small.
int geonet_wrap_shb(const uint8_t *its_payload, int its_len,
                     const uint8_t mac[6], uint8_t station_type,
                     int32_t latitude_tenmicrodeg, int32_t longitude_tenmicrodeg,
                     uint16_t btp_dest_port,
                     uint8_t *out, size_t out_len);

#endif
