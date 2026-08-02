# Changelog

All notable public-test changes are documented here. The project currently uses
pre-release tags rather than a stable semantic-versioning promise.

## Unreleased

## 0.1.0-sf2-guest-alpha.9 - 2026-08-02

### SF2 controls, campaign and widescreen

- Added independent native mouse yaw/pitch control and sensitivity settings
  for first-person aim and the enhanced third-person chase camera.
- Widened SF2's guest GTE projection for the host aspect ratio so backgrounds,
  world geometry, and NPCs remain present across the widescreen viewport.
- Restored H11/crossbow selection and rendering of the H11's line-based optic.
- Restored Normal difficulty for direct launches and prevented Gabe's health
  and inventory from leaking into Lian during the Mission 1-to-2 handoff.
- Serviced Mission 1's blocking post-C4 CD wait without replacing retail
  mission logic.
- Verified Missions 1-4 interactively with the new control and projection
  paths; all 21 configured Release tests pass.
- Documented the remaining Mission 1 parachute-death restart defect.

### Release artifact

- File: `SyphonFilterPC-0.1.0-sf2-guest-alpha.9-win64.zip`
- Checksum: supplied in the accompanying `.zip.sha256` file.
- Required discs: *Syphon Filter 2* USA Disc 1/Disc 2, BIN/CUE.

### SF2 cross-mission bring-up

- Boot-validated all eight USA Disc 1 mission packages through the retail
  guest runtime; Missions 1 and 4–8 also pass deterministic quick-state
  replay gates.
- Prevented AIRBASE movement from faulting in a stale player sound-bank
  payload by treating only unreadable retail variant tables as an empty sound
  selection. Forward, combat and byte-exact F5/F9 stress routes now complete.
- Fixed WRECK's clean-start parachute sequence by publishing the retail
  checkpoint-present flag only after the first real checkpoint capture.
  Mission 8 now lands and enters gameplay instead of falling into a
  129-frame restart loop.
- Limited the experimental collision-room containment to the captured HWAY
  room-14 case. Follow-up disassembly showed that the earlier request-level
  "player floor" classification was shared actor logic, so that unproven
  request rewrite and its misleading probe were retired.
- Enabled the USA Disc 2 executable and corrected its resident movie catalog
  bridge to use `MOVIE2.HOG`.
- Routed every SF2 scene-test mission, including Disc 2, through the retail
  guest runtime instead of falling back to the archived native reconstruction.
- Recovered Disc 2's campaign-to-archive permutation and kept mission loading
  on the retail TITLE handoff. All thirteen Disc 2 packages now reach live
  gameplay and pass 3,000-update combat/input stress routes in the headless
  product runtime; nine cross one or more retail restart cycles. All thirteen
  also pass 3,000-update exact F5/F9 replay gates.
- Contained stale sound variants at the 24-voice SPU hardware boundary.
  LABS2 no longer faults when a released variant's reused voice byte exceeds
  the physical voice bank, while valid retail voice updates remain untouched.
- Fixed F9 rejection after high-pitch sounds finish. Inactive SPU voices may
  legally retain a phase accumulator above one sample period; snapshot
  validation now accepts that dormant state while retaining the active-voice
  bound.

## 0.1.0-sf2-guest-alpha.2 - 2026-07-31

### Mission 3 playable alpha

- Verified Mission 3 end to end, from its retail in-engine opening through
  objectives, combat, dialogue, checkpoint death/restart, mission completion,
  the save menu and the following cinematic.
- Restored retail HUD and TIM/CLUT uploads for health, armor, ammunition,
  reticles, weapon icons, checkpoint/head-shot notices and pickup messages.
- Added persistent in-session F5/F9 quick states with coherent guest RAM,
  CD/XA, SPU, streamed VRAM and restored ordering-table presentation.
- Fixed post-F9 frame blinking and delayed guest corruption by restoring the
  saved immutable presentation frame without consuming extra retail GPU
  boundaries; full-RAM replay gates now require byte-identical results.
- Added a bounded retail list-merge validator for poisoned renderer links so
  one malformed object list cannot walk freed `0x5A` memory and fault.
- Added directional weapon selection, mouse-wheel switching and number-key
  owned-weapon slots without bypassing retail inventory or equip behavior.
- Improved modern PC input, including crouch-only C, A/D strafing and retained
  first-person mouse motion across the retail 20 Hz input sampler.
- Stabilized guest renderer ordering-table bounds, streamed texture residency,
  checkpoint audio continuity and malformed presentation edges encountered
  during full-mission play.
- Increased SF2 OpenAL startup/recovery buffering while retaining the bounded
  PCM timeline, reducing cadence starvation without adding guest-side latency.
- Added a player-floor request guard and diagnostics for the captured room-14
  fall-through path.

### Alpha limitations

- Mission 3 is the only interactively completed mission; other mission
  packages remain unvalidated.
- Collision or room residency can still intermittently allow Gabe to fall
  through the floor. The new request guard needs broader playtesting.
- The explosives-truck/Chance dialogue can pause unevenly and may require C to
  skip.
- Some UI atlas regions and initial/post-checkpoint HUD timing remain
  imperfect.
