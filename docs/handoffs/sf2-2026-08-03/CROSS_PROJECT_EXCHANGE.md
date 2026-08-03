# SF2 hybrid / recompilation lab exchange — 2026-08-03

## Scope and evidence labels

This comparison uses:

- live repository evidence from `sf-pc-port`;
- the lab's `docs/sf2/SESSION_REPORT_2026-08-03.md`, current objective,
  feasibility plan, comparison protocol, and CFG load-delay report; and
- the user's interactive report that the lab completed Mission 1 accurately.

It does not treat elapsed calendar time as an apples-to-apples benchmark. The
lab began with a mature PSXRecomp runtime and targeted PS1-compatible output;
the hybrid project also paid for hardware discovery, full campaign mapping,
native product flow, desktop presentation, controls, and several false starts.

The first companion package was received at clean lab commit `3b4c281` with
the framework suite passing 40/40. The overnight return is clean commit
`7613d6f` with 43/43 tests. Its relevant implementation checkpoints are
`61d3667` (non-`$ra` JALR), `dc873fc` (IRQ delivery inside native call units),
`09be64b` (generic OpenGL depth-24 VRAM ownership), `485b79b` (CD seek/read
ownership), and `89804a7` (exact guest-frame input and deterministic route
validation). The current primary session report's SHA-256 in this review is
`07A42C774C192525D4A8E87C567450A92E5060D1124935F731B40EA99A3E5C65`.
This identity is recorded so later comparisons do not silently mix reports.

## Present comparison

| Dimension | `sf-pc-port` hybrid | PSXRecomp feasibility lab |
|---|---|---|
| Execution model | Retail mission R3000A under interpreter/hybrid device bridge; native product shell and presentation | Static resident translation, captured/compiled overlays, measured interpreter fallback, PS1 runtime |
| License | MIT source; no retail assets distributed | PolyForm Noncommercial; not source-compatible with an MIT/commercial product |
| Retail frontend | Partly reconstructed/native; expected logos/intro/menu flow works on tested routes, but New Game flattens directly to one-player | Retail frontend/menu hierarchy executes; a clean no-input route now proves 989 -> Eidetic -> legal -> `ZINTRO` -> TITLE before exact guest-frame input |
| Mission evidence | All 21 boot; 63-route deterministic matrix; broad Missions 1–7 and selected later interactive evidence | Two deterministic Mission 1 routes; representative Mission 3 and campaign coverage pending |
| Saves | Native durable slot shell backed by retail/campaign state; Save/Quit now reaches the centered slot UI, saves, returns to title and reloads successfully. It persists the mission cursor rather than the retail checkpoint stream | Retail memory-card save/load completed and persisted through the real card path |
| Presentation baseline | Retail GPU packets translated to PsyCross with persistent pages, desktop resolution and widescreen | PS1-compatible presentation; reported retail menu/briefing effect fidelity is stronger |
| Known presentation gap | Briefing text and geometry can be reconstructed, but the current flat frame/grid fallback is not retail-faithful; native texture experiments failed. Retail-owned state 8 is now the recommended destination | Native PS1 presentation retains the complete retail briefing effect; the former OpenGL 24-bit FMV band defect is fixed generically and has zero CPU/FBO mismatch samples on the accepted route |
| Input | Keyboard/mouse, enhanced third-person camera, first-person mouse aim, controller | Retail controller path; no equivalent PC keyboard/mouse enhancement yet |
| Campaign/content model | Explicit two-disc mission/archive/movie/save-name tables and connected native flow | Only the exercised Disc 1/Mission 1 route is currently proven |
| Determinism | Guest and full presentation quick-state hashes; combat/quick-state matrix; semantic probes | Two clean native-enabled Mission 1 routes match same-frame RAM-write, store-PC, MMIO, scratchpad and cycle fingerprints at five semantic checkpoints |
| Modernization | High-resolution/widescreen/mouse already present; PGXP/interpolation/settings remain | Intentionally out of scope during feasibility phase |

## What the lab has already taught this project

1. **Captured-CFG load delays:** a generic PSXRecomp path exposed immediate
   load visibility where MIPS-I requires the following instruction to see the
   old register. This interpreter already keeps current and next delayed loads
   separate, but the lab supplied an independent audit and upstream PR #93.
2. **Non-`$ra` JALR semantics:** SF2 uses descriptor trampolines such as
   `jalr $a1,$t0`. Any reusable harness must honor the encoded link register and
   must not force every indirect call into a conventional `$ra` call unit. The
   hybrid interpreter already writes the encoded `rd` and ignores `rd=0`; a
   focused regression now proves both the `$a1` descriptor form and a linkless
   indirect jump so that this independently discovered invariant cannot
   regress here.
3. **IRQ delivery inside native call units:** atomicity cannot mean suppressing
   every IRQ. Retail may wait for an IRQ-backed event inside a call; defer only
   cooperative thread switching where required.
