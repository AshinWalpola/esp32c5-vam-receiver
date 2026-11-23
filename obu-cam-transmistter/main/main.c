#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "hal/modem_syscon_ll.h"   // modem_syscon_ll_enable_fe_40m_clock() - see initialize_wifi
#include "denm.h"
#include "cam.h"
#include "geonet.h"
#include "dot11p.h"
#include "tx_custom.h"

static const char *TAG = "obu-tx";

// CAM beacon: transmit a Cooperative Awareness Message every TX_INTERVAL_MS,
// unconditionally (no hazard-light gating - CAM is a continuous beacon, unlike
// the event-triggered DENM). Matches the working Rust reference
// (esp32-c_its-companion, feat/tx-cam), which beacons CAM on 5900 MHz.

// ISOLATION TEST for whether tx_custom.c is the blocker.
//   1 = transmit via the STANDARD, well-tested esp_wifi_80211_tx() using a
//       plain (non-QoS) Data frame, which that API accepts. This path is known
//       to actually key the PA. If the sniffer sees frames with this = 1 but
//       not with = 0, then tx_custom.c (its reverse-engineered driver-struct
//       offsets) is the problem, not the RF/channel/regulatory setup.
//   0 = original path: QoS Data frame via esp_wifi_80211_tx_custom().
// Non-QoS Data is non-standard for ITS-G5, but this is purely a "does any RF
// leave the chip" test - your capture-all sniffer logs it regardless.
//
// A/B TEST for the bursty-SDR symptom. Console is stable and tx_custom returns
// OK every second, but the SDR only sees sporadic bursts - the fingerprint of
// tx_custom.c's reverse-engineered driver-struct offsets not matching THIS IDF
// (v5.5.4) as opposed to the reference's bundled IDF. Setting this to 1 routes
// TX through the official, well-tested esp_wifi_80211_tx() (non-QoS Data), which
// uses NO reverse-engineered structs. If the SDR becomes a steady 1 Hz with
// this = 1, tx_custom's struct layout is confirmed as the culprit.
#define USE_STANDARD_TX  1

// Target frequency: 5900 MHz (ITS-G5 G5-CCH, channel 180). This is what the
// working Rust reference transmits on, proving the C5 PA reaches it despite the
// 5885 datasheet max. The reference sets band-mode 5G, then phy_11p_set +
// phy_change_channel(5900) directly - it does NOT call esp_wifi_set_channel at
// all, so we don't either (channel 180 isn't a normal Wi-Fi channel anyway).
#define TX_FREQ_MHZ  5900
// ----------------------------------------------------------------------------

// ---- CAM beacon profile ----
#define STATION_ID     0x0BADC0DE // placeholder 32-bit station id - pick your own
#define STATION_TYPE            5 // passengerCar (TS 102 894-2 StationType)
#define VEHICLE_LENGTH_DM      40 // VehicleLengthValue, 10cm steps (4.0 m)
#define VEHICLE_WIDTH_DM       18 // VehicleWidth, 10cm steps (1.8 m)
#define BTP_PORT_CAM         2001 // BTP-B destination port for CAM (ETSI TS 103 248)
#define TX_INTERVAL_MS       1000 // CAM beacon period (1 Hz; ITS allows 1-10 Hz)

// Bench location, hardcoded since there's no GNSS module wired in yet and
// the unit is genuinely stationary here: 53°33'16.8"N 10°01'20.6"E, in
// 1/10-microdegree units (decimal_degrees * 10,000,000). Replace with real
// GNSS output once you have a fix source; until then this beats 0/0
// ("Null Island"), which is an obvious placeholder-tell on any map.
#define BENCH_LATITUDE_TENMICRODEG    535546667
#define BENCH_LONGITUDE_TENMICRODEG   100223889

// Single source of truth for the pseudonym/link-layer address: used both as
// the 802.11 source MAC (Addr2) and as GN_ADDR's MID field, since the GN
// spec defines those as being the same address. Locally-administered bit
// set (0x02) per normal MAC convention. Fixed/non-rotating for now - real
// stacks rotate this every 5-15 min for privacy.
static const uint8_t pseudonym_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};

// Undocumented libphy.a calls that push the radio into 802.11p OCB mode on
// the 5.9 GHz ITS-G5 band. See docs/04-transmit-setup.md for source + what
// to do if the linker can't find these symbols in your ESP-IDF version.
extern void phy_11p_set(int enable, int unused);
extern void phy_change_channel(int freq_mhz, int bw_mode, int sec_chan_offset, int unused);

