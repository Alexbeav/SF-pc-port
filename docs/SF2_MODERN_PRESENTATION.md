# SF2 Modern Presentation Workstream

## Purpose

This worktree modernizes how the verified SF2 guest runtime is presented on a
PC without taking ownership of retail gameplay. It begins at commit `001b1e8`
and tag `v0.1.0-sf2-guest-alpha.10` on branch
`feature/sf2-modern-presentation`.

The sibling worktree `I:\Projects\sf-pc-port` remains the source of truth for
the retail-correct port on branch `research/sf2-full-bringup`.

## Ownership contract

The correctness stream owns:

- R3000A execution, overlays, mission scripts and actor behavior;
- campaign order, objectives, failure conditions, checkpoints and saves;
- CD, XA, SPU, STR and authored audio timing;
- original camera state, collision, inventory and difficulty;
- PS1 GPU semantics and a faithful 4:3 compatibility presentation; and
- crashes or corruption reproducible without modern presentation features.

The modern stream owns optional host-side features:

- output and internal render resolution;
- aspect ratio, widescreen projection and presentation-safe culling;
- HUD, matte, fullscreen-effect and menu placement outside 4:3;
- presentation interpolation above the retail update rate;
- PGXP-style transform precision and perspective-correct texture options;
- texture filtering, scaling, post-processing and renderer settings;
- optional modern mouse-camera behavior; and
- launcher/settings integration for those features.

Modern options must be individually disableable. The retail-compatible profile
must remain a regression oracle. A modern feature may observe guest state but
must not implement objectives, AI, movement, collision, checkpoint, inventory,
or campaign behavior.

## Synchronization policy

1. Fix gameplay and compatibility defects in `research/sf2-full-bringup`.
2. Validate them there with the Release suite and the relevant human route.
3. Merge that branch into `feature/sf2-modern-presentation` regularly.
4. Keep modern changes behind explicit settings or renderer/profile boundaries.
5. Send generally useful platform fixes back as focused commits only after they
   pass both SF1 and SF2 regression gates.
6. Never resolve a merge by discarding newer correctness-stream runtime work.

## Baseline capabilities

Alpha 10 provides the starting point:

- all 21 SF2 mission packages boot through the retail guest runtime;
- connected campaign, saves, STR transitions and the Disc 1/Disc 2 seam work;
- Missions 1-7 pass consecutive interactive coverage;
- retail HUD, radar, notifications, pause/map and briefings are functional;
- mouse aim/chase and controllers work while scripted cameras retain ownership;
- the two-page PS1 presentation model is stable in deterministic replay; and
- all 21 configured Release tests pass.

Known baseline defects must not be mistaken for modern regressions:

- Mission 1 parachute death can restore at an invalid boundary;
- the decorative briefing frame is absent;
- Mission 6 NVG can leave a right-edge strip;
- mission-failure mattes animate at 4:3 before settling at full width; and
- Missions 8-21 still need equivalent consecutive human completion coverage.

## Delivery plan

### P0 — profiles and measurement

- Add explicit `Retail`, `Modern`, and diagnostic presentation profiles.
- Record output resolution, internal scale, aspect, guest tick, presentation
  tick, interpolation state and feature toggles in logs.
- Establish comparison captures at 4:3 and 16:9 from deterministic replays.
- Keep the Alpha 10 retail-compatible image as the visual baseline.

### P1 — resolution-independent renderer

- Separate PS1 VRAM/page semantics from host render-target resolution.
- Support integer and arbitrary internal scales without changing guest logic.
- Define sampling rules for color, alpha, CLUT, mask bits and framebuffer reads.
- Add GPU timing and memory budgets for 1080p and 4K.

### P2 — generalized widescreen

- Replace mission-specific compensation with aspect-aware camera projection,
  culling, background coverage and safe-area rules.
- Anchor HUD and menus deliberately instead of stretching them.
- Generalize mattes, scopes, NVG, fades, damage overlays and fullscreen effects.
- Support at least 4:3, 16:9, 16:10 and ultrawide with per-feature fallbacks.

