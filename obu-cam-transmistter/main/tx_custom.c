// Copied verbatim (no logic changes) from opentrafficmap/its-g5-receiver-firmware_txenabled,
// main/tx_custom.c (https://codeberg.org/opentrafficmap/its-g5-receiver-firmware_txenabled),
// same authors as the receiver firmware (V2X2MAP) already used on the RX side of this
// project. Same chip (ESP32-C5), same class of problem (getting a raw 802.11 frame past
// esp_wifi_80211_tx()'s built-in frame-type gate), and a proven-different approach from our
// own abandoned main/wifi_patches.c attempt - see docs/04-transmit-setup.md for why that one
// didn't work and why this one is expected to.
//
// WHAT THIS DOES DIFFERENTLY FROM esp_wifi_80211_tx(): it doesn't call the public API at all.
// It reaches one layer deeper into the closed WiFi driver - ic_ebuf_alloc() (allocates an
// internal driver buffer), ieee80211_post_hmac_tx() (submits that buffer straight to the MAC
// for transmission) - and never goes through the code path that contains the QoS-frame-type
// sanity check that was rejecting us. Notice line "esp_err_t result = 0;//ieee80211_raw_frame_
// sanity_check(...)" below: the upstream authors don't override that check (like our old
// wifi_patches.c tried to), they just never call the function that calls it.
//
// REAL RISK, carried over from upstream, not introduced by us: this skips ALL frame-type and
// sanity validation, same caveat as our old override attempt. A malformed frame from a bug
// elsewhere in our own code could behave worse (silent corruption, crash) than a clean
// rejection.
//
// UNVERIFIED FOR OUR EXACT TOOLCHAIN - things worth checking before trusting this blindly:
//   1. The symbols this depends on (ieee80211_post_hmac_tx, ic_ebuf_alloc, ic_get_default_sched,
//      g_osi_funcs_p, g_wifi_global_lock) are undocumented/internal. We confirmed via `nm`
//      earlier that ieee80211_raw_frame_sanity_check exists in OUR esp32c5/IDF libnet80211.a -
//      we have NOT yet independently confirmed these other four/five symbols exist in our
//      exact ESP-IDF version (as opposed to whatever version the upstream repo's pinned
//      esp-idf submodule uses). If the linker can't find one of these, that's the first thing
//      to check - see docs/04-transmit-setup.md for the nm command.
//   2. x_eb_txdesc_t / x_middle_data_t / x_ebuf_t below are REVERSE-ENGINEERED struct layouts
//      of closed-source internal WiFi driver types, pinned only by a sizeof() static_assert -
//      that assert catches a total-size mismatch but NOT a field-order/semantic mismatch if a
//      different IDF version shuffled internal fields while keeping the same total size. If our
//      ESP-IDF version differs meaningfully from upstream's, this could compile and link fine
//      but write to the wrong offsets internally. Worth checking `idf.py --version` against
//      whatever esp-idf commit opentrafficmap's repo has pinned as a submodule, as a rough
//      compatibility signal (not a guarantee either way).
#include "esp_private/wifi_os_adapter.h"
#include "esp_wifi.h"

#include "tx_custom.h"

esp_err_t ieee80211_raw_frame_sanity_check(wifi_interface_t ifx, const void *buffer, int32_t len, bool en_sys_seq);
esp_err_t ieee80211_post_hmac_tx(void *ebuf);
void *ic_ebuf_alloc(const void *packet, uint32_t unknown, uint32_t len);
void *ic_get_default_sched(void);

extern wifi_osi_funcs_t *g_osi_funcs_p;
extern void *g_wifi_global_lock;

typedef struct x_eb_txdesc
{
    uint32_t flags;
    uint32_t field_4;
    uint32_t field_8;
    uint8_t rate;
    uint8_t field_d;
    uint8_t field_e;
    uint8_t field_f;
    uint32_t field_10;
    uint32_t field_14;
    uint32_t timestamp;
    void* sched;
    uint32_t field_20;
    uint32_t field_24;
    uint32_t field_28;
    union {
        uint32_t field_2c_32;
        struct {
            uint8_t field_2c;
            uint8_t field_2d;
            uint8_t field_2e;
            uint8_t field_2f;
        };
    };
    union {
        uint32_t field_30_32;
        struct {
            uint8_t field_30;
            uint8_t field_31;
            uint8_t field_32;
            uint8_t field_33;
        };
    };
    uint32_t field_34;
    uint32_t field_38;
    uint32_t field_3c;
    uint32_t field_40;
    uint32_t field_44;
} x_eb_txdesc_t;
static_assert(sizeof(x_eb_txdesc_t) == 0x48);

