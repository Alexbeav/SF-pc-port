param(
    [Parameter(Mandatory = $true)]
    [string]$Disc1Cue,

    [Parameter(Mandatory = $true)]
    [string]$Disc2Cue,

    [string]$Configuration = "Release",

    [ValidateSet("quickstate", "combat")]
    [string[]]$Modes = @("quickstate", "combat"),

    [ValidateRange(1, 21)]
    [int[]]$Missions = (1..21),

    [ValidateRange(1200, 100000)]
    [int]$Frames = 3000,

    [string]$OutputDirectory
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$tool = Join-Path $repoRoot "build\windows-psycross\$Configuration\sf_tool.exe"
if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) {
    throw "SF2 validation tool is missing: $tool"
}

$disc1 = (Resolve-Path -LiteralPath $Disc1Cue).Path
$disc2 = (Resolve-Path -LiteralPath $Disc2Cue).Path
if (-not $OutputDirectory) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputDirectory = Join-Path $repoRoot "build\sf2-validation-$stamp"
} elseif (-not [IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot $OutputDirectory
}
if (Test-Path -LiteralPath $OutputDirectory) {
    throw "Refusing to overwrite an existing validation directory: $OutputDirectory"
}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null

function Read-Count {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Line,
        [Parameter(Mandatory = $true)]
        [string]$Pattern,
        [Parameter(Mandatory = $true)]
        [string]$Name
    )
    $match = [regex]::Match($Line, $Pattern)
    if (-not $match.Success) {
        throw "Mission output omitted $Name"
    }
    return [uint64]$match.Groups[1].Value
}

$results = [Collections.Generic.List[object]]::new()
foreach ($mode in $Modes) {
    foreach ($mission in ($Missions | Sort-Object -Unique)) {
        $missionIndex = $mission - 1
        $cue = if ($mission -le 8) { $disc1 } else { $disc2 }
        $logPath = Join-Path $OutputDirectory (
            "mission-{0:D2}-{1}.log" -f $mission, $mode)

        $output = & $tool probe-sf2-product-runtime `
            $cue $Frames $mode $missionIndex 2>&1
        $exitCode = $LASTEXITCODE
        $output | Set-Content -LiteralPath $logPath -Encoding utf8
        if ($exitCode -ne 0) {
            throw "Mission $mission $mode failed with exit code $exitCode. See $logPath"
        }

        $completed = $output |
            ForEach-Object { $_.ToString() } |
            Where-Object { $_ -like "SF2 product runtime completed:*" } |
            Select-Object -Last 1
        if (-not $completed) {
            throw "Mission $mission $mode produced no completion record. See $logPath"
        }

        $checkpointRestores = Read-Count $completed `
            'checkpoint-restores=(\d+)' 'checkpoint restore count'
        $collisionGaps = Read-Count $completed `
            'collision-gaps=(\d+)/' 'collision gap count'
        $rendererRepairs = Read-Count $completed `
            'renderer-repairs=(\d+):' 'renderer repair count'
        $rejectedOts = Read-Count $completed `
            'rejected-renderer-ots=(\d+):' 'rejected ordering-table count'
        $rejectedMerges = Read-Count $completed `
            'rejected-renderer-merges=(\d+):' 'rejected list-merge count'
        $rejectedBanks = Read-Count $completed `
            'rejected-sound-banks=(\d+):' 'rejected sound-bank count'
        $rejectedVoices = Read-Count $completed `
            'rejected-sound-voices=(\d+):' 'rejected sound-voice count'
        $scriptLevelStarts = Read-Count $completed `
            'scripts=\d+:\d+/\d+@\d+/preactive=\d+/1/(\d+)/' `
            'retail LEVEL start count'

        if ($collisionGaps -ne 0 -or $rendererRepairs -ne 0 -or
            $rejectedOts -ne 0 -or $rejectedMerges -ne 0) {
            throw "Mission $mission $mode crossed a forbidden runtime containment path. See $logPath"
        }
        # AIRBASE and LABS2 retain localized stale retail sound allocations.
        # Their guest calls are safely rejected; every other mission must keep
        # both counters at zero so a new audio-lifetime regression fails this
        # release gate instead of being hidden in a long diagnostic line.
        if (($rejectedBanks -ne 0 -or $rejectedVoices -ne 0) -and
            $mission -notin @(2, 18)) {
            throw "Mission $mission $mode introduced an unexpected sound containment. See $logPath"
        }
        if ($scriptLevelStarts -ne 1) {
            throw "Mission $mission $mode restarted the retail LEVEL program. See $logPath"
        }

        $results.Add([pscustomobject]@{
            Mission = $mission
            Mode = $mode
            Frames = $Frames
            CheckpointRestores = $checkpointRestores
            CollisionGaps = $collisionGaps
            RendererRepairs = $rendererRepairs
            RejectedOrderingTables = $rejectedOts
            RejectedListMerges = $rejectedMerges
            RejectedSoundBanks = $rejectedBanks
            RejectedSoundVoices = $rejectedVoices
            ScriptLevelStarts = $scriptLevelStarts
            Log = [IO.Path]::GetFileName($logPath)
        })
        Write-Host ("Mission {0:D2} {1}: PASS (restores={2}, sound={3}/{4})" -f `
            $mission, $mode, $checkpointRestores, $rejectedBanks,
            $rejectedVoices)
    }
}

$summaryPath = Join-Path $OutputDirectory "summary.csv"
$results | Export-Csv -LiteralPath $summaryPath -NoTypeInformation
Write-Host "SF2 validation matrix: PASS ($($results.Count) routes)"
Write-Host "Summary: $summaryPath"