static void send_cam(void)
{
    // GenerationDeltaTime is TimestampIts mod 65536 (ms). No RTC/GNSS time here,
    // so use a free-running ms counter that advances one beacon-interval per
    // send. It wraps at 65536, which is exactly the field's defined behaviour.
    static uint16_t gen_delta = 0;

    uint8_t frame[300];
    cam_fields_t fields = {
        .station_id = STATION_ID,
        .station_type = STATION_TYPE,
        .generation_delta_time = gen_delta,
        .latitude_tenmicrodeg = BENCH_LATITUDE_TENMICRODEG,
        .longitude_tenmicrodeg = BENCH_LONGITUDE_TENMICRODEG,
        .speed_cm_s = 0,        // stationary
        .heading_ddeg = 3601,   // HeadingValue unavailable (no heading source)
        .vehicle_length_dm = VEHICLE_LENGTH_DM,
        .vehicle_width_dm = VEHICLE_WIDTH_DM,
    };
    gen_delta += TX_INTERVAL_MS;

    uint8_t cam_payload[96];
    int cam_len = cam_encode(&fields, cam_payload, sizeof(cam_payload));

    uint8_t gn_payload[160];
    int gn_len = geonet_wrap_shb(cam_payload, cam_len, pseudonym_mac, STATION_TYPE,
                                 BENCH_LATITUDE_TENMICRODEG, BENCH_LONGITUDE_TENMICRODEG,
                                 BTP_PORT_CAM, gn_payload, sizeof(gn_payload));

    // qos=false for the standard-TX path (esp_wifi_80211_tx accepts only non-QoS
    // Data - which is exactly what the Rust reference transmits); qos=true would
    // be a real ITS-G5 QoS Data frame for the tx_custom path.
    int frame_len = dot11p_build_frame(gn_payload, gn_len, pseudonym_mac, frame, sizeof(frame),
                                       USE_STANDARD_TX ? false : true);

    // PHY/OCB/channel is configured ONCE at boot in app_main and left alone,
    // matching the working Rust reference (band-mode 5G + phy_11p_set +
    // phy_change_channel(5900), set once).

    if (frame_len > 0) {
#if USE_STANDARD_TX
        // Standard, well-tested raw-TX API with a non-QoS Data frame - the same
        // transmit path the Rust reference uses (esp-radio send_raw_frame wraps
        // esp_wifi_80211_tx). err 258 ("unsupport QoS frame type") would mean the
        // frame wasn't built as non-QoS.
        esp_err_t err = esp_wifi_80211_tx(WIFI_IF_STA, frame, frame_len, true);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "esp_wifi_80211_tx (standard) failed: %d", err);
        } else {
            ESP_LOGI(TAG, "CAM sent via STANDARD tx (%d bytes) @ %d MHz genDeltaT=%u", frame_len, TX_FREQ_MHZ, gen_delta);
        }
#else
        // tx_custom path: submits to the driver's internal HMAC TX path,
        // bypassing the QoS-frame gate. 11A legacy OFDM, 12M rate.
        wifi_tx_rate_config_t tx_rate_cfg = {
            .phymode = WIFI_PHY_MODE_11A,
            .rate = WIFI_PHY_RATE_12M,
            .ersu = false,
            .dcm = false,
        };
        esp_err_t err = esp_wifi_80211_tx_custom(WIFI_IF_STA, frame, frame_len, true,
                                                  &tx_rate_cfg, WIFI_BAND_5G, WIFI_BW20);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "esp_wifi_80211_tx_custom failed: %d", err);
        } else {
            ESP_LOGI(TAG, "CAM sent (%d bytes) @ %d MHz genDeltaT=%u", frame_len, TX_FREQ_MHZ, gen_delta);
        }
#endif
    } else {
        ESP_LOGE(TAG, "CAM frame build failed (cam_len=%d gn_len=%d)", cam_len, gn_len);
    }
}

