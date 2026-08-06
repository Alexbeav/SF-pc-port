# Game Runtime Architecture

## Corrected decision

Syphon Filter 1, 2, and 3 are separate gameplay runtimes built on one shared
technical foundation. Similar disc formats or sequel data structures do not
establish identical gameplay behavior.

```text
Shared foundation
  assets / disc / R3000 + PSX devices / rendering / audio / input / math
                  |
        explicit runtime selection
          /          |          \
   SF1 guest     SF2 guest    future SF3 guest
```

The repository remains unified so proven low-level fixes do not drift between
games. Game behavior must nevertheless have an explicit owner.

The original R3000A executable and overlays own retail gameplay, mission
scripting, actors, combat, inventory, collision, camera state, and authored
effects. Native code owns presentation, platform I/O, modern controls, and
carefully bounded compatibility hooks. This is the architecture already used
by the upstream SF1 port; SF2 must follow it.

## Recovery classification

- **Product path:** disc/FOG loading, the PSX machine, continuous guest
  execution, guest RAM snapshots, native presentation bridges, input/audio
  backends, and per-game address profiles.
- **Reusable research:** executable/overlay maps, script decoding, asset
  parsers, mission catalogs, and deterministic probes.
- **Diagnostic scaffolding only:** native SF2 mission starts, mission-specific
  actor choreography, item/drop translation, interactions, and approximate
  combat. These may serve as assertions while the guest bridge is built, but
  are not an alternative gameplay implementation.
- **Deferred:** SF3 gameplay compatibility beyond shared low-level fixes.

Native SF2 scaffolding stays isolated until equivalent guest checkpoints are
working, so useful observations are not discarded prematurely. It must not be
expanded to reconstruct further retail gameplay.

## Current boundary

`GameRuntimeProfile` is the selection boundary. The three profile
implementations live in:

- `src/game/sf1_runtime.cpp`
- `src/game/sf2_runtime.cpp`
- `src/game/sf3_runtime.cpp`

The initial split owns:

- EMD and HMD decode strides;
- player collision dimensions;
- first-person camera height;
- legacy guest-runtime selection;
- native mission-item and interaction opt-in;
- HUD-atlas identity and relocation ownership;
- SF1 environment-atlas ownership.

SF2 item translation and AIRBASE delayed-actor policy are owned by
`sf2_runtime` as diagnostics. `Sf2GuestRuntimeProfile` owns the first verified
resident guest boundaries. SF3 deliberately does not inherit SF2 addresses,
translation, or native mission scaffolding.

## Dependency rules

1. Shared code must not infer compatibility from `game != SF1`.
2. A feature shared by two games still requires two explicit profile opt-ins.
3. Mission-specific source indices and retail item IDs belong to that game's
   runtime module.
4. SF1 behavior is frozen behind regression tests while SF2 is brought to
   parity.
5. SF3 receives only decoding and presentation support required for verified
   bootability until its own runtime work begins.
6. Code moves into the shared foundation only after identical behavior is
   demonstrated in at least two games.

## Hybrid recovery gates

1. Execute SF2 CRT and `Game_Main` continuously to the retail state loop.
2. Yield repeatably at the SF2 GPU-submission/native-presentation boundary,
   with snapshot/replay determinism.
3. Establish the SF2 virtual-CD and resident-overlay load path.
4. Select and boot a requested retail mission through guest state transitions.
5. Publish player, actors, world, HUD, effects, and audio from guest RAM.
6. Inject mouse-look and keyboard controls at explicit sequel input/camera
   boundaries while the guest retains movement and collision authority.
7. Validate Mission 3 end to end, then broaden campaign coverage.

The product must not select the SF2 guest runtime until a requested mission
boots and publishes a valid presentation snapshot. Until then the existing
native launch path is an explicitly incomplete diagnostic.

## SF2 mission-transition boundary

### Current verified status (2026-07-29)

The continuous retail transition is now the authoritative checkpoint. The
probe drives TITLE's selected state, runs the exact TITLE teardown, balances
the frontend packet-arena reservation through retail function `0x80015510`,
and lets INIT and the HWAY overlay own mission setup. The observed application
state path is `0 -> 1 -> 8 -> 0`; no native gameplay implementation is used.

