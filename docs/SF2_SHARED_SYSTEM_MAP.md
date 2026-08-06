# Syphon Filter 2 shared-system map

This is the implementation-facing map above individual mission scripts. It
uses descriptive names, not claimed original symbols. Addresses are for the
NTSC-U SF2 executable; paired SF3 addresses are recorded where proven.

## Frame, state, and campaign shell

| System boundary | SF2 | SF3 | Evidence |
|---|---:|---:|---|
| executable entry | `0x800f8598` | `0x800fb368` | PS-X EXE header |
| game main | `0x80029624` | `0x80029ed8` | direct CRT call |
| 14-state application loop | `0x80029700` | `0x80029fb8` | retail jump table |
| common initialization | `0x8002a518` | `0x8002aeac` | resident-overlay construction and structural match |
| application-state push | `0x8002bc44` | `0x8002c6ec` | state stack mutation and resident-overlay selection |
| application-state pop | `0x8002bc80` | `0x8002c728` | state stack mutation and prior-overlay selection |
| campaign advance/complete | `0x8002c95c` | `0x8002d3f4` | mission-index increment, retained player values, and final-mission branch |
| campaign mission commit | `0x800ad014` | `0x800afb6c` | snapshots player carry state, records completed/unlocked state, and conditionally copies the mission's 0x26-byte progress record |
| player carry snapshot | `0x800ad264` | `0x800afdbc` | clamps carried health to at least 10 and copies armor, equipped item, ownership words, and every inventory quantity pair |
| mission scheduler frame event | `0x8002ac88` | `0x8002b608` | SF1 semantic match and identical sequel structure |
| scheduler queue drain | `0x8002afa4` | `0x8002b924` | structural match |
| loading-overlay frame | `0x8002b8d0` | `0x8002c30c` | structural match |

The executable state loop, resident `MENU`/`TITLE`/`MOVIE`/`INIT` overlays,
mission resource tables, and `.SS` `LEVEL` activation are separate layers.
Mission completion should enter that existing state/campaign shell rather than
having native missions invent disc transitions.

State targets `0x8002995c` (states 0 and 5) and `0x80029d84` (state 9) now
have direct semantic evidence: the first dispatches objects then queues the
primary and optional secondary render views, while the second removes those
views and runs loading services plus `LoadingOverlay_Frame`.
`Campaign_AdvanceOrComplete` advances mission indices
below 20 in SF2 and below 18 in SF3; each game then has a distinct
final-mission branch. This boundary is where native mission completion must
hand retained inventory/health and disc/campaign state back to the shell.

The transition is not merely a mission-number increment.
`CampaignProgress_CommitMission` first tests the current completion state,
calls `PlayerCarry_Snapshot`, advances the highest-unlocked marker, and
conditionally copies the corresponding 38-byte mission progress record.
`PlayerCarry_Snapshot` is the exact cross-mission inventory boundary: it
copies two ownership words and 34 reserve/loaded quantity pairs from the SF2
player, along with health, armor, and equipped item. SF3 preserves the
structure but expands the copied quantity-pair count to 40. Native campaign
carry must therefore remain versioned and must not silently truncate
sequel-only item slots.

## Mission programs and authored events

The shared `.SS` VM is the main authored layer. Its event dispatcher calls the
same object, actor, scheduler, audio, and UI systems used by direct object
handlers. See [`SF2_MISSION_SCRIPT_VM.md`](SF2_MISSION_SCRIPT_VM.md) for the
container, 128 action descriptors, 64 predicate descriptors, save-state
format, corpus maps, and SF3 delta.

A frequency-guided handler pass now gives 10,384 of 10,655 reachable ordinary
SF2 action occurrences (97.5%) a direct native effect map. Important complete
paths include timer assign/decrement/expiry, player-to-object interaction,
actor activate/deactivate/health/item state, scene speech, objectives and
parameters, and authored mission success. This is sufficient to design a
generic interpreter around shared effects; it is not evidence that the
interpreter or full campaign choreography already runs natively.

The general object-event backbone is:

| Boundary | SF2 | SF3 |
|---|---:|---:|
| object/runtime record resolve | `0x8002d3a8` | `0x8002df1c` |
| object event dispatcher | `0x80042cf4` | `0x80043dc4` |
| object event queue wrapper | `0x800974fc` | `0x80099d14` |
| player/object interaction | `0x80047e3c` | `0x80048e70` |
| special-object callback setter | `0x800ac6a4` | `0x800af1fc` |
| mission lifecycle callback setter | `0x800892a8` | aligned SF3 INIT caller pending a compact symbol |

