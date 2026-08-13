param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDirectory,
    [Parameter(Mandatory = $true)]
    [string]$DependencyRoot,
    [Parameter(Mandatory = $true)]
    [string]$OutputPath,
    [string]$CMakePath = "cmake",
    [string]$VcpkgPath = "C:\vcpkg\vcpkg.exe"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$utf8 = New-Object System.Text.UTF8Encoding($false)

function Get-Sha256([string]$Path) {
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
}

function Get-TreeIdentity([string]$Root) {
    $lines = Get-ChildItem -LiteralPath $Root -Recurse -File |
        Sort-Object FullName |
        ForEach-Object {
            $relative = $_.FullName.Substring($Root.Length).TrimStart("\").Replace("\", "/")
            "$(Get-Sha256 $_.FullName)  $relative"
        }
    $bytes = $utf8.GetBytes(($lines -join "`n") + "`n")
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([Convert]::ToHexString($sha.ComputeHash($bytes))).ToLowerInvariant() }
    finally { $sha.Dispose() }
}

if (Test-Path -LiteralPath $OutputPath) {
    throw "Refusing to overwrite build receipt: $OutputPath"
}
$executable = Join-Path $BuildDirectory "Release\syphon_filter.exe"
$cache = Join-Path $BuildDirectory "CMakeCache.txt"
$status = Join-Path $DependencyRoot "vcpkg\status"
foreach ($path in @($executable, $cache, $status, $VcpkgPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required receipt input is missing: $path"
    }
}
if ((& git -C $repoRoot status --porcelain --untracked-files=all)) {
    throw "Source worktree must be clean before receipt generation."
}
$sourceRevision = (& git -C $repoRoot rev-parse HEAD).Trim()
$sourceTree = (& git -C $repoRoot rev-parse "HEAD^{tree}").Trim()
$cmakeCommand = Get-Command $CMakePath -ErrorAction Stop
$cmakeVersion = (& $cmakeCommand.Source --version | Select-Object -First 1).Trim()
$vcpkgVersion = (& $VcpkgPath version | Select-Object -First 1).Trim()
$vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = if (Test-Path -LiteralPath $vswhere) {
    (& $vswhere -latest -products * -format json | ConvertFrom-Json | Select-Object -First 1)
} else { $null }
$cl = Get-ChildItem "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC" -Directory |
    Sort-Object Name -Descending | Select-Object -First 1 |
    ForEach-Object { Join-Path $_.FullName "bin\Hostx64\x64\cl.exe" }
if (-not $cl -or -not (Test-Path -LiteralPath $cl -PathType Leaf)) {
    throw "MSVC compiler executable was not found."
}

$receipt = [ordered]@{
    schema_version = "sf1-phase1-build-provenance-v1"
    title = "Syphon Filter"
    region = "USA / NTSC-U"
    disc_serial = "SCUS-94240"
    disc_revision = "v1.1"
    retail_executable_sha256 = "bac292061ad5bc718ce137ef5b43d3d7e9b1b65248fb0d52229f328ccfe4ab4e"
    selected_source_base = "c24ce313b1356da2e3d5615f3001f6000e399f99"
    source_revision = $sourceRevision
    source_tree = $sourceTree
    toolchain = [ordered]@{
        cmake_version = $cmakeVersion
        cmake_sha256 = Get-Sha256 $cmakeCommand.Source
        visual_studio_version = if ($vs) { $vs.installationVersion } else { $null }
        msvc_file_version = (Get-Item -LiteralPath $cl).VersionInfo.FileVersion
        msvc_sha256 = Get-Sha256 $cl
        vcpkg_version = $vcpkgVersion
        vcpkg_sha256 = Get-Sha256 $VcpkgPath
        vcpkg_manifest_sha256 = Get-Sha256 (Join-Path $repoRoot "vcpkg.json")
        dependency_status_sha256 = Get-Sha256 $status
        dependency_tree_sha256 = Get-TreeIdentity $DependencyRoot
    }
    configuration = [ordered]@{
        generator = "Visual Studio 17 2022"
        architecture = "x64"
        build_type = "Release"
        cmake_cache_sha256 = Get-Sha256 $cache
        vcpkg_manifest_install = $false
        network_dependency_acquisition = "disabled"
        sf_enable_psycross = $true
        sf_build_tests = $true
        sf_build_rom_probes = $false
        warnings_as_errors = $true
    }
    baseline_policy = [ordered]@{
        bios = "not-consumed-native-runtime"
        fast_boot = "unavailable-no-bios-boot-path"
        aspect_ratio = "4:3"
        presentation_hz = 20
        msaa = 0
        bilinear_filtering = $false
        anisotropic_filtering = $false
        pgxp_geometry = $false
        mouse_chase_look = $false
    }
    executable = [ordered]@{
        name = "syphon_filter.exe"
        size = (Get-Item -LiteralPath $executable).Length
        sha256 = Get-Sha256 $executable
    }
}
[IO.File]::WriteAllText($OutputPath, ($receipt | ConvertTo-Json -Depth 8) + "`n", $utf8)
Write-Output $OutputPath
