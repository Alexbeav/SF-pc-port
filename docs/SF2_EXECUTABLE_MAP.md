# Syphon Filter 2 executable and overlay map

This is a clean-room, reproducible map of the NTSC-U sequel executables. Names
are descriptive and are not claimed to be original source identifiers. Retail
bytes and disassembly are deliberately not checked in. Source-only address,
fingerprint, and cross-game metadata are retained where they make the
analysis reproducible without redistributing game content.

## Reproduction

Build `sf_tool`, then run:

```powershell
sf_tool map-functions <sf2-disc-1.cue> sf2-disc1-functions.csv
sf_tool map-functions <sf2-disc-2.cue> sf2-disc2-functions.csv
sf_tool map-function-union <sf2-disc-1.cue> <sf2-disc-2.cue> sf2-campaign-functions.csv
sf_tool map-functions <sf3.cue> sf3-functions.csv
sf_tool map-function-calls <sf2-disc-1.cue> sf2-calls.csv
sf_tool compare-functions <sf2-disc-1.cue> <sf3.cue> sf2-sf3.csv
sf_tool map-embedded-archives <sf2-disc-1.cue> sf2-archives.csv
sf_tool map-resident-overlays <sf2-disc-1.cue> sf2-resident.csv
sf_tool map-mission-overlays <sf2-disc-1.cue> sf2-disc1-overlays.csv
sf_tool map-mission-overlays <sf2-disc-2.cue> sf2-disc2-overlays.csv
sf_tool map-mission-overlays <sf3.cue> sf3-overlays.csv
sf_tool map-mission-classes <sf2-disc-1.cue> sf2-disc1-classes.csv
sf_tool map-mission-classes <sf2-disc-2.cue> sf2-disc2-classes.csv
sf_tool map-mission-objects <sf2-disc-2.cue> sf2-disc2-objects.csv
sf_tool map-mission-scripts <sf2-disc-2.cue> sf2-disc2-scripts.csv
sf_tool map-mission-script-events <sf2-disc-2.cue> sf2-disc2-events.csv
sf_tool map-mission-script-actions <sf2-disc-2.cue> sf2-disc2-actions.csv
sf_tool map-mission-script-strings <sf2-disc-2.cue> sf2-disc2-strings.csv
sf_tool map-mission-script-opcodes <sf2-disc-1.cue> sf2-opcodes.csv
sf_tool map-mission-script-handler-calls <sf2-disc-1.cue> sf2-handler-calls.csv
sf_tool compare-mission-script-opcodes <sf2-disc-1.cue> <sf3.cue> sf2-sf3-opcodes.csv
sf_tool map-object-handlers <sf2-disc-1.cue> sf2-object-handlers.csv
sf_tool map-string-references <sf2-disc-1.cue> sf2-strings.csv
sf_tool map-xa-streams <sf2-disc-1.cue> sf2-disc1-xa.csv
sf_tool map-xa-streams <sf2-disc-2.cue> sf2-disc2-xa.csv
sf_tool map-xa-streams <sf3.cue> sf3-xa.csv
```

The action map includes both serialized operands, whether each came from the
instruction's inline 7/6-bit packing or an extended 16-bit word, descriptor
metadata, both predicate-branch successors, and decoded program
activation/deactivation targets. It does not execute mutable variables or
object selectors during static analysis.

`map-functions` combines the executable entry point, direct `jal` targets,
compiler stack prologues, and executable targets called only by mission
overlays. It emits byte-exact and relocation/immediate-normalized SHA-256
fingerprints. The inferred extent ends at the first `jr ra` delay slot.
Tail-called, assembly leaf, and nonstandard routines can therefore still need
manual control-flow refinement.

The prologue scanner now coalesces a stack prologue with a directly called
entry up to four straight-line instructions earlier. Retail commonly loads a
global before allocating its frame (for example `0x8002da38` and
`0x8002df04`); treating the later prologue as another function created 189
false SF2 campaign seeds. A branch, jump, return, or wider gap preserves the
independent conservative seed.

