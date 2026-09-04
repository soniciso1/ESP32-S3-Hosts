# POOPS-DEV — offline PS5 poopsploit (devkit) AP (ESP32-S3, 16 MB)

Devkit build of the poopsploit chain (**7.00–12.00**) served offline from a standalone
ESP32-S3 Wi-Fi AP. A PS5 joins, opens **Manuals**, and is auto-routed to the poopsploit
(dev) Jailbreak page.

## Flash

```powershell
.\flash.ps1                 # auto-detects the USB Serial COM port
.\flash.ps1 -Port COM17
```

`POOPS-DEV-merged.bin` is a single image at `0x0`. After flashing, **power-cycle the
board** (unplug/replug) — a native USB-JTAG reset can leave it in download mode.

## Use

1. Join Wi-Fi **`PS5-POOPS-DEV`**.
2. Open **Manuals** (console fetches `manuals.playstation.net` → AP answers 302 →
   `http://192.168.4.1/`).
3. Landing auto-detects firmware and shows **Jailbreak (Poopsploit dev)** for 7.00–12.00.
4. After the exploit lands, poopsploit's payload **menu** offers the ELFs in `payloads/`
   (etaHEN/pldmgr/autoloader omitted for flash space — menu entries intact, absent files
   just fail gracefully).

**Reboot the console between kernel-exploit runs.**

## Layout / how it serves

Same as the retail POOPS image: HTTPS 302 → plain HTTP, **not gzipped** (PS5 WebKit fails
gzipped ES modules). poopsploit pushes its own kexp/elfldr client-side from the root
`/payloads/` pool; `poops.html` lives under `/poopsploit/` and reaches `../offsets/` +
`../payloads/`.

```
POOPS-DEV-merged.bin   single 16 MB image, flash at 0x0
parts/                 bootloader / partition-table / manuals-ap / storage
flash.ps1
```

Source: `ps-exploit-host\publish\poopsploit-dev`. Tree: `build-dev-trees.py` →
`sites\poops-dev-esp`. SSID in `main\ap_ssid.h`.
