#ifndef CAM_H
#define CAM_H
#include <stdint.h>
#include <stddef.h>

// Minimal CAM (Cooperative Awareness Message) per ETSI EN 302 637-2 v1.4.1
// (CAM-PDU-Descriptions) + TS 102 894-2 v1.3.1 (CDD / ITS-Container), matching
// the field set the working Rust reference (esp32-c_its-companion, feat/tx-cam,
// src/applogic/cam_tx.rs) transmits:
//   - ItsPduHeader (protocolVersion 2, messageID 2 = cam)
//   - CoopAwareness { generationDeltaTime, camParameters }
//   - CamParameters {
//         basicContainer { stationType, referencePosition },
//         highFrequencyContainer = basicVehicleContainerHighFrequency { ... },
//         lowFrequencyContainer  = basicVehicleContainerLowFrequency  { ... }
//     }
// All vehicle-dynamics fields we don't measure are encoded as their ASN.1
// "unavailable" value. Speed is a real 0 (correct for a stationary station).

typedef struct {
    uint32_t station_id;
    uint8_t  station_type;            // StationType(0..255): 5 = passengerCar
    uint16_t generation_delta_time;   // TimestampIts mod 65536 (ms); 0 until a real clock is wired
    int32_t  latitude_tenmicrodeg;    // Latitude, 1/10 microdegree
    int32_t  longitude_tenmicrodeg;   // Longitude, 1/10 microdegree
    uint16_t speed_cm_s;              // SpeedValue, 0.01 m/s units (0 = stationary)
    uint16_t heading_ddeg;            // HeadingValue, 0.1 deg units (0..3600), 3601 = unavailable
    uint16_t vehicle_length_dm;       // VehicleLengthValue(1..1023), 10cm steps
    uint8_t  vehicle_width_dm;        // VehicleWidth(1..62), 10cm steps
} cam_fields_t;

// Encodes the CAM as ASN.1 UPER. Returns bytes written, or -1 if buf too small.
int cam_encode(const cam_fields_t *f, uint8_t *buf, size_t buf_len);

#endif
