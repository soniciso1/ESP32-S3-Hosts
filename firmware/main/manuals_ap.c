/* manuals-ap : a standalone Wi-Fi AP that a PS5 joins, which then answers DNS for
 * every name with its own address and serves the poopicker site over HTTP and HTTPS.
 *
 * The flow it recreates:
 *   1. PS5 joins this AP. Our DHCP server (built into esp_netif's SoftAP) hands out a
 *      lease whose DNS server is US.
 *   2. PS5 opens Manuals -> resolves manuals.playstation.net. Our captive DNS answers
 *      every A query with the AP IP, so that name points here.
 *   3. PS5 fetches https://manuals.playstation.net/document/en/ps5/ . Our HTTPS server
 *      serves index.html from the LittleFS image (the poopicker site). The console
 *      accepts our self-signed cert (proven against the live host).
 *
 * Board differences (ESP32 4MB no-PSRAM vs ESP32-S3 8/16MB PSRAM) are handled entirely
 * by sdkconfig / the partition table, not here. This file is identical on all four.
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_littlefs.h"
#include "esp_netif_ip_addr.h"

#include "manuals_ap.h"

static const char *TAG = "manuals-ap";

/* The AP address. esp_netif's SoftAP defaults to 192.168.4.1 with a DHCP pool behind
 * it; we keep that and hand it out as the DNS server too. Anything the console resolves
 * therefore lands back here. */
esp_ip4_addr_t g_ap_ip;

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *e = (wifi_event_ap_staconnected_t *)data;
        ESP_LOGI(TAG, "PS5 joined: " MACSTR " (aid=%d)", MAC2STR(e->mac), e->aid);
    } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *e = (wifi_event_ap_stadisconnected_t *)data;
        ESP_LOGI(TAG, "PS5 left: " MACSTR, MAC2STR(e->mac));
    }
}

/* This is the actual "DHCP worked" signal - it only fires once a client has been
 * handed a real lease, unlike WIFI_EVENT_AP_STACONNECTED (association only, no IP
 * yet). If a client associates but this never fires for it, DHCP is broken. */
static void ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == IP_EVENT_AP_STAIPASSIGNED) {
        ip_event_ap_staipassigned_t *e = (ip_event_ap_staipassigned_t *)data;
        ESP_LOGI(TAG, "DHCP lease handed out: " IPSTR " to " MACSTR,
                 IP2STR(&e->ip), MAC2STR(e->mac));
    }
}

static void start_ap(void)
{
    esp_netif_t *ap = esp_netif_create_default_wifi_ap();
    esp_netif_ip_info_t ip;
    esp_netif_get_ip_info(ap, &ip);
    g_ap_ip = ip.ip;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.ap.ssid, AP_SSID, sizeof(wc.ap.ssid));
    wc.ap.ssid_len = strlen(AP_SSID);
    wc.ap.channel = AP_CHANNEL;
    wc.ap.max_connection = AP_MAX_CONN;
    if (strlen(AP_PASS) == 0) {
        wc.ap.authmode = WIFI_AUTH_OPEN;
    } else {
        wc.ap.authmode = WIFI_AUTH_WPA2_PSK;
        strlcpy((char *)wc.ap.password, AP_PASS, sizeof(wc.ap.password));
    }
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wc));

    /* esp_wifi_start() first - it fires WIFI_EVENT_AP_START, whose default handler
     * starts the DHCP server. THEN reconfigure DHCP with the exact stop -> option ->
     * start sequence the official ESP-IDF captive-portal example uses
     * (examples/protocols/http_server/captive_portal/main/main.c): the option only
     * takes effect across a stop/start cycle, and that cycle must come AFTER
     * esp_wifi_start() so it operates on the server the event handler already brought
     * up. My earlier attempt set the option WITHOUT the surrounding stop/start, which
     * left the server half-configured and never answering DHCPDISCOVER. */
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Disable WiFi modem power-save. In AP mode the default power-save makes the radio
     * nap between beacons; a station (the PS5) that transmits during a nap can get no
     * ack and drop the link - exactly the "WiFi connection lost / super flaky" symptom.
     * WIFI_PS_NONE keeps the radio always-on for a rock-steady AP (it costs power, which
     * is fine for a USB-fed board serving one console). */
    esp_wifi_set_ps(WIFI_PS_NONE);

    /* Cut TX power to ~10 dBm (units are 0.25 dBm; 40 = 10 dBm, default is 80 = 20 dBm).
     * The board was BROWNING OUT under WiFi-TX current peaks - a serial capture caught
     * rst:0x1 (POWERON) mid-session, i.e. the chip reset from power loss, which drops
     * the AP and is why the exploit page stalled at "detecting..." (the server vanished
     * mid-request). The console sits inches from the ESP, so half the TX power is more
     * than enough range and roughly halves the peak current draw. Pair with a solid
     * USB feed (wall adapter / powered hub) for the real fix. */
    esp_wifi_set_max_tx_power(40);

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_stop(ap));

    esp_netif_dns_info_t dns = { 0 };
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4 = ip.ip;                 /* DNS server = the AP itself */
    esp_netif_set_dns_info(ap, ESP_NETIF_DNS_MAIN, &dns);

    uint8_t offer = 1;                         /* DHCP option 6 (DNS) on */
    esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET,
                           ESP_NETIF_DOMAIN_NAME_SERVER, &offer, sizeof(offer));

    ESP_ERROR_CHECK(esp_netif_dhcps_start(ap));

    ESP_LOGI(TAG, "AP '%s' up on " IPSTR " (channel %d, %s)",
             AP_SSID, IP2STR(&ip.ip), AP_CHANNEL,
             strlen(AP_PASS) ? "WPA2" : "open");
}

static void mount_site(void)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path = SITE_MOUNT,
        .partition_label = "storage",
        .format_if_mount_failed = false,
        .dont_mount = false,
    };
    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "site mount failed: %s", esp_err_to_name(err));
        return;
    }
    size_t total = 0, used = 0;
    esp_littlefs_info("storage", &total, &used);
    ESP_LOGI(TAG, "site mounted at %s : %u KB used of %u KB",
             SITE_MOUNT, (unsigned)(used / 1024), (unsigned)(total / 1024));
}

void app_main(void)
{
    esp_err_t nv = nvs_flash_init();
    if (nv == ESP_ERR_NVS_NO_FREE_PAGES || nv == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, &ip_event, NULL, NULL));

    start_ap();
    mount_site();

    /* captive_dns answers every A query with g_ap_ip; https_srv + http serve the site */
    captive_dns_start();
    web_start();

    ESP_LOGI(TAG, "ready - join '%s' and open Manuals", AP_SSID);
}
