# SF2 hybrid runtime status — 2026-08-03

## Snapshot

- Repository: `sf-pc-port`
- Branch: `research/sf2-full-bringup`
- Recorded HEAD: `f74b9ab1ef3ef19f03c7abd740534930685024f4`
- Public baseline: Alpha 10
- Working target: Alpha 11 candidate
- License: MIT (game images and extracted retail assets are not distributed)
- Tree state: a substantial uncommitted hardening pass is present; this report
  does not imply that it has been committed, packaged, or published.
- Parking-checkpoint executable: 2,131,456 bytes, SHA-256
  `3066D169C87787C417CBCF7309E381035A4BCBEE18CA00C731471B3A41FCA353`,
  built 2026-08-03 11:45:41 UTC. This is a local diagnostic candidate, not a
  published Alpha 11 artifact.

The active architecture executes retail R3000A mission/gameplay code and lets
retail own scripts, AI, collision, animation, objectives, inventory, camera,
checkpoint data, and authored timing. Native code owns the host/device bridge,
campaign shell, movie handoffs, PC input enhancements, durable product saves,
and PsyCross presentation. It is a deterministic hybrid runtime, not a full
static recompilation.

## What is working

### Retail gameplay and content

- All 21 campaign mission packages on both USA discs reach live gameplay.
- Retail actors, scripts, objectives, combat, stealth, animations, checkpoints,
  mission-specific mechanics, and in-engine cinematics run in the guest.
- Mission 3 has been completed interactively through its ending flow.
- Missions 1–7 have received substantial connected/manual play coverage;
  Mission 1 now completes and transitions into Mission 2 without leaking
  Gabe's loadout to Lian.
- Mission 5 opening actors, Homan/truck choreography, and silenced-rifle patrols
  were restored by servicing the nested retail resource completion path.
- Mission 7 music and Mission 1 dialogue/checkpoint audio were confirmed after
  the audio-generation and scheduling corrections.

### Campaign, movies, and saves

- Both disc catalogs and the non-linear Disc 2 campaign/archive mapping are
  represented explicitly.
- Connected mission flow supports start FMV, briefing, in-engine opening,
  gameplay, ending FMV, save prompt, next start FMV, and next mission.
- The Disc 1 to Disc 2 seam has worked interactively.
- Persistent save slots show corrected SF2 mission names and load the matching
  campaign mission.
- In-mission AIRBASE movies and mapped campaign STR files are bridged.
- Mission 21 ending and post-campaign movie ranges are mapped and decode in
  strict probes, but the complete ending/credits route still needs a human run.

### Rendering and UI

- Retail GPU packets drive world geometry, actors, HUD, radar, threat cones,
  target/danger bars, health/armor, weapons, prompts, pickup/objective text,
  scopes, pause, and map presentation.
- Two persistent PS1 framebuffer pages preserve infrequently redrawn UI and
  remove the former alternating-frame, doubled-UI, and invented-layer-expiry
  failures.
- Native-wide guest GTE projection prevents geometry and NPCs from disappearing
  at widescreen edges.
- Full-frame filters, fades, scopes, and cinematic mattes expand using authored
  framebuffer-edge ownership rather than mission/effect-specific patches.
- Briefing text order and placement are corrected. Native experiments can draw
  the frame, grid, gauge, and prompt, but the authentic animated mottled
  surface remains unresolved. The latest fallback is visibly flat and its grid
  can pierce the frame, so briefing visual parity is not accepted.

### Input and PC presentation

- Keyboard/mouse and controller gameplay are available together.
- Native relative mouse control drives first-person yaw/pitch and an enhanced
  third-person chase camera with separate sensitivity controls.
- Retail controller behavior remains on its original path.
- Scripted camera ownership suppresses enhanced mouse pitch during in-engine
  cinematics and restores it cleanly at player handoff.
- The player-handoff camera no longer snaps upward: native chase pitch now
  remains dormant across authored camera ownership until actual mouse-Y input
  arrives. This was confirmed interactively.
- Weapon wheel/number selection, crouch-only keyboard mapping, controller
  crouch/roll, lock-on, first-person optics, and ranged combat have received
  interactive validation.

## Important findings from this hardening pass

1. **Device waits must be cycle-driven.** A repeated PC at exhausted guest
   quanta proves cyclic execution without assigning meaning to that address.
   Advancing all devices by retired cycles then handles raw CD, VSync, card,
   DMA, timer, and callback waits through one rule; identifying particular PCs,
   return addresses, or polled APIs is brittle and hides ownership errors.