Mission resource descriptors use sector numbers relative to the mounted FOG
image. `DiscCdRomMedia` therefore exposes a scoped relative-extent mapping
after `FOG/HWAY.FOG` is selected. CD commands, sector delivery, DMA3, archive
parsing, relocation, allocation, model construction, and rendering remain
retail guest work. Without that mapping, low sector `0x0E` was incorrectly
read from the start of the physical disc and produced an empty model table.

The verified Mission 3 run reaches eight stable retail GPU submissions in
64 application frames. Native presentation capture follows the guest ordering
table and publishes a best frame containing 894 packets, 8,119 GP0 words,
894 GPU commands, and 830 draw commands. This closes the continuous boot and
semantic presentation gates. Product selection remains disabled pending
interactive opening/campaign validation and the narrow keyboard, mouse,
effects, HUD, and audio handoff work.

The remainder of this section records earlier bring-up checkpoints and
diagnoses. They are historical evidence and are superseded where they describe
the continuous path as blocked.

### Historical bring-up record

Retail MENU and TITLE overlays both call `MissionArchive_Open` at
`0x8002A338`. The stage-select path supplies the zero-based mission index and
two true flags; Mission 3 is therefore `(2, 1, 1)`. The function constructs
the mission FOG/SLF paths, opens the mission archive, and requests application
state 12 through the resident state-stack code at `0x8002BC44`.

The deterministic transition probe now completes that retail transition.
Its platform boundary resolves ISO files, loads exact resident overlays,
opens and bulk-loads `SLF.RFF`, and supplies neutral pad state. A narrow
catalog-copy compatibility hook preserves the FOG catalog across the current
observe/continue execution boundary. These bridges move untouched disc bytes
and platform results into guest RAM; they do not implement mission gameplay.

The guest then owns archive parsing, heap placement, state-stack mutations,
the 76-frame state-9 loading lifecycle, state-12 readiness/pop, and the
Mission 3 loop. The verified checkpoint contains the exact 2 KiB HWAY FOG
header at `0x801ECC90`, all 14,296 expected `HWAY.OVL` bytes at
`0x8014B978`, retail state 13, and eight stable GPU-submission boundaries.
The state-12 push and pop are both retail calls; there is no native gameplay
fallback.

`Sf2PresentationFrame` now publishes those boundaries as immutable native
input. It follows the retail GPU DMA linked list from guest RAM, rejects
unaligned/out-of-range links, cycles, oversized chains, and missing
terminators, then deep-copies only typed packet addresses and GP0 words. Eight
verified frames alternate the two retail roots `0x801F8CA8` and `0x801F8CE8`.
Each currently carries four packets and twelve GP0 words.

This closes the structural native-presentation snapshot gate, but not the
visible-gameplay or product-runtime gate. The current frames contain valid
draw-environment and VRAM-transfer commands but no polygon/line/rectangle
packet (`draw_command_count == 0`). The resident resource entry at
`0x8015BA54` requests the logical path `\COMMON\GLOBAL.DAT;1`. The
executable's resident HOG at `0x801B92A8` is the authoritative virtual source
for that path, not `DISK1.INF`/`DISK2.INF`. It contains exact `GLOBAL.DAT`,
`BEEPSX.VH`, `BEEPSX.VB`, INIT, MOVIE, and title resources. The platform file
boundary exposes those immutable executable bytes through retail handles and
synchronous reads; guest INIT still parses them and builds its callback
tables.

This closes the resident-overlay and embedded-file transport blockers, but a
later lifetime experiment retracts the earlier continuous `9 -> 1 -> 4`
claim. Under the suspended retail state-loop frame, exact INIT returns
normally and `MissionArchive_Open(2, 1, 1)` reaches state 9 at depth 3.
Because that direct diagnostic call allocates `SLF.RFF` before TITLE finishes,
the next state-1 embedded-HOG lookup deterministically stops at `0x80026D80`;
the valid observed prefix is `9 -> 1`. BIOS `B0:56 GetC0Table` remains exposed
only for this ROM-less sequel path with the guest-visible C0 table contract
required by MOVIE's IRQ patch.

