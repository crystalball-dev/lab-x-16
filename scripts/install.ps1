# Installs built effects into the standard VST3 folder as single .vst3 files (FL Studio 20 reliably scans those).
# -Plugin Kaymanov (target name without Lx16), or -All (every effect; the M0 stubs only with -IncludeStubs).
# Refuses while any FL64.exe runs (an old instance can outlive its window), when a file is in use, and when a bundle
# folder of the same name exists (FL would list the plugin twice).
param([string]$Plugin = "", [switch]$All, [switch]$IncludeStubs)
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$fl = Get-Process -Name FL64, FL -ErrorAction SilentlyContinue
if ($fl) {
    $fl | ForEach-Object { Write-Output ("FL Studio is running: pid {0}, started {1}, window '{2}'" -f $_.Id, $_.StartTime, $_.MainWindowTitle) }
    Write-Error "Close FL Studio (check Task Manager for a windowless FL64.exe) and run this again."
    exit 1
}

$names = if ($All) { Get-ChildItem "$root\build" -Directory -Filter "Lx16*_artefacts" |
                     Where-Object { $_.Name -notmatch 'Render_artefacts$' -and (Test-Path (Join-Path $_.FullName "Release\VST3")) } |
                     ForEach-Object { $_.Name -replace '^Lx16', '' -replace '_artefacts$', '' } |
                     Where-Object { $IncludeStubs -or $_ -notmatch '^Stub' } } else { @($Plugin) }
foreach ($name in $names) {
    $bundle = Get-ChildItem "$root\build\Lx16${name}_artefacts\Release\VST3" -Directory -Filter "*.vst3" -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $bundle) { Write-Error "Build Lx16$name first" }
    $file = $bundle.Name
    $source = Join-Path $bundle.FullName "Contents\x86_64-win\$file"
    $dest = "C:\Program Files\Common Files\VST3\$file"
    if (Test-Path $dest -PathType Container) { Write-Error "A bundle folder exists at $dest. Remove it so FL does not list the effect twice." }
    if (Test-Path $dest -PathType Leaf) {
        try { $s = [System.IO.File]::Open($dest, 'Open', 'ReadWrite', 'None'); $s.Close() }
        catch { Write-Error "$file is in use (a host has it loaded). Close it and run this again." }
    }
    Copy-Item $source $dest -Force
    $a = (Get-FileHash $source).Hash; $b = (Get-FileHash $dest).Hash
    $item = Get-Item $dest
    Write-Output ("Installed {0} ({1:N0} bytes, hash {2})" -f $item.FullName, $item.Length, $(if ($a -eq $b) { "matches the build" } else { "MISMATCH" }))
}