2. **Presentation time and device time are different clocks.** Coupling audio,
   CD, or guest logic to host display submissions caused slow motion, FIFO
   failures, starvation, and checkpoint desynchronization.
3. **PS1 framebuffer persistence is behavior, not residue.** Rare UI OTs remain
   visible because pixels persist until retail overwrites them. Host-side layer
   retention and unconditional clears both violate that contract.
4. **Draw-page ownership differs from display-page ownership.** OT capture,
   authored E-state, and display selection must remain distinct.
5. **A valid checkpoint stream is not necessarily a valid opening restart.**
   COLO and WRECK deliberately begin without checkpoints. Death while absent
   must rebuild the package and replay the complete parachute opening; ordinary
   missions retain the frontend-supplied initial serializer call.
6. **Camera ownership is useful but narrow.** It is authoritative for enhanced
   input, not for checkpoint creation.
7. **Campaign identity, archive order, movie catalogs, and save labels are
   separate tables.** Treating any one as the others caused wrong missions and
   wrong FMVs after the Disc 1 seam.
8. **Product assertions must be semantic.** A changed GPU hash is not proof of
   playability. Gates now inspect control handoff, player movement, checkpoint
   structure, presentation state, and connected flow.

The detailed caught/missed/near-miss analysis is in the
[bring-up retrospective](../../SF2_BRINGUP_RETROSPECTIVE.md).

## Briefing presentation investigation — parked 2026-08-03

The retail-compatible control and the native product now provide a useful
differential:

- The retail state-8 briefing emits a stable 716-command GP0 frame: 571
  `0x66` sprites, 86 `0x40` lines, 21 opaque `0x2C` textured quads, one
  semitransparent `0x2E` textured quad, five semitransparent `0x42` lines, and
  the surrounding state/upload commands.
- No Gouraud quads participate. The earlier theory that the mottled frame was
  primarily a Gouraud-shaded native surface is false.
- The exact 21 textured-strip coordinates were translated into the native
  renderer. A synthetic direct-16 upload at `(640,0)` failed to produce the
  surface. A corrected inferred indexed setup—TPAGE `0x009c`, CLUT `0x7fc0`,
  8-bit texture base `(768,256)`, sampled UV range approximately `x=0..9`,
  `y=160..191`, upload at `(768,416)`, palette at `(0,511)`—also produced no
  discernible texture effect.
- Moving that upload before `PsyX_BeginScene` did not change the result, so a
  simple active-scene upload-boundary error is not sufficient to explain it.
- A Gouraud fallback made the missing region obvious but rendered as coarse
  bright green/blue polygons and was rejected. Returning to the flat fallback
  restored the morning appearance but not retail fidelity.

The sibling recompilation lab supplies the architectural control: it renders
the effect accurately because retail owns frontend state 8 and emits the
original GPU work. The pause menu in this project is accurate for the same
reason; the reusable value is its retail-owned presentation path, not a hidden
native copy of the effect. The next implementation should route retail state 8
through the existing persistent-page compositor. Native briefing texture code
should then be removed or retained only behind a diagnostic gate.

## Automated evidence for the current candidate

- The earlier 2,126,848-byte executable and its recorded hash are obsolete
  after the Save/Quit, camera, and briefing experiments. The parking-checkpoint
  executable identity is recorded in the Snapshot above. Any further source
  change invalidates it.
- A clean Windows PsyCross Release build succeeded.
- All 21 configured CTests passed after relinking the current sources.
- The real SF1 `SUBWAY.OVL` VM and CD-ROM/DMA3 compatibility probes passed.
- The semantic raw-CD synchronization probe passed.
- The dedicated Mission 1 checkpoint-absent clean-restart probe passed.
- Fresh-generation probes now cover every observed opening-transfer package:
  Missions 1/8 replay event `0x72`; Missions 5/6/17 replay event `0x62`.
- A product-shell code audit confirms the yielded restart clears mission PCM,
  resets the host audio generation, destroys the scene runtime, and constructs
  a new guest runtime for the same campaign/disc cursor. It deliberately keeps
  the campaign-entry carry while skipping the opening STR and briefing. This
  is structural evidence only; the helicopter/death route remains a human
  gate on the exact binary.
- The first exact Mission 6 playtest proved the guest cleanly yielded after an
  early helicopter-blade death, but the direct `--scene-test` host then exited
  normally instead of consuming `restart_mission`. Scene-test now mirrors the
  campaign host by reconstructing the same package in-process. The full 21/21
  CTest suite passes after this correction; interactive generation-two flyby
  confirmation remains pending.