State 4 is the `TITLE.OVL` frame, not an STR loop. The retail selector at
`0x8002B9A8` now performs its exact mode-4 TITLE request through the embedded
HOG file boundary; all 53,032 bytes are verified at `0x8014B950`, including
the formerly missing callback at `0x80153E24`. TITLE's staged loader reaches
internal state 0 after five additional resident resource opens. Native
presentation capture observes a primitive-bearing immutable frame with 18
packets, 54 GP0 words, 18 commands, and two opcode-`0x2A` textured-polygon
draws.

The primitive-bearing TITLE checkpoint and the exact HWAY checkpoint are
independent diagnostic snapshots. They must not be interpreted as one
continuous boot: opening the mission directly before TITLE cleanup overwrites
the resident archive, while waiting for TITLE cleanup currently reaches the
unimplemented movie/stream platform boundary.

When exact MOVIE loading is enabled in its real owner, TITLE advances through
internal states `15 -> 3 -> 0`. Two platform lifecycles were previously
conflated:

- TITLE registers a slot-7 worker at `0x80145020` for asynchronous memory-card
  polling. Servicing that exact worker and delivering the no-card events
  completes its job (state 0, depth `0xFFFFFFFF`); it is not STR playback.
- The executable initializes the movie catalog at `0x8002C2CC`. Its only
  required data transfer is the exact first 2 KiB sector of `MOVIE1.HOG`,
  read through `0x8002723C`/`0x80026414`. An experimental five-slot external
  handle plus that sector read produces a nonzero guest catalog and reaches
  retail playback initialization at `0x80142E60`.

The apparent STR-ring stop was a scheduler diagnosis, not a decoder
requirement. `MOVIE_Update` calls `0x8002686C`, which queues the retail
asynchronous file callback `0x800266D4` through `0x800226D4`; the earlier
probe never dispatched executable callback slot 4. The active interrupt table
is rooted at `0x8011D0D4`, so slot 4 is `0x8011D0E4` (not the separate table
at `0x8011D108`). Once the exact task worker at `0x80022584` runs at the
20 Hz retail scheduler cadence, the seek completes before the one-shot task
fires.

Two libcd bridge details were also required. Host-bound async `CdControl`
must publish the command word polled by `CdSync`, and command completion must
mirror the returned status byte into `CdSync`'s paired result buffer at
`0x80141A10` as well as the active response pointer at `0x80141A18`. Command
ACK and completion are distinct scheduled events: every command waits only for
INT3 synchronously, while Stop, Pause, Init, and SeekL publish their later INT2
through the retail completion path. Clock-neutral completion selects the
earliest event deadline rather than the first event by storage order.

The initial MOVIE and `Common_Init(4)` “returns” were invalid evidence.
`invokeFrameCall` had left the CPU at its callback return sentinel after an
asynchronous CD callback, so the interrupted guest function appeared to finish
at `0xFFFFFFFF`. CD callbacks now use the isolated `0x807F0000` stack and
restore the complete interrupted CPU state. INT1 still enters the exact
data-ready callback at `0x800F703C`; INT2 enters the guest sync callback stored
at signed-address slot `0x8011D1BC` (currently `0x800F7008`) and mirrors the
PsyQ pending/completion state before dispatch.

With those corrected contracts, guest code reaches `0x8002676C`, registers the
STR ready path through `0x800F7808`, and receives a data-ready dispatch at
`0x800F703C` without the old `0x80142874` memory fault. This proves stream
startup, not MOVIE completion. The diagnostic scheduler also derives its
retrace counter from PSX CPU ticks at 60 Hz; incrementing once per
20,000-instruction slice had made retail frame timeouts run roughly 28 times
too fast. No native ring filler, decoder, movie state, or gameplay was added.

Opening the mission before that handoff remains lifetime-invalid: the
536,576-byte `SLF.RFF` downward heap allocation overwrites the raw embedded
HOG at `0x801B92A8`, which later TITLE resource searches still require.

The corrected continuous-path blocker is obtaining an executable-owned,
quiescent continuation after bootstrap CD activity and before
`Common_Init(4)`. The state-loop entry snapshot still has a retail read in
flight. Starting another init there mixes stale INT1 sectors into the new
one-sector read: header DMA succeeds and the expected sector eventually
arrives, but `CdReadSync` has already observed failure and retries. Capturing
an arbitrary later PC inside the overlay is equally unsafe because it is not
an application-frame ownership boundary.

