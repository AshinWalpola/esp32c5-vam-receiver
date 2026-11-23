#include "denm.h"
#include <string.h>

// Minimal MSB-first bit packer - ASN.1 UPER is a bitstream, not a byte
// stream, so we can't just memcpy structs.
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
            return; // overflow guard - silently truncates, check return value of denm_encode
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

int denm_encode(const denm_fields_t *f, uint8_t *buf, size_t buf_len)
{
    bitwriter_t bw;
    bw_init(&bw, buf, buf_len);

    // ---- ItsPduHeader ---- (SEQUENCE, no OPTIONALs, no "..." -> no preamble at all)
    bw_put_bits(&bw, 2, 8);              // protocolVersion INTEGER(0..255) = 2
    bw_put_bits(&bw, 1, 8);              // messageID INTEGER(0..255) = denm(1)
    bw_put_bits(&bw, f->station_id, 32); // stationID = StationID INTEGER(0..4294967295) = 32 bits

    // ---- DenmPayload (DecentralizedEnvironmentalNotificationMessage) ----
    // No "..." on this SEQUENCE -> no extension bit, just the 3-bit
    // optional-component preamble in declared order: situation, location,
    // alacarte. "No additional parameters" means location/alacarte stay
    // absent.
    bw_put_bits(&bw, 1, 1); // situation present
    bw_put_bits(&bw, 0, 1); // location absent
    bw_put_bits(&bw, 0, 1); // alacarte absent

    // ---- ManagementContainer ----
    // This SEQUENCE ends in "..." in the real ASN.1 module -> extensible,
    // so it needs a leading 1-bit extension flag (0 = no extension
    // additions used) BEFORE the 5-bit optional/default preamble
    // (termination, relevanceDistance, relevanceTrafficDirection,
    // validityDuration, transmissionInterval, in that declared order). An
    // earlier version of this code omitted the extension bit entirely,
    // which would shift every single bit after it and corrupt the whole
    // rest of the message for any spec-compliant decoder.
    bw_put_bits(&bw, 0, 1); // ManagementContainer extension bit: none used
    bw_put_bits(&bw, f->terminate ? 1 : 0, 1); // termination present only when cancelling
    bw_put_bits(&bw, 0, 1); // relevanceDistance absent
    bw_put_bits(&bw, 0, 1); // relevanceTrafficDirection absent
    bw_put_bits(&bw, 0, 1); // validityDuration absent -> default 600s applies
    bw_put_bits(&bw, 0, 1); // transmissionInterval absent

    // actionID = ActionID{ originatingStationID StationID(32), sequenceNumber
    // SequenceNumber(0..65535, 16 bits) } - no OPTIONALs/"..." -> no preamble.
    // Keep sequenceNumber constant across repeats of the SAME event - it's
    // the caller's job (see main.c) to only bump it on a genuinely new event
    // and reuse it for that event's eventual termination message.
    bw_put_bits(&bw, f->station_id, 32);
    bw_put_bits(&bw, f->sequence_number, 16);

    // detectionTime / referenceTime: TimestampIts INTEGER(0..4398046511103)
    // = exactly 42 bits (2^42), ms since 2004-01-01T00:00:00Z. NOT WIRED UP
    // YET - there's no RTC/NTP sync in this skeleton, so this is 0 (decodes
    // as 2004-01-01). Wire in SNTP or a GNSS UTC fix before this is real.
    bw_put_bits(&bw, 0, 42);
    bw_put_bits(&bw, 0, 42);

    // termination VALUE - only emitted when present (per the preamble bit
    // above - UPER never encodes a value for an absent optional component).
    // Termination ::= ENUMERATED{isCancellation(0), isNegation(1)}, no
    // "...", 2 values -> 1 bit.
    if (f->terminate) {
        bw_put_bits(&bw, 0, 1); // isCancellation
    }

    // eventPosition (ReferencePosition ::= SEQUENCE{latitude, longitude,
    // positionConfidenceEllipse, altitude} - no OPTIONALs/"..." -> no
    // preamble, straight concatenation). Widths below are each field's
    // exact constrained-INTEGER range size from ITS-Container.asn, encoded
    // as an unsigned offset from the type's declared minimum - NOT assumed
    // to match neighboring fields (latitude and longitude are different
    // widths, which is easy to miss).
    //   Latitude ::= INTEGER(-900000000..900000001) -> range 1800000002 -> 31 bits
    uint32_t lat_offset = (uint32_t)(f->latitude_tenmicrodeg - (-900000000));
    bw_put_bits(&bw, lat_offset, 31);
    //   Longitude ::= INTEGER(-1800000000..1800000001) -> range 3600000002 -> 32 bits
    uint32_t lon_offset = (uint32_t)(f->longitude_tenmicrodeg - (-1800000000));
    bw_put_bits(&bw, lon_offset, 32);
    //   PosConfidenceEllipse ::= SEQUENCE{semiMajorConfidence, semiMinorConfidence,
    //   semiMajorOrientation} - no preamble.
    //     SemiAxisLength ::= INTEGER(0..4095) -> 12 bits (not 16 - this was wrong before)
    bw_put_bits(&bw, 4095, 12); // semiMajorConfidence: unavailable
    bw_put_bits(&bw, 4095, 12); // semiMinorConfidence: unavailable
    //     HeadingValue ::= INTEGER(0..3601) -> 12 bits (not 16 - this was wrong before)
    bw_put_bits(&bw, 3601, 12); // semiMajorOrientation: unavailable
    //   Altitude ::= SEQUENCE{altitudeValue, altitudeConfidence} - no preamble.
    //     AltitudeValue ::= INTEGER(-100000..800001) -> range 900002 -> 20 bits
    //     (not 24 - this was wrong before), offset-encoded from -100000.
    bw_put_bits(&bw, 900001, 20); // 800001 ("unavailable") - (-100000) = 900001
    //     AltitudeConfidence ::= ENUMERATED, 16 named values, no "..." -> 4 bits
    bw_put_bits(&bw, 15, 4); // unavailable

    // stationType: StationType INTEGER(0..255) -> 8 bits fixed regardless of
    // how sparse the named values are.
    bw_put_bits(&bw, f->station_type, 8);

    // ---- SituationContainer ----
    // This SEQUENCE also ends in "..." -> its own 1-bit extension flag,
    // THEN the 2-bit preamble (linkedCause, eventHistory), THEN the
    // mandatory field values. An earlier version of this code put the
    // linkedCause/eventHistory bits at the END instead of the start, and
    // had no extension bit at all - both are structural bugs that would
    // desync any spec-compliant decoder from this point on.
    bw_put_bits(&bw, 0, 1); // SituationContainer extension bit: none used
    bw_put_bits(&bw, 0, 1); // linkedCause absent
    bw_put_bits(&bw, 0, 1); // eventHistory absent

    // informationQuality: InformationQuality INTEGER(0..7) -> 3 bits
    bw_put_bits(&bw, 1, 3); // low quality - no real sensor input, just the hazard-light GPIO

    // eventType: CauseCode ::= SEQUENCE{causeCode, subCauseCode, ...} - this
    // inner SEQUENCE is ALSO extensible ("..."), so it gets its own leading
    // extension bit before its two mandatory fields.
    bw_put_bits(&bw, 0, 1);                  // CauseCode extension bit: none used
    bw_put_bits(&bw, f->cause_code, 8);      // CauseCodeType INTEGER(0..255) -> 8 bits
    bw_put_bits(&bw, f->sub_cause_code, 8);  // SubCauseCodeType INTEGER(0..255) -> 8 bits

    // linkedCause / eventHistory: both absent, already signalled in the
    // preamble above - UPER writes no value bits for them.

    return (int)bw_byte_len(&bw);
}
