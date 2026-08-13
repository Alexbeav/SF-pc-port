param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,
    [Parameter(Mandatory = $true)]
    [string]$DependencyRoot,
    [string]$CMakePath = "cmake",
    [string]$VcpkgRoot = "C:\vcpkg"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$selectedBase = "c24ce313b1356da2e3d5615f3001f6000e399f99"

if (Test-Path -LiteralPath $OutputDirectory) {
    throw "Refusing to reuse occupied build directory: $OutputDirectory"
}
if (-not (Test-Path -LiteralPath (Join-Path $DependencyRoot "vcpkg\status") -PathType Leaf)) {
    throw "Preverified dependency closure is incomplete: $DependencyRoot"
}
foreach ($relative in @(
    "x64-windows\include\SDL2\SDL.h",
    "x64-windows\bin\SDL2.dll",
    "x64-windows\bin\OpenAL32.dll",
    "x64-windows\lib\avcodec.lib",
    "x64-windows\share\ffmpeg\copyright"
)) {
    if (-not (Test-Path -LiteralPath (Join-Path $DependencyRoot $relative) -PathType Leaf)) {
        throw "Required offline dependency is missing: $relative"
    }
}
$toolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
if (-not (Test-Path -LiteralPath $toolchain -PathType Leaf)) {
    throw "Pinned vcpkg toolchain is missing: $toolchain"
}
if ((& git -C $repoRoot status --porcelain --untracked-files=all)) {
    throw "Source worktree must be clean before the reproducible build."
}
& git -C $repoRoot merge-base --is-ancestor $selectedBase HEAD
if ($LASTEXITCODE -ne 0) {
    throw "Source revision does not descend from selected SF1 base $selectedBase"
}

New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
& $CMakePath -S $repoRoot -B $OutputDirectory -G "Visual Studio 17 2022" -A x64 `
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
    "-DVCPKG_INSTALLED_DIR=$DependencyRoot" `
    "-DVCPKG_MANIFEST_INSTALL=OFF" `
    "-DSF_ENABLE_PSYCROSS=ON" `
    "-DSF_BUILD_TESTS=ON" `
    "-DSF_BUILD_ROM_PROBES=OFF" `
    "-DSF_WARNINGS_AS_ERRORS=ON"
if ($LASTEXITCODE -ne 0) { throw "Offline CMake configure failed." }
& $CMakePath --build $OutputDirectory --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw "Offline Release build failed." }

Write-Output (Join-Path $OutputDirectory "Release\syphon_filter.exe")
