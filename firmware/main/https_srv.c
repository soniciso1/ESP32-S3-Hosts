/* Serving strategy, learned from the working ps5-webkit-autoloader-esp32:
 *
 *   The console opens Manuals -> fetches https://manuals.playstation.net/document/en/ps5/
 *   over TLS. We answer that ONE request with a 302 redirect to http://192.168.4.1/ .
 *   The console follows it and loads the WHOLE site over plain HTTP.
 *
 * Why not serve the site over HTTPS directly: a page pulls 20+ assets, and each
 * RSA-2048 TLS handshake on the ESP is slow. Serving everything over HTTPS meant a
 * handshake per asset, which choked the socket pool and the page never finished (white
 * screen / never loads). One TLS handshake for the redirect, then fast HTTP, fixes it.
 *
 * WebKit runs the exploit fine over HTTP - the console only insists on TLS for the
 * initial manuals.playstation.net fetch, which we satisfy with the redirect.
 *
 * HTTP also serves pre-gzipped files transparently: if foo.gz exists it is sent with
 * Content-Encoding: gzip, which roughly halves the flash the site costs.
 */
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "esp_log.h"
#include "esp_https_server.h"
#include "esp_http_server.h"

#include "manuals_ap.h"

static const char *TAG = "web";

#define PAYLOAD_DIR   SITE_MOUNT "/payloads/"
#define ELFLDR_PORT   9021

extern const uint8_t servercert_start[] asm("_binary_server_crt_start");
extern const uint8_t servercert_end[]   asm("_binary_server_crt_end");
extern const uint8_t serverkey_start[]  asm("_binary_server_key_start");
extern const uint8_t serverkey_end[]    asm("_binary_server_key_end");

/* http://<ap-ip>/ , filled in at web_start() from g_ap_ip */
static char g_redirect_url[32];

/* Ring buffer of recent requests, readable from the PS5 browser at /_reqlog when the
 * exploit page stalls - shows the LAST URL the console asked for (i.e. the stall point)
 * without needing serial or a second machine. */
#define REQLOG_N 40
static char g_reqlog[REQLOG_N][96];
static int  g_reqlog_head = 0;
static void reqlog_add(const char *method, const char *uri)
{
    snprintf(g_reqlog[g_reqlog_head % REQLOG_N], 96, "%s %s", method, uri);
    g_reqlog_head++;
}

static const char *mime_for(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (!strcasecmp(dot, ".html") || !strcasecmp(dot, ".htm")) return "text/html";
    if (!strcasecmp(dot, ".js"))   return "application/javascript";
    if (!strcasecmp(dot, ".css"))  return "text/css";
    if (!strcasecmp(dot, ".json")) return "application/json";
    if (!strcasecmp(dot, ".png"))  return "image/png";
    if (!strcasecmp(dot, ".jpg") || !strcasecmp(dot, ".jpeg")) return "image/jpeg";
    if (!strcasecmp(dot, ".gif"))  return "image/gif";
    if (!strcasecmp(dot, ".svg"))  return "image/svg+xml";
    if (!strcasecmp(dot, ".wasm")) return "application/wasm";
    if (!strcasecmp(dot, ".bin") || !strcasecmp(dot, ".elf")) return "application/octet-stream";
    return "text/plain";
}

/* ---------------------------------------------------------------- HTTPS: redirect only
 * Any HTTPS request (the console only ever makes the manuals one) -> 302 to http://ap/ .
 */
