# Syphon Filter 2/3 port notes

This document records the verified compatibility boundary between the existing
Syphon Filter runtime and the two sequel discs. It contains no retail assets.

The narrative record of the first playable SF2 bring-up is in the
[2026-07-28 SF2 devlog](devlogs/2026-07-28-sf2-bring-up.md).
The whole-engine reverse-engineering maps are in
[`SF2_EXECUTABLE_MAP.md`](SF2_EXECUTABLE_MAP.md),
[`SF2_SHARED_SYSTEM_MAP.md`](SF2_SHARED_SYSTEM_MAP.md), and
[`SF2_MISSION_SCRIPT_VM.md`](SF2_MISSION_SCRIPT_VM.md).

## Supported executable profiles

The game catalog recognizes:

- Syphon Filter 2 USA Disc 1 (`SCUS-94451`)
- Syphon Filter 2 USA Disc 2 (`SCUS-94492`)
- Syphon Filter 3 USA (`SCUS-94640`)

Both SF2 discs contain the same executable. The profiles retain the
game-specific VSync, libcd command, CD-ready callback, and streaming-audio
locations required by the executable bootstrap probe.

## Confirmed compatible mission data

`sf_tool inspect-mission-archive` validates a mission without constructing the
SF1-specific native campaign shell. The following retail structures are
already shared:

- FOG archive directory
- `WLDEMD.HOG` world models
- `VRAM.HOG` and optional `VRAM1.HOG` texture banks
- room visibility and initial-room data in `<MISSION>.DAT`
- object definitions, transforms, room membership, paths, and player index in
  `<MISSION>.BIN`
- raw-sector virtual-CD backing

All 40 single-player sequel archives now pass complete native
mission-package and gameplay construction, not just archive-level readers:

- SF2 Disc 1: 8/8
- SF2 Disc 2: 13/13
- SF3: 19/19

Command-line mission validation is disc-driven as well. It no longer caps
selection at SF1's 20 entries, so SF2 mission 21 (`CHINBOSS`) and the mapped
SF3 campaign can reach `--scene-test`.

COLO's 31 world rooms, 10,148 polygons, two VRAM banks, 181 mission records,
six articulated object models, and 166 resident animation clips validate.
`--scene-test` enters the native PGXP/Z-buffer renderer, submits the first
gameplay frame, and remains in the presentation loop.

The sequel BIN header tag is `0x00990318`. Its definition, object, room, and
player fields move to offsets `0x1c`, `0x20`, `0x24`, and `0x18`
respectively; the table bodies retain the SF1 layout. SF2 `SLUMS.DAT` also
proved that the fixed 16-byte resident-room table may be completely full
without a trailing `0xff`.

## SF2 mission resources

The two discs use the following selection-index mapping:

| Index | Disc | Resource |
|---:|---:|---|
| 0 | 1 | `COLO` |
| 1 | 1 | `AIRBASE` |
| 2 | 1 | `HWAY` |
| 3 | 1 | `BRIDGE` |
| 4 | 1 | `AIRBASEX` |
| 5 | 1 | `TRAIN` |
| 6 | 1 | `TRAIN2` |
| 7 | 1 | `WRECK` |
| 8 | 2 | `DISCO` |
| 9 | 2 | `DARKMUSE` |
| 10 | 2 | `MOSCOW` |
| 11 | 2 | `MOSCOW2` |
| 12 | 2 | `MOSCOW3` |
| 13 | 2 | `GARAGE` |
| 14 | 2 | `GULAG` |
| 15 | 2 | `GULAG2` |
| 16 | 2 | `LABS1` |
| 17 | 2 | `LABS2` |
| 18 | 2 | `SLUMS` |
| 19 | 2 | `SLUMS2` |
| 20 | 2 | `CHINBOSS` |

`sf_tool inspect-disc-info` joins this mapping to the labels in `DISK1.INF`
or `DISK2.INF`.

## SF3 mission resources

SF3 stores the main resource-name table in reverse campaign order and
references `SNOWCAMP` separately. The native campaign mapping is:

| Index | Mission | Resource |
|---:|---|---|
| 0 | Hotel Fukushima | `TOKYO` |
| 1 | Costa Rican Plantation | `JUNGLE` |
| 2 | C-5 Galaxy Transport | `JUNGLE3` |
| 3 | Pugari Gold Mine | `AFRICA1` |
| 4 | Pugari Complex | `AFRICA2` |
| 5 | Kabul, Afghanistan | `AFGHAN2` |
| 6 | S.S. Lorelei | `LONDON1` |
| 7 | Aztec Ruins | `JUNGLE2` |
| 8 | Waterfront | `LONDON2` |
| 9 | Docks Final Assault | `LONDON3` |
| 10 | Convoy | `AFGHAN1` |
| 11 | The Beast | `AFGHAN3` |
| 12 | Australian Outback | `TRIAGE1` |
| 13 | St. George Australia | `TRIAGE2` |
| 14 | Paradise Ridge | `RIDGE` |
| 15 | Militia Compound | `SNOWCAMP` |
| 16 | Underground Bunker | `MCAVES` |
| 17 | Senate Building | `SENATE` |
| 18 | DC Subway | `SENATE2` |

## Other verified deltas

- SF1 uses `COMMON/TITLE.HOG`; SF2/SF3 use root `TITLE.HOG`.
- SF2 uses `SCENES1.XA` or `SCENES2.XA`; SF3 uses `SCENES1.XA`; SF1 uses
  `XA/INGAME.XA`.
- The sequel scene archives use fifteen playable interleaved XA channels.
  Disc 1 has 52 synchronized clip groups, Disc 2 has 74, and SF3 has 76;
  channel 16 is filler. `sf_tool map-xa-streams` reproduces every clip LBA.
- SF3 TIM files use metadata bits from mask `0x84000000` (both the complete
  mask and the observed `0x80000000` subset) without changing their standard
  pixel-mode or CLUT payload.
- SF3 replaces the SF2 `VIDEO.TIM` title visual with `MINIGAME.TIM`.
- SF2/SF3 register a direct libcd data-ready callback. SF1 stores an indirect
  callback pointer. Both forms now use one VM service path.
- SF2 keeps PCHAN, INTRFACE, SPFX, and BEEPSX resident HOGs inside the
  executable instead of an SF1-style `COMMON` directory.
- Some SF2 debris GMDs retain the retail empty-extents sentinel
  (`+32000/-32000`); their packed vertices supply the usable bounds.
- SF2 interface pixels occupy the complete 256-line authored texture-page
  range, while SF1's restored HUD policy reserves the lower portion sooner.
- EMD flag bit 4 alone selects `VRAM1.HOG`; bits 5 through 7 are independent
  scene flags. Decoding the complete high nibble as a bank number only appeared
  to work in early SF1/SF2 rooms and failed six Disc 2 missions.
- SF1/SF2 EMD polygons encode compact transformed-vertex offsets in three-byte
  units. SF3 changes that unit to two bytes.
- SF1/SF2 non-flat-lit HMD triangles encode transformed-vertex offsets in
  twelve-byte units (flat-lit models use eight). SF3 uses eight-byte units for
  both forms. These EMD/HMD stride changes are the first geometry-format delta
  that applies to all 19 SF3 campaign missions.

## Executable bootstrap status

The probe mounts the real CUE track as CD-ROM media, supplies the required
PsyQ BIOS calls, keeps startup interrupt-neutral, services GPU/SPU/CD DMA, and
dispatches the profile's CD-ready callback.

SF2 now completes the probed executable entry and performs a real 2048-byte CD
DMA read. SF3 passes its earlier CD/GPU/BIOS boundaries and reaches later
startup state, but still enters an uninitialized guest target after its BIOS
event/SPU setup. That is the next executable-runtime boundary to identify; it
does not block SF2 mission-data integration.

## Mouselook migration

