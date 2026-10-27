param(
    [string]$XiaozhiDir = "..\xiaozhi-esp32",   # xiaozhi-esp32 @ c0fe42a with patches\*.patch applied
    [string]$IdfPath = "C:\esp\v6.0.1\esp-idf",
    [string]$IdfToolsPath = "C:\Espressif\tools6",
    [string]$Port = "COM8",
    [switch]$SyncBoard,   # copy firmware\esp32s3-weact-nhi into the xiaozhi tree first
    [switch]$SkipBuild,
    [switch]$EraseFlash   # also wipes saved Wi-Fi / activation
)

# Build + flash the Nhi desk robot firmware (board esp32s3-weact-nhi, vi-VN).
$ErrorActionPreference = "Stop"
$Board = "esp32s3-weact-nhi"
$XiaozhiDir = (Resolve-Path $XiaozhiDir).Path

if ($SyncBoard) {
    $dst = Join-Path $XiaozhiDir "main\boards\$Board"
    Write-Host "==> Copy board sources -> $dst" -ForegroundColor Cyan
    # Mirror: the build globs every *.cc in the folder, so stale files must go.
    if (Test-Path $dst) { Remove-Item -Recurse -Force $dst }
    New-Item -ItemType Directory -Force $dst | Out-Null
    Copy-Item -Recurse -Force (Join-Path $PSScriptRoot "..\firmware\$Board\*") $dst
}

$env:IDF_TOOLS_PATH = $IdfToolsPath
. (Join-Path $IdfPath "export.ps1") | Out-Null
Set-Location $XiaozhiDir

if (-not $SkipBuild) {
    Write-Host "==> Build $Board (vi-VN)..." -ForegroundColor Cyan
    python scripts/build.py $Board --language vi-VN
    if ($LASTEXITCODE -ne 0) { throw "Build that bai." }
}

$targets = if ($EraseFlash) { @("erase-flash", "flash") } else { @("flash") }
Write-Host "==> Flash len $Port..." -ForegroundColor Green
idf.py -p $Port -b 921600 @targets
if ($LASTEXITCODE -ne 0) { throw "Flash that bai." }
Write-Host "==> XONG. Monitor: idf.py -p $Port monitor" -ForegroundColor Cyan
