param(
    [string]$Version = "0.1.0-public-test.8",
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot "build\windows-psycross\$Configuration"
$distDir = Join-Path $repoRoot "dist"
$packageName = "SyphonFilterPC-$Version-win64"
$packageDir = Join-Path $distDir $packageName
$archivePath = Join-Path $distDir "$packageName.zip"
$archiveHashPath = "$archivePath.sha256"

foreach ($path in @($packageDir, $archivePath, $archiveHashPath)) {
    if (Test-Path -LiteralPath $path) {
        throw "Refusing to overwrite an existing release artifact: $path"
    }
}

$runtimeFiles = @(
    "syphon_filter.exe",
    "avcodec-62.dll",
    "avformat-62.dll",
    "avutil-60.dll",
    "fmt.dll",
    "OpenAL32.dll",
    "SDL2.dll",
    "swresample-6.dll",
    "swscale-9.dll"
)

foreach ($file in $runtimeFiles) {
    $source = Join-Path $buildDir $file
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Required runtime file is missing: $source"
    }
}

$vcRedistRoots = [Collections.Generic.List[string]]::new()
if ($env:VCToolsRedistDir) {
    $vcRedistRoots.Add($env:VCToolsRedistDir)
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
    $installations = & $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.Redist.14.Latest -property installationPath
    foreach ($installation in $installations) {
        if ($installation) {
            $vcRedistRoots.Add((Join-Path $installation "VC\Redist\MSVC"))
        }
    }
}

$vcRuntimeDir = $vcRedistRoots |
    Where-Object { Test-Path -LiteralPath $_ -PathType Container } |
    ForEach-Object {
        Get-ChildItem -LiteralPath $_ -Recurse -Filter "vcruntime140.dll" -File -ErrorAction SilentlyContinue
    } |
    Where-Object { $_.FullName -match "\\x64\\Microsoft\.VC\d+\.CRT\\vcruntime140\.dll$" -and $_.FullName -notmatch "\\onecore\\" } |
    Sort-Object FullName -Descending -Unique |
    Select-Object -First 1 -ExpandProperty DirectoryName

if (-not $vcRuntimeDir) {
    throw "Microsoft Visual C++ x64 runtime was not found. Install the VC++ workload or set VCToolsRedistDir."
}

$vcFiles = @(
    "msvcp140.dll",
    "msvcp140_2.dll",
    "msvcp140_atomic_wait.dll",
    "vcruntime140.dll",
    "vcruntime140_1.dll"
)

New-Item -ItemType Directory -Path $packageDir | Out-Null
New-Item -ItemType Directory -Path (Join-Path $packageDir "licenses") | Out-Null

foreach ($file in $runtimeFiles) {
    Copy-Item -LiteralPath (Join-Path $buildDir $file) -Destination $packageDir
}

foreach ($file in $vcFiles) {
    $source = Join-Path $vcRuntimeDir $file
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Required Visual C++ runtime file is missing: $source"
    }
    Copy-Item -LiteralPath $source -Destination $packageDir
}

Copy-Item -LiteralPath (Join-Path $repoRoot "LICENSE") -Destination (Join-Path $packageDir "LICENSE.txt")
Copy-Item -LiteralPath (Join-Path $repoRoot "external\PsyCross\LICENSE") -Destination (Join-Path $packageDir "licenses\PsyCross.txt")

$vcpkgShare = Join-Path $repoRoot "build\windows-psycross\vcpkg_installed\x64-windows\share"
$licenseSources = @{
    "FFmpeg.txt" = Join-Path $vcpkgShare "ffmpeg\copyright"
    "fmt.txt" = Join-Path $vcpkgShare "fmt\copyright"
    "OpenAL-Soft.txt" = Join-Path $vcpkgShare "openal-soft\copyright"
    "SDL2.txt" = Join-Path $vcpkgShare "sdl2\copyright"
}

foreach ($entry in $licenseSources.GetEnumerator()) {
    if (-not (Test-Path -LiteralPath $entry.Value -PathType Leaf)) {
        throw "Required license file is missing: $($entry.Value)"
    }
    Copy-Item -LiteralPath $entry.Value -Destination (Join-Path $packageDir "licenses\$($entry.Key)")
}

