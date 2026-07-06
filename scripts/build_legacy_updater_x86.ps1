param(
    [string]$Configuration = "Release",
    [string]$OutputDir = ""
)

$ErrorActionPreference = "Stop"

$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$sourceDir = Join-Path $root "legacy_updater"
$buildDir = Join-Path $root "build\legacy_updater_x86"

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $root "build\app\$Configuration"
}

$generator = $null
$medusaRoot = ""
$mainCache = Join-Path $root "build\CMakeCache.txt"
if (Test-Path -LiteralPath $mainCache) {
    $line = Select-String -Path $mainCache -Pattern "^CMAKE_GENERATOR:INTERNAL=(.+)$" | Select-Object -First 1
    if ($line -and $line.Matches.Count -gt 0) {
        $generator = $line.Matches[0].Groups[1].Value
    }
    $medusaLine = Select-String -Path $mainCache -Pattern "^GAMECQ_MEDUSA_ROOT:PATH=(.+)$" | Select-Object -First 1
    if ($medusaLine -and $medusaLine.Matches.Count -gt 0) {
        $medusaRoot = $medusaLine.Matches[0].Groups[1].Value
    }
}
if ([string]::IsNullOrWhiteSpace($generator)) {
    $generator = "Visual Studio 18 2026"
}
if ([string]::IsNullOrWhiteSpace($medusaRoot)) {
    $medusaRoot = Join-Path $root "medusa-vs26"
}

$legacyCache = Join-Path $buildDir "CMakeCache.txt"
if (-not (Test-Path -LiteralPath $legacyCache)) {
    cmake -S $sourceDir -B $buildDir -G $generator -A Win32 -DGAMECQ_MEDUSA_ROOT="$medusaRoot"
}

cmake --build $buildDir --config $Configuration --target GameCQLegacyUpdate

$builtExe = Join-Path $buildDir "$Configuration\GameCQLegacyUpdate.exe"
if (-not (Test-Path -LiteralPath $builtExe)) {
    throw "Expected helper was not produced: $builtExe"
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
Copy-Item -LiteralPath $builtExe -Destination (Join-Path $OutputDir "GameCQLegacyUpdate.exe") -Force

Write-Host "Copied GameCQLegacyUpdate.exe to $OutputDir"
