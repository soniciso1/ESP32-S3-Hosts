# Build one single-site ESP image per exploit site. Each: distinct SSID, that site's
# tree served directly (open Manuals -> its JB/sender landing). Assumes site trees are
# already assembled by build-site-trees.py into ..\sites\<site>.
#
#   .\build-images.ps1                       # all sites, 16MB S3 board
#   .\build-images.ps1 -Sites p2jb-dev       # one site
#   .\build-images.ps1 -Board 8m             # 8MB S3 variant
param(
    [string[]]$Sites = @('luasauce','luasaucedev','poopsploit','poopsploit-dev','p2jb','p2jb-dev'),
    [ValidateSet('16m','8m')] [string]$Board = '16m'
)
$env:IDF_PATH='E:\esp-idf'; $env:IDF_TOOLS_PATH='E:\esp-idf-tools'
# The managed littlefs component is already cached under managed_components/. Offline
# mode stops the component manager from reaching the registry during set-target, which
# was hanging configure for minutes with no output.
$env:IDF_COMPONENT_OFFLINE_MODE='1'
Set-Location 'E:\01-EXPLOIT-HOST\esp32\manuals-ap'
& 'E:\esp-idf\export.ps1' *>&1 | Out-Null

$defaults = if ($Board -eq '8m') { 'sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.8m' }
            else                 { 'sdkconfig.defaults;sdkconfig.defaults.esp32s3' }

foreach ($site in $Sites) {
    $ssid = "PS5-$site"
    $bdir = "build_$site" + $(if ($Board -eq '8m') { '_8m' } else { '' })
    Write-Host "==== building $site  (SSID $ssid, board $Board) ====" -ForegroundColor Cyan
    # generate the SSID header - the ONLY per-image code difference
    Set-Content -Path 'main\ap_ssid.h' -Value "#define AP_SSID `"$ssid`"" -Encoding ascii
    # a FRESH build dir defaults to the esp32 target; set-target esp32s3 first or the
    # partition table falls back to a 2 MB layout and the image gets LFS_ERR_NOSPC
    $common = @("-DSDKCONFIG=sdkconfig.$site.$Board.out", "-DSITE_DIR=../sites/$site", "-DSDKCONFIG_DEFAULTS=$defaults")
    # set-target only when the build dir is new (it forces a full recompile of all ~1050
    # components). An existing dir is already esp32s3, so skip straight to build and only
    # the changed SSID header + the storage image are rebuilt - seconds, not minutes.
    if (-not (Test-Path (Join-Path $bdir 'sdkconfig'))) {
        idf.py -B $bdir @common set-target esp32s3 2>&1 | Select-Object -Last 2
    }
    idf.py -B $bdir @common build 2>&1 | Select-Object -Last 3
    $bin = Join-Path $bdir 'manuals-ap.bin'
    if (Test-Path $bin) {
        $ok = (Select-String -Path (Join-Path $bdir 'manuals-ap.elf') -Pattern ([regex]::Escape($ssid)) -Quiet)
        Write-Host ("  $site : bin built, SSID '$ssid' embedded=$ok") -ForegroundColor Green
    } else {
        Write-Host "  $site : BUILD FAILED" -ForegroundColor Red
    }
}
Write-Host 'done. flash a site with:  idf.py -B build_<site> -p <COM> flash' -ForegroundColor Yellow