$buildDate = Get-Date -Format "yyyy-MM-dd"

$readme = @"
SYPHON FILTER PC — $Version
Windows x64, $buildDate

This package does not contain the game or a disc image. It requires a legally
obtained Syphon Filter USA v1.1 BIN/CUE image (SCUS-94240).

1. Extract the ZIP into a new directory.
2. Run syphon_filter.exe.
3. Select BROWSE and choose the CUE file.
4. Configure graphics and controls, then select DEPLOY.

Keep every BIN file beside the CUE file. Saves and launcher settings are stored
under %LOCALAPPDATA%\SyphonFilterPC.

Do not send game images with bug reports. Include only reproduction steps,
hardware information, screenshots and generated logs that contain no game data.

This is an unofficial compatibility runtime. It is not affiliated with or
endorsed by Sony Interactive Entertainment or Bend Studio.
"@

$notes = @"
PUBLIC TEST $Version
====================

This source-only distribution excludes localization packs, game-derived
assets, character artwork, dossier screens, saves and game images.
"@

$commit = try { (& git -C $repoRoot rev-parse --short HEAD 2>$null).Trim() } catch { "unknown" }
if (-not $commit) { $commit = "unknown" }

$buildInfo = @"
Product: Syphon Filter PC
Channel: Public Test
Version: $Version
Platform: Windows x64
Build type: $Configuration
Build date: $buildDate
Source revision: $commit
Supported disc: Syphon Filter USA v1.1, SCUS-94240, BIN/CUE
Launcher: integrated; no CMD bootstrap
Game image included: no
Save data included: no
Cheat marker included: no
"@

$notices = @"
This package includes or dynamically links third-party software.

FFmpeg — LGPL/GPL components as built by the package toolchain.
SDL2 — zlib license.
OpenAL Soft — LGPL license.
fmt — MIT license.
PsyCross — MIT license.
Microsoft Visual C++ Runtime — redistributed under the applicable Microsoft
Visual Studio license terms.

See the licenses directory for the supplied license texts.
"@

$utf8 = New-Object System.Text.UTF8Encoding($true)
[IO.File]::WriteAllText((Join-Path $packageDir "README_FIRST.txt"), $readme, $utf8)
[IO.File]::WriteAllText((Join-Path $packageDir "PUBLIC_TEST_NOTES.txt"), $notes, $utf8)
[IO.File]::WriteAllText((Join-Path $packageDir "BUILD_INFO.txt"), $buildInfo, $utf8)
[IO.File]::WriteAllText((Join-Path $packageDir "THIRD_PARTY_NOTICES.txt"), $notices, $utf8)

$forbidden = Get-ChildItem -LiteralPath $packageDir -Recurse -File | Where-Object {
    $_.Name -match "(?i)(syphon_filter_cheats|save|\.sav(?:\.bak)?$|\.cue$|\.bin$|\.iso$|\.img$|\.chd$|\.cmd$|\.log$|\.dmp$|\.obj$|\.pdb$|\.ilk$|\.lib$|\.exp$)"
}
if ($forbidden) {
    throw "Forbidden files found in release: $($forbidden.FullName -join ', ')"
}

$hashLines = Get-ChildItem -LiteralPath $packageDir -Recurse -File |
    Where-Object { $_.Name -ne "SHA256SUMS.txt" } |
    Sort-Object FullName |
    ForEach-Object {
        $relative = $_.FullName.Substring($packageDir.Length + 1).Replace("\", "/")
        $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash.ToLowerInvariant()
        "$hash  $relative"
    }
[IO.File]::WriteAllLines((Join-Path $packageDir "SHA256SUMS.txt"), $hashLines, $utf8)

Compress-Archive -LiteralPath $packageDir -DestinationPath $archivePath -CompressionLevel Optimal
$archiveHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $archivePath).Hash.ToLowerInvariant()
[IO.File]::WriteAllText($archiveHashPath, "$archiveHash  $packageName.zip`r`n", $utf8)

Write-Output $packageDir
Write-Output $archivePath
Write-Output $archiveHashPath
