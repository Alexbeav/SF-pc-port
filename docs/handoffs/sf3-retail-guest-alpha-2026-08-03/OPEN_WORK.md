# Remaining work

The current alpha proves a retail-owned path into Mission 1. It does not prove
Mission 1 completion, the campaign, final presentation, or public-release
stability.

## P0 — validate and stabilize the first product session

### Human presentation/input/audio test

Required observations:

- first visible authored gameplay frame;
- world, actor, HUD and palette correctness on both framebuffer pages;
- keyboard and controller movement/turn/action/fire behavior;
- retail pause entry with `P`, navigation and return to gameplay;
- opening XA/dialogue/music audibility and sustained audio without starvation;
- behavior after at least ten minutes of play; and
- any crash/freeze log plus screenshot and approximate elapsed time.

Acceptance gate: repeatable Mission 1 entry and five-to-ten-minute interactive
play with no guest fault, black world, persistent corruption, false state-7
push or audio collapse.

### Complete retail GPU composition

The runtime currently selects one qualifying OT publication per guest display
frame. Determine all submissions belonging to the same retail tick and compose
them in authored order, including world, HUD/radar/text and framebuffer effects.

Acceptance gate: deterministic per-tick submission sequence and human captures
matching retail ordering, clip, draw offset, TPAGE/CLUT and two-page behavior.

### Make texture residency guest-authoritative

The product seeds PS1 textures using the native Mission 1 package and its
initial room. Expose verified SF3 player/camera/room state and synchronize
`TextureStreamer` as retail moves across room/bank portals, or transport retail
VRAM writes directly where appropriate.

Acceptance gate: traverse multiple Hotel Fukushima rooms without missing,
stale or wrong-bank textures.

## P0 — expose the retail frontend in the product

`Sf3GuestMissionRuntime` currently auto-drives title/card/loading inside its
constructor. Refactor bootstrap into incremental phases so product updates can
publish TITLE/TITLE2/MENU/movie/loading frames and accept user PAD at the exact
verified returns.

Do not replace title presentation or selection with native UI.

Acceptance gate: cold launch visibly presents the retail title/menu shell; a
user selects Story and Mission 1; retail owns the observed transition and
requests TOKYO without scripted input.

## P1 — controls and pause

- Map SF3-specific player/controller semantics rather than assuming the SF2
  action layout is complete.
- Locate and validate SF3 camera/manual-aim boundaries before adding
  high-resolution mouse motion.
- Verify state-7 menu OT composition, PAD navigation, options and resume.
- Ensure host Escape lifecycle control cannot alias retail button input.

Acceptance gate: deterministic recorded PAD replay plus interactive parity for
movement, aim, combat, interaction and pause.

## P1 — gameplay systems and completion

- Map SF3 player, actor, camera, weapon, damage, objective and checkpoint
  layouts from live Mission 1 state.
- Validate scripts, AI, collision, combat, pickups and dialogue without native
  substitution.
- Follow death/failure through retail restart.
- Capture a retail checkpoint and add exact quick-state replay gates.
- Reach Hotel Fukushima EOL and observe the retail mission-completion request.
- Connect campaign carry/save flow only after its guest boundaries are proven.

Acceptance gate: complete Mission 1 from authored opening to retail transition,
including death/restart and at least one checkpoint restore.

## P1 — audio and movies

- Measure long-running SPU callback cadence and OpenAL queue health.
- Validate `SCENES1.XA` file/channel routing, dialogue, music and mute/demute.
- Replace headless movie completion in product mode with real STR/MDEC
  presentation while keeping retail selection/timing authoritative.
- Verify transitions between movie, title, loading and gameplay audio states.

Acceptance gate: no dropped/stuck XA, repeated callbacks, permanent mute or
audio starvation during title-to-gameplay and a scripted movie handoff.

## P2 — remaining campaign

Implement retail guest loading in this order:

1. Mission 2 — outdoor geometry, AI and scripts.
2. Mission 7 — comparison against the established Lorelei scene test.
3. Mission 8 — jungle presentation and special mechanics.
4. Mission 16 — separately referenced `SNOWCAMP` resource.
5. Missions 17-19 — final campaign, saves and credits.
6. Complete all 19 missions in campaign order.

Each mission needs exact archive/overlay evidence. Do not generalize the TOKYO
member sequence or special overlays without observing that mission.

Acceptance gate: every mission boots and completes through retail-owned state,
with deterministic smoke gates and no native gameplay fallback.

## P2 — packaging and diagnostics

- Add SF3 status/known issues to launcher and release notes.
- Add actionable product fault logs with PC, instruction, state/depth, CD/SPU
  state, last file request and last PAD sample.
- Add an automatic bounded product diagnostic exit usable with a valid hidden
  graphics backend.
- Package only required DLLs, docs and executable; verify from a clean folder.
- Retain source-only/proprietary-data scans and publish checksums.

## P3 — modern presentation

Only after the retail-compatible campaign is stable:

- widescreen/high resolution;
- interpolation and PGXP validation;
- mouse camera enhancements;
- fullscreen effects and HUD placement; and
- optional modern controls/presentation.

Every enhancement must be switchable and compared against SF3-specific retail
behavior. SF2 modern hooks are leads, not automatically valid SF3 code.

## Useful notes requested from sibling projects

The highest-value incoming material would be:

- exact SF3 or closely matched retail renderer submission composition;
- verified PSX GPU/VRAM upload ownership around persistent two-page output;
- SF2/SF3 player, camera, pause and checkpoint structural matches with callers;
- archive-handle reuse or overlay relocation findings from other missions;
- MDEC/STR handoff timing and completion contracts;
- long-run SPU/XA scheduling diagnostics;
- known PsyCross offscreen/hidden OpenGL initialization support on Windows; and
- any conflicting executable revision/address evidence, always with disc hash.

For each external note, include game/revision, executable hash, evidence type,
address/caller, observed state effect and whether it was reproduced against
SCUS-94640.
