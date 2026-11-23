#ifndef DENM_H
#define DENM_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Full CauseCodeType enumeration, straight from the authoritative source:
// ETSI TS 102 894-2 (CDD) ITS-Container.asn, CauseCodeType definition.
// (Values 1/2/3/14/26/27/91/94/95/97 were already cross-checked earlier
// against a real captured DENM; the rest are now confirmed the same way,
// from the actual ASN.1 module rather than guessed.)
#define DENM_CAUSE_RESERVED                              0
#define DENM_CAUSE_TRAFFIC_CONDITION                      1
#define DENM_CAUSE_ACCIDENT                               2
#define DENM_CAUSE_ROADWORKS                              3
#define DENM_CAUSE_IMPASSABILITY                          5
#define DENM_CAUSE_ADVERSE_WEATHER_ADHESION               6
#define DENM_CAUSE_AQUAPLANNING                           7
#define DENM_CAUSE_HAZARDOUS_LOCATION_SURFACE_CONDITION   9
#define DENM_CAUSE_HAZARDOUS_LOCATION_OBSTACLE_ON_ROAD   10
#define DENM_CAUSE_HAZARDOUS_LOCATION_ANIMAL_ON_ROAD     11
#define DENM_CAUSE_HUMAN_PRESENCE_ON_ROAD                12
#define DENM_CAUSE_WRONG_WAY_DRIVING                     14
#define DENM_CAUSE_RESCUE_AND_RECOVERY_WORK_IN_PROGRESS  15
#define DENM_CAUSE_ADVERSE_WEATHER_EXTREME               17
#define DENM_CAUSE_ADVERSE_WEATHER_VISIBILITY            18
#define DENM_CAUSE_ADVERSE_WEATHER_PRECIPITATION         19
#define DENM_CAUSE_SLOW_VEHICLE                          26
#define DENM_CAUSE_DANGEROUS_END_OF_QUEUE                27
#define DENM_CAUSE_VEHICLE_BREAKDOWN                     91
#define DENM_CAUSE_POST_CRASH                            92
#define DENM_CAUSE_HUMAN_PROBLEM                         93
#define DENM_CAUSE_STATIONARY_VEHICLE                    94
#define DENM_CAUSE_EMERGENCY_VEHICLE_APPROACHING         95
#define DENM_CAUSE_HAZARDOUS_LOCATION_DANGEROUS_CURVE    96
#define DENM_CAUSE_COLLISION_RISK                        97
#define DENM_CAUSE_SIGNAL_VIOLATION                      98
#define DENM_CAUSE_DANGEROUS_SITUATION                   99

typedef struct {
    uint32_t station_id;
    uint16_t sequence_number; // keep constant across repeats of the SAME event; only bump on a genuinely new event
    uint8_t  cause_code;      // e.g. 94 = stationaryVehicle
    uint8_t  sub_cause_code;  // 0 = unspecified
    uint8_t  station_type;    // StationType, e.g. 5 = passengerCar - match geonet_wrap_shb's station_type param
    int32_t  latitude_tenmicrodeg;   // 1/10 microdegree; 0 = placeholder/unavailable
    int32_t  longitude_tenmicrodeg;  // 1/10 microdegree; 0 = placeholder/unavailable
    bool     terminate; // true = encode this as a Termination(isCancellation) message instead of a normal update
} denm_fields_t;

// Encodes a minimal DENM (ItsPduHeader + ManagementContainer +
// SituationContainer only - no location/alacarte containers) as ASN.1 UPER,
// per the actual ETSI EN 302 637-3 / TS 102 894-2 ASN.1 modules (fetched
// from forge.etsi.org, not reconstructed from memory). Returns bytes
// written, or -1 if buf too small.
//
// Two things worth knowing if you're reading this against the modules
// yourself: ManagementContainer, SituationContainer, and CauseCode are all
// declared with a trailing "..." (extensible), which means each needs its
// own leading extension bit in the UPER encoding - easy to miss, and this
// code got it wrong in an earlier version. Field bit-widths below (e.g.
// latitude=31 bits, longitude=32 bits, position-confidence fields=12 bits,
// altitudeValue=20 bits) are derived directly from each type's declared
// INTEGER constraint range, not assumed to match neighboring fields.
int denm_encode(const denm_fields_t *f, uint8_t *buf, size_t buf_len);

#endif
