# Implementation map

## Source checkpoints

The workstream starts at `001b1e8` and is represented by these commits:

```text
f2572fa  Establish SF3 retail guest bring-up workstream
2faaadb  sf3: reach retail Mission 1 load boundary
0704870  sf3: load TOKYO retail mission assets
f845828  sf3: enter stable retail Mission 1 gameplay
d84e475  sf3: connect retail Mission 1 product runtime
```

Projects importing pieces should cherry-pick only after comparing their shared
runtime baseline. The large `sf_tool` probe change depends on the same
`LegacyGameplayVm` and PSX device APIs as this branch.

## Changed source areas

### `include/sf/game/sf3_runtime.hpp`

Defines the independent `Sf3GuestRuntimeProfile`, exact SCUS-94640 addresses,
SF3 presentation-capture entry point, product diagnostics and the public
`Sf3GuestMissionRuntime` PIMPL API.

The profile is the canonical address source. Avoid scattering literals into
other projects; if a boundary is adopted, copy it into that project's own
revision-specific profile and retain the executable hash gate.

### `src/game/sf3_runtime.cpp`

Implements the continuous product runtime:

1. Open and validate the user-owned disc.
2. Parse `FOG/TOKYO.FOG` and retain immutable member spans.
3. Bind BIOS core services and the optional formatted blank memory card.
4. Bind VSync, CD pending/control/ready/completion and SCUS-94640 wrapper
   mirrors.
5. Bind exact `CdSearchFile` metadata and resident/member-read transports.
6. Bind title, TITLE2, MENU, state-8 and gameplay processed-PAD returns.
7. Observe retail renderer submissions and capture authored ordering tables.
8. Service SPU-DMA, CD-ready, scheduler, card and movie callbacks.
9. Auto-operate retail frontend/loading until state `0`, depth `1`.
10. Advance one authored display publication per host update and expose PCM.

The constructor currently performs the whole frontend-to-gameplay bootstrap
synchronously. This is suitable for the first alpha but prevents the product
from presenting and interactively operating those retail frontend frames.

### `src/game/legacy_gameplay_vm.cpp` and header

Shared changes relevant to other projects:

- optional in-memory formatted blank card in BIOS A0/B0 calls;
- correct `CdControl` response-buffer copying;
- actual PsyQ completion reason instead of a disk-error constant;
- Setloc/Setmode retail mirror support supplied by the game profile; and
- preserved asynchronous completion behavior.

Focused tests cover these contracts. These are shared platform fixes, but
their game-specific addresses remain profile-owned.

### `apps/sf_tool/main.cpp`

Contains the detailed research probes:

- `probe-sf3-guest-bootstrap` — executable entry through verified TITLE gate;
- `probe-sf3-title-shell` — title/movie/card/state transition through Mission
  1 requests and later TOKYO/gameplay gates depending on budget; and
- `probe-sf3-product-runtime` — compact continuous-class smoke with neutral or
  forward PAD input.

The large title-shell probe intentionally keeps verbose forensic counters and
state traces. The product class is smaller and should not replace the forensic
probe until it exposes equivalent diagnostics.

### `src/platform/psycross_scene_viewer.cpp`

Adds `runSf3GuestScene` and dispatches SF3 Mission 1 to it. It:

- uses native mission construction only to seed PS1 texture residency;
- converts configured keyboard/mouse/controller actions to retail PAD;
- routes `P` to Start and Escape to host return-to-title;
- advances the retail guest at 60 Hz display cadence;
- queues retail PCM to `PsyCrossAudioOutput`; and
- submits captured retail OTs through the persistent two-page guest renderer.

For SF3 missions other than index zero, it returns to title. It must not fall
through into native substitute gameplay.

### Tests and documentation

`tests/r3000_runtime_tests.cpp` verifies CD response/completion behavior and
blank-card sector round trips. `tests/test_main.cpp` locks the SF3 profile.
`docs/SF3_PC_PORT.md` is the milestone contract; the dedicated devlog is the
chronological evidence record.

## Authority split

| Retail guest owns | Host owns |
| --- | --- |
| Application state and stack | R3000/device scheduling |
| Mission scripts and objectives | Immutable disc and member delivery |
| Actor AI, combat and animation | BIOS/CD/SPU callback transport |
| Camera, checkpoints and difficulty | Keyboard/mouse/controller encoding |
| Title/menu/loading decisions | GP0 submission to PC graphics backend |
| SPU/XA/STR command production | PCM output and final STR presenter |
| Save/campaign semantics | Optional storage backend and packaging |

The host must not write application-state globals, fabricate gameplay, skip
retail overlay parsing or reinterpret mission data as an alternate game.

## Product data flow

```text
user-owned cue/bin
    -> GameDisc / DiscCdRomMedia
    -> SCUS-94640 + retail overlays in LegacyGameplayVm
    -> retail CdSearchFile/member requests
    -> immutable ISO/FOG byte transport
    -> retail application/mission/controller/render/audio code
       -> processed PAD boundary <- host input mapping
       -> GPU ordering tables   -> presentation capture -> PsyCross
       -> SPU/XA PCM            -> bounded queue         -> OpenAL
```

## Current containment and limitations

- Product mode is Mission 1 only.
- Frontend operation is deterministic and automatic, not yet interactive.
- The runtime publishes selected state-0/state-7 OTs rather than a proven
  complete same-tick composition of every submission.
- Product texture residency is seeded from the native mission package; retail
  position/room ownership is not yet connected to texture-streamer updates.
- Actual STR frames are not decoded/presented in the headless bootstrap.
- No SF3 quick states, checkpoint restore, mission completion, campaign carry
  or persistent saves are connected.
- Mouse movement does not yet have an independently verified SF3 camera hook;
  mouse buttons/actions and controller/keyboard PAD transport are present.
- The product executable compiled, but its visible rendering/audio path awaits
  human validation. SDL offscreen stalls before PsyCross/OpenGL initialization.

These are open engineering boundaries, not authorization to add native
gameplay substitutes.
