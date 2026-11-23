# wifi_patches.c is intentionally NOT in this list anymore - superseded by
# tx_custom.c (see that file for why). Left on disk, unused, for history.
idf_component_register(
    SRCS "main.c" "denm.c" "cam.c" "geonet.c" "dot11p.c" "tx_custom.c"
    INCLUDE_DIRS "."
    REQUIRES esp_event esp_netif nvs_flash driver esp_phy
    PRIV_REQUIRES esp_wifi
)
