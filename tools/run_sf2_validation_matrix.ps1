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

    [string]$OutputDirectory,

    [switch]$SkipCompletionFlow
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

# COLO's early-death behavior is a lifecycle gate, not a per-frame checksum:
# while its authored parachute opening owns the clean-start flag, retail
# restore must yield a same-mission package reconstruction rather than consume
# a manufactured checkpoint stream.
$cleanRestartLog = Join-Path $OutputDirectory "mission-01-clean-restart.log"
$cleanRestartOutput = & $tool probe-sf2-clean-restart $disc1 2>&1
$cleanRestartExit = $LASTEXITCODE
$cleanRestartOutput | Set-Content -LiteralPath $cleanRestartLog -Encoding utf8
if ($cleanRestartExit -ne 0) {
    throw "Mission 1 clean-restart ownership failed with exit code $cleanRestartExit. See $cleanRestartLog"
}

# Opening-transfer packages consume authored attachment/vehicle state before
# a safe frontend baseline exists. Require a host-owned fresh generation and
# the same transfer event in both generations for every known package using
# either retail event family.
foreach ($openingMission in @(1, 5, 6, 8, 17)) {
    $openingIndex = $openingMission - 1
    $openingCue = if ($openingMission -le 8) { $disc1 } else { $disc2 }
    $openingRestartLog = Join-Path $OutputDirectory (
        "mission-{0:D2}-opening-restart.log" -f $openingMission)
    $openingRestartOutput = & $tool probe-sf2-opening-restart `
        $openingCue $openingIndex 2>&1
    $openingRestartExit = $LASTEXITCODE
    $openingRestartOutput | Set-Content -LiteralPath $openingRestartLog -Encoding utf8
    if ($openingRestartExit -ne 0) {
        throw "Mission $openingMission fresh-generation opening restart failed with exit code $openingRestartExit. See $openingRestartLog"
    }
}

# MENU destroys application state 7 before its two outer lifecycle calls.
# Execute both loaded retail confirmation callbacks through that teardown and
# require the distinct host-owned outcome yields. Static call-site inspection
# alone previously missed an invalid post-teardown state predicate.
foreach ($pauseOutcome in @("restart", "savequit")) {
    $pauseLog = Join-Path $OutputDirectory "pause-$pauseOutcome.log"
    $pauseOutput = & $tool probe-sf2-pause-lifecycle $disc1 $pauseOutcome 2>&1
    $pauseExit = $LASTEXITCODE
    $pauseOutput | Set-Content -LiteralPath $pauseLog -Encoding utf8
    if ($pauseExit -ne 0) {
        throw "Pause lifecycle $pauseOutcome failed with exit code $pauseExit. See $pauseLog"
    }
}

# Exercise both retail optic families after guest inventory construction. H11
# is a line-based non-zoom reticle; the silenced sniper uses a sprite-heavy
# digital scope. The tool asserts their distinct packet compositions so a
# missing optic cannot pass as a merely stable gameplay frame.
foreach ($scopeMode in @("h11scope", "sniperscope")) {
    $scopeLog = Join-Path $OutputDirectory "$scopeMode.log"
    $scopeOutput = & $tool probe-sf2-product-runtime `
        $disc1 2200 $scopeMode 2>&1
    $scopeExit = $LASTEXITCODE
    $scopeOutput | Set-Content -LiteralPath $scopeLog -Encoding utf8
    if ($scopeExit -ne 0) {
        throw "SF2 $scopeMode presentation failed with exit code $scopeExit. See $scopeLog"
    }
}