static void tx_task(void *arg)
{
    while (1) {
        // CAM is a continuous beacon - send every interval, unconditionally.
        send_cam();
        vTaskDelay(pdMS_TO_TICKS(TX_INTERVAL_MS));
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Enable the modem FRONT-END 40 MHz clock BEFORE esp_wifi_init(). This is
    // the one step the proven-working receiver firmware
    // (its-g5-receiver-firmware_txenabled, main/main.c -> initialize_wifi())
    // performs that this OBU was missing. Without the FE clock enabled the
    // 5 GHz front-end / transmit chain is not fully clocked - which matches the
    // exact symptom here: the radio calibrates (boot RF ping) and receives
    // fine, but data frames are accepted by the API and never actually key the
    // PA. This is a low-level modem_syscon register write via the HAL LL layer,
    // copied verbatim from the reference firmware.
    modem_syscon_ll_enable_fe_40m_clock(&MODEM_SYSCON, 1);

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM)); // match reference initialize_wifi()
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // ---- Regulatory / TX-authorization override -----------------------------
    // THE fix for "RX works but TX is silent". By default the driver uses
    // WIFI_COUNTRY_POLICY_AUTO, whose 5 GHz regulatory table does NOT authorize
    // transmit on the 5.9 GHz ITS band (and treats DFS channels as no-IR /
    // radar-gated). Receiving is never gated - which is exactly why the sniffer
    // hears traffic but our own frames never key the PA, and why the only RF
    // seen from this board is the uninhibited PHY-calibration burst at boot.
    //
    // Switching to WIFI_COUNTRY_POLICY_MANUAL with an explicit 5 GHz channel
    // mask (wifi_5g_channel_mask, which only takes effect under manual policy)
    // tells the driver these channels are permitted and lifts the transmit
    // gate. WIFI_CHANNEL_177 (BIT(28)) = 5885 MHz; we enable the full 5 GHz set
    // (bits 1..28) so both the primer channel and the target are authorized.
    // Manual policy = the operator asserts regulatory responsibility, which is
    // appropriate for licensed/university research on the ITS band.
    wifi_country_t ctry = {
        .cc = "US",                            // nominal under manual policy
        .schan = 1,
        .nchan = 11,
        .policy = WIFI_COUNTRY_POLICY_MANUAL,
        .wifi_5g_channel_mask = 0x1FFFFFFE,    // all 5 GHz channels, bits 1..28 (incl. 140 and 177)
    };
    esp_err_t ctry_err = esp_wifi_set_country(&ctry);
    if (ctry_err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_set_country(MANUAL) failed: %d (continuing)", ctry_err);
    }
    // Ensure the PA runs at full configured power (not a reduced regulatory
    // default). Units are 0.25 dBm; 80 = 20 dBm.
    esp_wifi_set_max_tx_power(80);
    // -------------------------------------------------------------------------

    // Force the dual-band C5 onto its 5 GHz PHY. This MUST be called after
    // esp_wifi_start() - calling it before returns ESP_ERR_WIFI_NOT_STARTED
    // (0x3002 / 12290). Locking the band to 5G explicitly keeps the driver
    // from ever falling back to 2.4 GHz ch1 (the old "stuck at primary=1"
    // symptom), which would key the wrong PHY and make us inaudible to a
    // 5.9 GHz sniffer. Valid 5 GHz channels on the C5 are 36..177. Not
    // ESP_ERROR_CHECK'd: log and continue if a given IDF build differs.
    esp_err_t band_err = esp_wifi_set_band_mode(WIFI_BAND_MODE_5G_ONLY);
    if (band_err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_set_band_mode(5G_ONLY) failed: %d (continuing)", band_err);
    }

    // Disable Wi-Fi power save. An unassociated STA with the default
    // WIFI_PS_MIN_MODEM power save sleeps its radio between beacons it will
    // never receive (we're not joined to any AP), and drops outbound raw
    // frames while asleep - the classic "esp_wifi_80211_tx returns OK but
    // nothing goes on air". Must be called after esp_wifi_start().
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    // Enable promiscuous mode. This is the single most important change: our
    // *receiver* firmware (V2X2MAP) - which demonstrably works at 5.9 GHz,
    // 13k+ frames captured - runs promiscuous, and ESP-IDF documents that the
    // raw-frame TX path only actually emits when the MAC is in promiscuous
    // mode or associated to an AP. Plain STA (what this firmware used before)
    // is neither, so frames were being accepted by the API and then dropped
    // by the driver. Putting the OBU in the same radio state as the working
    // sniffer, then injecting, is the whole fix. Must be after start.
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));

    // Force 802.11p OCB mode on the ITS-G5 channel, exactly like the working
    // Rust reference (esp32-c_its-companion, src/radio.rs setup_wifi_sniffer):
    // enable 802.11p, then jump straight to the target frequency. With band-mode
    // already locked to 5 GHz above, NO esp_wifi_set_channel priming is needed -
    // the reference doesn't call it, and channel 180 (5900 MHz) isn't a normal
    // Wi-Fi channel anyway. phy_change_channel takes the frequency in MHz.
    ESP_LOGI(TAG, "about to call phy_11p_set...");
    phy_11p_set(1, 0);
    ESP_LOGI(TAG, "phy_11p_set returned, about to call phy_change_channel(%d)...", TX_FREQ_MHZ);
    phy_change_channel(TX_FREQ_MHZ, 1, 0, 0);
    ESP_LOGI(TAG, "phy_change_channel returned");

    ESP_LOGW(TAG, "OCB @ %d MHz - CAM beacon armed, transmitting every %d ms",
             TX_FREQ_MHZ, TX_INTERVAL_MS);

    xTaskCreate(tx_task, "tx_task", 4096, NULL, 5, NULL);
}
