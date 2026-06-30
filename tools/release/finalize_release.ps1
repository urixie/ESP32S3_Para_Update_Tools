param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectDir,

    [Parameter(Mandatory = $true)]
    [string]$ExeSrc
)

$ErrorActionPreference = "Stop"

$appName = "UniEdge" + [string]::Concat([char[]](
    0x9A71, # qu dong qi jia mi can shu pei zhi gong ju
    0x52A8,
    0x5668,
    0x52A0,
    0x5BC6,
    0x53C2,
    0x6570,
    0x914D,
    0x7F6E,
    0x5DE5,
    0x5177
))
$exeName = "$appName.exe"
$releaseDir = Join-Path $ProjectDir "release"
$exeDst = Join-Path $releaseDir $exeName

New-Item -ItemType Directory -Force -Path $releaseDir | Out-Null

$processName = [System.IO.Path]::GetFileNameWithoutExtension($exeName)
Get-Process -Name $processName -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Get-Process -Name "ParamBinTool" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 500

$copied = $false
for ($attempt = 1; $attempt -le 5; $attempt++) {
    try {
        Copy-Item -LiteralPath $ExeSrc -Destination $exeDst -Force
        $copied = $true
        break
    } catch {
        if ($attempt -eq 5) {
            throw
        }
        Write-Host "Copy attempt $attempt failed, retrying in 1s..."
        Start-Sleep -Seconds 1
    }
}

if (-not $copied) {
    throw "Failed to copy release exe."
}

foreach ($staleName in @("ParamBinTool.exe", "param_bin_tool.exe")) {
    $stalePath = Join-Path $releaseDir $staleName
    if (Test-Path -LiteralPath $stalePath) {
        Remove-Item -LiteralPath $stalePath -Force
    }
}

$readme = @(
    $appName,
    "================================",
    "",
    "This is a portable Windows executable build.",
    "",
    "Usage:",
    "  Double-click $exeName to run.",
    "",
    "Notes:",
    "  1. This tool is used to build and parse encrypted ESP32 parameter bin files.",
    "  2. The generated bin file uses AES-256-GCM encryption.",
    "  3. Chinese parameter names are stored in encrypted payload and should not appear as plaintext in the bin file.",
    "  4. This is not an installer. It is a directly runnable exe.",
    "  5. Windows WebView2 Runtime may be required on older Windows systems.",
    "  6. Fixed 72 parameters, addresses 0~71, simplified 17-byte Header + AES-GCM Payload.",
    "",
    "For ESP32 firmware integration, see docs/bin_protocol.md.",
    ""
)
Set-Content -LiteralPath (Join-Path $releaseDir "README.txt") -Value $readme -Encoding UTF8

Write-Host "Release exe generated:"
Write-Host $exeDst