static esp_err_t https_redirect(httpd_req_t *req)
{
    ESP_LOGI(TAG, "HTTPS %s -> 302 %s", req->uri, g_redirect_url);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", g_redirect_url);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* ---------------------------------------------------------------- HTTP: serve the site */
/* Resolve a request URI to a filesystem path. Sets *want_gzip if the file only exists
 * pre-compressed (foo.gz) and the browser should be told Content-Encoding: gzip.
 * The tree is stored gzipped (originals deleted to save flash and shrink transfers over
 * the marginal WiFi link), so the .gz form is the NORMAL case, not just an optimization.
 */
static void resolve_path(const char *uri, char *out, size_t cap, int *want_gzip)
{
    *want_gzip = 0;
    char clean[256];
    size_t i = 0;
    for (const char *p = uri; *p && *p != '?' && i < sizeof(clean) - 1; ++p)
        clean[i++] = *p;
    clean[i] = 0;

    if (clean[0] == 0 || !strcmp(clean, "/") || clean[strlen(clean) - 1] == '/')
        snprintf(out, cap, "%s", SITE_INDEX);
    else
        snprintf(out, cap, "%s%s", SITE_MOUNT, clean);

    struct stat st;
    if (stat(out, &st) == 0 && S_ISREG(st.st_mode))
        return;                                  /* plain file exists - serve as-is */

    /* plain missing: try <path>.gz before giving up */
    char gz[300];
    snprintf(gz, sizeof(gz), "%s.gz", out);
    if (stat(gz, &st) == 0 && S_ISREG(st.st_mode)) {
        *want_gzip = 1;
        return;                                  /* caller opens out + ".gz" */
    }

    /* unknown path -> landing page (also try its .gz) */
    snprintf(out, cap, "%s", SITE_INDEX);
    snprintf(gz, sizeof(gz), "%s.gz", out);
    if (stat(out, &st) != 0 && stat(gz, &st) == 0)
        *want_gzip = 1;
}

static esp_err_t api_payload(httpd_req_t *req);   /* forward decl - dispatched to below */
static esp_err_t api_elfldr(httpd_req_t *req);
static esp_err_t beacon_log(httpd_req_t *req);

static esp_err_t http_serve(httpd_req_t *req)
{
    /* log every request as it ARRIVES (not just on success): to serial, and to the ring
     * buffer readable at /_reqlog from the console itself. */
    const char *m = req->method == HTTP_POST ? "POST" : "GET";
    ESP_LOGI(TAG, "req %s %s", m, req->uri);
    reqlog_add(m, req->uri);

    /* diagnostic: dump the request ring buffer as plain text */
    if (strstr(req->uri, "/_reqlog")) {
        httpd_resp_set_type(req, "text/plain");
        int start = g_reqlog_head > REQLOG_N ? g_reqlog_head - REQLOG_N : 0;
        for (int i = start; i < g_reqlog_head; i++) {
            char line[110];
            int n = snprintf(line, sizeof(line), "%d: %s\n", i, g_reqlog[i % REQLOG_N]);
            httpd_resp_send_chunk(req, line, n);
        }
        httpd_resp_send_chunk(req, NULL, 0);
        return ESP_OK;
    }

    /* p2jb's endpoints can appear at any depth (see api_payload's comment) - check for
     * them before treating the request as a static file. */
    if (strstr(req->uri, "/api/payload/"))
        return api_payload(req);
    if (strstr(req->uri, "/api/elfldr"))
        return api_elfldr(req);
    /* Exploit progress beacons -> serial, so a stalled run narrates itself over UART.
     * poopsploit POSTs JSON to /__poops_log; p2jb GETs log/<line>. Both handled here. */
    if (strstr(req->uri, "/__poops_log") || strstr(req->uri, "/log/"))
        return beacon_log(req);

    char path[300];
    int gzip = 0;
    resolve_path(req->uri, path, sizeof(path), &gzip);

    char openpath[312];
    if (gzip) snprintf(openpath, sizeof(openpath), "%s.gz", path);
    else      snprintf(openpath, sizeof(openpath), "%s", path);

    int fd = open(openpath, O_RDONLY);
    if (fd < 0) {
        ESP_LOGW(TAG, "404 %s", req->uri);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
        return ESP_OK;
    }

    httpd_resp_set_type(req, mime_for(path));      /* mime by the real name, not .gz */
    if (gzip) httpd_resp_set_hdr(req, "Content-Encoding", "gzip");

    static char chunk[4096];
    ssize_t r;
    while ((r = read(fd, chunk, sizeof(chunk))) > 0) {
        if (httpd_resp_send_chunk(req, chunk, r) != ESP_OK) {
            close(fd);
            return ESP_FAIL;
        }
    }
    close(fd);
    httpd_resp_send_chunk(req, NULL, 0);
    ESP_LOGI(TAG, "200 %s -> %s%s", req->uri, path, gzip ? " (gz)" : "");
    return ESP_OK;
}

/* ------------------------------------------------------- p2jb payload relay
 * p2jb.html (unlike poopsploit/umtx2) does not push the ELF to the console's own
 * elfldr via a client-side raw syscall - it POSTs to /api/payload/<name> and expects
 * the SERVER to open a TCP connection to the requesting console on port 9021 and
 * stream the file. Ported from ps-exploit-host's src/http.c (the same relay, same
 * response shape), using ESP-IDF's BSD sockets instead of Winsock.
 *
 * The peer IP is read off the accepted socket via getpeername() - httpd_req_to_sockfd()
 * gives us that fd. No client-supplied address is trusted for where the ELF goes.
 */
static esp_err_t api_payload(httpd_req_t *req)
{
    /* p2jb.html fetches "api/payload/<name>" with NO leading slash, so the browser
     * resolves it relative to wherever p2jb.html itself lives (e.g. a request for
     * /p2jb/api/payload/x.elf if the page is nested under /p2jb/). The real host
     * (src/http.c) matches with strstr() for exactly this reason - the endpoint's
     * position in the path is not fixed. Mirror that here instead of requiring the
     * literal URI to start with /api/payload/. */
    const char *PFX = "/api/payload/";
    const char *ap = strstr(req->uri, PFX);
    size_t pfxlen = strlen(PFX);
    if (!ap) {                       /* http_serve only calls us on a match; defensive */
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
        return ESP_OK;
    }

    /* name: up to the next '?' or end, no path separators, no ".." */
    char name[128];
    size_t n = 0;
    for (const char *p = ap + pfxlen; *p && *p != '?' && n < sizeof(name) - 1; ++p) {
        if (*p == '/' || *p == '\\') { n = 0; break; }
        name[n++] = *p;
    }
    name[n] = 0;

    char body[192];
    int okbytes = -1;

    if (n > 0 && !strstr(name, "..")) {
        char path[256];
        snprintf(path, sizeof(path), "%s%s", PAYLOAD_DIR, name);
        int fd = open(path, O_RDONLY);
        if (fd >= 0) {
            /* who is asking? that is who gets the ELF */
            int sockfd = httpd_req_to_sockfd(req);
            struct sockaddr_in peer;
            socklen_t plen = sizeof(peer);
            if (sockfd >= 0 && getpeername(sockfd, (struct sockaddr *)&peer, &plen) == 0) {
                int cs = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                if (cs >= 0) {
                    struct sockaddr_in ta = { 0 };
                    ta.sin_family = AF_INET;
                    ta.sin_port = htons(ELFLDR_PORT);
                    ta.sin_addr = peer.sin_addr;
                    struct timeval tmo = { .tv_sec = 15, .tv_usec = 0 };
                    setsockopt(cs, SOL_SOCKET, SO_SNDTIMEO, &tmo, sizeof(tmo));
                    if (connect(cs, (struct sockaddr *)&ta, sizeof(ta)) == 0) {
                        char chunk[2048];
                        ssize_t r;
                        int sok = 1;
                        okbytes = 0;
                        while ((r = read(fd, chunk, sizeof(chunk))) > 0) {
                            ssize_t off = 0;
                            while (off < r) {
                                ssize_t w = send(cs, chunk + off, r - off, 0);
                                if (w <= 0) { sok = 0; break; }
                                off += w;
                            }
                            if (!sok) break;
                            okbytes += (int)r;
                        }
                        if (!sok) okbytes = -1;
                    }
                    close(cs);
                }
            }
            close(fd);
        }
    }

    if (okbytes >= 0) {
        snprintf(body, sizeof(body), "{\"ok\":true,\"name\":\"%s\",\"bytes\":%d,\"port\":%d}",
                 name, okbytes, ELFLDR_PORT);
        ESP_LOGI(TAG, "api/payload %s -> %d bytes to elfldr", name, okbytes);
    } else {
        snprintf(body, sizeof(body), "{\"ok\":false,\"name\":\"%s\"}", name);
        ESP_LOGW(TAG, "api/payload %s -> FAILED (missing file or elfldr not listening)", name);
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* Exploit progress beacons -> UART. poopsploit POSTs a JSON body of marks to
 * /__poops_log; p2jb does a synchronous GET log/<line> per mark. We print whatever
 * arrives and answer 204 (empty) so the beacon is cheap and never blocks the run. This
 * is how a stall like "stuck at detecting..." tells us the exact last mark reached. */
static esp_err_t beacon_log(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        /* p2jb: the mark is in the URL path after log/ */
        const char *l = strstr(req->uri, "/log/");
        ESP_LOGI("beacon", "%s", l ? l + 5 : req->uri);
    } else {
        /* poopsploit: JSON body of events */
        int total = req->content_len, got = 0;
        char buf[512];
        while (got < total) {
            int r = httpd_req_recv(req, buf, sizeof(buf) - 1);
            if (r <= 0) break;
            buf[r] = 0;
            ESP_LOGI("beacon", "%s", buf);
            got += r;
        }
    }
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* Liveness probe: GET .../api/elfldr connect-tests the requesting console on TCP 9021
 * with a short timeout and reports {"up":bool}, so p2jb.html can skip straight to the
 * payload screen when the console is already jailbroken. Ported from src/http.c. */
static esp_err_t api_elfldr(httpd_req_t *req)
{
    int up = 0;
    int sockfd = httpd_req_to_sockfd(req);
    struct sockaddr_in peer;
    socklen_t plen = sizeof(peer);
    if (sockfd >= 0 && getpeername(sockfd, (struct sockaddr *)&peer, &plen) == 0) {
        int cs = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (cs >= 0) {
            struct timeval tmo = { .tv_sec = 1, .tv_usec = 500000 };
            setsockopt(cs, SOL_SOCKET, SO_SNDTIMEO, &tmo, sizeof(tmo));
            setsockopt(cs, SOL_SOCKET, SO_RCVTIMEO, &tmo, sizeof(tmo));
            struct sockaddr_in ta = { 0 };
            ta.sin_family = AF_INET;
            ta.sin_port = htons(ELFLDR_PORT);
            ta.sin_addr = peer.sin_addr;
            up = (connect(cs, (struct sockaddr *)&ta, sizeof(ta)) == 0);
            close(cs);
        }
    }
    char body[24];
    snprintf(body, sizeof(body), "{\"up\":%s}", up ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static const httpd_uri_t http_all = {
    .uri = "/*", .method = HTTP_GET, .handler = http_serve, .user_ctx = NULL,
};
/* p2jb's relay POSTs; dispatch through the same http_serve() -> api_payload() path. */
static const httpd_uri_t http_all_post = {
    .uri = "/*", .method = HTTP_POST, .handler = http_serve, .user_ctx = NULL,
};
static const httpd_uri_t https_all = {
    .uri = "/*", .method = HTTP_GET, .handler = https_redirect, .user_ctx = NULL,
};

static void start_https(void)
{
    httpd_ssl_config_t cfg = HTTPD_SSL_CONFIG_DEFAULT();
    cfg.servercert = servercert_start;
    cfg.servercert_len = servercert_end - servercert_start;
    cfg.prvtkey_pem = serverkey_start;
    cfg.prvtkey_len = serverkey_end - serverkey_start;
    cfg.httpd.uri_match_fn = httpd_uri_match_wildcard;
    cfg.httpd.max_uri_handlers = 2;
    cfg.httpd.stack_size = 10240;
    /* HTTPS only ever issues the redirect, so it needs few sockets. */
    cfg.httpd.max_open_sockets = 3;
    cfg.httpd.lru_purge_enable = true;

    httpd_handle_t h = NULL;
    esp_err_t err = httpd_ssl_start(&h, &cfg);
    if (err != ESP_OK) { ESP_LOGE(TAG, "HTTPS start failed: %s", esp_err_to_name(err)); return; }
    httpd_register_uri_handler(h, &https_all);
    ESP_LOGI(TAG, "HTTPS up on :443 (redirect -> %s)", g_redirect_url);
}

static void start_http(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.ctrl_port = 32080;
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.max_uri_handlers = 2;                  /* catch-all GET + catch-all POST */
    cfg.lru_purge_enable = true;
    cfg.max_open_sockets = 10;                 /* the site loads ~13 assets; give room */
    /* keep_alive OFF: esp_http_server runs ONE worker task, and a kept-alive connection
     * parks that task waiting for the next request on it, starving other pending
     * connections. The symptom is exactly "some assets load, then the next request
     * (e.g. /offsets/<fw>.js) hangs forever". Per-request connections + LRU purge
     * round-robin cleanly over HTTP (handshake is cheap without TLS). */
    cfg.keep_alive_enable = false;
    cfg.recv_wait_timeout = 10;
    cfg.send_wait_timeout = 10;

    httpd_handle_t h = NULL;
    if (httpd_start(&h, &cfg) != ESP_OK) { ESP_LOGE(TAG, "HTTP start failed"); return; }
    httpd_register_uri_handler(h, &http_all);
    httpd_register_uri_handler(h, &http_all_post);
    ESP_LOGI(TAG, "HTTP up on :80 (serves the site + /api/payload/ relay)");
}

void web_start(void)
{
    snprintf(g_redirect_url, sizeof(g_redirect_url), "http://" IPSTR "/", IP2STR(&g_ap_ip));
    start_http();
    start_https();
}
