# Syphon Filter 3 PC Port Workstream

## Purpose

This worktree brings the NTSC-U retail *Syphon Filter 3* executable and its 19
campaign missions into the same hybrid PC runtime architecture proven by SF1
and SF2. It begins at commit `001b1e8` and tag
`v0.1.0-sf2-guest-alpha.10` on branch `research/sf3-full-bringup`.

The supported disc profile is:

| Field | Required value |
| --- | --- |
| Game | Syphon Filter 3 USA / NTSC-U |
| Serial | `SCUS-94640` |
| Executable | `SCUS_946.40` |
| Revision | 1.0 |
| Executable SHA-256 | `b4b32cc92e6b8634762893b637bc9a471442edbeb7569afcfb18eafbe82b9460` |

No retail executable, disc sector, extracted asset, movie, speech, music, or
save data belongs in Git.

## Architecture contract

The original SF3 R3000A executable and overlays own:

- mission scripts, objectives, parameters and authored event timing;
- actor AI, animation, combat, stealth, inventory and collision;
- camera state, checkpoints, difficulty and character state;
- title/menu state, campaign progression, saves and mission transitions; and
- authored SPU, XA, STR and GPU command production.

Native code owns bounded platform services:

- deterministic R3000A/PSX device execution and virtual CD transport;
- immutable disc-file and overlay delivery requested by retail code;
- GPU/SPU/XA/STR presentation through the PC backends;
- keyboard, mouse and controller translation at verified input boundaries;
- snapshots, probes, diagnostics, settings and packaging; and
- optional modern presentation only after retail-compatible gameplay is stable.

SF3 is a related executable, not an SF2 mode. Do not reuse an SF2 address,
mission permutation, item table, script action, predicate, overlay rule,
checkpoint layout, audio cue, or camera hook without SF3 evidence and a
profile-owned implementation.

## Verified starting point

The shared repository already provides substantial SF3 research:

- `SCUS-94640` disc and executable identification;
- all 19 campaign resource names in presentation order;
- native parsing/construction for all 19 mission packages;
- 541 mapped mission definitions and 3,869 placed objects;
- 360 compiled mission programs, 4,573 events and 23,837 reachable words;
- 59 populated predicate descriptors and 108 used action slots;
- SF3-specific EMD two-byte and HMD eight-byte transformed-vertex strides;
- object class `0x84`, `MENU2`/`TITLE2`, and three non-generic mission overlays;
- 76 synchronized `SCENES1.XA` clip groups;
- 2,917 executable/resident function seeds;
- 2,111 unique exact/structural SF2-to-SF3 function relationships; and
- verified executable entry, application loop, VSync, CD, overlay, script,
  actor, campaign and item-system address families.

The current guest-bootstrap boundary is narrower than initial discovery: SF3
passes early CD, GPU, BIOS event and SPU setup, then dispatches through an
uninitialized guest target. The first task is to capture and explain that
control transfer, not bypass it with native gameplay.

## Bring-up milestones

### S0 — reproducible baseline

- Validate disc identity and executable hash.
- Regenerate the SF3 function, overlay, mission, script, object and XA maps.
- Record the current executable-entry stop with registers, stack, MMIO and the
  last control-flow window.
- Add a named SF3 bootstrap probe instead of relying on SF2-named tooling.

### S1 — continuous executable bootstrap

- Identify the uninitialized target's producer and required BIOS/device event.
- Execute CRT and `Game_Main` continuously into the 14-state application loop.
- Verify VBlank, CD-ready callback, GPU submission and SPU scheduling cadence.
- Preserve deterministic snapshots at stable guest boundaries.

### S2 — resident shell and title transition

- Load SF3 `MENU`/`MENU2`, `TITLE`/`TITLE2`, `MOVIE` and `INIT` through retail
  archive requests.
- Reach title/menu state without native state reconstruction.
- Select Mission 1 and observe retail teardown/loading/state-stack ownership.

As of 2026-08-02, the deterministic headless probe completes this gate. The
retail shell accepts Mission 1, follows application states `4 -> 6 -> 0`, and
issues `\FOG\TOKYO.FOG;1` plus `\TOKYO\SLF.RFF;1`. S3 begins at the resulting
mission archive/heap boundary; no native gameplay or presentation substitute
is used.

### S3 — Mission 1 vertical slice

- Load `TOKYO.FOG`, its overlay and nested resources through retail callbacks.
- Capture persistent two-page GPU presentation with correct SF3 TIM metadata,
  EMD/HMD strides, HUD atlases and draw/display environments.
- Route native PAD input into retail movement and camera paths.
- Establish SPU sound effects, `SCENES1.XA` dialogue/music and STR handoffs.
- Complete Hotel Fukushima from authored opening to mission transition.

The current deterministic probe has crossed the archive portion of this gate:
retail parses the TOKYO catalog and loads `SLF.RFF`, `TOKYO.DAT`,
`GENERIC.OVL`, `VLF.RFF`, `DLF.RFF`, `WLDEMD.HOG`, `TOKYO.SS`, and the
`NPC.HOG` catalog through immutable host file transport. It reaches the retail
Mission 1 PAD caller and captures a 640-packet, 625-draw frame. Product runtime
selection and interactive validation remain pending.