Only after that continuation is established can the probe honestly complete
the MOVIE/TITLE handoff. TITLE states 16/17/18 call `0x80153D30`, which pops
the frontend application state and invokes `MissionArchive_Open` with the
selected mission. The following gates remain continuous state 12, exact HWAY
placement, stable frames, and Mission 3 world/HUD primitives. The earlier
direct mission and presentation checkpoints remain useful independent
diagnostics, but neither is a continuous product boot. Product SF2 selection
therefore remains disabled.

All application-state globals in `Sf2GuestRuntimeProfile` use resolved signed
MIPS immediates. In particular, current state, depth, transition, and stack
are `0x8011EE90`, `0x8011EE8C`, `0x8011EE94`, and `0x8010C5E4`;
interpreting `lui 0x8012` plus a negative offset as a `0x8012...` address is
incorrect.

## SF2 Mission 3 product presentation boundary

Mission 3 now has a narrow PsyCross product path rather than only a diagnostic
snapshot. `Sf2GuestMissionRuntime` owns the Disc 1 executable, TITLE-to-HWAY
transition, CD/SPU scheduler, mission pad record, and immutable GPU
publication. `PsyCrossSceneViewer` selects it only for SF2 mission index 2;
all other missions retain their existing paths.

The guest remains authoritative for scripts, actors, collision, camera, HUD,
effects, and ordering. The host:

- converts configured keyboard/mouse/controller input into one standard retail
  PAD sample;
- publishes complete guest display submissions on a fixed 60 Hz boundary
  while retail gameplay and PAD logic retain their internal 20 Hz cadence;
- replays validated GP0 polygon, line, sprite, environment, copy, fill, and
  upload commands;
- relocates the raw retail TPAGE/CLUT identities through the existing native
  mission-residency map; and
- drains guest SPU/XA PCM into `PsyCrossAudioOutput`.

Publication rejects the large sprite/line loading list and waits for authored
world geometry (at least 100 textured gouraud polygons). The first product
frame is therefore the 893-packet/8,118-word/830-draw Mission 3 world list,
not the earlier 612-packet loading card.

The processed input record is the pointer returned by retail registration
function `0x800222BC`: `0x80122FEC`, with a 60-byte stride. The superficially
similar `0x80112FEC` address belongs to the registration table and must never
receive PAD samples. The host writes only the registered record at the retail
input boundary; buttons and analog axes then flow through the original player,
camera, collision, and animation code.

The initial restart checkpoint is authored by retail code. The direct mission
bridge replaces the frontend handoff which normally invokes retail capture
function `0x800AD48C`. Ordinary missions begin that capture once application
state 0 and `LEVEL` are live and accept it only after the serialized retail
command stream is structurally complete. Clean-start packages which dispatch
the shared special-opening object event `0x72` do not receive that synthetic
frontend capture at all. Their checkpoint-present flag remains zero through
the COLO and WRECK parachute openings; only a later call from retail mission
logic may make a checkpoint valid.

Restore function `0x800AD9F4` therefore has two retail-authored outcomes. With
a set checkpoint-present flag and complete stream, the host observes the edge
but executes the original function. Instruction-budget slices inside that
synchronous restore advance the guest hardware scheduler, allowing its CD
command, DMA, callbacks, and mission reload to complete. With the flag clear,
the request means Restart Mission (or failure before the first checkpoint):
the guest yields and the campaign host reconstructs the active package without
returning to title or replaying its SOL movie/briefing. No CPU/RAM/CD/SPU host
snapshot substitutes for either lifecycle, and no actor, script, camera, or
mission state is synthesized.

MENU keeps three restart/exit paths separate. Restart At Last Checkpoint
reaches the checkpoint restore above. Restart Mission calls the retail package
teardown from MENU's dedicated callback, so the runtime yields at that boundary
and asks the native campaign host to reconstruct the same mission. Save and
Quit uses MENU's separate shared-outcome caller after its confirmation and
menu-side cleanup. It yields to the native durable-save owner, which presents
the slot picker, stores the current mission cursor if requested, and then
returns to TITLE. Caller identity is used only to distinguish these authored
callbacks which converge on shared executable functions, not to replace their
gameplay behavior. Both callbacks have already dismantled application state 7
before reaching those shared functions, so their exact architectural return
addresses are the lifecycle discriminator; testing the former application
state at that point rejects every authentic confirmation. The validation
matrix executes the loaded callbacks through their real teardown and requires
both distinct yields.

