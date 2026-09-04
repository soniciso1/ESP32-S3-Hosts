# P2JB-DEV — offline PS5 P2JB (devkit) AP (ESP32-S3, 16 MB)

Devkit build of the P2JB WebKit chain (**12.00–12.70**) served offline from a standalone
ESP32-S3 Wi-Fi AP. A PS5 joins, opens **Manuals**, and is auto-routed to the P2JB (dev)
Jailbreak page (launcher `v=204`).

## Flash

```powershell
.\flash.ps1                 # auto-detects the USB Serial COM port
.\flash.ps1 -Port COM17
```

`P2JB-DEV-merged.bin` is a single image at `0x0`. After flashing, **power-cycle the
board** (unplug/replug) — a native USB-JTAG reset can leave it in download mode.

## Use

1. Join Wi-Fi **`PS5-P2JB-DEV`**.
2. Open **Manuals** (302 → `http://192.168.4.1/`).
3. Landing auto-detects firmware → **Jailbreak (P2JB dev)** for 12.00–12.70.
4. After the kernel exploit lands, P2JB's payload **menu** offers the ELFs in `payloads/`
   (etaHEN/pldmgr/autoloader omitted; menu entries intact).

**Reboot the console between kernel-exploit runs.**

## Layout / how it serves

Same as the retail P2JB image: HTTPS 302 → plain HTTP, **not gzipped**. P2JB's own kernel
shellcode + elfldr are fetched client-side over same-origin XHR from `/p2jb/payloads/`;
the payload menu POSTs `api/payload/<name>`, which the firmware relay streams to the
console's `IP:9021` from the root `/payloads/` pool.

```
P2JB-DEV-merged.bin    single 16 MB image, flash at 0x0
parts/                 bootloader / partition-table / manuals-ap / storage
flash.ps1
```

Source: `ps-exploit-host\sites-live\p2jb-dev`. Tree: `build-dev-trees.py` →
`sites\p2jb-dev-esp`. SSID in `main\ap_ssid.h`.