typedef struct x_middle_data
{
    uint32_t field_40;
    uint8_t* buf;
    uint32_t field_48;
    uint32_t field_4c;
} x_middle_data_t;
static_assert(sizeof(x_middle_data_t) == 0x10);

typedef struct x_ebuf
{
    uint32_t field_0;
    x_middle_data_t* ds_head;
    x_middle_data_t* ds_tail;
    uint16_t field_c;
    uint16_t field_e;
    uint32_t extra_data_start;
    uint16_t header_length;
    uint32_t data_length;
    uint16_t field_1c;
    uint8_t alloc_type;
    uint8_t field_1f;
    uint32_t field_20;
    uint8_t field_24;
    uint8_t field_25;
    uint8_t field_26;
    uint8_t field_27;
    uint32_t field_28;
    uint8_t field_2c;
    uint32_t field_30;
    uint32_t next_free;
    x_eb_txdesc_t* txdesc;
    uint16_t field_3c;
    uint8_t field_3e;
    uint8_t field_3f;
} x_ebuf_t;
static_assert(sizeof(x_ebuf_t) == 0x40);

esp_err_t esp_wifi_80211_tx_custom(wifi_interface_t ifx, const void *buffer, int32_t len, bool en_sys_seq, wifi_tx_rate_config_t *tx_rate_config, wifi_band_t band, wifi_bandwidth_t bw)
{
    esp_err_t result = 0;//ieee80211_raw_frame_sanity_check(ifx, buffer, len, en_sys_seq);

    if (!result)
    {
        g_osi_funcs_p->_mutex_lock(g_wifi_global_lock);
        x_ebuf_t* eb = ic_ebuf_alloc(buffer, 1, len);

        if (eb)
        {
            //eb->data_length = len - 0x1a;
            eb->data_length = 0;
            x_eb_txdesc_t *txdesc_1 = eb->txdesc;
            //eb->header_length = 0x1a;
            eb->header_length = len;
            txdesc_1->flags |= 0x4000;
            txdesc_1->sched = ic_get_default_sched();
            wifi_phy_rate_t rate = tx_rate_config->rate;
            x_eb_txdesc_t *txdesc = eb->txdesc;

            if (rate)
                txdesc->rate = (char)rate;
            else if (band != WIFI_BAND_5G)
                txdesc->rate = 0;
            else
                txdesc->rate = (char)WIFI_PHY_RATE_6M;

            wifi_phy_mode_t phymode = tx_rate_config->phymode;

            if (phymode == WIFI_PHY_MODE_HE20)
            {
                txdesc->flags |= 0x80000000;
                txdesc->field_2f =
                    (char)((((uint32_t)tx_rate_config->ersu + 6) & 0xf) << 3)
                    | (txdesc->field_2f & 0x87);

                if ((uint32_t)tx_rate_config->dcm)
                    txdesc->field_31 |= 0x80;
            }
            else if (phymode == WIFI_PHY_MODE_VHT20)
                txdesc->flags |= 0x1000000;

            // No idea if this is correct, but this is what the original code does...
            uint32_t bw_is_bw40 = bw == WIFI_BW40;
            txdesc->field_8 = (bw_is_bw40 << 0xf) | (txdesc->field_8 & 0xffff7fff);

            if (en_sys_seq)
                txdesc->flags |= 1;

            txdesc->field_10 =
                (txdesc->field_10 & 0xfff3ffff) | ((ifx & WIFI_IF_MAX) << 0x12);
            txdesc->field_14 = 0x100;

            ieee80211_post_hmac_tx(eb);
            g_osi_funcs_p->_mutex_unlock(g_wifi_global_lock);
        }
        else
        {
            result = ESP_ERR_NO_MEM;
            g_osi_funcs_p->_mutex_unlock(g_wifi_global_lock);
        }
    }

    return result;
}
