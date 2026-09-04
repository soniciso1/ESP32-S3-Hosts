# P2JB — offline PS5 WebKit jailbreak AP (ESP32-S3, 16 MB)

A standalone Wi-Fi access point on an ESP32-S3. A PS5 on firmware **12.00–12.70**
joins the AP, opens **Manuals**, and is served the **P2JB** WebKit exploit entirely
offline — no internet, no second device.

## Flash

```powershell
.\flash.ps1                 # auto-detects the USB Serial COM port
.\flash.ps1 -Port COM17     # or name it
```

`P2JB-merged.bin` is a single image written at `0x0` (bootloader + partition table +
app + LittleFS site). After flashing, **power-cycle the board** (unplug/replug) — a
native USB-JTAG reset can leave it in download mode; a physical power cycle boots the app.

## Use

1. On the PS5, join Wi-Fi **`PS5-P2JB`**.
2. Open **Settings → User's Guide / Manuals** (the console fetches
   `manuals.playstation.net` → the AP answers with a 302 to `http://192.168.4.1/`).
3. The landing page auto-detects the console firmware from its User-Agent and shows a
   **Jailbreak (P2JB)** button for 12.00–12.70. Tap it; P2JB runs.
4. After the kernel exploit lands, P2JB's own **payload menu** offers the ELFs in
   `payloads/` (etaHEN/pldmgr/autoloader omitted for flash space — the menu entries are
   intact and any listed-but-absent file simply fails gracefully if picked).

**Reboot the console between kernel-exploit runs** — running the chain twice on one boot
panics.

## How it serves

- Console fetches `manuals.playstation.net/...` over HTTPS → one RSA-2048 TLS handshake
  (PSRAM + `MBEDTLS_EXTERNAL_MEM_ALLOC`), answered with a **302 → `http://192.168.4.1/`**;
  the whole site is then served over plain HTTP.
- **Not gzipped** — PS5 WebKit silently fails ES-module `import` served
  `Content-Encoding: gzip`.
- **P2JB's own kernel shellcode + elfldr** (`kexp_2026_05_25.bin`, `elfldr-ps5-1360.elf`)
  are fetched client-side over same-origin XHR from `/p2jb/payloads/`.
- **The payload menu** POSTs `api/payload/<name>`; the firmware relay opens a TCP socket
  to the console's own IP:9021 and streams the ELF from the root `/payloads/` pool.

## Layout

```
P2JB-merged.bin      single 16 MB image, flash at 0x0
parts/
  bootloader.bin         0x0
  partition-table.bin    0x8000
  manuals-ap.bin         0x10000    (factory app, 1.5 MB slot)
  storage.bin            0x190000   (LittleFS site, 14.44 MB slot)
flash.ps1
```

Firmware project: `E:\01-EXPLOIT-HOST\esp32\manuals-ap`. Site tree assembled by
`build-p2jb-tree.py` → `sites\p2jb-esp`. SSID set in `main\ap_ssid.h`.
