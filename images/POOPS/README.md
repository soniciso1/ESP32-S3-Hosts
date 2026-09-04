# POOPS — offline PS5 jailbreak on ESP32-S3 (16 MB)

Standalone Wi-Fi AP. A PS5 joins, opens **Manuals**, and gets a unified jailbreak site
served entirely offline from the board:

- **poopsploit** 7.00 – 12.00
- **P2JB**       12.02 – 12.70
- **umtx2**      1.00 – 5.50  *(upstream marks this range "System out of memory" / not
  supported — included but unproven)*

The landing page reads the console firmware from its User-Agent and routes to the right
chain automatically. After the jailbreak you get the ELF payload menu (send ftpsrv,
klogsrv, gdbsrv, shsrv, websrv, kstuff, shadowmountplus, elfldr). The 3 biggest payloads
(etaHEN, pldmgr, autoloader) are listed but their files are omitted to fit flash — those
tiles fail gracefully if tapped.

## Flash

    .\flash.ps1                 # auto-detects the COM port
    # or: python -m esptool --chip esp32s3 -p COMx write_flash 0x0 POOPS-merged.bin

Target: **ESP32-S3, 16 MB flash, PSRAM** (board MAC cc:ba:97:14:1e:94 in our set).
After flashing, **physically power-cycle** the board — a software reset can leave the
native USB-JTAG chip in download mode; a power cycle boots the app.

`parts/` holds the individual regions if you prefer:
`0x0 bootloader.bin · 0x8000 partition-table.bin · 0x10000 manuals-ap.bin · 0x190000 storage.bin`

## Use

1. Power the board (USB or wall adapter — a solid feed avoids WiFi-TX brownout).
2. On the PS5: join Wi-Fi **`PS5-Jailbreak`** (open network).
3. Open **Manuals**. It redirects to the jailbreak site over HTTP.
4. Run the jailbreak, then send payloads from the menu.

## Verification (2026-09-04)

- **HW-confirmed:** jailbroke a retail 12.00 end-to-end from this exact build (uid 1→0,
  authid set, elfldr up, ELFs sent).
- **Offsets 7.00–12.00:** every WebKit ROP gadget (26 per firmware) disassembled at its
  offset from the real decrypted `libSceNKWebKit` module for each firmware — 24 firmwares
  26/26 correct, 9.05 gadgets identical to 9.00 (its module is a minor rev with no
  separate build). vtable / host-constructor / imports land in valid segments. All 25
  offset files byte-identical to the proven production poopsploit set.

## Firmware internals (so a rebuild doesn't regress)

Serving: console fetches `manuals.playstation.net/document/en/ps5/` over HTTPS → 302 to
`http://192.168.4.1/` → whole site over plain HTTP (one TLS handshake, needs PSRAM).
**Do NOT gzip** — PS5 WebKit fails ES-module imports served with Content-Encoding: gzip.
DHCP: configure the DNS option AFTER `esp_wifi_start()` (stop→option→start), never around
it. `WIFI_PS_NONE` + reduced TX power for link stability. Source at
`E:\01-EXPLOIT-HOST\esp32\manuals-ap`.
