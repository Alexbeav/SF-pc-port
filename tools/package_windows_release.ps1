param(
    [string]$Version = "0.1.0-sf2-guest-alpha.3",
    [string]$Configuration = "Release",
    [switch]$Sf2GuestAlpha
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

$readme = if ($Sf2GuestAlpha) {
@"
SYPHON FILTER 2 PC — $Version
EXPERIMENTAL GUEST-RUNTIME ALPHA
Windows x64, $buildDate

This package does not contain the game or a disc image. It requires legally
obtained Syphon Filter 2 USA BIN/CUE images. Missions 1-8 use Disc 1;
missions 9-21 use Disc 2.

INSTALLATION
============

1. Extract the complete ZIP into a new writable directory.
2. Keep every BIN file beside the CUE file according to the CUE contents.
3. Open PowerShell in the extracted directory.
4. Create the required empty developer marker:

   New-Item -ItemType File -Path .\syphon_filter_cheats

5. Start a mission, replacing N and the example path. Use Disc 1 for missions
   1-8 and Disc 2 for missions 9-21:

   .\syphon_filter.exe --no-launcher --mission=N --scene-test "D:\PS1\Syphon Filter 2 (USA) (Disc 1).cue"

The ordinary launcher remains the Syphon Filter 1 product launcher and does
not expose this experimental SF2 path. Do not select an SF2 image there.

CURRENT SCOPE
=============

- All 21 mission packages boot into their authored in-engine openings through
  direct mission launch. Mission 3 is verified finishable through its save
  menu and following cinematic; campaign flow is not implemented.
- Controls, combat, doors, climbing, weapons, dialogue, sound effects, death,
  checkpoint restart and in-session F5/F9 quick states are functional.
- The retail HUD, text, weapon artwork, radar actors/threat cones and
  widescreen in-engine cinematic bars render through the guest GPU bridge.
- Music and full completion of missions other than Mission 3 are not
  validated.
- A later scripted conversation can pause unevenly or fail to release player
  control; C skips it.
- Collision/room residency can still intermittently allow Gabe to fall
  through the floor. The room-14 floor-request guard in this build needs
  broader interactive testing.
- P reaches the retail pause/map input, but that transition is incomplete and
  can hide Gabe's model after closing; avoid P in this build.

Controls use the existing PC bindings. Mouse aiming works in first person;
A/D strafe and C crouches. Mouse wheel and bracket keys select previous/next
weapons, middle click advances once, and number keys select owned weapon slots.

BUG REPORTS
===========

Include reproduction steps, approximate location, the last dialogue line,
screenshots and the generated log beside the executable. Do not upload or send
BIN/CUE files or other copyrighted game data.

Settings are stored under %LOCALAPPDATA%\SyphonFilterPC. The empty
syphon_filter_cheats marker enables developer-only launch overrides; remove it
when returning to an ordinary SF1 installation if both packages share a folder.

This is an unofficial compatibility-runtime experiment. It is not affiliated
with or endorsed by Sony Interactive Entertainment or Bend Studio.
"@
} else {
@"
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
}

$notes = if ($Sf2GuestAlpha) {
@"
EXPERIMENTAL SF2 GUEST ALPHA $Version
=====================================

This build runs retail Syphon Filter 2 R3000A gameplay inside the native PC
runtime and presents the guest GPU/SPU output through PsyCross. All 21 missions
can be launched directly and Mission 3 is verified finishable, but this is a
testing build rather than a complete campaign port.

The archive contains no game image, save, settings, extracted game assets or
syphon_filter_cheats marker. Read README_FIRST.txt before launching.
"@
} else {
@"
PUBLIC TEST $Version
====================

This source-only distribution excludes localization packs, game-derived
assets, character artwork, dossier screens, saves and game images.
"@
}

$commit = try { (& git -C $repoRoot rev-parse --short HEAD 2>$null).Trim() } catch { "unknown" }
if (-not $commit) { $commit = "unknown" }

$channel = if ($Sf2GuestAlpha) { "Experimental SF2 Guest Alpha" } else { "Public Test" }
$supportedDisc = if ($Sf2GuestAlpha) {
    "Syphon Filter 2 USA Disc 1 and Disc 2, BIN/CUE"
} else {
    "Syphon Filter USA v1.1, SCUS-94240, BIN/CUE"
}
$launcher = if ($Sf2GuestAlpha) {
    "SF2 alpha uses the documented direct command line"
} else {
    "integrated; no CMD bootstrap"
}

$buildInfo = @"
Product: Syphon Filter PC
Channel: $channel
Version: $Version
Platform: Windows x64
Build type: $Configuration
Build date: $buildDate
Source revision: $commit
Supported disc: $supportedDisc
Launcher: $launcher
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
