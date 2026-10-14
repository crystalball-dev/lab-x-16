# Runs pluginval against built VST3 bundles. -Plugin Kaymanov (target name without Lx16) or -All.
# Strictness 5 is the minimum for host compatibility; 10 adds fuzzing.
param([string]$Plugin = "", [switch]$All, [int]$Strictness = 5)

$root = Split-Path -Parent $PSScriptRoot
$pluginval = (Get-Command pluginval -ErrorAction SilentlyContinue).Source
if (-not $pluginval) { $pluginval = "F:\_DEV\tools\pluginval\pluginval.exe" }

$names = if ($All) { Get-ChildItem "$root\build" -Directory -Filter "Lx16*_artefacts" |
                     Where-Object { $_.Name -notmatch 'Render_artefacts$' -and (Test-Path (Join-Path $_.FullName "Release\VST3")) } |
                     ForEach-Object { $_.Name -replace '^Lx16', '' -replace '_artefacts$', '' } } else { @($Plugin) }
$failed = 0
foreach ($name in $names) {
    $bundle = Get-ChildItem "$root\build\Lx16${name}_artefacts\Release\VST3" -Directory -Filter "*.vst3" -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $bundle) { Write-Error "Build Lx16$name first"; $failed++; continue }
    $log = "$root\build\pluginval-$name-level$Strictness.log"
    & $pluginval --strictness-level $Strictness --validate $bundle.FullName | Out-File -FilePath $log -Encoding utf8
    $code = $LASTEXITCODE
    Write-Output ("{0,-16} strictness {1}: exit {2} (log: {3})" -f $name, $Strictness, $code, $log)
    if ($code -ne 0) { $failed++ }
}
exit $failed