The retail data structures needed by the renderer and mission loader are
largely shared, but the SF1 mouselook hook instruction signatures do not map
directly into the sequel executables. Port the behavior through newly
identified sequel camera/actor semantic boundaries rather than copying SF1
addresses. The existing host aim ray, first-person locomotion, Q/E lean, and
A/D strafe behavior remain the reference contract.

The COLO scene-test profile currently uses that native controller directly,
so chase/aim mouselook and collision-resolved movement can be tested before
the SF2 guest camera hooks are mapped. Retail SF2 scripting, actors, mission
audio callbacks, UI VAB cue IDs, and sequel-specific effect families remain
explicit follow-up boundaries; the scene-test path does not claim those are
implemented.

## Mission-script ABI delta

The 128-slot action-table shape is shared, but individual action contracts
are not guaranteed to be one-to-one. SF2 uses separate actions `0x08`,
`0x09`, and `0x0a` for objective reveal, failure, and completion, plus
`0x1e`/`0x1f` for parameter failure/status. SF3 consolidates the objective
family into action `0x08` at `0x800b4c9c`; operand 0 selects complete, clear,
fail, secondary-status set, or reveal. It consolidates parameter set, clear,
and fail into action `0x1e` at `0x800b4df8`.

This comes from handler disassembly and progress-record mutation rather than
descriptor alignment. The VM binding must be selected by detected game
profile and consume each game's descriptor metadata. Sharing only the
high-level objective/parameter service is safe.

SF3 also fills four predicate slots that are empty in SF2. Their resident
handlers are now bounded at the storage/call level:

| Slot | SF3 handler | Confirmed behavior |
|---:|---:|---|
| `0x27` | `0x800b3788` | combines object lookup with object-node byte `+8` bit `0x40` |
| `0x28` | `0x800b5aa0` | runs a shared state refresh then tests an enabled global halfword against operand 1 |
| `0x29` | `0x800b3654` | returns global script-state byte `0x80122278` |
| `0x2a` | `0x800b3664` | returns GP-relative script-state byte `+0x711` |

The first two source-language meanings remain neutral, but all four slots are
real runtime predicates rather than unused descriptor padding. A shared VM
must therefore select a 64-slot predicate profile per game.

## AIRBASE grounded-play findings

`AIRBASE` starts in DAT visibility state 2 at source 101
(`293, 0, -382`). The contact floor is resident connector `AROOM04.EMD`
(model 18), whose own visibility row is not the mission's starting portal
envelope. Native room tracking must therefore keep resident models out of
visibility-state ownership: treating the contact model number as the room
changed state 2 to state 18 on the first step and discarded rooms visible
from the opening envelope.

The `AIRBASE.OVL` payload contains only 168 bytes of executable MIPS code. Its
mission-specific dispatcher handles runtime classes `0x31` and `0x49`;
ordinary exits (`0x34`), doors (`0x0e`), switches (`0x54`), pickups, and actor
controllers are shared executable systems. This makes a reusable sequel
object-class layer the correct port boundary rather than AIRBASE-specific
interaction scripts.

Retail `.SS` analysis now proves where the missing authored behavior lives.
AIRBASE contains nine compiled programs (`DAMNIT`, `OPEN_SESAME`,
`FRONTGATE`, `STRAGGLER`, `BOXTRAP`, `COME_WITH_ME_DOCTOR`, `ACTIVATION`,
`LOOKING_FOR_TROUBLE`, and `LEVEL`). The tiny overlay registers callbacks;
the shared `.SS` VM owns the ordinary objective, timer, door, dialogue, and
progression program flow. The native AIRBASE progression below remains a
reversible bring-up approximation until those shared opcode bindings replace
it.

BIN class IDs in the sequels carry shared behavior flags above their low
16-bit class family. For example, all four AIRBASE `DOOR.TMD` definitions are
class `0x5000e` and its five `EXIT.TMD` definitions are `0x20034`. Native
collision and interaction now resolve those as door `0x0e` and exit `0x34`.
The first reusable interaction slice makes flagged door meshes solid while
closed, opens linked door halves together, swaps `LOCKERA/LOCKERB`, and lets a
class-`0x54` switch act on its linked source.

