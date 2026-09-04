# ESP32-S3-Hosts

Standalone **ESP32-S3 Wi-Fi access points** that serve the PS5 WebKit jailbreak chains
**entirely offline**. A PS5 joins the AP, opens **Manuals**, and is served the exploit —
no internet, no second device, no host PC.

Modeled on the serving strategy from
[owendswang/ps5-webkit-autoloader-esp32](https://github.com/owendswang/ps5-webkit-autoloader-esp32),
using our own poopsploit / P2JB builds.

## The images

| Image | SSID | Firmware | Chain |
|---|---|---|---|
| **POOPS** | `PS5-Jailbreak` | 7.00–12.70 | unified poopsploit (7.00–12.00) + P2JB (12.02–12.70) + umtx2 |
| **P2JB** | `PS5-P2JB` | 12.00–12.70 | P2JB (Y2JB WebKit port) — ✅ HW-jailbroke retail 12.00 |
| **POOPS-DEV** | `PS5-POOPS-DEV` | 7.00–12.00 | poopsploit (devkit) — ✅ HW-jailbroke 12.00 |
| **P2JB-DEV** | `PS5-P2JB-DEV` | 12.00–12.70 | P2JB (devkit, launcher v=204) |
| **POOPS-4M** | `PS5-POOPS-4M` | 7.00–12.00 | poopsploit on a **4 MB ESP32 (no PSRAM)** — ✅ HW-jailbroke 12.00; **trimmed payload set** (see below) |

**POOPS-4M is the odd one out:** it targets a plain **4 MB ESP32 (WROOM, no PSRAM)**, not the S3. Flash is tight, so the payload menu is **cut down to only what fits** — `ftpsrv`, `gdbsrv`, `klogsrv`, `shsrv`. The big payloads (**etaHEN, kstuff, shadowmountplus, websrv, pldmgr, autoloader**) are **not included** and their menu tabs are removed; use a 16 MB board (POOPS) for the full set, or push them post-JB over FTP. Its TLS handshake is timing-sensitive and currently paced via verbose mbedTLS logging (harmless UART output) — details in `images/POOPS-4M/README.md`.

Prebuilt merged images (flash at `0x0`) live in **[Releases](https://github.com/soniciso1/ESP32-S3-Hosts/releases/latest)**, not the repo tree — `<NAME>-merged.bin`, one per image.

## Flash

```powershell
cd images\P2JB
.\flash.ps1                 # downloads the .bin from Releases if missing, auto-detects COM
.\flash.ps1 -Port COM17
```

`flash.ps1` pulls its `<NAME>-merged.bin` from the latest release automatically, or grab it
manually from the [Releases page](https://github.com/soniciso1/ESP32-S3-Hosts/releases/latest).

After flashing, **power-cycle the board** (unplug/replug) — a native USB-JTAG reset can
leave the S3 in download mode; a physical power cycle boots the app. Then join the SSID
and open **Manuals**. **Reboot the console between kernel-exploit runs.**

## How it serves (the hard-won details)

- Console fetches `manuals.playstation.net/...` over HTTPS → the AP answers one RSA-2048
  TLS handshake (needs PSRAM + `MBEDTLS_EXTERNAL_MEM_ALLOC`) with a **302 →
  `http://192.168.4.1/`**, then serves the whole site over plain HTTP.
- **Not gzipped** — PS5 WebKit silently fails ES-module `import` served
  `Content-Encoding: gzip`.
- **poopsploit** pushes its own kexp/elfldr client-side from the root `/payloads/` pool.
  **P2JB** fetches its kexp/elfldr client-side over same-origin XHR from `/p2jb/payloads/`,
  and its payload **menu** POSTs `api/payload/<name>` — the firmware relay opens a TCP
  socket to the console's own `IP:9021` and streams the ELF from the root `/payloads/`.
- DHCP DNS option is set **after** `esp_wifi_start()` (stop → `set_dns_info` →
  `dhcps_option(DOMAIN_NAME_SERVER)` → start); `WIFI_PS_NONE` + TX power 40 + octal PSRAM
  at 40 MHz for link stability.

The three big ELFs (`etaHEN` / `pldmgr` / `autoloader`) are omitted from every image for
flash space; the payload menus are left intact — a listed-but-absent payload just fails
gracefully if picked.

## Layout

```
firmware/           ESP-IDF v5.5 project (source)
  main/             manuals_ap.c, https_srv.c, certs/ (server.crt + gen-cert.sh; NO key)
  index-src/        firmware-detect landing pages (per image)
  build-*.py        site-tree assemblers  ·  build-images.ps1  per-SSID image builder
  partitions_s3_16m.csv, sdkconfig.defaults*
sites/              the assembled LittleFS site trees (source of each storage image)
images/             per-image flash.ps1 + README (the .bin comes from Releases)
```

Binaries (`*.bin`) are published as **Release assets**, never committed to the tree.

## Building from source

ESP-IDF v5.5. Install, then:

```powershell
# 1. regenerate the TLS key (NOT committed - see the keys note below)
sh firmware/main/certs/gen-cert.sh
# 2. assemble a site tree (example: P2JB)
python firmware/build-p2jb-tree.py
# 3. build + flash
cd firmware
idf.py -B build_p2jb-esp -DSITE_DIR=../sites/p2jb-esp set-target esp32s3
idf.py -B build_p2jb-esp -DSITE_DIR=../sites/p2jb-esp build
```

## Keys

`server.key` (the self-signed manuals-spoof private key) is **not** committed. Regenerate
it with `firmware/main/certs/gen-cert.sh` before building. The prebuilt images embed a
working throwaway cert, so flashing them needs nothing extra.

## Credit

Serving architecture: owendswang/ps5-webkit-autoloader-esp32. Exploit chains: poopsploit
and P2JB (Y2JB WebKit port).