This native Save and Quit handoff does not yet claim retail memory-card
checkpoint fidelity. The durable slot records the active mission cursor and
player snapshot, but does not serialize the guest checkpoint command stream;
loading it re-enters the mission package. The independent recompilation route
executes retail's complete card protocol and is the differential reference for
the eventual last-checkpoint persistence contract.

Connected SF2 missions do not import the completed mission's live player RAM.
Each destination package authors its playable character, health, armor and
inventory during retail bootstrap. Injecting the preceding snapshot after
that bootstrap once armed Mission 2 Lian with Mission 1 Gabe's arsenal. The
native save/campaign shell may still retain the exact 34-slot sequel snapshot
for format compatibility and inspection, but `runSf2GuestScene` treats it as
metadata and leaves the new guest runtime untouched. The completion-flow gate
poisons the outgoing snapshot with distinctive vitals and inventory, boots
every same-disc destination, and fails if that state appears in the new
mission. Mission 8-to-9 remains owned by the two-disc resolver, while Mission
21 is terminal and has no destination runtime.

The GP0 side-effect stream retains incomplete VRAM-upload packets between
updates and compacts consumed words, so long product sessions do not grow an
unbounded capture buffer.

Retail `LoadImage` at `0x800F1598` is also an explicit HLE observation
boundary. The room streamer and the resident TIM helper can submit their pixel
payload through GPU DMA without leaving a reconstructible raw GP0 stream at
the public display boundary. The bridge therefore validates each rectangle
against 1024x512 PS1 VRAM, bounds and copies its payload from guest RAM, and
retains only the latest upload for a destination rectangle. Mission 3
initialization yields 543 valid TIM/CLUT uploads across 206 current
destinations; none overlap the native framebuffer. Retail ZCLUT writes at
`(768,480)..(1023,511)` are additionally mirrored through the live
bank-specific row map into the native framebuffer-safe CLUT residency at
`(0,192)`, matching the relocation applied to guest draw packets.

Native replay preserves the retail `E5` draw offset and therefore submits raw
ordering-table coordinates without adding a second host centre offset. Retail
`E1` draw mode is also normalized only at the presentation boundary: its
draw-to-display bit is forced on after TPAGE relocation because PsyCross maps
that PS1 VRAM control bit to an offscreen framebuffer. Without these two host
presentation translations, the guest runs correctly but the product window is
black or shows a doubly shifted partial frame.

This boundary is intentionally narrower than a campaign runtime and currently
requires Disc 1 and Mission 3. It is a development runtime, not yet a playable
alpha. Interactive audit invalidated the earlier GP0-hash-only claim:
SF2 display publication is 60 Hz rather than 20 Hz, its alternating VRAM pages
must be normalized to one host framebuffer, and `HWAY.SS` must activate LEVEL
after its asynchronously populated registry becomes ready.

The corrected control gate compares guest player state. Over 3,000 display
frames, neutral input leaves Gabe at x=4206 while held-forward reaches x=1389;
both retain 150 health and trigger no checkpoint restore. Hardware time over
600 display frames produces exactly 441,000 PCM frames. These gates establish
real input and audio cadence. Interactive validation now confirms the authored
camera handoff, Chance encounter, player control, target lock, and contextual
execution animation. Retail XA audio now passes through the same guest PCM
handoff after correcting relative raw-sector MSF headers, segmented CD FIFO
requests, and the two-byte `Setfilter` contract. A 1,500-frame probe produces
the exact 1,102,500-frame PCM duration with 273,140 nonzero frames and first
XA output at display frame 22. The guest HUD callback is not present in the
direct TITLE-to-HWAY path, so the product projects live guest health, armor,
equipped item, ownership and ammunition into the existing native SF2 HUD atlas
after the authored opening. PC adaptation maps A/D to retail L2/R2 strafe,
accumulates relative mouse motion until the 20 Hz PAD sample consumes it, and
emits sampled Select edges for middle-click, wheel, bracket, and number-key
weapon actions. A post-truck guest probe establishes the retail Mission 3
selection ring as item `20 -> 2 -> 4 -> 8 -> 20` (knife, pistol, M16,
shotgun). The adapter plans only forward retail Select edges over the
ascending owned-item ring, so previous selection and signed wheel movement
preserve direction while the guest remains the sole inventory owner.
The guest remains authoritative for all resulting movement, aim, inventory
and combat mutations.

