# SF2 bring-up retrospective

Status: working retrospective through the post-Alpha-10 hardening pass. The
final campaign and public-release results must be added after consecutive
Missions 8–21 validation.

This document separates three kinds of result:

- conclusions reached from this project's own captures and experiments;
- important facts missed or validated too weakly; and
- diagnoses which were close, but used the wrong abstraction.

The distinction matters. A porting method improves only when it preserves the
failed reasoning and the evidence that corrected it, not just the final patch.

## What the project caught independently

### The correct product architecture

The first native-C++ mission reconstruction was useful as a map, but it was not
a scalable route to retail behavior. The project independently converged on a
hybrid runtime: execute the original R3000A gameplay and scripts, supply native
platform/device boundaries, and translate immutable retail presentation into
the PC renderer. That matches the successful architectural boundary already
used by the SF1 port.

### The original “playable” build was not running at retail speed

A deterministic probe showed that 1,400 host updates advanced the retail clock
only 232 ticks and sampled PAD 248 times. The product paced one GPU submission
at 20 Hz even though SF2 gameplay retires several GPU boundaries per gameplay
tick. This explained the stop-motion opening, unusable input sampling and audio
starvation. The correction came from measured guest clocks and packet
boundaries, not visual guesswork.

### Draw-page and display-page ownership were different

Retail alternated drawing environments across two PS1 framebuffer pages. The
bridge replayed drawing packets but initially held one host display page fixed,
causing picture/black alternation and later doubled UI. Packet captures and the
E3/E4/E5 state established that ordering-table submission and displayed-page
ownership are separate contracts.

### SF2 depends on framebuffer persistence

Rare text submissions, continuously updated radar submissions and per-page
ordering tables could not be represented by one “latest auxiliary layer.” On
real hardware, pixels survive until overdrawn. That explains why text could be
submitted briefly and remain visible, why replaying retained translucent OTs
stacked blending, and why invented layer-expiry heuristics were unstable. The
eventual persistent-page model followed the retail GPU contract.

### Presentation correctness needed a stronger deterministic oracle

Input changing a GP0 hash proved only that input reached some guest code. It did
not prove player control, HUD state or mission progression. Later probes added
player position, script state, exact checkpoint behavior and complete
presentation hashing. F5/F9 validation now compares all guest RAM plus page
indices, submission roots, packet boundaries, packet addresses and every GP0
word.

### Audio FIFO failures were scheduling failures

The FIFO-bound abort was not fundamentally a request to enlarge a queue. Device
time was coupled to host presentation and later to instruction-heavy paths in
ways retail did not authorize. Separating gameplay presentation time,
synchronous device-wait time and callback ownership restored exact PCM rates
and stable checkpoint audio generations.

### Campaign and disc order could not be inferred from archive order

Disc 2 mission archives, campaign labels and retail progression order differ.
Interactive FMV/mission flow exposed the mapping, including the Disc 1→2 seam.
The product now treats campaign identity, archive selection, movie catalog and
save-slot naming as separate data contracts.

### Camera ownership is an authoritative input boundary

The retail camera wrapper names either a scripted camera actor or the live
player. That state fixed mouse pitch leaking into cinematics. It is stronger
than a cutscene timer or a mission-specific input suppression, but later
checkpoint testing also proved that camera ownership must not be promoted into
a save/restart authority.

### Widescreen culling and fullscreen effects are different problems

Native-wide world projection required widening the guest GTE horizontal
projection/culling contract, not merely scaling the final image. Conversely,
screen effects must retain their authored UI geometry and expand only when a
primitive owns both retail framebuffer edges (plus the animated-black-matte
case). Keeping those policies separate fixed missing edge geometry without
stretching normal HUD elements.

## What the project missed or validated too weakly

### The first playable claim had no playability gate

The build was called a playable alpha because forward input changed rendered
geometry. No test required the opening to finish, the camera to enter player
ownership, the HUD to appear or the player position to move. A human test
immediately disproved the label.

Lesson: product claims require product-level assertions. Machine determinism is
necessary but not sufficient.

### PS1 framebuffer semantics should have been researched earlier

The project spent too long composing ordering tables into one host target and
inventing retained auxiliary state. PS1 GPU documentation and established
emulator designs would have made page persistence, draw/display environment
separation and overdraw-based removal the starting model.

Lesson: before repairing a console rendering symptom, write down the original
hardware ownership and persistence model.

### Broad reference review came late

Relevant decompilation indexes, PS1 recompilation projects, emulator hardware
documentation and the upstream SF1 architecture were surveyed after substantial
experimentation. They would not have supplied an SF2 port, but they would have
reduced rediscovery and warned against several host-side approximations.

Lesson: reference collection is Phase 0, followed by a short applicability
matrix—not an emergency step after a bug becomes expensive.

### The project over-corrected checkpoint capture

Eager capture at the first application-state-0 boundary broke the two parachute
openings. Removing host-triggered capture entirely then made ordinary deaths
restart the complete `LEVEL` program. The direct frontend bridge still had to
invoke retail's serializer for ordinary starts. A later camera-return policy
also failed interactively: its structurally valid frame-1327 snapshot still
skipped Mission 1's opening and detached Gabe after an early fall. Special
parachute starts must instead remain checkpoint-free until retail mission logic
itself invokes the serializer; restore while absent is a clean package restart.

Lesson: when removing a bridge operation, identify which original owner the
bridge replaced and prove that another owner still performs it.

### The validation matrix did not initially require a checkpoint

