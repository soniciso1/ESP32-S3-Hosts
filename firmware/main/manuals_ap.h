/* Shared config + entry points for the manuals-ap firmware. */
#pragma once
#include "esp_netif_ip_addr.h"

/* ---- AP identity. Per-image SSID is written into main/ap_ssid.h by the build script
 * (one line: #define AP_SSID "PS5-p2jb-dev"). Generating a header sidesteps the
 * CMake -> compiler string-define escaping mess. Falls back to a generic name. */
#if defined(__has_include)
#  if __has_include("ap_ssid.h")
#    include "ap_ssid.h"
#  endif
#endif
#ifndef AP_SSID
#define AP_SSID      "PS5-Manuals"
#endif
#define AP_PASS      ""            /* empty = open network; else >=8 chars for WPA2 */
#define AP_CHANNEL   6
#define AP_MAX_CONN  4

/* ---- where the LittleFS site image mounts ----------------------------------------- */
#define SITE_MOUNT   "/site"
#define SITE_INDEX   "/site/index.html"

/* The AP's own IPv4 address, filled in at start_ap(). captive_dns answers every query
 * with it, and the HTTP/HTTPS servers bind it. */
extern esp_ip4_addr_t g_ap_ip;

/* subsystems */
void captive_dns_start(void);   /* captive_dns.c */
void web_start(void);           /* https_srv.c : starts both :80 and :443 */
