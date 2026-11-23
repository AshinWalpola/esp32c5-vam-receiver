#include "cam.h"
#include <string.h>

// MSB-first bit packer - identical approach to denm.c (ASN.1 UPER is a
// bitstream, not a byte stream).
typedef struct {
    uint8_t *buf;
    size_t buf_len;
    size_t bit_pos;
} bitwriter_t;

static void bw_init(bitwriter_t *bw, uint8_t *buf, size_t len)
{
    bw->buf = buf;
    bw->buf_len = len;
    bw->bit_pos = 0;
    memset(buf, 0, len);
}

static void bw_put_bits(bitwriter_t *bw, uint64_t value, int nbits)
{
    for (int i = nbits - 1; i >= 0; i--) {
        size_t byte_idx = bw->bit_pos / 8;
        int bit_idx = 7 - (int)(bw->bit_pos % 8);
        if (byte_idx >= bw->buf_len) {
            return; // overflow guard - check return value of cam_encode
        }
        uint8_t bit = (value >> i) & 1;
        bw->buf[byte_idx] = (uint8_t)(bw->buf[byte_idx] | (bit << bit_idx));
        bw->bit_pos++;
    }
}

static size_t bw_byte_len(const bitwriter_t *bw)
{
    return (bw->bit_pos + 7) / 8;
}