# The frame-survival matrix cannot prove that the retail success shell selects
# the right movie/mission or that a newly booted package keeps its authored
# player state. Exercise that ownership separately for every selected mission.
# The outgoing snapshot is deliberately distinctive; the tool fails if any
# same-disc destination inherits it. Mission 8 -> 9 is owned by the native
# two-disc resolver and Mission 21 is terminal, so neither has a destination
# runtime under this single-CUE probe.
$completionResults = [Collections.Generic.List[object]]::new()
if (-not $SkipCompletionFlow) {
    foreach ($mission in ($Missions | Sort-Object -Unique)) {
        $missionIndex = $mission - 1
        $cue = if ($mission -le 8) { $disc1 } else { $disc2 }
        $logPath = Join-Path $OutputDirectory (
            "mission-{0:D2}-completeflow.log" -f $mission)
        $output = & $tool probe-sf2-product-runtime `
            $cue 5000 completeflow $missionIndex 2>&1
        $exitCode = $LASTEXITCODE
        $output | Set-Content -LiteralPath $logPath -Encoding utf8
        if ($exitCode -ne 0) {
            throw "Mission $mission completion flow failed with exit code $exitCode. See $logPath"
        }
        $completed = $output |
            ForEach-Object { $_.ToString() } |
            Where-Object { $_ -like "SF2 product runtime completed:*" } |
            Select-Object -Last 1
        if (-not $completed) {
            throw "Mission $mission completion flow produced no completion record. See $logPath"
        }
        $flow = [regex]::Match(
            $completed,
            'completion-flow=(\d+)/(\d+)/(\d+)/title=(\d+)/(\d+)/disc=(\d+)/catalog=(\d+)/next-authored=(\d+)')
        if (-not $flow.Success) {
            throw "Mission $mission completion flow omitted ownership fields. See $logPath"
        }
        $nextAuthored = [uint64]$flow.Groups[8].Value
        $expectsDestination = $mission -notin @(8, 21)
        if (($expectsDestination -and $nextAuthored -ne 1) -or
            (-not $expectsDestination -and $nextAuthored -ne 0)) {
            throw "Mission $mission completion flow reported invalid destination ownership. See $logPath"
        }
        $completionResults.Add([pscustomobject]@{
            Mission = $mission
            CampaignAdvanceCalls = [uint64]$flow.Groups[1].Value
            MovieRequestCalls = [uint64]$flow.Groups[2].Value
            MoviePlaybackCalls = [uint64]$flow.Groups[3].Value
            TitleTransitionMode = [uint64]$flow.Groups[4].Value
            TitleSubstate = [uint64]$flow.Groups[5].Value
            MountedDisc = [uint64]$flow.Groups[6].Value
            MovieCatalogIndex = [uint64]$flow.Groups[7].Value
            NextMissionAuthoredState = $nextAuthored
            Log = [IO.Path]::GetFileName($logPath)
        })
        Write-Host ("Mission {0:D2} completeflow: PASS (next-authored={1})" -f `
            $mission, $nextAuthored)
    }
}

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
        $checkpointMatch = [regex]::Match(
            $completed, ':checkpoint=(\d+)/(\d+):forward=')
        if (-not $checkpointMatch.Success) {
            throw "Mission $mission $mode omitted checkpoint ownership state. See $logPath"
        }
        $checkpointPresent = [uint64]$checkpointMatch.Groups[1].Value
        $checkpointCaptureFrame = [uint64]$checkpointMatch.Groups[2].Value
        $openingDeferred = Read-Count $completed `
            'opening-defer=(\d+):checkpoint=' 'special-opening ownership'
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
        $minimumHealth = Read-Count $completed `
            'min-health=(\d+)' 'minimum player health'

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
        $authoredCleanStartFailureReentry =
            $openingDeferred -eq 1 -and $checkpointPresent -eq 0 -and
            $minimumHealth -eq 0 -and $scriptLevelStarts -ge 1
        if ($scriptLevelStarts -ne 1 -and
            -not $authoredCleanStartFailureReentry) {
            throw "Mission $mission $mode restarted the retail LEVEL program. See $logPath"
        }
        # Event-family 0x72 owns the parachute openings (1/8); 0x62 owns
        # opening vehicle/actor sequences (5/6/17). All remain checkpoint-free
        # until retail itself serializes a complete restart stream.
        if ($mission -in @(1, 5, 6, 8, 17)) {
            if ($openingDeferred -ne 1 -or
                ($checkpointPresent -eq 0 -and $checkpointCaptureFrame -ne 0) -or
                ($checkpointPresent -ne 0 -and $checkpointCaptureFrame -eq 0)) {
                throw "Mission $mission $mode violated authored opening/checkpoint ownership. See $logPath"
            }
        } elseif ($openingDeferred -ne 0 -or $checkpointPresent -ne 1 -or
                  $checkpointCaptureFrame -eq 0) {
            throw "Mission $mission $mode omitted its initial retail checkpoint. See $logPath"
        }

        $results.Add([pscustomobject]@{
            Mission = $mission
            Mode = $mode
            Frames = $Frames
            OpeningCheckpointDeferred = $openingDeferred
            CheckpointPresent = $checkpointPresent
            CheckpointCaptureFrame = $checkpointCaptureFrame
            CheckpointRestores = $checkpointRestores
            CollisionGaps = $collisionGaps
            RendererRepairs = $rendererRepairs
            RejectedOrderingTables = $rejectedOts
            RejectedListMerges = $rejectedMerges
            RejectedSoundBanks = $rejectedBanks
            RejectedSoundVoices = $rejectedVoices
            ScriptLevelStarts = $scriptLevelStarts
            MinimumPlayerHealth = $minimumHealth
            Log = [IO.Path]::GetFileName($logPath)
        })
        Write-Host ("Mission {0:D2} {1}: PASS (checkpoint={2}/{3}, restores={4}, sound={5}/{6})" -f `
            $mission, $mode, $checkpointPresent, $checkpointCaptureFrame, $checkpointRestores,
            $rejectedBanks, $rejectedVoices)
    }
}

$summaryPath = Join-Path $OutputDirectory "summary.csv"
$results | Export-Csv -LiteralPath $summaryPath -NoTypeInformation
$completionSummaryPath = Join-Path $OutputDirectory "completion-summary.csv"
$completionResults | Export-Csv -LiteralPath $completionSummaryPath -NoTypeInformation
Write-Host "SF2 validation matrix: PASS ($($results.Count) routes)"
Write-Host "Summary: $summaryPath"
if (-not $SkipCompletionFlow) {
    Write-Host "Completion summary: $completionSummaryPath"
}