Each seed now records whether it came from the entry point, a direct call, an
overlay, or only a prologue scan. It also marks addresses inside exact
non-final embedded-archive entry ranges. In SF2, 329 of 2,960 raw seeds fall
inside those proven asset ranges (323 are prologue-only); 1,787 of 8,686 raw
`jal`-shaped words also originate there. The remaining 2,631 seeds and 6,899
direct-call sites form the cleaner code-analysis baseline. Final archive
entries retain an upper-bound size and are not automatically excluded.

`compare-functions` reports only unique equal fingerprints. It does not guess
when a hash is duplicated or code changed structurally.

## Whole-executable baseline

| Build | Function seeds |
|---|---:|
| SF1 USA 1.1, executable only | 2,278 |
| SF2, Disc 1 mission/resident overlay closure | 2,960 |
| SF2, Disc 2 mission/resident overlay closure | 2,960 |
| SF2, union of both campaign overlay closures | 2,964 |
| SF3, campaign/resident overlay closure | 2,917 |

The two SF2 discs contain the same executable, but their different mission
overlays expose four unique seed boundaries on each disc. Use
`map-function-union` for the 2,964-seed whole-campaign map; it first verifies
that the executables are byte-identical. The complete SF2 campaign has 124 distinct
overlay-to-executable targets; Disc 1 contributes 100 and Disc 2 contributes
61, with overlap.

The normalized comparison uniquely pairs 2,111 SF2 Disc 1 seeds with SF3:
279 remain byte-identical and 1,832 are structurally identical after address
and immediate normalization. This is about two thirds of the discovered SF2
map and confirms that SF3 is a close evolution of the same engine.

The complete source-only union is retained as
[`sf2-function-catalog.csv`](research/sf2-function-catalog.csv). Its 2,964
rows record Disc 1/Disc 2 presence, conservative function metrics, per-disc
exact fingerprints, normalized fingerprints, overlay/asset provenance, the
2,111 unique SF3 matches, and compact descriptive symbols where a seed has
one. The compact symbol list also names internal handler entry points which
are intentionally coalesced into an earlier conservative function seed, so
not every symbol appears as a separate catalog row.

The raw direct-call inventories contain 8,686 SF2 call-shaped words and 8,477
in SF3. Use `site_in_embedded_asset_range` to reject proven asset data. Indirect
`jalr` handlers are instead recovered through descriptor tables,
object-handler tables, and overlay callbacks.

## Resident overlays and embedded executable archives

Resident overlays occupy adjacent runtime windows, independently confirmed by
their internal calls and prologues:

| Game | Overlay | Runtime base | Function seeds |
|---|---|---:|---:|
| SF2 | `MENU.OVL` | `0x80142150` | 100 |
| SF2 | `MOVIE.OVL` | `0x80142150` | 85 |
| SF2 | `TITLE.OVL` | `0x8014b950` | 135 |
| SF2 | `INIT.OVL` | `0x80158878` | 173 |
| SF3 | `MENU.OVL` / `MENU2.OVL` / `MOVIE.OVL` | `0x80146950` | 104 / 102 / 85 |
| SF3 | `TITLE.OVL` / `TITLE2.OVL` | `0x80150950` | 94 / 132 |
| SF3 | `INIT.OVL` | `0x8015e978` | 174 |

SF2's two discs contain byte-identical resident overlays. Across games, 257
resident functions have unique structural matches, including 148
`INIT`-to-`INIT` and 56 `MOVIE`-to-`MOVIE` pairs.

Five embedded HOG-like archives are present in each sequel executable. SF2
contains 365 named entries: 166 animations, 87 UI/inventory textures, 97
weapon/effect textures, eight resident-package entries, and seven title
assets. SF3 contains 398 entries with the same five-way organization. The last
entry size reported by `map-embedded-archives` is an upper bound because nested
archives and executable tail data share the containing range.

## Runtime and subsystem anchors

The maintained compact symbol list is
[`sf2-function-symbols.csv`](research/sf2-function-symbols.csv). Evidence
levels mean:

- `confirmed`: executable header, runtime profile, string reference, or direct
  instruction semantics establishes the role;
- `strong`: call shape plus several independent mission call sites establishes
  a narrow role, but the exact retail contract still needs a guest probe;
- `provisional`: useful descriptive boundary, not yet safe for implementation.

Important confirmed boundaries include:

- SF2 PS-X entry `0x800f8598`, which initializes the runtime and calls
  `Game_Main` at `0x80029624`;