- Music is not yet present, and first-person mouse sensitivity needs tuning.

### Release artifact

- File: `SyphonFilterPC-0.1.0-sf2-guest-alpha.2-win64.zip`
- Checksum: supplied in the accompanying `.zip.sha256` file.
- Required disc: *Syphon Filter 2* USA Disc 1, BIN/CUE.

## 0.1.0-sf2-guest-alpha.1 - 2026-07-30

### Experimental Syphon Filter 2 runtime

- Added a product-owned R3000A guest runtime for the USA sequel executable and
  a direct retail TITLE-to-HWAY Mission 3 bootstrap.
- Replayed retail GPU submissions through PsyCross with authored textures,
  frame clearing, streamed texture-bank residency and a partial native HUD.
- Routed keyboard, mouse and controller input into retail gameplay, including
  first-person mouse aim, strafing, crouch and sampled weapon selection.
- Routed retail XA dialogue and SPU sound effects into native PCM output.
- Preserved retail death and checkpoint reload behavior while keeping host
  audio, callbacks and CD scheduling coherent across restarts.
- Added deterministic probes, runtime diagnostics, guarded guest-memory writes
  and extensive executable/script/audio research documentation.

### Alpha limitations

- Mission 3 is the only interactively validated mission.
- Campaign transitions, saves, music and the complete mission set are not yet
  supported as a public gameplay path.
- Some HUD weapon artwork is absent, and a later scripted conversation may
  fail to release player control.
- Deep-room texture residency has a new correction that needs wider testing.

### Release artifact

- File: `SyphonFilterPC-0.1.0-sf2-guest-alpha.1-win64.zip`
- Checksum: supplied in the accompanying `.zip.sha256` file.
- Required disc: *Syphon Filter 2* USA Disc 1, BIN/CUE.

## 0.1.0-public-test.8 - 2026-07-27

### Gameplay and presentation

- Restored the documented USA/PAL retail cheat chords, centralized their
  persistent runtime state and added an original-style **Options > Cheats**
  page; launcher-side mission and cheat controls were removed.
- Corrected the Russian `Ш/Щ/ш/щ` middle stems in the generated Industria atlas
  and retained canonical English labels in sniper/night-vision scopes.
- Reworked Russian map-objective wrapping and replaced frame-rate-dependent
  flashing with a synchronized low-contrast objective glow.
- Protected the final campaign stream from an inherited input edge so the
  credits and post-credits sequence play to completion.
- Added dynamic scene lights and geometry-driven shadows for Gabe, allies and
  enemies, including stable floor/wall projection and bounded translucency.
- Restored original English sniper/night-vision scope labels and resolved every
  button token against the active keyboard, mouse or gamepad binding.
- Isolated mission-menu overrides to the selected locale so Russian objective
  records can never replace the original English guest strings.

### Rendering and streaming

- Reworked VRAM texture aliases around explicit scene generations and complete
  page/CLUT identity, preventing stale room textures and disappearing actors.
- Corrected mission texture-bank validation and retail SCRIM copy semantics
  across streamed segments without weakening invalid-state guards.
- Added an opt-in Surface Picker that highlights one submitted surface and
  writes a complete diagnostic dump on request.

### Audio and timing

- Separated the 120 Hz retail SPU clock from presentation rates up to 240 FPS,
  and made synchronous CD/DMA completion advance devices without generating
  future audio.
- Rebuilt OpenAL buffering and underrun recovery around one bounded monotonic
  timeline, avoiding stale replay, dropped samples and accumulated latency.

### Architecture

- Moved retail cheat definitions, chord detection and state into a dedicated
  game module shared by the title screen, pause menu and gameplay runtime.
- Kept cheat state alive across mission transitions and returns to the title
  screen, with one activation path for both original button codes and menu
  switches.
- Split dynamic lighting, scope-text policy, mission texture ownership and
  Surface Picker diagnostics into testable modules with regression coverage.
- Updated headless gameplay probes to advance the same independently driven
  120 Hz hardware/audio clock as the real frontend, without double-clocking the
  production runtime.

### Release artifact

- File: `SyphonFilterPC-0.1.0-public-test.8-win64.zip`
- Checksum: supplied in the accompanying `.zip.sha256` file.
- Supported disc: *Syphon Filter* USA v1.1 (`SCUS-94240`), BIN/CUE.

## 0.1.0-public-test.7 - 2026-07-25

### Russian localization

- Added the complete text-only Russian language pack for all 20 missions,
  including proofread menus, objectives, parameters, briefings, gameplay
  messages, weapon descriptions and baked map/title labels.
- Rebuilt the ViT-compatible font sheets as a unified 2x Industria-style atlas
  while preserving the retail byte map, advances and logical menu geometry.
- Fixed incomplete or mismatched mission briefings, missing weapon descriptions,
  weapon specification tables and every observed spelling/case variant of the
  gas-grenade pickup message.
- Kept speech, music and FMV on the original USA v1.1 disc; the pack changes
  presentation text only.

### Presentation and runtime

- Finished the original two-page Weapons presentation and restored complete
  descriptions, ammunition data and the four authored specifications.