### P3 — geometry precision and textures

- Preserve guest transform provenance needed for PGXP-style subpixel geometry.
- Add perspective-correct texture interpolation as an optional renderer path.
- Prevent seams, vertex explosions, depth instability and HUD contamination.
- Offer nearest, bilinear and texture-scale options without altering VRAM data.

### P4 — high-refresh presentation

- Decouple host presentation from the retail simulation/audio cadence.
- Interpolate camera, actor and rigid-object transforms without advancing guest
  state or duplicating effects/UI submissions.
- Detect camera cuts, teleports, death/restart, pause, movies and room changes;
  suppress interpolation across discontinuities.
- Target stable 60, 120 and 144 Hz output while retaining a retail-rate mode.

### P5 — product integration

- Expose SF2 and presentation profiles in the launcher.
- Add resolution, aspect, frame-rate, interpolation, PGXP, filtering, HUD and
  mouse-camera controls to persistent settings.
- Detect both discs and provide actionable validation errors.
- Package a modern-preview channel separately from correctness alphas.

## Required validation matrix

Every modern milestone must pass:

- all configured Release CTests;
- Mission 3's deterministic truck replay and quick-state roundtrip;
- clean boot and checkpoint death/restart at 4:3 with all modern options off;
- 1280x960 4:3, 1920x1080 16:9 and 3840x2160 16:9 captures;
- Mission 1 outdoor/parachute/cinematic coverage;
- Mission 3 HUD, radar, dialogue, truck notifications and checkpoint coverage;
- Mission 6 NVG and scoped-weapon coverage;
- Mission 7 train motion, timer, music and completion coverage;
- Mission 8 parachute/open-area and Disc 1-to-Disc 2 transition coverage; and
- pause/map, briefing, STR, save menu, mission-failure and campaign-transition
  discontinuities.

Performance changes require before/after frame-time, GPU-memory and pacing
measurements. A prettier frame is not accepted if replay determinism, audio
cadence, collision, checkpoint state, or the retail-compatible profile changes.

## In-repository reference map

Read these before implementation:

- [`GAME_RUNTIME_ARCHITECTURE.md`](GAME_RUNTIME_ARCHITECTURE.md) — guest/native
  authority boundary.
- [`SF2_EXECUTABLE_MAP.md`](SF2_EXECUTABLE_MAP.md) — executable and overlay map.
- [`SF2_SHARED_SYSTEM_MAP.md`](SF2_SHARED_SYSTEM_MAP.md) — shared retail systems.
- [`SF2_MISSION_SCRIPT_VM.md`](SF2_MISSION_SCRIPT_VM.md) — authored scripting;
  useful for recognizing, not replacing, camera/effect transitions.
- [`devlogs/2026-07-28-sf2-bring-up.md`](devlogs/2026-07-28-sf2-bring-up.md) —
  complete diagnostic history and rejected approaches.
- [`research/`](research/) — function, event, handler, predicate and XA maps.
- [`../tests/data/sf2/README.md`](../tests/data/sf2/README.md) — deterministic
  replay provenance and commands.
- [`releases/0.1.0-sf2-guest-alpha.10.md`](releases/0.1.0-sf2-guest-alpha.10.md)
  — exact public baseline and known issues.

Additional privately supplied material is indexed under `.local-context/` in
this worktree. That directory is ignored by Git. Do not commit, redistribute,
quote, or derive public source from an external item until its provenance and
permission are clear. Independently verify useful addresses and behavior
against the user-owned retail executable and record reproducible evidence in
the public maps.

## Build and launch

```powershell
cd I:\Projects\SF2-Modern
cmake --preset windows-psycross
cmake --build --preset windows-psycross-release
ctest --preset windows-psycross-release

.\build\windows-psycross\Release\syphon_filter.exe `
  --no-launcher --resolution=1920x1080 --mission=3 --scene-test `
  "<path-to-Syphon Filter 2 (USA) (Disc 1).cue>"
```

The worktree has its own `build/`, `dist/`, logs and local settings context, so
building it does not overwrite binaries in `I:\Projects\sf-pc-port`.