- SF3 PS-X entry `0x800fb368`, which calls `Game_Main` at `0x80029ed8`;
- the paired application-state loops at SF2 `0x80029700` and SF3
  `0x80029fb8`;
- `VSync` at SF2 `0x800f48f0` / SF3 `0x800f7660`;
- `CdControl` at SF2 `0x80103968` / SF3 `0x8010669c`;
- the direct CD-ready callbacks at SF2 `0x800f703c` / SF3 `0x800f9e0c`;
- scene-XA archive selection at SF2 `0x8008cafc` / SF3 `0x8008f338`;
- fixed-point multiply at `0x80010654` in both sequels.

The SF2 and SF3 state loops each dispatch fourteen application states. Their
verified jump targets are:

| State | SF2 target | SF3 target | Proven role |
|---:|---:|---:|---|
| 0 | `0x8002995c` | `0x8002a2c4` | gameplay object dispatch and render-view queue |
| 1 | `0x80029e54` | `0x8002a7d8` | no state-specific body |
| 2 | `0x80029e54` | `0x8002a7d8` | no state-specific body |
| 3 | `0x80029920` | `0x8002a288` | conditional player/view service |
| 4 | `0x80029a48` | `0x8002a388` | `TITLE.OVL` frame |
| 5 | `0x8002995c` | `0x8002a2c4` | gameplay object dispatch and render-view queue |
| 6 | `0x80029e54` | `0x8002a7d8` | no state-specific body |
| 7 | `0x80029aac` | `0x8002a410` | `MENU.OVL` frame and menu audio cleanup |
| 8 | `0x80029b1c` | `0x8002a4a0` | recorded-demo (`.DEM`) playback/exit state |
| 9 | `0x80029d84` | `0x8002a708` | render-view removal and loading services/overlay |
| 10 | `0x80029dd8` | `0x8002a75c` | input-gated state pop/idle service |
| 11 | `0x800299f8` | `0x8002a338` | transition completion and campaign advance |
| 12 | `0x800299f8` | `0x8002a338` | transition completion and campaign advance |
| 13 | `0x80029e3c` | `0x8002a7c0` | common display/audio service frame |

The targets are confirmed from the retail jump tables. The role labels use
direct overlay ownership, `.DEM` path construction, and visible state
mutations; state 3 remains deliberately generic because its player/view
service has not yet been named.

## SF2 mission overlays

SF2 mission overlays are small extension modules, not complete mission
scripts. Most behavior therefore belongs to shared executable systems and BIN
object data.

The loader removes each overlay's fixed 40-byte file header and maps the
remaining payload at `0x8014b978`. Data between that header and the first
function remains mapped. The first inferred function is therefore at
`0x8014b978 + (code_offset - 40)`, not at `0x8014b978` for every overlay.
Retail absolute callback pointers prove the relationship: COLO's first code
is at `0x8014b988`, HWAY's at `0x8014b980`, and TRAIN's at `0x8014b9a8`;
AIRBASE and BRIDGE have no intervening data. `map-mission-overlays` now emits
both the payload load address and corrected code address.

| Mission | Overlay | Payload bytes | Function seeds | Unique executable calls |
|---:|---|---:|---:|---:|
| 1 | `COLO.OVL` | 3,972 | 8 | 46 |
| 2 | `AIRBASE.OVL` | 164 | 2 | 3 |
| 3 | `HWAY.OVL` | 6,332 | 11 | 35 |
| 4 | `BRIDGE.OVL` | 148 | 2 | 2 |
| 5 | `AIRBASEX.OVL` | 668 | 1 | 7 |
| 6 | `TRAIN.OVL` | 7,004 | 22 | 32 |
| 7 | `TRAIN2.OVL` | 644 | 3 | 6 |
| 8 | `WRECK.OVL` | 340 | 3 | 2 |
| 9 | `DISCO.OVL` | 4,008 | 9 | 34 |
| 10 | `DARKMUSE.OVL` | 4 | 1 | 0 |
| 11 | `MOSCOW.OVL` | 160 | 2 | 4 |
| 12 | `MOSCOW2.OVL` | 2,592 | 1 | 1 |
| 13 | `MOSCOW3.OVL` | 4,352 | 5 | 7 |
| 14 | `GARAGE.OVL` | 3,976 | 7 | 17 |
| 15 | `GULAG.OVL` | 4 | 1 | 0 |
| 16 | `GULAG2.OVL` | 4 | 1 | 0 |
| 17 | `LABS1.OVL` | 556 | 1 | 3 |
| 18 | `LABS2.OVL` | 4 | 1 | 0 |
| 19 | `SLUMS.OVL` | 724 | 1 | 10 |
| 20 | `SLUMS2.OVL` | 1,228 | 3 | 14 |
| 21 | `CHINBOSS.OVL` | 28 | 1 | 0 |