- Corrected text flow, pagination and placement across briefing, objective,
  parameter, map, weapon and options pages for both supported languages.
- Added working vertical synchronization and a high-resolution frame limiter,
  and removed the guest CPU-overclock workaround from normal gameplay timing.
- Replaced restart-prone gameplay audio queues with a bounded continuous
  callback stream, stock-rate SPU scheduling and deterministic transition reset.
- Corrected scene ordering and depth behavior for translucent polygons, pickups,
  grenade sprites and transient effects without bypassing authored occlusion.
- Preserved mission completion/save flow, campaign unlock progress and
  restart-safe destructible state.

### Launcher and architecture

- Corrected launcher layout and Unicode language labels, and exposed the new
  Russian text pack through the persistent language selector.
- Split file I/O, localization, pause-menu data, retail map projection, VRAM,
  runtime guards and native font upload into explicit modules with automated
  architecture checks.
- Updated the public packager to require and include the complete `locales`
  directory while continuing to reject disc images, saves, logs, cheats and
  developer binaries.

### Release artifact

- File: `SyphonFilterPC-0.1.0-public-test.7-win64.zip`
- Checksum: supplied in the accompanying `.zip.sha256` file.
- Supported disc: *Syphon Filter* USA v1.1 (`SCUS-94240`), BIN/CUE.

## 0.1.0-public-test.6 - 2026-07-24

### Stability

- Fixed a renderer/UI bridge fault in the PHARCOM warehouse missions when the
  retail streamer recycled an active world's relocated vertex-color payload.
- Kept guest world visibility and geometry authoritative while treating the
  transient per-vertex lighting payload as an optional presentation cache.
- Extended the headless retail environment probe and validated all three
  warehouse missions past the reported failure frame.

### Release artifact

- File: `SyphonFilterPC-0.1.0-public-test.6-win64.zip`
- Checksum: supplied in the accompanying `.zip.sha256` file.
- Supported disc: *Syphon Filter* USA v1.1 (`SCUS-94240`), BIN/CUE.

## 0.1.0-public-test.5 - 2026-07-24

### Launcher and distribution

- Replaced the command-script bootstrap with an integrated Windows launcher.
- Added first-run CUE selection, persistent graphics/input settings and styled
  auxiliary dialogs.
- Added an English **DOSSIERS** button and a four-page bonus gallery.
- Added a new multi-resolution launcher icon featuring Gabe Logan.
- Added a controlled release packager with clean-install checks, dependency
  licenses, per-file SHA-256 sums and an archive checksum.
- Ensured public packages contain no save data, settings, game images or
  `syphon_filter_cheats` marker.

### Rendering and graphics

- Applied the selected resolution to the internal color/depth render targets,
  not only the UI.
- Added independent anisotropic filtering and seam-safe bilinear texture
  filtering that clamps PS1 atlas tiles without bleeding across their edges.
- Added selectable MSAA and original/adaptive aspect modes.
- Improved FMV presentation and removed the additional dithering pass.
- Reworked weapon muzzle flashes with a smaller textured star shape and reliable
  player/enemy shot triggering.
- Corrected first-person muzzle-flash rules: the player's own flash is hidden
  while enemy flashes remain visible.
- Restored depth occlusion for pickups, grenade sprites, blood, sparks and other
  transient effects.
- Fixed scene lighting on weapon crates and multiple level texture/model mapping
  errors, including the Kazakhstan gas tank case.
- Removed the development FPS counter from the game image.

### Gameplay presentation

- Restored every documented retail cheat and its original title/pause-menu
  button context, including PAL aliases, infinite ammunition, hard mode,
  one-shot kills, weak enemies, stage select and the Georgia Street theater.
- Added the original-style **Options > Cheats** page with synchronized switches
  for all six restored modes.
- Removed mission selection and cheat controls from the launcher permanently;
  `syphon_filter_cheats` now activates persistent retail cheats directly.

- Rebuilt the pause map presentation around the original PS1 layout, including
  map layers, current Gabe position and active-objective indicators.
- Reworked Objectives, Parameters, Options and Weapons pages, including the
  three weapon-stat bars and full-information panel backgrounds.
- Muted world audio while the pause menu is open while retaining menu sounds;
  restored audio levels on close.
- Fixed campaign mission unlock progression and preserved the highest unlocked
  mission when replaying an earlier stage.
- Gated unrestricted mission selection behind an explicit local
  `syphon_filter_cheats` marker.
- Restored the grenade sprite, ballistic flight path and scene occlusion for
  player and enemy throws.
- Fixed disappearing held weapons and bomb/destructible models during missions.
- Restored window, glass-panel and stained-glass destruction into visible shards.
- Fixed destructible state restoration after both manual restart and
  failure-triggered mission restart.

### Release artifact

- File: `SyphonFilterPC-0.1.0-public-test.5-win64.zip`
- SHA-256: `4B99A0EE167C0F9C649E36010D70ADD2480682A245265E9C32CB3D098063F403`
- Supported disc: *Syphon Filter* USA v1.1 (`SCUS-94240`), BIN/CUE.
