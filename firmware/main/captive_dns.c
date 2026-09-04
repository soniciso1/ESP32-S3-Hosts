/* A minimal captive-portal DNS server: every A query is answered with the AP's own
 * address, so manuals.playstation.net (and anything else the console resolves) points
 * back here. Non-A queries get an empty NOERROR so resolvers do not stall retrying.
 *
 * This is deliberately tiny - no upstream forwarding. The console needs exactly one
 * name to resolve to us; there is no benefit to being a real resolver, and forwarding
 * would need an upstream the AP does not have (it has no internet).
 */
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "manuals_ap.h"

static const char *TAG = "captive-dns";

/* DNS header is 12 bytes; then QNAME (len-prefixed labels, 0 terminator), QTYPE(2),
 * QCLASS(2). We echo the question and append one A answer pointing at g_ap_ip. */
#define DNS_PORT 53
#define BUF_MAX  512

static void dns_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) { ESP_LOGE(TAG, "socket() failed"); vTaskDelete(NULL); return; }

    struct sockaddr_in me = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&me, sizeof(me)) < 0) {
        ESP_LOGE(TAG, "bind(:53) failed"); close(sock); vTaskDelete(NULL); return;
    }
    ESP_LOGI(TAG, "captive DNS up on :53 -> " IPSTR, IP2STR(&g_ap_ip));

    uint8_t buf[BUF_MAX];
    for (;;) {
        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &flen);
        if (n < 12) continue;                       /* smaller than a DNS header */

        /* Find the end of the question section: walk the QNAME labels. */
        int qn = 12;
        while (qn < n && buf[qn] != 0) {
            if (buf[qn] & 0xC0) { qn = n; break; }   /* compression in a query: bail */
            qn += buf[qn] + 1;
        }
        if (qn >= n) continue;
        int qend = qn + 1 + 4;                       /* 0 terminator + QTYPE + QCLASS */
        if (qend > n) continue;

        uint16_t qtype = (buf[qn + 1] << 8) | buf[qn + 2];

        /* Turn the query into a response in place. */
        buf[2] |= 0x80;            /* QR = response */
        buf[3] = (buf[3] & 0x0F);  /* RA off, RCODE 0 */
        buf[3] |= 0x00;
        buf[6] = 0; buf[7] = 0;    /* ANCOUNT set below */
        buf[8] = 0; buf[9] = 0;    /* NSCOUNT */
        buf[10] = 0; buf[11] = 0;  /* ARCOUNT */

        int out = qend;

        if (qtype == 1 /* A */ && out + 16 <= BUF_MAX) {
            /* Answer: name pointer to the question (0xC00C), A/IN, TTL, RDLENGTH 4,
             * then the AP address. */
            buf[out++] = 0xC0; buf[out++] = 0x0C;
            buf[out++] = 0x00; buf[out++] = 0x01;    /* TYPE A  */
            buf[out++] = 0x00; buf[out++] = 0x01;    /* CLASS IN */
            buf[out++] = 0x00; buf[out++] = 0x00;    /* TTL... */
            buf[out++] = 0x00; buf[out++] = 0x3C;    /* ...60s  */
            buf[out++] = 0x00; buf[out++] = 0x04;    /* RDLENGTH 4 */
            uint32_t a = g_ap_ip.addr;               /* already network byte order */
            memcpy(&buf[out], &a, 4);
            out += 4;
            buf[7] = 1;                              /* ANCOUNT = 1 */
        }
        /* Non-A queries fall through as NOERROR with ANCOUNT 0. */

        sendto(sock, buf, out, 0, (struct sockaddr *)&from, flen);
    }
}

void captive_dns_start(void)
{
    xTaskCreate(dns_task, "captive_dns", 4096, NULL, 5, NULL);
}