`0x80042cf4` has 163 direct executable call sites before indirect script and
overlay callers are counted. That makes it a shared event bus, not an
AIRBASE-specific helper.

The complete 132-entry SF2 class-family dispatch table is checked in as
[`research/sf2-object-class-handlers.csv`](research/sf2-object-class-handlers.csv).
It distinguishes direct resident handlers, named object-family imports, and
mission-overlay slots for every class `0x00`–`0x83`. This is the stable
whole-campaign ownership map behind the smaller mission-specific class
inventories; SF3 extends it with class `0x84`.

Objective/parameter state is owned by a separate shared progress record, not
the `.SS` package bitset. `0x8002df04` reveals and presents an objective,
`0x8002da38` records and presents objective/parameter outcomes, and
`0x8002e6e8` applies objective completion state. Script actions
`0x08`/`0x09`/`0x0a` and `0x1e`/`0x1f` expose those two families.

The handler currently named `ObjectEventVolume_Handler` at `0x80038dd0`
dispatches event IDs `2` through `0x2b`, manages class-`4c`/`4d` linked
records, and calls collision/actor/object services. Its role is broader than a
simple trigger volume; source should retain the old name only until the full
event contract is enumerated.

## Actors and AI

| Boundary | SF2 | SF3 | Notes |
|---|---:|---:|---|
| actor class `01`–`03` handler | `0x8008244c` | `0x80084a48` | confirmed object-handler table |
| player class `05` handler | `0x80078c24` | `0x8007b168` | confirmed object-handler table |
| render-view queue add | `0x80066b54` | `0x80068fa8` | inserts a view in the linked submission list and sets flag `0x1000` |
| render-view queue remove | `0x80066be0` | `0x80069034` | unlinks the view and clears flag `0x1000` |
| actor auxiliary-resource reset | `0x8005cc3c` | `0x8005e29c` | overlay call semantics |
| mission actor-damage callback setter | `0x80089298` | `0x8008b8d4` | callback is invoked inside shared actor damage resolution |
| mission actor-death callback setter | `0x800892a8` | `0x8008b8e4` | callback is invoked by death finalization before object cleanup |
| actor death finalization | `0x80089324` | `0x8008b960` | schedules authored death work, invokes mission hook, and releases class state |
| actor activation | `0x80086d4c` | `0x80089434` | event-5 path sets active bit `0x80`, constructs actor/player state, and reports whether activation occurred |
| actor deactivation | `0x80087194` | `0x800897b8` | event-4 path releases actor/player state, clears active bit `0x80`, and decrements the active-actor count |
| actor damage reaction | `0x80081ebc` | `0x800844b8` | event-12 path after the mission damage hook; selects reaction, authored event, pain cue, and scheduler work |
| actor death reaction | `0x80082174` | `0x80084770` | event-9 path after the mission death hook; resolves death animation, attachment, body, and class-specific state |

The actor handler is a 39-way dispatcher for request events `2` through `40`.
Its principal lifecycle slots are now direct: event 2 invokes the mission
actor-initialization import, event 4 deactivates, event 5 activates, event 9
runs the death hook/reaction path, and event 12 runs the damage hook/reaction
path. The activation pair is independently confirmed by mirrored mutation of
actor-state bit `0x80`, player/attachment cleanup, and the global active-actor
count.

The complete jump table is recorded in
[`research/sf2-actor-events.csv`](research/sf2-actor-events.csv). Only nine of
the 39 request IDs have code in the actor family itself; the other thirty
deliberately return through the common epilogue. Beyond the lifecycle slots,
event 24 processes the dense interaction/hit request, event 30 clears
actor-sourced scene speech and sets actor state bit `0x2000`, event 39
serializes actor runtime state into its request record, and event 40 restores
that state. The save/restore pair copies flags, animation/state bytes, and
position data symmetrically, so it is not speculative AI naming.

Its other direct shared callees include:

- object resolution `0x8002d3a8`;
- actor event/state path `0x80087758` → SF3 `0x80089de8`;
- mission event dispatch `0x80042cf4`;
- sound-scene cue play `0x8008d21c`;
- `0x8008e050` → SF3 `0x80090894`;
- the mission INIT callback at `0x801646fc`.