The matrix rejected repeated `LEVEL` starts but could allow a mission which did
not die to pass without ever creating a restart snapshot. Requiring a nonzero
capture on every route then over-corrected in the opposite direction: COLO and
WRECK intentionally have no checkpoint during their parachute starts. It now
requires positive complete streams for ordinary starts, authored deferral for
the two special starts, and an explicit clean-restart ownership probe.

Lesson: validate the positive invariant, not only the absence of its most
obvious failure mode.

### Consecutive campaign validation started too late

Standalone mission smoke tests found many runtime issues, but they could not
expose carried inventory, save naming, movie ordering, disc swaps or character
ownership. Connected play found these quickly.

Lesson: start a short connected-flow gate as soon as two adjacent missions can
boot, then expand it alongside standalone coverage.

### The clean-room audit found a private absolute path late

A roadmap commit referred to a local reference-library path. It contained no
retail material, but public documentation must be self-contained and must not
name a contributor workstation layout. The link was replaced with the
repository contribution policy, and the complete public diff is scanned for
personal paths and private-source identifiers.

Lesson: run the public-diff audit continuously, not only while packaging.

## Where the diagnosis was close

### “The framebuffer is not being cleared”

This correctly recognized temporal persistence and explained trails, but the
root abstraction was incomplete. Retail intentionally depends on persistent
VRAM; the bridge was routing and presenting pages incorrectly. Unconditionally
clearing the host target could hide trails while deleting valid persistent UI.

### “Retain multiple auxiliary UI layers”

This correctly identified last-writer-wins loss, but a layer cache would have
needed invented expiry rules and would repeatedly blend translucent packets.
Two persistent framebuffer pages made removal an ordinary retail overdraw.

### “Checkpoint when the camera returns to the player”

This looked correct in deterministic traces: Mission 1 captured at frame 1327,
Mission 8 at frame 463, and Volkov Park exposed the need for a direct-control
path. Interactive early-fall testing disproved it. Camera ownership marks
presentation/control handoff, not checkpoint validity. The correct two-path
model is retail ownership: ordinary starts receive the replaced frontend's
capture; special-opening starts remain checkpoint-free until retail itself
calls the serializer, and restore while absent reconstructs the mission.

### “Increase or recover the audio FIFO”

Recovery prevented an abort and made play sessions longer, but it treated the
queue as the authority. Exact guest/device clocks showed why production and
consumption diverged. The durable fix was clock ownership and audio-generation
invalidation at retail restore.

### “The mission is stuck before gameplay”

No HUD and a stationary-looking camera suggested a broken handoff. The user
recognized that it was the authentic in-engine opening running in extreme slow
motion. The observation was accurate; the interpretation failed because the
test window was shorter than the incorrectly paced cinematic.

## How external work helped

Public PS1 hardware documentation confirmed GPU page, ordering-table, DMA,
timer and interrupt semantics. Emulator implementations supplied comparison
models for persistent VRAM and device scheduling. Decompilation/recompilation
projects validated the value of deterministic executable maps, symbol unions
and generated metadata while also showing that full static recompilation is a
separate backend—not a prerequisite for a playable hybrid port. The current
upstream SF1 project validates the same guest-authoritative/native-presentation
boundary and provides useful presentation and validation patterns.

The companion recompilation lab also exposed a load-delay defect in
PSXRecomp's captured-overlay CFG emitter within hours of reaching the retail
frontend. That finding did not require a change here: this interpreter already
keeps current and next delayed loads separate and tests the immediate-consumer
case. It nevertheless supplied a valuable cross-project audit and a small,
generic upstream contribution. This is the intended relationship between the
streams: each implementation is both a product experiment and an independent
oracle for the other.

These sources sharpened or confirmed the architecture. Game-specific behavior
in this branch remains based on reproducible retail traces, project-owned
tests and user-observed behavior; private third-party material is not a public
implementation dependency.

## Process changes carried forward

The reusable porting playbook now requires:

- a reference survey and legal/provenance boundary before implementation;
- semantic device events instead of PC/return-address wait recognition;
- persistent hardware-state modeling before host composition heuristics;
- a golden replay with perceptual assertions, not hashes alone;
- exact guest plus host-derived presentation state in quick-state tests;
- early connected-flow coverage;
- explicit positive gates for checkpoint/save/transition ownership; and
- a continuous public-diff and upstream-contribution audit.

## Evidence at this checkpoint

- one consolidated 42-route matrix passes all 21 missions under both
  quick-state and combat input, including COLO/WRECK authored deferral (`0/0`)
  and ordinary checkpoint captures at frames 16-19;
- the dedicated Mission 1 clean-restart probe yields a host restart request
  rather than entering the checkpoint loader while the retail flag is zero;
- ordinary combat still crosses authentic retail checkpoint restores, while
  an opening death can legitimately reconstruct checkpoint-free `LEVEL`;
- the Windows PsyCross Alpha 11 candidate builds cleanly and all 21 CTests
  pass;
- the real SF1 `SUBWAY.OVL` VM and CD-ROM/DMA3 compatibility gates pass;
- interactive Mission 1 and Mission 8 checkpoint-free restart behavior is
  confirmed; and
- MENU full-restart/quit behavior, generalized fullscreen effects, and
  consecutive Missions 8–21 remain explicit human release gates.

This evidence supports a strong candidate build. It does not yet prove the
entire campaign release objective; the remaining human checks must be recorded
before the retrospective is finalized.