Interactive validation additionally confirms live XA dialogue, firearm pickup,
Select-based switching to the M16, guard combat, contextual execution, and
two-axis first-person mouse aim, A/D strafing, door kicking, traversal through
doors, climbing, and stable ordinary play beyond two minutes. Mouse
sensitivity remains provisional. The native HUD pass establishes its own
zero-offset 384x240 draw environment after the guest frame; otherwise the
retail centre-origin E5 state shifts top-left status primitives toward the
screen centre. The reticle remains guest-authored; drawing a native fallback
at the same time produced a duplicate large/small third-person sight.

The direct mission handoff also omits the outer retail lifecycle call which
normally services the sound-command flush at `0x80104B40`. Mission gameplay
did allocate and configure SPU voices, but their pending key masks were never
committed. The product registers that exact retail routine in interrupt
callback slot 6 and services it at 120 Hz. Retail checkpoint cleanup clears
the slot, so the runtime suspends sound service while the synchronous loader
owns guest resource state and reinstalls it once the checkpoint reload
settles. Calling the flush from the loader's instruction-budget hardware
slices would re-enter sound/resource globals at a boundary retail never uses.
A 7,200-update probe crosses retail restore at update 6,834 and continues with
23 peak active voices, 4,166 key-on writes, nonzero PCM, and the sound callback
still installed. This restores retail SPU sound effects without replacing
voice allocation or mixing. SF2 produces 735 PCM frames per 60 Hz presentation
update, so its OpenAL callback uses a 12-block (~35 ms) recovery threshold
rather than starting from a single 10 ms device quantum; this prevents the
producer cadence from repeatedly starving an otherwise healthy continuous
stream. Music still requires an interactive audit.

The SF2 pistol HUD uses the embedded `PISTOL2A/B` assets rather than the
intentionally empty SF1 definition. Four host-only alias pages isolate the
complete native HUD atlas from guest framebuffers and mission textures; this
also makes the full-height `KNIFEA/B` placement safe.

Mission failure and blocking CD/card paths can wait for hardware without
submitting an ordering table. During one host-boundary search, the runtime
records PCs at exhausted 50,000-instruction slice boundaries. Recurrence of
the same PC proves that guest execution has entered a cycle; only then are all
devices advanced by the retired guest cycles. The rule recognizes no routine,
API, return address, MMIO register, mission state, or display operation. A
single heavy slice remains clock-neutral, while a completed GPU boundary is
padded only to its next 60 Hz event. This keeps ordinary gameplay at exactly
735 PCM frames per update while allowing CD, DMA, SPU, timers, callbacks, and
future device waits to make deterministic progress. PC-synthesized face actions also omit the
processed PAD's derived face-button axes, preventing Cross crouch from
simultaneously becoming backward movement. Physical controller buttons retain
their retail-derived axes.

The SF2 product renderer is a persistent inner loop and therefore begins a
fresh PsyCross scene after every `EndScene`. Before `PsyX_BeginScene` it
establishes a black, display-sized background environment so the native color
and depth targets are cleared without modifying emulated VRAM. This is
required after collapsing retail's alternating PS1 display pages onto one
host framebuffer; omitting the begin/clear accumulated snow and world pixels
across swaps as mouse-trail ghosting.

The renderer's two calls to the resident `AddPrim` helper are guarded by the
live ordering-table base and bucket count. Retail can transiently pass an
unclamped depth bucket (observed as `0x925C` for a 16-entry table); the
resulting KSEG address crosses the 2 MiB RAM mirror and aliases resident
renderer code. The presentation bridge clamps only those two call sites to
the final valid bucket. This prevents the upstream alias rather than repairing
executable words after corruption.