- The current H11 and silenced-sniper probes positively distinguish the
  line-based non-zoom reticle from the sprite-heavy digital scope after retail
  inventory bootstrap; both are release-matrix preflights. Neither is evidence
  for Mission 6's separate scripted full-screen night-vision filter.
- Exact F5/F9 validation hashes guest RAM and the complete published
  presentation contract: draw/display pages, roots, packet boundaries,
  addresses, GP1 state, and every GP0 word.
- The exact post-restart runtime passes all 21 forced success-shell routes,
  all 21 exact quick-state/presentation routes, and all 21 combat routes. The
  evidence is split across
  `build/sf2-validation-alpha11-opening-final-20260803` (completion,
  quick-state, combat 1–7) and
  `build/sf2-validation-alpha11-opening-combat08-21-20260803` (combat 8–21).
  The split is intentional evidence: the first full attempt rejected an
  over-broad checkpoint policy at Mission 2, and the second exposed a
  diagnostic-only Mission 6 discriminator at Mission 8. Both were corrected
  generically before the remaining routes passed.
- A fresh unsplit release run after the final camera/briefing parking changes
  passed the same complete matrix on both legal disc images: 21/21 completion,
  21/21 quick-state, and 21/21 combat routes. Evidence is in
  `build/sf2-validation-20260803-151025`; this local directory is not a release
  dependency or distributed artifact.
- Every same-disc destination rejects the deliberately poisoned outgoing
  inventory/vitals and retains its retail-authored player state. Mission
  8-to-9 remains owned by the native two-disc resolver, and Mission 21 is
  terminal. Both loaded MENU callbacks and distinct H11/silenced-sniper optic
  preflights also pass.
- A 1,200-update Mission 21 combat smoke ended inside the authored
  post-checkpoint camera window and was rejected. The required 3,000-update
  route recovered player ownership and passed after two checkpoint restores;
  the release horizon remains 3,000 rather than weakening that assertion.

These build directories are local evidence, not release artifacts and not
portable dependencies.

## Confirmed human evidence versus pending human evidence

Confirmed on recent candidate lineage:

- Mission 1 and Mission 8 checkpoint-free parachute restart behavior;
- Mission 1 opening/audio and end-to-end completion;
- Mission 1 to Mission 2 character/loadout ownership;
- Mission 3 end-to-end gameplay and campaign handoff;
- Mission 5 opening choreography after nested-resource repair;
- Mission 6 NVG presentation across the widescreen frame;
- Mission 7 gameplay/music;
- title logos, `ZINTRO`, main menu, mapped mission FMVs, save slot persistence,
  pause/map, controller input, and modern mouse aiming on exercised routes;
- centered Save and Quit slot selection, successful durable save, return to
  title, and subsequent load; and
- stable camera pitch at the transition from an authored in-engine camera to
  player control.

Still required on the exact Alpha 11 candidate:

1. `Restart Mission` must rebuild the same package without returning to title
   or replaying SOL/briefing; `Restart At Last Checkpoint` must remain the
   separate retail-owned path.
2. Mission 6 must replay its helicopter/troop opening after death, Restart
   Mission, and a checkpoint-free Restart At Last Checkpoint. Its NVG, a
   representative scope/fade, and animated cinematic/failure
   mattes should remain regression checks even though NVG is now confirmed.
3. Missions 8–21 must be played consecutively using natural flow, including a
   death/restart and F5/F9 spot check per mission where practical.
4. The finale must reach the authored ending and credits rather than an
   ordinary next-mission path.

## Product gaps after the single-player gate

- Restore the authentic retail `New Game -> One Player / Two Players` menu.
- Host two-player arena selection, two pads, and split-screen before advertising
  multiplayer.
- Expose the retail difficulty selector; direct startup currently uses Normal.
- Make quick states persist between application sessions if retained as a
  product feature.
- Replace the incomplete native briefing reconstruction with retail-owned
  state-8 (`INIT.OVL`) presentation through the persistent-page GPU path. The
  packet topology is mapped; the current native texture/upload experiments are
  not a fidelity solution.
- Finish launcher/settings integration and the remaining SF1-parity modern
  features: high-refresh interpolation, PGXP, perspective-correct textures,
  renderer options, and presentation presets.
- Continue campaign-wide audio/dialogue, failure-condition, collision, and
  edge-case testing even after the first full completion.

## Release rule

Do not call Alpha 11 end-to-end complete or publish it as the campaign candidate
until the four exact-candidate human checks above are recorded. Automated
mission survival is deliberately not a substitute for completing authored
objectives and transitions.