These edges establish the reusable AI integration seam: actor class handling,
authored actor events, mission events, and sound are coupled in shared code.
The two mission hook slots are now distinguished by their call sites rather
than merely by registration: `0x80089298` is the damage hook and
`0x800892a8` is the death hook. COLO, HWAY, BRIDGE, TRAIN, TRAIN2, DISCO,
MOSCOW, MOSCOW3, and GARAGE register the death hook. No SF2 mission overlay
directly registers the damage hook in the recovered call inventory. AIRBASE
registers neither; its tiny overlay registers only the separate pickup/
special-object callback. The lower-level combat-state helpers called by these
four lifecycle routines remain neutral pending deterministic actor-state
probes; the dispatcher boundaries themselves no longer do.

## Items, weapons, drops, and inventory

| Boundary | SF2 | SF3 | Notes |
|---|---:|---:|---|
| pickup/container class `47`–`49` handler | `0x800ac9e0` | `0x800af538` | confirmed table target |
| dropped-item detach | `0x80063dc4` | `0x80066208` | provisional SF1 semantic match |
| held-model resolve | `0x800639f4` | `0x80065e10` | explicit missing-model diagnostic |
| object-event queue | `0x800974fc` | `0x80099d14` | aligned pickup call |
| packed item-spec decode | `0x800ab584` | `0x800ae0f4` | low six-bit item and bit-six flag |
| pickup quantity resolve | `0x800ab59c` | `0x800ae10c` | authored metadata, multiplier, and item cap |
| actor pickup-queue append | `0x800ab7e8` | `0x800ae358` | bounded ten-entry parallel arrays |
| actor pickup-queue remove | `0x800ac048` | `0x800aeba0` | shifts fields and clears queue tail |
| actor pickup-queue process | `0x800ac0f0` | `0x800aec48` | resolves queued item, applies inventory/ammo mutation, presentation, and removal |
| owned-item count | `0x80062ff4` | `0x80065494` | population count across the actor's two-word item bitset |
| inventory item acquire | `0x80063044` | `0x800654e4` | sets ownership bit, optionally adds quantity, refreshes first equipped item |
| inventory item remove | `0x80063108` | `0x800655a8` | clears both item halfwords and ownership bit; repairs equipped selection |
| inventory item replace | `0x800631d0` | `0x80065670` | transfers the old item's combined quantity through acquire, then removes old item |
| inventory item at capacity | `0x800632e4` | `0x80065700` | compares both per-item halfwords against item-table capacity |
| inventory quantity add/clamp | `0x80063378` | `0x80065794` | mutates two per-item halfwords with retail magazine/reserve limits |
| inventory quantity assign/clamp | `0x800634e0` | `0x800658fc` | clears both halfwords then routes quantity through add/clamp |
| inventory reload transfer | `0x800642f4` | `0x80066738` | transfers `min(reserve, capacity-loaded)` and refreshes equipped-weapon display |
| actor reload request | `0x80040ba4` | `0x80041c88` | resolves actor/current item, checks weapon state, reloads, then dispatches authored actor event |
| reload sound dispatch | `0x80064398` | `0x800667dc` | selects shotgun versus general reload cue from current item |

The pickup handler reaches inventory mutation, scheduler work, event queueing,
pickup presentation, and the mission INIT callback. Sequel item numbers are
not SF1 weapon IDs: the native port's explicit table is therefore the correct
temporary boundary for pickups, actor loadouts, drops, carry state, and
reload. Unsupported H11/crossbow entries must remain explicit rather than
aliasing unrelated SF1 slots.

Mission-script action `0x32` is the direct authored grant boundary
(`0x800b0b3c`, SF3 `0x800b37fc`). It decodes the item and authored quantity,
scales the quantity through the corresponding retail item-table byte, and
then calls `InventoryItem_Acquire`. Native mission reconstruction should
route scripted starting equipment and rewards through the same typed item
mapping rather than treating the action operand as a raw ammunition count.

Disassembly corrects an earlier suspicion: `0x800ab6f8` and `0x800ac890`
prepare collection records; `0x800ac0f0` is the final queue consumer. It
calls the inventory core above and therefore is direct evidence for retained
ownership and ammunition writes. Each item occupies a reserve halfword and a
loaded-magazine halfword beginning at actor-state offset `+0x44`, while
ownership uses two words beginning at `+0x3c`. Retail item-table fields
determine magazine capacity and reserve limits. `InventoryReload_Transfer`
proves the field names by subtracting from the first halfword and filling the
second to the item-table capacity. Its callers cover actor weapon state,
weapon selection, pickup overflow, and item equip paths.