int cam_encode(const cam_fields_t *f, uint8_t *buf, size_t buf_len)
{
    bitwriter_t bw;
    bw_init(&bw, buf, buf_len);

    // ---- ItsPduHeader ---- (SEQUENCE, no OPTIONALs, no "..." -> no preamble)
    bw_put_bits(&bw, 2, 8);              // protocolVersion INTEGER(0..255) = 2
    bw_put_bits(&bw, 2, 8);              // messageID INTEGER(0..255) = cam(2)
    bw_put_bits(&bw, f->station_id, 32); // stationID StationID INTEGER(0..4294967295)

    // ---- CoopAwareness ---- (SEQUENCE, no OPTIONALs, no "...")
    // generationDeltaTime GenerationDeltaTime INTEGER(0..65535) -> 16 bits
    bw_put_bits(&bw, f->generation_delta_time, 16);

    // ---- CamParameters ---- (SEQUENCE, EXTENSIBLE "...", 2 OPTIONALs:
    // lowFrequencyContainer, specialVehicleContainer)
    bw_put_bits(&bw, 0, 1); // extension bit: no extension additions
    bw_put_bits(&bw, 1, 1); // lowFrequencyContainer present
    bw_put_bits(&bw, 0, 1); // specialVehicleContainer absent

    // ---- BasicContainer ---- (SEQUENCE, EXTENSIBLE "...", no OPTIONALs)
    bw_put_bits(&bw, 0, 1);                 // extension bit: none
    bw_put_bits(&bw, f->station_type, 8);   // stationType StationType INTEGER(0..255)

    // ReferencePosition (SEQUENCE, no OPTIONALs/"..."), identical widths to
    // DENM eventPosition (see denm.c for the constraint derivations):
    //   Latitude INTEGER(-900000000..900000001)   -> 31 bits, offset from -900000000
    uint32_t lat_offset = (uint32_t)((int64_t)f->latitude_tenmicrodeg - (-900000000));
    bw_put_bits(&bw, lat_offset, 31);
    //   Longitude INTEGER(-1800000000..1800000001) -> 32 bits, offset from -1800000000
    uint32_t lon_offset = (uint32_t)((int64_t)f->longitude_tenmicrodeg - (-1800000000));
    bw_put_bits(&bw, lon_offset, 32);
    //   PosConfidenceEllipse: SemiAxisLength(0..4095)->12, HeadingValue(0..3601)->12
    bw_put_bits(&bw, 4095, 12); // semiMajorConfidence: unavailable
    bw_put_bits(&bw, 4095, 12); // semiMinorConfidence: unavailable
    bw_put_bits(&bw, 3601, 12); // semiMajorOrientation: unavailable
    //   Altitude: AltitudeValue(-100000..800001)->20 (offset from -100000),
    //             AltitudeConfidence ENUM 16 values -> 4 bits
    bw_put_bits(&bw, 900001, 20); // 800001 ("unavailable") - (-100000) = 900001
    bw_put_bits(&bw, 15, 4);      // altitudeConfidence: unavailable(15)

    // ---- HighFrequencyContainer ---- CHOICE { basicVehicleContainerHighFrequency,
    // rsuContainerHighFrequency, ... } - EXTENSIBLE, 2 root alternatives.
    bw_put_bits(&bw, 0, 1); // CHOICE extension bit: value is in root
    bw_put_bits(&bw, 0, 1); // index: 0 = basicVehicleContainerHighFrequency (1 bit for 2 alts)

    // BasicVehicleContainerHighFrequency (SEQUENCE, NOT extensible, 7 OPTIONALs
    // accelerationControl..cenDsrcTollingZone - all absent).
    bw_put_bits(&bw, 0, 7); // 7 optional-presence bits, all absent

    // Heading: HeadingValue(0..3601)->12, HeadingConfidence(1..127)->7 (offset from 1)
    bw_put_bits(&bw, f->heading_ddeg, 12);
    bw_put_bits(&bw, 127 - 1, 7);           // headingConfidence: unavailable(127)
    // Speed: SpeedValue(0..16383)->14, SpeedConfidence(1..127)->7 (offset from 1)
    bw_put_bits(&bw, f->speed_cm_s, 14);
    bw_put_bits(&bw, 127 - 1, 7);           // speedConfidence: unavailable(127)
    // DriveDirection ENUM {forward,backward,unavailable} -> 2 bits
    bw_put_bits(&bw, 2, 2);                 // unavailable
    // VehicleLength: VehicleLengthValue(1..1023)->10 (offset from 1),
    //                VehicleLengthConfidenceIndication ENUM 5 values -> 3 bits
    bw_put_bits(&bw, (uint32_t)f->vehicle_length_dm - 1, 10);
    bw_put_bits(&bw, 4, 3);                 // vehicleLengthConfidenceIndication: unavailable(4)
    // VehicleWidth INTEGER(1..62) -> 6 bits (offset from 1)
    bw_put_bits(&bw, (uint32_t)f->vehicle_width_dm - 1, 6);
    // LongitudinalAcceleration: value(-160..161)->9 (offset from -160),
    //                           AccelerationConfidence(0..102)->7
    bw_put_bits(&bw, 161 - (uint32_t)(-160), 9); // longitudinalAccelerationValue: unavailable(161)
    bw_put_bits(&bw, 102, 7);                    // confidence: unavailable(102)
    // Curvature: CurvatureValue(-1023..1023)->11 (offset from -1023),
    //            CurvatureConfidence ENUM 8 values -> 3 bits
    bw_put_bits(&bw, 1023 - (uint32_t)(-1023), 11); // curvatureValue: unavailable(1023)
    bw_put_bits(&bw, 7, 3);                         // curvatureConfidence: unavailable(7)
    // CurvatureCalculationMode ENUM {yawRateUsed,yawRateNotUsed,unavailable} -> 2 bits
    bw_put_bits(&bw, 2, 2);                 // unavailable
    // YawRate: YawRateValue(-32766..32767)->16 (offset from -32766),
    //          YawRateConfidence ENUM 8 values -> 3 bits
    bw_put_bits(&bw, 32767 - (uint32_t)(-32766), 16); // yawRateValue: unavailable(32767)
    bw_put_bits(&bw, 7, 3);                           // yawRateConfidence: unavailable(7)

    // ---- LowFrequencyContainer ---- CHOICE { basicVehicleContainerLowFrequency,
    // ... } - EXTENSIBLE, 1 root alternative (index needs 0 bits).
    bw_put_bits(&bw, 0, 1); // CHOICE extension bit: value is in root

    // BasicVehicleContainerLowFrequency (SEQUENCE, no OPTIONALs/"...")
    //   vehicleRole VehicleRole ENUM 16 values -> 4 bits
    bw_put_bits(&bw, 0, 4);  // default(0)
    //   exteriorLights ExteriorLights BIT STRING(SIZE(8)) -> 8 bits, all off
    bw_put_bits(&bw, 0, 8);
    //   pathHistory PathHistory ::= SEQUENCE(SIZE(0..40)) OF PathPoint -> count 0..40 = 6 bits
    bw_put_bits(&bw, 0, 6);  // empty path history

    return (int)bw_byte_len(&bw);
}
