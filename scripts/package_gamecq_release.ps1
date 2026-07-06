param(
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\build"),
    [string]$Configuration = "Release",
    [string]$OutputDir = (Join-Path $PSScriptRoot "..\dist\GameCQ"),
    [string]$WindeployQtPath = "",
    [switch]$IncludeDebugSymbols
)

$ErrorActionPreference = "Stop"

$buildRoot = [IO.Path]::GetFullPath($BuildDir)
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$sourceDir = Join-Path $buildRoot "app\$Configuration"
if (-not (Test-Path -LiteralPath $sourceDir)) {
    throw "Build output not found: $sourceDir. Build GameCQ first."
}

$destination = [IO.Path]::GetFullPath($OutputDir)
if (Test-Path -LiteralPath $destination) {
    Remove-Item -LiteralPath $destination -Recurse -Force
}
New-Item -ItemType Directory -Path $destination -Force | Out-Null

$excludedFilePatterns = @(
    "GameCQDiscordBridge.exe",
    "GameCQDiscordBridge.pdb",
    "discord-bridge.json",
    "discordghost-emotes.txt",
    "discordghost-personality.json",
    "dpp.dll",
    "opus.dll",
    "*.exp",
    "*.lib",
    "*.ilk",
    "*.lastcodeanalysissucceeded",
    "*.log"
)

if (-not $IncludeDebugSymbols) {
    $excludedFilePatterns += "*.pdb"
}

$excludedDirectoryNames = @(
    ".Cache",
    "DarkSpace",
    "DarkSpaceD12",
    "LaunchArt",
    "UserData"
)

function Test-ExcludedFile([IO.FileInfo]$File) {
    foreach ($pattern in $excludedFilePatterns) {
        if ($File.Name -like $pattern) {
            return $true
        }
    }
    return $false
}

function Test-ExcludedDirectory([IO.DirectoryInfo]$Directory) {
    return $excludedDirectoryNames -contains $Directory.Name
}

function Find-WindeployQt {
    if (-not [string]::IsNullOrWhiteSpace($WindeployQtPath)) {
        $candidate = [IO.Path]::GetFullPath($WindeployQtPath)
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
        throw "windeployqt was not found at $candidate"
    }

    $mainCache = Join-Path $buildRoot "CMakeCache.txt"
    if (Test-Path -LiteralPath $mainCache) {
        $prefixLine = Select-String -Path $mainCache -Pattern "^CMAKE_PREFIX_PATH:[^=]*=(.+)$" | Select-Object -First 1
        if ($prefixLine -and $prefixLine.Matches.Count -gt 0) {
            foreach ($prefix in $prefixLine.Matches[0].Groups[1].Value -split ";") {
                if ([string]::IsNullOrWhiteSpace($prefix)) {
                    continue
                }
                $candidate = Join-Path $prefix "bin\windeployqt.exe"
                if (Test-Path -LiteralPath $candidate) {
                    return [IO.Path]::GetFullPath($candidate)
                }
            }
        }

        $qtDirLine = Select-String -Path $mainCache -Pattern "^Qt6_DIR:[^=]*=(.+)$" | Select-Object -First 1
        if ($qtDirLine -and $qtDirLine.Matches.Count -gt 0) {
            $qtDir = $qtDirLine.Matches[0].Groups[1].Value
            $candidate = [IO.Path]::GetFullPath((Join-Path $qtDir "..\..\bin\windeployqt.exe"))
            if (Test-Path -LiteralPath $candidate) {
                return $candidate
            }
        }
    }

    $command = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    return ""
}

Get-ChildItem -LiteralPath $sourceDir -Force | ForEach-Object {
    if ($_.PSIsContainer) {
        if (Test-ExcludedDirectory $_) {
            return
        }
        Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $destination $_.Name) -Recurse -Force
        return
    }

    if (Test-ExcludedFile $_) {
        return
    }
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $destination $_.Name) -Force
}

$gamecqExe = Join-Path $destination "GameCQ.exe"
$windeployQt = Find-WindeployQt
if ([string]::IsNullOrWhiteSpace($windeployQt)) {
    Write-Warning "windeployqt.exe was not found. Qt DLLs and WebEngine runtime files were not deployed."
} else {
    $deployMode = if ($Configuration -eq "Debug") { "--debug" } else { "--release" }
    $pluginDir = Join-Path $destination "plugins"
    Write-Host "Deploying Qt runtime with $windeployQt"
    & $windeployQt $deployMode `
        --compiler-runtime `
        --plugindir $pluginDir `
        --skip-plugin-types qmltooling,generic,position `
        --translations en,en-US `
        --no-system-dxc-compiler `
        $gamecqExe
    if ($LASTEXITCODE -ne 0) {
        throw "windeployqt failed with exit code $LASTEXITCODE"
    }
}

[IO.File]::WriteAllText((Join-Path $destination "qt.conf"), "[Paths]`nPlugins = plugins`nTranslations = translations`n", [Text.UTF8Encoding]::new($false))

$emptyQmlDir = Join-Path $destination "qml"
if (Test-Path -LiteralPath $emptyQmlDir -PathType Container) {
    $qmlFiles = Get-ChildItem -LiteralPath $emptyQmlDir -Recurse -Force -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $qmlFiles) {
        Remove-Item -LiteralPath $emptyQmlDir -Force
    }
}

$webEngineLocaleDir = Join-Path $destination "translations\qtwebengine_locales"
if (Test-Path -LiteralPath $webEngineLocaleDir -PathType Container) {
    Get-ChildItem -LiteralPath $webEngineLocaleDir -Filter "*.pak" -File |
        Where-Object { $_.Name -ne "en-US.pak" } |
        Remove-Item -Force
}

$defaultArtSource = Join-Path $repoRoot "app\res\default_launch_art"
if (Test-Path -LiteralPath $defaultArtSource) {
    Copy-Item -LiteralPath $defaultArtSource -Destination (Join-Path $destination "DefaultLaunchArt") -Recurse -Force
}

[IO.File]::WriteAllText((Join-Path $destination "portable-settings.ini"), "# GameCQ portable settings marker`n", [Text.UTF8Encoding]::new($false))

$debugSuffix = if ($Configuration -eq "Debug") { "d" } else { "" }
$requiredWebEngineFiles = @(
    "QtWebEngineProcess$debugSuffix.exe",
    "resources\icudtl.dat",
    "translations\qtwebengine_locales\en-US.pak"
)
foreach ($relative in $requiredWebEngineFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $destination $relative))) {
        Write-Warning "Packaged browser runtime may be incomplete; missing $relative"
    }
}

$manifest = [ordered]@{
    name = "GameCQ"
    configuration = $Configuration
    createdUtc = [DateTime]::UtcNow.ToString("o")
    source = $sourceDir
    portableSettings = "portable-settings.ini"
    notes = @(
        "DiscordGhost and other operator tools are intentionally excluded.",
        "Installed DarkSpace clients, cache folders, user launch art, local configs, and logs are intentionally excluded.",
        "Default DarkSpace launch art is bundled in DefaultLaunchArt and can be overridden by user LaunchArt."
    )
}
$manifestPath = Join-Path $destination "release-manifest.json"
[IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 4), [Text.UTF8Encoding]::new($false))

Write-Host "Packaged GameCQ release payload:"
Write-Host "  $destination"