AIRBASE actor records also establish sequel faction class `0x03` (`BUDDY.HMD`)
as airbase opposition. Sequel `0x02`/`0x20002` remain ally classes, while
`0x01` is hostile as in the first game.

The opening-room collision failure was reproduced without an interactive
test. The native resolver inherited SF1's 390-unit capsule height and
projected every wall-polygon edge into XZ without testing the edge's vertical
span. It consequently treated AIRBASE's below-floor and overhead door-frame
edges as full-height barriers. Sequel movement now uses its 260-unit actor
height, edge collision requires overlap with that vertical span, and quads
test only their four perimeter edges rather than their internal diagonals.
An offline deterministic forward probe crossed from AIRBASE visibility state
2 into room 1 after these corrections.

The SF2 executable's resident item-name table also proves that sequel item
attributes cannot be cast to SF1 `WeaponId`: slot 3 is the Colt .45, slot 4 is
the M16, slot 8 is the shotgun, slot 19 is the hand taser, and slot 22 is the
fragmentation grenade. The complete supported table is translated explicitly;
currently unsupported H11 and crossbow records remain unmapped instead of
silently becoming unrelated SF1 equipment. This mapping is used for both NPC
loadouts and class-`0x48` pickups.

AIRBASE now starts unarmed and without armor, displays its authored two-minute
countdown, and clears that countdown when source 103 (the adrenaline) is
collected. Its linked class-`0x48` combat-gear chain grants the authored .45,
armor, and hand taser. Lian begins at 15/150 health: the documented ten-percent
retail ratio expressed against the native maximum. The ratio is a reversible
approximation because no direct health literal appears in the tiny mission
overlay.

The native progression layer now also exposes AIRBASE's three objectives and
parameters, completes the combat-gear objective after sources 97–100, operates
overlay-scripted post-locker switch 105 on paired doors 31/32, and treats
event volume 24 as the end-corridor escape boundary. Detection and
non-electrical lethal force fail their respective parameters. Exact
cinematics, patrol timing, notifications, and the final STR handoff remain
overlay work.

## HWAY bring-up

`HWAY` constructs 30 world models, 10,575 polygons, 294 BIN records, 219
native scene objects, and five articulated model resources. It starts Gabe at
source 159 in room 28 with the expected adjacent outdoor visibility envelope.

HWAY now starts with its authored knife. SF1's unused inventory slot 3 carries
the sequel-only item without expanding the fixed 26-slot retail bridge; the
recovered SF2 record supplies `KNIFE.GMD`, 100 close damage, zero ranged
damage, and no ammunition. Its authored supply sources translate to the Glock
17, M16, shotgun, armor, and fragmentation grenade rather than the
same-numbered SF1 inventory slots.

Source 197 is the grounded opening takedown candidate: Gabe's source-159
transform lies behind its authored facing and source 197 carries an M16.
Sources 278–280 instead own five-point paths which converge on Chance at
source 45. The native reconstruction preserves those targets ahead of
nearest-player selection and gives Chance the reciprocal opening target.

The recovered knife's 100 close damage is paired with source 197's 200 health.
Native rear attacks apply a provisional 2x contextual multiplier and the
defeated guard detaches its translated M16 into the shared sequel pickup pool.
That multiplier and temporary protection for Chance are reversible
approximations pending the exact sequel AI/damage overlay branch.

`sf_sf2_native_mission_probe` deterministically validates AIRBASE progression,
the Chance exchange, the rear knife input, the M16 drop/collection path, and a
clear stationary chase-camera segment using a user-supplied Disc 1 image.

`SPOOKYX.HMD` retains an empty right-hand hierarchy node with zero declared
vertices and the retail `+32767/-32767` bounds sentinel. The HMD reader now
normalizes that sentinel only for zero-vertex parts; populated parts with
inverted bounds remain invalid. This is the articulated-model counterpart to
the sequel's previously identified empty GMD bounds.