### S4 — runtime systems

- Map SF3 player, actor, camera, item, weapon, damage and checkpoint layouts.
- Validate SF3-only script actions/predicates and object class `0x84` in guest
  execution rather than through a native substitute.
- Add exact quick-state replay gates for gameplay, combat, pause and restart.
- Implement PC mouse enhancements only at proven SF3 camera boundaries.

### S5 — complete campaign

- Boot and stress all 19 missions through the product guest runtime.
- Validate every objective, failure condition, boss, special mechanic,
  conversation, checkpoint, scoped view and mission-specific resource.
- Connect title, briefings, FMVs, saves, carry, mission flow and final credits.
- Complete the campaign interactively with deterministic coverage beneath it.

### S6 — stabilization and public alpha

- Eliminate crashes, instruction-budget stops, renderer containment, audio
  starvation and state leakage under long play and repeated restarts.
- Add SF3 selection, disc validation, controls and known issues to packaging.
- Publish a source-only, no-retail-data playtester build with checksums.

### S7 — modern presentation

- Merge proven host presentation infrastructure from the SF2 Modern stream
  only after the SF3 retail-compatible campaign is stable.
- Revalidate high resolution, widescreen, interpolation, PGXP, fullscreen
  effects, HUD placement and modern camera options against SF3-specific scenes.

## Required validation gates

Each milestone must retain:

- all configured Release CTests;
- byte-identical deterministic replay from the same guest snapshot/input;
- no SF1 or SF2 regression in shared code;
- bounded guest execution with actionable PC/register/device diagnostics;
- no native gameplay fallback presented as retail compatibility; and
- no copyrighted disc data, RAM dump, extracted asset or private research file
  committed to the repository.

Human validation should expand in this order:

1. Mission 1 — Hotel Fukushima: title-to-mission vertical slice.
2. Mission 2 — Costa Rican Plantation: outdoor geometry/AI/scripts.
3. Mission 7 — S.S. Lorelei: existing scene-test comparison point.
4. Mission 8 — Aztec Ruins: jungle presentation and special mechanics.
5. Mission 16 — Militia Compound: separately referenced `SNOWCAMP` resource.
6. Missions 17-19 — final campaign/save/credits flow.
7. Complete campaign pass in order.

## Reference map

Mandatory in-repository reading:

- [`GAME_RUNTIME_ARCHITECTURE.md`](GAME_RUNTIME_ARCHITECTURE.md) — authority
  and shared-code rules.
- [`SF2_SF3_PORT_NOTES.md`](SF2_SF3_PORT_NOTES.md) — mission catalog, formats,
  executable-bootstrap boundary and known ABI deltas.
- [`SF2_EXECUTABLE_MAP.md`](SF2_EXECUTABLE_MAP.md) — function/overlay map and
  SF2↔SF3 structural matches.
- [`SF2_SHARED_SYSTEM_MAP.md`](SF2_SHARED_SYSTEM_MAP.md) — paired application,
  actor, item, audio, camera and campaign boundaries.
- [`SF2_MISSION_SCRIPT_VM.md`](SF2_MISSION_SCRIPT_VM.md) — SF3 action,
  predicate and mission-program corpus.
- [`devlogs/2026-07-28-sf2-bring-up.md`](devlogs/2026-07-28-sf2-bring-up.md)
  — early SF3 discoveries within the SF2 research history.
- [`research/`](research/) — source-only function, action, predicate, event,
  object-handler and XA catalogs.
- [`devlogs/2026-08-02-sf3-bring-up.md`](devlogs/2026-08-02-sf3-bring-up.md)
  — dedicated SF3 chronological record beginning at this baseline.

The ignored `.local-context/` directory indexes the user-owned disc and any
future external research. External information is a lead: independently verify
addresses and behavior against the retail executable before placing results in
public source or documentation.

## Local build and baseline commands

```powershell
cd I:\Projects\SF3-PC-Port
cmake --preset windows-psycross-local
cmake --build --preset windows-psycross-local-release
ctest --preset windows-psycross-local-release

$sf3 = "Z:\Emulators\PS1 Games\Syphon Filter 3 (USA).cue"
.\build\windows-psycross\Release\sf_tool.exe inspect $sf3
.\build\windows-psycross\Release\sf_tool.exe inspect-mission-archive $sf3 TOKYO
.\build\windows-psycross\Release\sf_tool.exe probe-sf3-guest-bootstrap $sf3 5000000
```

`inspect-disc-info` is currently SF2-specific because SF3 has no external
`DISK*.INF` selection catalog. Use `inspect` plus the checked-in SF3 mission
resource table until an SF3-specific information view is added.

The existing `--scene-test` path is useful only as a data/presentation smoke
test until the SF3 guest runtime owns gameplay. Do not label it a playable
mission or use it as proof of script, AI, checkpoint, audio, or campaign
correctness.