## Collision and camera

| Boundary | SF2 | SF3 | Notes |
|---|---:|---:|---|
| world collision scan | `0x800570a0` | `0x80058608` | SF1 semantic and sequel structural match |
| actor camera/render collision preparation | `0x80049940` | `0x8004ac88` | provisional; unique joint caller of two SF1-matched collision helpers |
| common player camera/facing boundary | `0x80053464` | unknown | instruction-aligned with proven SF1 `0x80037B08`; live hook and controller-pointer probe |
| completed sight-angle consumption | `0x800539d0` | unknown | live manual-aim boundary; consumes the selected actor bearing after retail initialization |

`WorldCollision_Scan` calls the shared geometry helper at `0x80022a18`
(SF3 `0x800231bc`) and a result/record path at `0x80099018`. This is the
retail collision anchor for actor movement and camera obstruction.

One camera-adjacent boundary comes from a three-game call-graph proof. SF1
`0x8002ff70` references the verified retail camera-controller pointer and
calls helpers `0x8003a7fc` and `0x8003a8bc`. Those helpers map structurally
to SF2 `0x80057480`/`0x80057540`; `0x80049940` is their only joint caller.
SF3 keeps the same relationship at `0x8004ac88`. Full disassembly of the SF2
caller proves this is actor render/attachment and collision preparation, not
the chase-camera controller.

The later guest write probe identified the live player camera/facing boundary
at SF2 `0x80053464`, instruction-aligned with SF1 `0x80037B08`. Across 400
ordinary gameplay updates it recorded 132 calls, retained the controller
pointer in `s2`, resolved camera base `0x801285dc`, and drove the authored
desired/rendered chase-pitch pair to its +/-512 clamp. The host mouse adapter
uses the proportional direction-vector fields at controller
`+0xcc/+0xd0/+0xd4` while leaving the retail turn axis active. This dual path
is required to keep body facing, locomotion, animation and collision coupled
to the chase camera. SF3's corresponding boundary remains unmapped and must be
established by instruction alignment plus a live probe rather than address
inference.

Outdoor visibility and sky submission are scene/portal concerns and should
not be folded into the camera routine merely because COLO exposes both
symptoms.

The native black-sky failure has a narrower confirmed cause. Every
`GameplaySession` independently parses a resident `SCRIM.EMD` when the
mission object archive contains it, but the native presentation snapshot
initializes `scrim`, `environment`, and `retail_environment_active` to zero.
Only a live legacy guest frame supplies:

- the SCRIM visibility bit and final authored matrix;
- the scene clear/back/fog colors and GTE depth-cue coefficients;
- the SCRIM copy-ring/page state used by the retail mutable backdrop.

The renderer already has a semantic non-depth-writing SCRIM background pass.
The missing native piece is therefore authored environment/SCRIM state
production, not EMD parsing or draw submission. COLO cannot safely render its
detached SCRIM at an identity or camera-relative transform: neither is
supported by retail state. A sequel environment bridge or native execution of
the responsible object/script controller is required.

An earlier cross-game fuzzy name for `0x80066b54`/`0x80066be0` was rejected
after full disassembly. They are render-view queue insertion/removal, not
player integration or chase-camera updates. The live sequel camera/facing hook
is instead `0x80053464`; the large object interaction path at `0x8004a264` and
visibility/event wrapper at `0x8004a050` remain ruled out as chase-camera
owners.

## Audio, speech, and ambience

| Boundary | SF2 | SF3 | Notes |
|---|---:|---:|---|
| scene-XA archive open/select | `0x8008cafc` | `0x8008f338` | `SCENES%d.XA` references |
| spatial sound start | `0x8008cdd0` | `0x8008f60c` | structural match; listener/source transforms and pan/volume |
| sound-scene cue play | `0x8008d21c` | `0x8008fa58` | cue-bank resolution and eight-record active pool |
| scene-speech stream start | `0x8008dbe4` | `0x80090408` | optional source, mode, cue, and completion state |
| scene-speech stream stop | `0x8008df60` | `0x800907a4` | stop, completion event, and sentinel reset |
| XA cue resolve/play | `0x800f9458` | `0x800fb7c8` | divides cue by archive channel count and selects group LBA/channel |
| XA filtered stream start | `0x800f957c` | `0x800fb8ec` | programs CD filter, seek, mixer, and stream state |
| XA stream stop | `0x800f9a10` | `0x800fbd80` | clears stream state and stops the CD path |

