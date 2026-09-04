# POOPS-4M — poopsploit on a 4 MB ESP32 (no PSRAM) — WORKING

Minimal poopsploit (7.00-12.00) for a **plain 4 MB ESP32 (WROOM, no PSRAM)**.
**HW-confirmed: full jailbreak of a retail 12.00** served over TLS with no PSRAM.

## Flash
```powershell
.\flash.ps1                 # auto-detect COM (esp32, 40MHz, bootloader at 0x1000)
.\flash.ps1 -Port COM7
```
Plain ESP32: if auto-reset fails, hold BOOT (GPIO0) + tap EN, release BOOT, rerun.
Join SSID **`PS5-POOPS-4M`**, open Manuals. Reboot console between kernel-exploit runs.

## What fits (2.94 MB LittleFS slot)
Site ~1.27 MB (decoration GIFs stripped) + the exploit's own kexp/elfldr, leaving room
for a **small payload menu**: `ftpsrv`, `shsrv` (shell), `klogsrv`, `gdbsrv`, `bridge`
(~1.5 MB). The big payloads do **not** fit alongside on 4 MB and are omitted:
`etaHEN` (5.4 MB), `kstuff`, `shadowmountplus`, `websrv`, `pldmgr`, `autoloader`. The
The poopsploit **menu is trimmed to only the ELFs actually in flash** (ftpsrv, gdbsrv,
klogsrv, shsrv) - no dead tabs. For the big payloads use a 16 MB board (POOPS) or push them
post-JB over `ftpsrv`.

## The no-PSRAM TLS gotcha (solved)
With no PSRAM the RSA-2048 manuals handshake runs from internal RAM — that works on stock
config. The handshake is **timing-sensitive** on no-PSRAM esp32: at full send speed the PS5 rejects
our ServerHello/Certificate/ServerKeyExchange flight (-0x7780). Ruled OUT on hardware: RSA/SHA
HW accelerators, ECDHE curve (x25519 vs secp256r1), WiFi buffer sizes, AMPDU. What reliably
completes it is pacing the record writes - currently via `CONFIG_MBEDTLS_DEBUG` verbose
logging (harmless UART output; nothing reads it). Replacing that with a direct send-path
delay is the open follow-up.

## Layout (esp32, not S3)
```
POOPS-4M-merged.bin   4 MB, flash at 0x0 (bootloader inside at 0x1000, 40MHz)
parts/                bootloader / partition-table / manuals-ap / storage
```
Source: build-poops-4mb.py -> sites/poops-4mb; sdkconfig.4m + partitions_4m.csv.
