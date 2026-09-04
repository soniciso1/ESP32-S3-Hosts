# Flash P2JB-DEV to a 16 MB ESP32-S3. One merged image at 0x0.
#
#   .\flash.ps1                 # auto-detect the USB Serial COM port
#   .\flash.ps1 -Port COM17
param([string]$Port)
$env:IDF_PATH='E:\esp-idf'; $env:IDF_TOOLS_PATH='E:\esp-idf-tools'
& 'E:\esp-idf\export.ps1' *>&1 | Out-Null
if (-not $Port) {
    $Port = (Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -match '\(COM\d+\)' -and $_.Name -match 'USB Serial' } |
             ForEach-Object { if ($_.Name -match '\((COM\d+)\)') { $Matches[1] } } | Select-Object -First 1)
}
if (-not $Port) { Write-Host 'No USB Serial COM port found. Plug the board in (hold BOOT if needed).' -ForegroundColor Red; exit 1 }
Write-Host "Flashing P2JB-DEV-merged.bin to $Port ..." -ForegroundColor Cyan
python -m esptool --chip esp32s3 -p $Port -b 460800 --before default_reset --after hard_reset `
    write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB `
    0x0 "$PSScriptRoot$bin"
Write-Host ''
Write-Host 'Done. POWER-CYCLE the board (unplug/replug) - a native USB-JTAG reset can land' -ForegroundColor Yellow
Write-Host 'it in download mode; a physical power cycle boots the app. Then join SSID' -ForegroundColor Yellow
Write-Host '"PS5-P2JB-DEV" on the PS5 and open Manuals.' -ForegroundColor Yellow
