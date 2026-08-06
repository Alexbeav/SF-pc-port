# SF2 versus SF1 presentation delta

## Purpose

This report tracks presentation behavior which can transfer from the SF1 PC
runtime to SF2, and the evidence for every place where the sequel needs a
different adapter. It is not an upstream-conformance checklist. The immediate
target is stable PGXP geometry, perspective-correct retail textures and
high-refresh presentation for the USA/NTSC-U SF2 release.

## Compared baselines

- SF1 upstream reference: `Madxbio97/SF-pc-port` commit
  `a4a93ccca9b8f5a5fb77fb266280546e7d72965e` (Public Test 23).
- SF1 chase-mouselook validation: `Alexbeav/SF-pc-port` commit
  `c24ce313b1356da2e3d5615f3001f6000e399f99`, submitted upstream as
  [PR #97](https://github.com/Madxbio97/SF-pc-port/pull/97).
- SF2 modern baseline: commit
  `d33acac5ed2a68f908861f4762b8bd2683d28d22` on
  `feature/sf2-modern-presentation`.
- SF2 correctness source: the `research/sf2-full-bringup` branch.
- Retail oracle: user-owned USA/NTSC-U SF2 executable and both discs, with the
  deterministic replay and Release test matrix documented in
  [`tests/data/sf2/README.md`](../tests/data/sf2/README.md).

The two Git histories do not share a merge base because the local project was
imported through a sanitized source snapshot. Comparisons therefore use tree
content, named upstream commits and behavior, not merge ancestry.

## Executive finding

The underlying PS1/GTE/PsyCross architecture is shared. In particular, the
current SF1 and SF2 trees have byte-identical `PsyX_GTE.cpp`, PGXP cache types
and the generic polygon clipper. The material difference is where 3D
provenance crosses into host presentation:

- SF1 rebuilds world primitives from scene state. It registers exact view,
  object and vertex data before PsyCross converts the polygon.
- SF2 executes the retail renderer in the guest, deep-copies its DMA ordering
  tables and replays already-projected GP0 packets. Those packets contain
  quantized screen XY and UV values, but not the GTE intermediates needed for
  PGXP depth or perspective correction.

Consequently, setting `g_cfg_pgxpTextureCorrection` for SF2 is insufficient.
The SF2 product path also uses `DrawPrim`, whose single-primitive PsyCross path
explicitly assigns the PGXP sentinel. SF2 must preserve guest transform
provenance and attach it to captured world polygons before replay. Replacing
the verified guest renderer is not required for this first approach.

## Transfer map

| Concern | SF1 implementation | Current SF2 behavior | Transfer decision |
| --- | --- | --- | --- |
| Guest simulation rate | Deterministic 20 Hz gameplay | Deterministic 20 Hz retail guest | Share the fixed-tick contract unchanged. |
| Presentation clock | Wall-time presentation with immutable previous/current snapshots | Repeats the newest persistent guest framebuffer page between guest publications | Transfer the presentation clock and discontinuity model after polygon provenance exists. |
| GTE execution | Native renderer calls PsyCross GTE and immediately populates its PGXP cache | R3000 interpreter executes the retail GTE; the host sees only resulting GP0 packets | Observe RTPS/RTPT in the interpreter and preserve exact intermediates without changing registers or timing. |
| PGXP cache | Exact cache index travels with each native primitive | SF2 `DrawPrim` forces the cache index to `0xffff` | Add an explicit captured-packet PGXP submission path; retain fallback for unmatched packets. |
| Geometry depth | Exact camera-space depth is available during native projection | Depth is absent from GP0 polygon packets | Carry observed GTE camera depth with matched packet vertices. |
| Texture perspective | OpenGL perspective interpolation follows PGXP vertex Z/W | SF2 replay is screen-space/affine | Use the same PsyCross shader after all vertices of a world polygon have trustworthy depth. |
| Precise UVs | Native near-plane clipping attaches floating UVs with `PGXP_SetLastTextureCoords` | Guest GP0 carries authored byte UVs; no host 3D clipping occurs | Preserve authored UVs initially. Add precise generated UVs only if SF2 host clipping is introduced. |
| Near clipping | Shared polygon clipper interpolates XYZ/UV/color | Performed by the retail renderer before packet publication | Do not clip again until guest provenance proves a polygon crosses the host near plane. |
| Widescreen | SF1 widens native projection | SF2 scales guest GTE X around OFX; HUD submissions remain centered | Retain SF2 behavior. Projection trace must record the already-applied Q16 horizontal scale. |
| HUD and menus | Native/guest presentation paths deliberately bypass world PGXP | Retail auxiliary OTs and native supplements are screen-space | Keep them on the affine 2D path using submission classification. |
| Framebuffer semantics | Native world with PS1-compatible VRAM support | Two persistent retail display pages mirrored by two native color targets | Preserve the SF2 two-page model and never interpolate framebuffer uploads or menus. |
| Camera cuts and transitions | Aim changes, pause, movies and state changes suppress interpolation | No SF2 world interpolation yet | Derive explicit SF2 discontinuities from application state, page changes, sequence gaps, pause/movie/restart and packet identity changes. |
| Chase mouse yaw | Optional host input preserves retail turn input and writes a proportional facing vector at `0x80037B08` | The instruction-aligned live boundary is `0x80053464`; retail turn input remains active | Share the dual-path behavior, not either game's address: retail owns locomotion/body turning while proportional mouse yaw removes the analog response ceiling. |
| Chase mouse pitch | Optional host presentation changes chase framing while respecting camera locks | Desired/rendered pitch is adapted through the live player-owned camera wrapper and clamped to +/-512 | Keep pitch host-side and reject accumulated input whenever scripted camera ownership replaces the player. |

## First implementation seam

`GteRuntime::executeCommand` now optionally publishes a
`GteProjectionTrace` for RTPS and RTPT. Each vertex records:

- exact Q12 camera-space X, Y and Z accumulators;
- exact Q16 projected screen X and Y before SXY packing/saturation;
- OFX/OFY, projection H and the packed retail SXY result; and
- SF2's already-applied horizontal projection scale.

`R3000Runtime` exposes this through a separate post-command observer. The trace
is not stored in `GteState`, so it does not enter quick states, RAM digests or
guest-visible execution. Existing GTE integer results remain the regression
oracle.

### Mission 3 measurement

A 240-presented-frame Disc 1 Mission 3 diagnostic run established that the
trace reaches real SF2 packets, but also rejected packed screen position as a
safe identity key. Early frames ranged from 0 of 911 to 431 of 1,239 world
polygon packets matched, and later frames were commonly below 60 of roughly
1,500. One frame observed 4,201 projected vertices with 637 ambiguous packed
positions. Shared screen coordinates and repeated transforms therefore make
value-only correlation both sparse and temporally unstable.

PGXP replay remained disabled at that measurement checkpoint. The current
tree now carries provenance through direct `SWC2` stores and the common
`MFC2`/load-delay/`SW` sequence, then
correlates each projected vertex with the exact RAM word later captured in a
GP0 packet. Packed-position matching remains a diagnostic fallback when no
store trace exists, not an enablement gate.

### First visible PGXP slice

The renderer now emits fully address-matched SF2 polygon vertices into the
same PsyCross PGXP cache used by SF1 and preserves the cache index through a
narrow immediate-primitive submission entry point. Eligible polygons receive
subpixel screen positions and camera depth for perspective-correct texture
interpolation. Incomplete, ambiguous, UI and non-polygon packets retain the
existing affine path. PGXP-Z remains disabled for this slice, preserving SF2's
retail ordering-table ownership while geometry and texture behavior are
validated independently.

Set `SF2_DISABLE_PGXP=1` before launch for an affine A/B comparison without
rebuilding.

## Mouse-camera transfer result

The SF2 camera work transferred back to SF1 cleanly only as a behavioral
contract. SF1 `0x80037B08` and SF2 `0x80053464` are instruction-aligned common
camera/facing boundaries. At both boundaries register `s2` retains the
controller pointer, and the processed proportional direction vector is written
at controller offsets `+0xcc`, `+0xd0`, and `+0xd4`.

Horizontal chase input requires two simultaneous paths:

1. retain the retail turn axis so locomotion, animation, collision and the
   player body's facing remain coupled; and
2. inject proportional mouse yaw at the common facing boundary so response is
   not limited by retail analog acceleration.

Directly replacing the retail axis produced three rejected behaviors during
SF1 interactive testing: no horizontal response, Logan rotating without the
camera, and a detached orbit/pan camera resembling a photo mode. Direct camera
yaw alone is therefore not equivalent to chase mouse look. The accepted
dual-path implementation was interactively confirmed while standing and
moving, then covered by a synthetic R3000 hook test. It leaves controller and
keyboard behavior unchanged when disabled.

Vertical chase pitch remains a presentation extension rather than player
movement. It must reset on aim entry and camera discontinuities, obey the
retail clamp, and run only while the active camera wrapper is owned by the live
player. Scripted, locked and control-locked cameras retain authority.

## Packet matching requirements

The address-backed matcher associates observed vertices with polygon words
later captured from guest RAM. A match is eligible only when:

1. the packet is a world polygon submission rather than HUD/menu/VRAM work;
2. every polygon corner has a provenance record for its exact packet-word RAM
   address whose packed SXY agrees with the retail packet;
3. duplicate packed SXY values do not resolve to conflicting exact vertices;
4. projection parameters belong to the same guest presentation generation;
5. no trace or PGXP cache capacity limit was exceeded; and
6. all depths are positive and finite after conversion.

An ambiguous or incomplete polygon stays on the retail affine replay path. A
modern option must improve matched polygons without making unmatched packets
disappear or corrupt the retail-compatible profile.

## High-refresh requirements

After packet provenance is stable, SF2 can retain two immutable world packet
snapshots and match polygons by guest address, opcode, material fields and
vertex count. Interpolation is presentation-only:

- interpolate exact screen/camera provenance, not guest RAM or gameplay state;
- retain current authored UV, color, CLUT, TPAGE and ordering-table ownership;
- never interpolate HUD, uploads, framebuffer copies, movies or menus;
- snap to the current frame on cuts, page changes, pause, death/restart,
  checkpoint load, room changes, packet identity changes or large motion; and
- keep the 20 Hz guest and 120 Hz audio/device cadence unchanged.

## Validation and measurements

Every slice must retain the complete 21-test Release baseline. Focused tests
will additionally cover:

- RTPS and RTPT provenance without changes to integer GTE output;
- the SF2 Q16 widescreen projection scale;
- ambiguous and incomplete packet matches falling back safely;
- 2D submission exclusion;
- cache saturation; and
- interpolation discontinuities and endpoint stability.

Visual comparisons will use Mission 1 outdoor/parachute/camera coverage,
Mission 3 truck/HUD/checkpoint coverage and Mission 6 NVG/scope coverage at
2560x1440 during development. The performance acceptance floor is 3840x2160
at 60 Hz on a discrete GPU, with 60/120/144 Hz behavior measured separately.

## SF3 implications

The reusable collection boundary is an immutable guest-presentation packet
plus optional transform provenance. The GTE observer and PsyCross submission
adapter are hardware-level facilities; packet classification, discontinuity
signals and any renderer-specific exceptions remain game modules. SF3 should
therefore be able to reuse the same mechanism without inheriting SF2
addresses, mission logic or framebuffer assumptions.
