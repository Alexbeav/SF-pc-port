param(
    [string]$Version = "0.1.0-phase1-baseline.1",
    [string]$Configuration = "Release",
    [string]$BuildDirectory = "",
    [string]$DependencyRoot = "",
    [Parameter(Mandatory = $true)]
    [string]$BuildReceiptPath
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = if ($BuildDirectory) { $BuildDirectory } else { Join-Path $repoRoot "build\windows-psycross" }
$buildDir = Join-Path $buildRoot $Configuration
$dependencyRoot = if ($DependencyRoot) { $DependencyRoot } else { Join-Path $repoRoot "build\windows-psycross\vcpkg_installed" }
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

if (-not (Test-Path -LiteralPath $BuildReceiptPath -PathType Leaf)) {
    throw "Build provenance receipt is missing: $BuildReceiptPath"
}
$buildReceipt = Get-Content -Raw -LiteralPath $BuildReceiptPath | ConvertFrom-Json
$sourceRevision = (& git -C $repoRoot rev-parse HEAD).Trim()
if ($buildReceipt.schema_version -ne "sf1-phase1-build-provenance-v1" -or
    $buildReceipt.source_revision -ne $sourceRevision) {
    throw "Build provenance does not match the exact package source revision."
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

$localeSource = Join-Path $buildDir "locales"
$localeDestination = Join-Path $packageDir "locales"
if (-not (Test-Path -LiteralPath $localeSource -PathType Container)) {
    throw "Required language-pack directory is missing: $localeSource"
}
Copy-Item -LiteralPath $localeSource -Destination $localeDestination -Recurse

foreach ($file in @(
    "ru-vit\manifest.txt",
    "ru-vit\briefings.dat",
    "ru-vit\mission_menu.dat",
    "ru-vit\WEAPDESC.TXT",
    "ru-vit\fonts\FONTA.TIM",
    "ru-vit\fonts\FONTB.TIM",
    "ru-vit\fonts\FONTC.TIM"
)) {
    $path = Join-Path $localeDestination $file
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required Russian language-pack file is missing: $path"
    }
}

$dossierSource = Join-Path $buildDir "assets\dossiers\screens"
$dossierDestination = Join-Path $packageDir "assets\dossiers\screens"
New-Item -ItemType Directory -Path $dossierDestination | Out-Null
foreach ($file in @("dossier_01.png", "dossier_02.png", "dossier_03.png", "dossier_04.png")) {
    $source = Join-Path $dossierSource $file
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Required dossier image is missing: $source"
    }
    Copy-Item -LiteralPath $source -Destination $dossierDestination
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

$vcpkgShare = Join-Path $dependencyRoot "x64-windows\share"
$licenseSources = @{
    "FFmpeg.txt" = Join-Path $vcpkgShare "ffmpeg\copyright"
    "fmt.txt" = Join-Path $vcpkgShare "fmt\copyright"
    "OpenAL-Soft.txt" = Join-Path $vcpkgShare "openal-soft\copyright"
    "Industry-Font-COPYRIGHT.txt" = Join-Path $repoRoot "tools\fonts\industry\COPYRIGHT.txt"
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

ВАЖНО
======
Пакет не содержит игру или образ диска. Нужна ваша легальная копия
Syphon Filter USA v1.1: SCUS-94240, BIN/CUE.

ЗАПУСК
======
1. Распакуйте ZIP в отдельную папку.
2. Запустите syphon_filter.exe.
3. Нажмите BROWSE и выберите CUE-файл образа игры.
4. Настройте графику и управление, затем нажмите DEPLOY.

Русская локализация выбирается в поле TEXT LANGUAGE. Пакет содержит только
текст, шрифты и локализованные элементы интерфейса; озвучка, музыка и FMV
остаются оригинальными.

BIN-файлы должны оставаться рядом с CUE-файлом согласно его содержимому.
Путь к образу запоминается лаунчером. CMD-скрипт не требуется.

ЧИСТЫЙ СТАРТ
=============
В пакете нет сохранений, настроек, образов игры и файла syphon_filter_cheats.
Выбор миссий открывается только игровым прогрессом. Developer-функции доступны
только если пользователь самостоятельно создаст файл syphon_filter_cheats рядом с EXE.

СИСТЕМНЫЕ ТРЕБОВАНИЯ
====================
- Windows 10/11 x64.
- Видеокарта и драйвер с OpenGL 3.x.
- Syphon Filter USA v1.1 (SCUS-94240), BIN/CUE.
- Клавиатура/мышь или совместимый геймпад.

СОХРАНЕНИЯ И НАСТРОЙКИ
======================
Пользовательские сохранения и настройки хранятся в:
%LOCALAPPDATA%\SyphonFilterPC

ОТЧЁТ ОБ ОШИБКЕ
================
Укажите номер миссии, шаги воспроизведения, модель видеокарты и приложите
скриншот и созданный рядом с EXE файл *.log. Не отправляйте BIN/CUE-файлы.

This is an unofficial work-in-progress compatibility runtime. It is not
affiliated with or endorsed by Sony Interactive Entertainment.
"@

# Windows PowerShell 5 reads UTF-8 scripts without a BOM through the active
# ANSI code page. Recover the original UTF-8 literals when that happens while
# leaving PowerShell 7 and UTF-8 system locales untouched.
$utf8LiteralProbe = -join @(
    [char]0x0412,
    [char]0x0410,
    [char]0x0416,
    [char]0x041D,
    [char]0x041E
)
$repairScriptLiterals = -not $readme.Contains($utf8LiteralProbe)
if ($repairScriptLiterals) {
    $readme = [Text.Encoding]::UTF8.GetString([Text.Encoding]::Default.GetBytes($readme))
}

$notes = @"
PUBLIC TEST $Version
=====================

- Восстановлена оригинальная затемнённость уровней: постоянные нативные
  источники больше не пересвечивают запечённую геометрию PS1; вспышки, огонь,
  взрывы и фонарик продолжают динамически освещать окружение.
- Увеличена дистанция представления на одно портальное кольцо. Дополнительный
  чанк подгружается только для рендера, плавно проявляется за PS1 depth cue и
  не меняет коллизии, объекты или авторитетное состояние оригинальной игры.
- Ламповые ореолы и светящиеся билборды всегда остаются яркими, а корпуса
  источников корректно получают освещение и туман.
- Исправлена маршрутизация огня, дыма и взрывов: оригинальные SPFX-пакеты и
  нативный fallback больше не дублируются, не исчезают вдали и сохраняют
  корректный размер.
- HUD и letterbox получили упорядоченные плавные переходы, синхронизированные
  с оригинальным viewport/радиосостоянием без случайных срабатываний от
  прицеливания, выстрелов или смены чанка.
- Сохранена оригинальная retail-резидентность моделей во всех 20 миссиях;
  устранена регрессия, при которой presentation-lookahead мог активировать
  лишние игровые объекты или вытеснять always-resident модели.
- Расширены тесты динамического света, глубинного тумана, плавной подгрузки,
  SPFX, HUD/letterbox и R3000 runtime.
- Проведена полная проверка MSVC/PsyCross, всех поддерживаемых ROM-проб,
  манифеста, внутренних хешей и запрещённых файлов релизного архива.

Архив не содержит образ игры, сохранения, настройки или syphon_filter_cheats.
"@

if ($repairScriptLiterals) {
    $notes = [Text.Encoding]::UTF8.GetString([Text.Encoding]::Default.GetBytes($notes))
}

$releaseNotesPath = Join-Path $repoRoot "docs\releases\$Version.md"
if (Test-Path -LiteralPath $releaseNotesPath -PathType Leaf) {
    $notes = [IO.File]::ReadAllText($releaseNotesPath, [Text.Encoding]::UTF8)
}

$commit = try { (& git -C $repoRoot rev-parse HEAD 2>$null).Trim() } catch { "unknown" }
if (-not $commit) { $commit = "unknown" }

$buildInfo = @"
Product: Syphon Filter PC
Channel: Phase 1 launcher-ready baseline
Version: $Version
Platform: Windows x64
Build type: $Configuration
Build date: $buildDate
Source revision: $commit
Selected source base: c24ce313b1356da2e3d5615f3001f6000e399f99
Supported disc: Syphon Filter USA v1.1, SCUS-94240, BIN/CUE
Retail executable SHA-256: bac292061ad5bc718ce137ef5b43d3d7e9b1b65248fb0d52229f328ccfe4ab4e
Launcher: integrated; no CMD bootstrap
Game image included: no
Save data included: no
Cheat marker included: no
BIOS policy: not consumed by this native runtime; no BIOS is included
Fast boot policy: unavailable; there is no BIOS boot path to bypass
Faithful defaults: 640x480, 4:3, 20 Hz, MSAA/filtering/PGXP/mouse-look off
"@

$notices = @"
This package includes or dynamically links third-party software.

FFmpeg — LGPL/GPL components as built by the package toolchain.
SDL2 — zlib license.
OpenAL Soft — LGPL license.
fmt — MIT license.
PsyCross — MIT license.
Industry Bold (RUS by Slavchansky) — used to generate the Russian font atlas;
the source TTF is not included in this package.
Microsoft Visual C++ Runtime — redistributed under the applicable Microsoft
Visual Studio license terms.

See the licenses directory for the supplied license texts.
"@

if ($repairScriptLiterals) {
    $notices = [Text.Encoding]::UTF8.GetString([Text.Encoding]::Default.GetBytes($notices))
}

$utf8 = New-Object System.Text.UTF8Encoding($true)
[IO.File]::WriteAllText((Join-Path $packageDir "README_FIRST.txt"), $readme, $utf8)
[IO.File]::WriteAllText((Join-Path $packageDir "PUBLIC_TEST_NOTES.txt"), $notes, $utf8)
[IO.File]::WriteAllText((Join-Path $packageDir "BUILD_INFO.txt"), $buildInfo, $utf8)
[IO.File]::WriteAllText((Join-Path $packageDir "THIRD_PARTY_NOTICES.txt"), $notices, $utf8)
Copy-Item -LiteralPath $BuildReceiptPath -Destination (Join-Path $packageDir "BASELINE_BUILD_PROVENANCE.json")

$packagedExecutable = Join-Path $packageDir "syphon_filter.exe"
$packagedExecutableHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $packagedExecutable).Hash.ToLowerInvariant()
if ($buildReceipt.executable.sha256 -ne $packagedExecutableHash) {
    throw "Packaged executable does not match the build provenance receipt."
}
$buildReceiptHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $BuildReceiptPath).Hash.ToLowerInvariant()
$payloadLines = Get-ChildItem -LiteralPath $packageDir -Recurse -File |
    Sort-Object FullName |
    ForEach-Object {
        $relative = $_.FullName.Substring($packageDir.Length + 1).Replace("\", "/")
        $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash.ToLowerInvariant()
        "$hash  $relative"
    }
$payloadBytes = $utf8.GetBytes(($payloadLines -join "`n") + "`n")
$payloadHasher = [Security.Cryptography.SHA256]::Create()
try {
    $payloadIdentity = ([Convert]::ToHexString($payloadHasher.ComputeHash($payloadBytes))).ToLowerInvariant()
} finally {
    $payloadHasher.Dispose()
}
$identity = @"
Title: Syphon Filter
Region: USA / NTSC-U
Disc serial: SCUS-94240
Disc revision: v1.1
Retail executable SHA-256: bac292061ad5bc718ce137ef5b43d3d7e9b1b65248fb0d52229f328ccfe4ab4e
Selected source base: c24ce313b1356da2e3d5615f3001f6000e399f99
Prepared source revision: $commit
Build provenance SHA-256: $buildReceiptHash
Executable SHA-256: $packagedExecutableHash
Package payload identity: $payloadIdentity
BIOS policy: not-consumed-native-runtime (no BIOS required or packaged)
Fast boot policy: unavailable-no-bios-boot-path
Faithful defaults: 640x480; 4:3; 20 Hz; MSAA/filtering/PGXP/mouse-look off
Controller: retail-style gamepad remains available
Keyboard/mouse: remappable; optional chase mouse-look remains default-off
Claim: launcher-ready baseline only; no campaign, enhancement, full-game, or release-readiness claim
"@
[IO.File]::WriteAllText((Join-Path $packageDir "BASELINE_IDENTITY.txt"), $identity, $utf8)

$forbidden = Get-ChildItem -LiteralPath $packageDir -Recurse -File | Where-Object {
    $_.Name -match "(?i)(syphon_filter_cheats|save|bios|scph[-_ ]?[0-9]|\.sav(?:\.bak)?$|\.cue$|\.bin$|\.iso$|\.img$|\.chd$|\.cmd$|\.log$|\.dmp$|\.obj$|\.pdb$|\.ilk$|\.lib$|\.exp$)"
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