“Payload bytes” is the non-padding payload after the inferred overlay header;
it can include local tables and is not a claim that every byte is code.

Corrected addresses close the SF2 callback-registration map:

| Mission | Init entry | Registered callback |
|---|---:|---|
| COLO | `0x8014bbd0` | death `0x8014bac4`; special-object `0x8014bb54` |
| AIRBASE | `0x8014b9f8` | special-object `0x8014b978` |
| HWAY | `0x8014bc94` | death `0x8014b980` |
| BRIDGE | `0x8014b9e8` | death `0x8014b978` |
| TRAIN | `0x8014baac` | death `0x8014b9a8` |
| TRAIN2 | `0x8014bb24` | death `0x8014b9cc` |
| DISCO | `0x8014ba1c` | death `0x8014b9a8` |
| MOSCOW | `0x8014b9f4` | death `0x8014b978` |
| MOSCOW3 | `0x8014ba70` | death `0x8014b978` |
| GARAGE | `0x8014c8b4` | death `0x8014c4dc` |

These are literal values reconstructed from the init functions. Missions
omitted from the table register neither mapped hook, and no SF2 mission
overlay directly registers the separate damage hook.

Repeated overlay call contexts establish several shared boundaries:

- `0x800ac6a4` swaps the mission special-object callback pointer;
- `0x800892a8` swaps a second mission lifecycle callback pointer;
- `0x8002d3a8` resolves an object index to its runtime pointer and associated
  short field;
- `0x8008d21c` resolves and starts a sound-scene cue, including its bounded
  active-cue pool;
- `0x800974fc` constructs and queues an object event record;
- `0x800f41f0` is the BIOS `rand` wrapper used for authored variation;
- `0x80010b14` is the GTE-backed three-component vector normalization helper.

The exact contracts for the two callback tables and object event record remain
guest-probe work. They should not be replaced with mission-specific switches.

## Data-driven object scripting

Across both discs, SF2 contains 666 object definitions and 3,791 instances
using 121 low-byte class families and 155 raw class/flag combinations. The
shared object dispatcher at `0x8002a7e0` reads the class byte at definition
offset `0x2a` and indexes the 132-entry handler table at `0x8010c3d4`.
SF3 preserves the dispatch shape with its table at `0x8010f0f0`.
SF3 adds class family `84`, so its table has 133 entries rather than SF2's
132; the mapper now includes that final retail entry.

Many handler entries point at resident import trampolines whose embedded names
survive in the executable:

| Resident module | Class families |
|---|---|
| `OBJ_GEN` | `04, 0a, 23, 2a, 42, 43, 7a` |
| `OBJ_SND` | `06, 14, 7f, 82, 83` |
| `OBJ_PART` | `07` |
| `OBJ_BEAM` | `09` |
| `OBJ_INTR` | `0b, 0c, 31, 67, 6b, 77` |
| `OBJ_CAT` | `0d, 78` |
| `OBJ_DOOR` | `0e, 12, 65` |
| `OBJ_DORP` | `0f, 10, 11` |
| `OBJ_ELEF` | `13` |
| `OBJ_TGT` | `15, 1f, 20, 21, 5c, 5d, 7b, 7c, 7d` |
| `OBJ_ELEV` | `16` |
| `OBJ_FNPC` | `18` |
| `OBJ_ROT` | `19, 2e` |
| `OBJ_FIRE` | `1a, 1b, 1c, 1d` |
| `OBJ_EFX3` | `22` |
| `OBJ_GLSS` | `24, 25, 26, 27, 28, 29, 2b, 2c, 61` |
| `OBJ_HELI` | `2d` |
| `OBJ_IMAG` | `2f, 30` |
| `OBJ_LIT` | `32, 34, 35, 36, 37, 38, 39, 3a, 3b, 3c, 3f, 62, 63, 80` |
| `OBJ_LROT` | `33, 40` |
| `OBJ_LDET` | `3d` |
| `OBJ_LIR` | `3e, 79` |
| `OBJ_LSPT` | `41` |
| `OBJ_NVIS` | `44` |
| `OBJ_NVNS` | `45` |
| `OBJ_PATH` | `46` |
| `OBJ_RAIN` | `4a` |
| `OBJ_EFX4` | `4b` |
| `OBJ_EFX` | `4e, 4f, 5e` |
| `OBJ_SNKF` | `50` |
| `OBJ_SNOW` | `51` |
| `OBJ_EFX2` | `52, 53` |
| `OBJ_SWIT` | `54, 55, 56, 57, 58, 59, 5a` |
| `OBJ_VEH` | `5f, 60` |
| `OBJ_PARA` | `64, 6c` |
| `OBJ_PROP` | `6e` |
| `OBJ_SCAM` | `72` |