`SoundSceneCue_Play` calls the spatial path, low-level SPU services, and active
record allocation. Actor handlers and many shared object families call it
directly; `.SS` actions reach the same sound stack through script handlers.
Four `.SS` action variants (`0x16`, `0x17`, `0x77`, and `0x78`) call
`SceneSpeech_Start`, varying whether an authored source is supplied and the
mode flag. Action `0x6a` directly calls `SceneSpeech_Stop`.
This separates three required native layers:

1. VAB/SPU sound effects and positional ambience;
2. `.SS` cue/state dispatch;
3. streamed `SCENES1.XA`/`SCENES2.XA` speech and music.

Native mission audio is incomplete until all three are connected. Rendering
or AI progress must not be used as evidence that authored audio state ran.

`sf_tool map-xa-streams` now scans the physical Mode-2 sectors rather than
the ISO payload and emits exact clip LBAs, file/channel filters, coding,
sector counts, durations, EOF state, and interleave gaps. The retail sequel
archives have one XA file number, mono 37.8-kHz ADPCM, fifteen playable
channels (`0`–`14`), and a padding/filler channel `16`:

| Disc | Archive | synchronized clip groups | playable channel clips |
|---|---|---:|---:|
| SF2 Disc 1 | `SCENES1.XA` | 52 | 780 |
| SF2 Disc 2 | `SCENES2.XA` | 74 | 1,110 |
| SF3 | `SCENES1.XA` | 76 | 1,140 |

Within a group all fifteen playable channels share the same physical
start/end window; the CD filter selects one channel while the other sectors
pass. This explains why treating a scene cue as one contiguous ISO byte range
cannot work. It also supplies the exact seek/filter data needed to connect
`SceneSpeech_Start` to the already working CD-XA decoder without extracting
or redistributing audio.

Disassembly of `SceneSpeech_Start` further fixes its native record contract:
the requested scene/cue halfword is stored at active record `+8`, the mode at
`+0x0c`, volume at `+0x0d`, and optional source object at `+6`; playback is
submitted through `0x800f9458` after spatial volume calculation. The
cue-to-stream join is exact: `XaCue_ResolveAndPlay` divides the cue by the
archive handle's channel count, uses the quotient to select the cumulative
group offset from the table at handle `+0x90`, and uses the remainder as the
CD-XA channel filter. For the sequel archives this reduces to
`group = cue / 15`, `channel = cue % 15`. The group LBA is
`handle[+0x84] + cumulative_offset[group - 1]` (or just the base for group
zero). The remaining native gap is executing the authored action/event path
that requests the cue, not decoding, seeking, filtering, or outputting XA.

The complete per-mission authored cue surface is checked in as
`docs/research/sf2-mission-xa-cues.csv`. The four scene-speech actions account
for 534 reachable occurrences: 191 on Disc 1 (148 distinct cue IDs) and 343
on Disc 2 (241 distinct IDs). Every operand in this retail corpus is a
literal cue rather than a variable-indirected value. Disc 1 mission scripts
reference groups through 23 and Disc 2 through 34; higher physical archive
groups therefore belong to non-`.SS` users or unused/padding content and must
not be started merely because they exist in the XA catalog.

## SF3 compatibility boundary

The sequels share most executable structure, resident overlays, object
families, and the complete 128-slot action descriptor shape. Concrete deltas
already requiring versioned handling are:

- SF3 adds object class `84` (`OBJ_VEH`) and therefore has 133 handler entries;
- SF3 uses four action slots absent from the SF2 retail corpus, while fourteen
  SF2-used slots disappear from SF3;
- SF3 adds four populated predicate slots (`27`–`2a`);
- SF3's EMD/HMD transformed-vertex stride differs;
- SF3 retains three non-generic mission overlays;
- SF3 has its own resident `MENU2` and `TITLE2` variants.

Shared systems should key off the detected game profile and descriptor/object
tables, not a blanket “sequel” boolean where one of these deltas applies.

## Evidence discipline

The generated maps intentionally retain three levels:

- address/table/call-flow facts are confirmed;
- structural SF2↔SF3 matches are strong semantic boundaries;
- names inferred from SF1 or native behavior remain provisional until a guest
  probe observes the state mutation.

`map-functions` marks exact embedded-asset ranges and seed provenance.
Prologue-only or `jal`-shaped words inside those ranges are data, not evidence
of executable code.
