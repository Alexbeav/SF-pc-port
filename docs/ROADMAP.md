# Roadmap

## Retail gameplay completion

- G2 — retail scripts, terrain triggers, spawn/despawn, doors, portals and
  scene transitions for all 20 missions. Complete; G2 ROM gates remain green.
- G3 — guest-authoritative AI, combat, stealth, targets, special actors,
  objectives, success and failure for all 20 missions. Complete; G3.2-G3.5
  natural-route, actor-lifecycle, AI/combat, stealth and overlay outcome gates
  are green and the gameplay test is accepted.
- G4 — connected campaign, FMV handoffs, checkpoints, saves, restarts and final
  stabilization of all 20 missions. Complete: transactional V3 saves with V1/V2
  migration, interruption-safe SOL/EOL ordering, final completion, exact
  checkpoint restart, explicit save-write recovery and a release `--game`
  entrypoint. The production-path ROM gate validates retail failure, checkpoint
  restore, success and staged EOL finalization for every mission; all 29 cataloged
  STR movies decode with video and audio.

## Hybrid migration

- H0 — guest ownership boundary: native input to PAD; retail gameplay/player/
  animation frames are authoritative. Complete.
- H1 — machine core: bus, IRQ, DMA, timers and deterministic scheduling around the
  existing interpreter/GTE. Complete.
- H2 — CD-ROM/DMA3 overlay loading and a resumable continuous guest loop. Complete.
- H3 — SPU MMIO/DMA4/IRQ9, CD-XA decode and bounded PCM output while keeping
  native FMV playback. Complete: strict builds/CTest and the retail gameplay-audio
  cadence/nonzero/checkpoint replay gate are green.
- H4 — renderer/UI command bridges and removal of remaining native gameplay paths.
  Complete.
- H5 — all mission overlays, snapshots, replay gates, native campaign/FMVs and
  campaign validation. Complete: 20/20 retail mission resources and all 13
  distinct overlays pass bootstrap, PCM, checkpoint and exact-replay gates.

## Syphon Filter 2 product roadmap

The SF2 guest runtime has reached live gameplay in all 21 campaign mission
packages on both retail discs. Mission 3 has been completed interactively from
its authored opening through the save menu and following cinematic. Mission
startup coverage is broad, but full-campaign completion and product parity are
not yet claimed.

Current August 2026 gate: the retail two-page presentation, complete gameplay
HUD/radar/text path, widescreen cinematic overlays, pause/menu roundtrip,
Mission 1 introduction, checkpoint restart, and in-session F5/F9 are stable in
the tested routes. A reproducible 42-route matrix now passes 3,000-update
quick-state and combat/restart coverage for Missions 1--21 across both discs
with no collision-residency gap or renderer containment. Connected campaign
save/carry/disc/movie handoff is implemented and deterministically reaches the
Mission 3-to-4 boundary. Retail-indexed in-mission FMVs are also bridged for
AIRBASE (`3_2`/`3_3`) and AIRBASEX (`6_3`). Retail success-shell tracing now
maps every authored EOL selection on both discs, including Mission 21's
`21_2.STR` and the post-campaign `Z17_1.STR`; all mapped ranges decode strictly.
The final interactive Mission 3-to-4 handoff and broader natural completion
playthroughs remain the release gate.

1. **Presentation stability — complete for the current playtest gate.** The
   persistent two-page renderer, atlas/state handling, room residency, floor
   requests, and polygon validation eliminate every deterministically reproduced
   world/HUD flicker, stale-frame, texture-accumulation, floor-loss, and vertex-
   explosion case. Retain cross-room, checkpoint, pause, and F5/F9 regressions.
2. **Complete SF2 HUD and gameplay UI — complete for gameplay.** Retail radar,
   timer, actor dots/threat cones, TARGET/DANGER, health/armor, weapon art,
   objective/pickup/general text, and cutscene/checkpoint reveal timing render
   from the authored GPU packets. Pause/map presentation is also stable.
3. **Mission 1 opening fidelity — complete.** The exact `2_1.STR` opening is
   mapped, and moving Colorado Mountains' loading-confirmation pulse from PAD
   poll 8 to poll 1 prevents that input edge from skipping the early parachute
   choreography. Its 1,800-update neutral route reaches stable gameplay.
4. **All-mission validation** — complete all 21 missions while auditing
   objectives, conversations, failure conditions, bosses, special mechanics,
   checkpoints, mission-specific audio/resources, and end-of-level transitions.
5. **Connected SF2 campaign** — integrate title/menu selection, natural
   mission-to-mission progression, persistent saves, cinematic handoffs,
   Disc 1-to-Disc 2 transition, final completion/credits, and launcher support.
6. **Audio, input, and PC polish** — validate music and dialogue interruption
   across the campaign, tune mouse aiming, expose original/modern control
   options, complete controller testing, and polish quick-state presentation.
7. **SF1 product-feature parity** — after the compatibility campaign is
   complete, bring SF2 to the same native presentation level as `sf-pc-port`:
   high-resolution rendering, widescreen scene presentation, high-refresh
   frame interpolation, PGXP geometry and perspective-correct textures,
   renderer options, and equivalent launcher/settings integration.

The older native-port milestones below are retained as implementation history.

## M0: input and build foundation

- Validate the exact NTSC-U 1.1 executable.
- Read CUE/BIN and ISO9660 without external tools.
- Parse the PS-X EXE load contract.
- Keep all tests independent of copyrighted data.

## M1: executable map

- Generate a reproducible MIPS disassembly and function map using pinned Ghidra/PSX-loader versions.
- Identify PsyQ library functions and isolate them from game code.
- Catalog the 18 executable overlays in `BIN` and their load addresses.
- Add byte-accurate fixtures containing only project-owned metadata.

## M2: native bootstrap

- Bring up PsyCross through the backend boundary.
- Port initialization, title and menu modules.
- Load original TIM/HOG assets directly from the disc image.
- Stream the original startup STR/MDEC/XA directly from raw disc sectors.

## M3: vertical slice

- Mount and validate the first mission's original FOG/HOG package.
- Dispatch New Game through the Georgia Street opening sequence.
- Parse and render the original VLF/EMD terrain with PGXP texture correction.
- Port one mission's update/render/input loop.
- Add deterministic frame-state regression tests.
- Preserve original timing while allowing uncapped presentation.

## M4: content completion

- Port remaining missions and overlays.
- Complete remaining XA/STR playback paths and memory cards.
- Run static analysis, sanitizers and full automated regression suites.

Gameplay validation remains a user task.
