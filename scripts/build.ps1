# Configures and builds LAB X-16. -Only kaymanov,silo limits the configuration to those slugs (empty: every effect).
param(
    [string]$Config = "Release",
    [string]$Only = "",
    [string]$JucePath = "F:/_DEV/tools/JUCE"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) { $cmake = "F:\_DEV\tools\cmake\cmake-4.4.4-windows-x86_64\bin\cmake.exe" }

$only = ($Only -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ }) -join ';'
& $cmake -S $root -B "$root\build" -G "Visual Studio 17 2022" -A x64 "-DLABX16_JUCE_PATH=$JucePath" "-DLABX16_ONLY=$only"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $cmake --build "$root\build" --config $Config --parallel
exit $LASTEXITCODE
