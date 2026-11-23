cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# No longer need -Wl,-zmuldefs here - that was only for main/wifi_patches.c's
# symbol-override attempt (which didn't work anyway; see docs/04-transmit-setup.md),
# and that file is no longer part of the build. Superseded by main/tx_custom.c,
# which bypasses the gate at a different layer instead of trying to override it.

project(obu_firmware)