4. **Split GPU representations:** if one backend owns an FBO while 24-bit
   scanout reads a CPU VRAM mirror, fills/copies need explicit coherence. A
   correct MDEC upload does not guarantee correct unrefreshed bands.
5. **Authentic frontend first:** running retail TITLE/MENU/SIO avoids replacing
   lifecycle behavior that later has to be rediscovered one callback at a time.
6. **A seek changes CD ownership:** the lab proved that SF2 issued a valid new
   `SetLoc`/`SeekL` sequence while the device continued the old `ReadN` stream.
   `SeekL`/`SeekP` must cancel the active read and pending data-ready ownership;
   fixing the input trigger would never have repaired the missing presentation.
7. **Deterministic input belongs to guest time:** arming controls on exact
   guest frames after a compound stable-TITLE predicate removed host socket
   timing from the comparison and produced matching semantic checkpoints.

### Overnight resolved results

The lab has now answered several questions from the first exchange rather than
leaving them as hypotheses:

- The authentic startup sequence is reproducible without early input: 989 at
  frame 925, Eidetic at 1268, legal at 1422, `ZINTRO` at 1752, and stable TITLE
  at 18493.
- The missing startup clips were caused by stale CD read ownership after seek,
  not by corrupt media or a retail-flow omission.
- The OpenGL movie bands were a split CPU/FBO VRAM ownership defect. The
  generic handoff/fill correction changes the affected acceptance route from
  292 mismatch samples to zero while the software control remains correct.
- Two clean Mission 1 routes now reach retail player/camera ownership and
  movement with matching fingerprints. At player control, the lab reports
  approximately 15.83 million resident-AOT hits, 141.71 million compiled-
  overlay dispatches, and 0.683 million interpreter fallbacks. The reported
  overlay-tier fallback share is 0.480%, not zero and not described as native
  coverage.
- The lab's briefing fidelity is an architectural control: it presents the
  retail GPU result. This confirms that the hybrid briefing's missing textured
  surface is a real presentation delta, not missing briefing text or state.

### Save-path differential result

The two projects' superficially identical Save and Quit hangs had different
causes, which is precisely why they are useful independent oracles. In the
recompilation lab, retail had completed card reads and delivered BIOS events,
but an outer native call unit suppressed the IRQ needed by its own inner event
pump. Commit `dc873fc` permits IRQ delivery while retaining the narrower
thread-switch deferral.

This hybrid runtime does not execute that retail IRQ ownership path: it clocks
devices with guest IRQ entry suppressed, dispatches selected callbacks through
the host bridge, and hands durable slots to a native shell. Its hang occurred
later in the loaded MENU callback. Both Save and Quit and Restart Mission tear
down application state 7 before their distinguishing outer calls, while the
native bridge incorrectly required state 7 at callee entry. Live overlay
disassembly established callbacks `0x801435e4`/`0x80143624` and unique return
addresses `0x80143614`/`0x80143654`; retaining those architectural call-site
discriminators and removing the impossible state predicate fixes both paths.

The reusable process lesson is not to transplant either patch blindly. Trace
the same retail milestone in both implementations, determine which owner
actually failed to advance, and transfer the invariant: nested retail work may
outlive or dismantle the outer state in which it was initiated. Focused gates
should execute through that teardown/event wait, rather than asserting only
that the callback address exists.

### Briefing differential follow-up — 2026-08-03

The hybrid project used the lab as a bounded behavioral oracle and captured
the retail briefing's primitive topology without importing captured assets or
license-incompatible implementation code. A stable state-8 frame contains 716
GP0 commands: 571 sprites, 86 ordinary lines, 21 opaque textured quads, one
semitransparent textured quad, five semitransparent lines, plus GPU state and
uploads. There are no Gouraud quads. This directly falsifies the hybrid's old
Gouraud-surface interpretation.

The hybrid then tested three native explanations:

1. exact strip geometry with a generated direct-16 texture;
2. an inferred retail 8-bit indexed page/CLUT setup (TPAGE `0x009c`, CLUT
   `0x7fc0`, texture upload at `(768,416)`, palette at `(0,511)`); and
3. moving the upload outside the active PsyCross scene.

None produced a discernible retail surface. A coarse Gouraud fallback did
render, proving the geometry was visible, but was visually wrong and was
rejected. The latest flat fallback still lacks the effect and can allow the
grid to pierce the border.

This is the useful convergence: the lab does not contain a better native
briefing renderer; it is accurate because the retail frontend owns state 8.
The hybrid pause menu is accurate for the same architectural reason. Further
native texture guessing has been parked. The next cross-project comparison
should identify the state-8 entry/exit contract and let the hybrid's existing
persistent-page presentation consume the authentic retail GPU stream.

## What the hybrid project can give the lab

The following are useful as behavioral oracles or factual maps, not as code to
copy blindly:

- the 21-mission campaign/archive permutation and Disc 1-to-Disc 2 seam;
- complete movie-catalog mapping and expected movie/briefing/mission ordering;
- save-slot names, character/loadout ownership, and final-campaign behavior;
- the executable/function map, mission script notes, shared-system map, and
  research symbol tables;
- checkpoint-present semantics, especially checkpoint-free COLO/WRECK
  parachute openings and clean package restart;
- representative dialogue/XA, in-mission FMV, music, scope, and HUD behavior;
- persistent two-page framebuffer observations and full-presentation hash
  schema;
- golden input/replay methodology and semantic player-control assertions;
- mission matrix routes and connected human acceptance checklist; and
- camera-owner evidence for distinguishing scripted and player-controlled
  presentation.

Do not exchange disc sectors, executable bytes, captured overlays, generated
game C, RAM/quick-state dumps, memory-card images, extracted audio/video/art,
or private third-party material.

## High-value comparison runs

The next comparison should use equivalent retail-compatible routes before any
modern presentation is considered:

1. Clean process to stable TITLE without injected pre-title input (now passed
   in the lab; retain it as a regression route).
2. Retail New Game, One Player, Mission 1 start and first player handoff.
3. Mission 1 death during the playable parachute opening and clean restart.
4. Mission 1 completion, Save and Quit, card reload, and Mission 2 ownership.
5. Retail selection of Mission 3, opening, truck equipment objective, dialogue,
   death/checkpoint restore, and two minutes of combat.
6. A 24-bit FMV transition under software and hardware renderers.
7. One two-page HUD/pause/map transition with page indices and full GP0/GP1
   presentation evidence.

At each boundary record the fields in the lab's comparison protocol: commits,
input identities, guest registers/application state, stable memory hashes,
active overlays, dispatch tiers, GPU/display state, CD/SPU/XA state, clocks,
frame pacing, CPU time, peak memory, warnings, and exact input artifact.

## Specific questions for the recompilation lab

1. What is the minimal state-8 entry/exit and overlay-lifetime contract needed
   to run the authentic briefing through the hybrid runtime? Primitive topology
   is now known; frontend ownership is the remaining architectural question.
2. Does death during Mission 1's playable parachute opening reproduce the full
   opening and retain the parachute attachment?
3. Does the retail Restart Mission command differ from Restart At Last
   Checkpoint exactly as observed by the hybrid runtime?
4. After Mission 1 save/load and Mission 2 entry, are Lian's authored health
   and inventory restored without Gabe's loadout?
5. On the retail-selected Mission 3 route, which new overlay variants or
   interpreter fallbacks appear beyond the now-deterministic Mission 1 set?

## Specific questions for the hybrid project

1. Save and Quit now passes. Does Restart Mission rebuild the active package
   while Restart At Last Checkpoint remains on its distinct retail path?
2. Do Mission 6 NVG, H11/sniper optics, fades, and animated mattes remain fully
   wide without stretching ordinary HUD primitives?
3. Can Missions 8–21 and the finale be completed consecutively with correct
   saves, inventory, movies, dialogue, audio, and credits?
4. Can retail state 8 replace the incomplete native briefing renderer while
   retaining the product's widescreen layout and input handoff?
5. Which remaining native lifecycle bridges disappear if more of the retail
   frontend is executed, and which are deliberate product abstractions?

## Process verdict for future projects

The lab's rapid Mission 1 result is meaningful. It demonstrates that an
informed project should begin with a mature, hardware-faithful runtime and run
the authentic frontend before implementing product replacements. That likely
would have avoided this project's early native-mission reconstruction, 20 Hz
GPU-bound pacing, one-target OT composition, guessed checkpoint ownership, and
several campaign-flow patches.

It does not prove that static recompilation alone makes a complete remaster
cheap. The remaining work still includes overlay convergence, interpreter
coverage, every device edge case, campaign validation, modern input and
presentation, packaging, licensing, and product QA. The faster general method
is therefore:

1. inventory public references and provenance before implementation;
2. boot through a mature retail-compatible runtime;
3. preserve authentic frontend, device, framebuffer, save, and campaign
   ownership wherever possible;
4. establish deterministic semantic checkpoints before visual polish;
5. validate two connected missions early, then expand to an all-content matrix;
6. separate compatibility completion from modern presentation;
7. use a hybrid interpreter first where necessary, then promote verified hot
   code to recompilation without making playability wait for 100% AOT coverage;
8. keep two independent implementations long enough to act as differential
   oracles; and
9. upstream every minimal generic fix with a focused regression.

For publishers, a permissively licensed clean runtime remains important. For
players and noncommercial research, PSXRecomp may provide an exceptionally
fast compatibility route. For modders and modernizers, either execution model
still needs explicit content/tooling APIs; recompilation improves code-level
instrumentation and patchability but does not automatically provide asset
pipelines or safe high-level game abstractions.