The highest-value direct handlers are:

- actor classes `01`–`03` at `0x8008244c`;
- player class `05` at `0x80078c24`;
- authored event-volume classes `4c`/`4d` at `0x80038dd0`;
- pickup/container classes `47`–`49` at `0x800ac9e0`.

Classes `4c` and `4d` alone account for 355 placed instances across twenty
missions. Their handler calls `0x80042cf4` with authored event IDs. This proves
that general event-volume dispatch, rather than AIRBASE source-number checks,
is the reusable boundary for objectives, exits, dialogue, and encounter
progression.

Thirteen SF2 classes (`08, 66, 68, 69, 6a, 6d, 6f, 70, 73, 74, 75, 76,
7e`) initially point into the mission-overlay load range. In SF3 these entries
fall back to one direct generic handler; `0d` and `78` also move from
`OBJ_CAT` to `OBJ_GEN`. All other resident module ownership is preserved.

## SF3 overlay delta

Sixteen of nineteen SF3 campaign missions use a four-byte `GENERIC.OVL`
containing only `jr ra`. Mission behavior for those levels is therefore fully
shared/data-driven. Three missions retain authored extension modules:

| Mission | Overlay | Payload bytes | Function seeds | Unique executable calls |
|---:|---|---:|---:|---:|
| 3 | `JUNGLE3.OVL` | 3,504 | 6 | 16 |
| 4 | `AFRICA1.OVL` | 48 | 1 | 1 |
| 5 | `AFRICA2.OVL` | 1,980 | 3 | 25 |

The native catalog previously labeled all SF3 missions `GENERIC.OVL`.
Retail archive enumeration proves the three exceptions above, and the catalog
now names them correctly.

## Shared mission-script VM

The `.SS` resources are the principal authored mission-program layer. Across
both discs SF2 contains 227 named programs and 3,117 event records; SF3
contains 360 programs and 4,573 event records. A control-flow walker validates
15,450 reachable SF2 action/control words and 23,837 in SF3.

The container, runtime entry points, predicate/action descriptor tables,
save-state format, and exact cross-game deltas are documented in
[`SF2_MISSION_SCRIPT_VM.md`](SF2_MISSION_SCRIPT_VM.md). This corrects the early
bring-up assumption that `.SS` was limited to sound cues. Mission overlays are
extension hooks around this shared VM, not the normal home of objectives,
doors, timers, dialogue, or campaign progression.

## Current mapping limits

- Function extents are seeds for analysis, not a replacement for a full MIPS
  control-flow graph.
- Indirect calls and jump-table destinations are not automatically promoted
  unless another seed source discovers them.
- String references recognize conventional `lui` plus `addiu`/`ori` address
  construction. GP-relative and computed references require manual analysis.
- `.SS` predicate and action handlers are mapped by descriptor slot, but most
  source-language opcode names and native contracts remain intentionally
  neutral until handler-side effects are proven.
- The executable text includes large embedded asset archives. Prologue scans
  inside known data ranges can produce false function seeds; call-flow,
  descriptor, or overlay evidence is required before naming those candidates.
- No generated map or symbol name by itself proves gameplay behavior; native
  implementation still requires a deterministic guest or mission probe.
