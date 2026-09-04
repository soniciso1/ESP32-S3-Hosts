# Flash POOPS-4M to a 4 MB plain ESP32 (WROOM, no PSRAM). esp32 target, 40MHz.
#   .\flash.ps1                 # auto-detect COM
#   .\flash.ps1 -Port COM7
param([string]$Port)
# Prebuilt image lives in GitHub Releases, not the repo. Fetch it if missing.
$img = Join-Path $PSScriptRoot "POOPS-4M-merged.bin"
if (-not (Test-Path $img)) {
    Write-Host "downloading POOPS-4M-merged.bin from Releases ..." -ForegroundColor Cyan
    Invoke-WebRequest -Uri "https://github.com/soniciso1/ESP32-S3-Hosts/releases/download/v1.0/POOPS-4M-merged.bin" -OutFile $img
}
$env:IDF_PATH='E:\esp-idf'; $env:IDF_TOOLS_PATH='E:\esp-idf-tools'
& 'E:\esp-idf\export.ps1' *>&1 | Out-Null
if (-not $Port) {
    $Port = (Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -match '\(COM\d+\)' -and $_.Name -match 'USB|CP210|CH340|Serial' } |
             ForEach-Object { if ($_.Name -match '\((COM\d+)\)') { $Matches[1] } } | Select-Object -First 1)
}
if (-not $Port) { Write-Host 'No serial COM port found. Plug the board in.' -ForegroundColor Red; exit 1 }
Write-Host "Flashing POOPS-4M-merged.bin to $Port (esp32, 4MB, 40MHz) ..." -ForegroundColor Cyan
python -m esptool --chip esp32 -p $Port -b 460800 --before default_reset --after hard_reset `
    write_flash --flash_mode dio --flash_freq 40m --flash_size 4MB 0x0 $img
Write-Host 'If auto-reset failed: hold BOOT (GPIO0) + tap EN, release BOOT, rerun. Then join SSID "PS5-POOPS-4M".' -ForegroundColor Yellow
