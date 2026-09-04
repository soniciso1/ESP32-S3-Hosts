# The LittleFS image is built from ../site by the littlefs_create_partition_image call,
# which the esp_littlefs component provides. The TLS cert+key are embedded straight into
# the binary as text so there is no filesystem dependency for the crypto material.
idf_component_register(
    SRCS "manuals_ap.c"
         "captive_dns.c"
         "https_srv.c"
    INCLUDE_DIRS "."
    EMBED_TXTFILES "certs/server.crt" "certs/server.key"
)

# Build the site partition image. SITE_DIR selects which tree: the full 12 MB site for
# the 16 MB board, or a stripped tree for the 8 MB board. Pass -DSITE_DIR=... at
# configure time; defaults to the full site. FLASH_IN_PROJECT writes it with the app so
# one `idf.py flash` does everything.
if(NOT DEFINED SITE_DIR)
    set(SITE_DIR "../site")
endif()
littlefs_create_partition_image(storage "${SITE_DIR}" FLASH_IN_PROJECT)

# Per-image AP SSID comes from main/ap_ssid.h, written by build-images.ps1 before each
# build. No CMake string-define escaping involved.
