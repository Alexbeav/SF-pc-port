# Syphon Filter 2/3 mission-script VM map

This document records evidence from the US retail executables and mission
archives. It deliberately separates confirmed layout and control flow from
still-unknown script semantics.

## Why `.SS` matters

The sequel `.SS` files are compiled mission programs, not merely sound-scene
lists. Their program names cover objectives, doors, actor choreography,
dialogue, timers, mission pass/fail, and level transitions. `AIRBASE.OVL`, for
example, only registers two special-object callbacks; the authored AIRBASE
progression is in `AIRBASE.SS`.

All 21 SF2 missions and all 19 SF3 missions parse with the shared container
reader:

| Game/disc | Missions | Programs |
|---|---:|---:|
| SF2 disc 1 | 8 | 66 |
| SF2 disc 2 | 13 | 161 |
| SF3 | 19 | 360 |

## Container and program header

An `.SS` file begins with a 32-bit program count followed by that many
file-relative 32-bit program offsets. Each program begins with:

| Offset | Meaning | Evidence |
|---:|---|---|
| `+0x00` byte 0 | format/version byte (retail value `1`) | all retail programs |
| `+0x00` byte 1 | mutable-variable halfword count | save-state loop and variable access |
| `+0x00` byte 2 | first serialized variable, or `0xff` for none | save-state loop |
| `+0x00` byte 3 | timer/counter halfword count | frame-event decrement loop |
| `+0x04` | unknown raw header word | retained without interpretation |
| `+0x08` | event-record stream pointer | event dispatcher |
| `+0x0c` | mutable-variable table pointer | predicate/action decoders and save state |
| `+0x10` | timer/counter table pointer | frame event and save state |
| `+0x14` | program-name pointer | name lookup routine |
| `+0x18` | unknown/aliased section pointer | retained without interpretation |
| `+0x1c` | unknown payload pointer | retained without interpretation |
| `+0x20` | serialized program size | matches each blob end before alignment/padding |

The INIT overlay loader adds the program base to all six pointers. It does not
translate the bytecode.

The native parser now materializes and bounds-checks both initial halfword
tables. Variables are manipulated as unsigned halfwords. Timers are read as
signed halfwords: retail treats `-1` as inactive, decrements positive values
on event `0`, and dispatches event `8` with the timer index when one reaches
zero.

Every sequel `MissionPackage` now retains this parsed archive alongside its
BIN/DAT resources. That is a data-only integration boundary for the eventual
runtime interpreter: it does not execute any unverified action side effects.
Each parsed event also retains its exact encoded action/control halfwords and
each program retains its complete serialized image, so a future descriptor-
driven interpreter no longer needs to reopen or retain the disc archive.
All 40 SF2/SF3 campaign packages were reconstructed through this path.

## Retail entry points

| Role | SF2 | SF3 | Confidence |
|---|---:|---:|---|
| `.SS` archive load/relocate | `0x80164fdc` | `0x8016b348` | confirmed |
| predicate decoder | `0x800b2b08` | `0x800b5cf8` | structural match |
| action decoder | `0x800b3158` | `0x800b6330` | aligned event-dispatch call |
| program event dispatcher | `0x800b36a0` | `0x800b6894` | aligned activation calls |
| dispatch active programs | `0x800b38b0` | `0x800b6ae0` | structural match |
| find program by name | `0x800b3920` | `0x800b6b50` | aligned `LEVEL` startup call |
| find active-list node | `0x800b39ac` | `0x800b6bdc` | structural match |
| activate program | `0x800b39e4` | `0x800b6c14` | structural match |
| deactivate program | `0x800b3a30` | `0x800b6c60` | structural match |
| clear active list | `0x800b3a84` | `0x800b6cb4` | aligned reset/startup call |
| serialize program state | `0x800b3ad0` | `0x800b6d00` | structural match |
| restore program state | `0x800b3c2c` | `0x800b6e5c` | structural match |
| reset and activate `LEVEL` | `0x800b3d34` | `0x800b6f64` | structural match |

`0x800b2a6c`/`0x800b2ac4` push and pop the current-program/event context.
They map structurally to `0x800b5c5c`/`0x800b5cb4` in SF3.

## Event records

The dispatcher reads variable-length records from the pointer at program
`+0x08`:

- word 0 high byte selects the event (the low six bits are compared);
- word 0 low byte is the record length in halfwords;
- word 1 is an event-dependent selector/filter;
- the action stream begins at record `+0x04`;
- a high byte of `0xff` terminates the record list.

Event `0` is the per-frame path. It decrements every positive timer in the
`+0x10` table and recursively dispatches event `8` with the timer index when a
counter reaches zero. Activation and deactivation dispatch events `1` and `2`.
Other event IDs carry actor/object selectors in different packed forms, so
their names remain provisional until their callers are classified.

The complete authored event inventory is checked in as
[`research/sf2-script-events.csv`](research/sf2-script-events.csv). SF2 uses
38 distinct event IDs across 3,117 records; the table records per-ID record,
mission, program, selector, and header-flag coverage. Event 3 is the largest
family with 1,143 records; event 5 has 172 records in 20 missions.

The scheduler-to-script bridge is exact. SF2 `0x8002a4ec` / SF3
`0x8002ae80` reads the scheduled record word at `+0x0c`, passes its high
halfword as the event ID and low halfword as the selector to
`MissionScript_DispatchActivePrograms`, and therefore fans the event into
every active program. This proves that action `0x29`'s completion word
`0x0005xxxx` is authored `.SS` event 5, rather than a similarly numbered actor
event. Event domains must remain typed in native code.

The currently proven `.SS` event sources are:

| Event | Source |
|---:|---|
| 0 | per-frame active-program dispatch |
| 1 | program activation |
| 2 | program deactivation |
| 5 | completion of action `0x29`'s player/object interaction |
| 8 | expiry of one program timer; selector is the timer index |
| 11 | action `0x28` decoder-special redispatch to the current program |

The other 32 used IDs remain numerically mapped with their selector coverage
but are not assigned source-language names until their scheduler producers are
classified.

## Predicate and action tables

The predicate decoder uses 64 eight-byte descriptors:

- SF2 table: `0x801150fc`
- SF3 table: `0x80117dd0`

SF2 populates 55 predicate slots. SF3 populates 59, adding slots
`0x27`–`0x2a`. The shared slots preserve the same broad layout while some
handlers and metadata evolve.

All 55 populated SF2 predicate slots now have a storage-level operation map in
[`research/sf2-script-predicates.csv`](research/sf2-script-predicates.csv).
This closes the resident predicate dispatch surface without inventing
source-language names. It includes comparisons, object-health and flag tests,
program variable/timer tests, progress-record bit tests, player packed-state
tests, distance/random tests, and the side-effecting handlers in slots
`0x30`–`0x3f`. The latter are not a transcription mistake: retail deliberately
aliases several action handlers into the predicate descriptor window.

The action decoder uses a biased 128-entry descriptor window. Encoded high
bytes `0x80`–`0xbf` select the first 64 descriptors and `0xc0`–`0xff` select
the second 64 (with several `0xf9`–`0xff` values intercepted as control words).
The two logical action-table roots are:

- SF2: `0x8011527c` and `0x8011547c`
- SF3: `0x80117f50` and `0x80118150`

All 128 action slots are populated in both games. Each descriptor contains a
handler pointer and two 16-bit metadata words used by the decoders to resolve
inline values, variables, object selectors, and result/argument behavior. The
metadata words are retained numerically where their retail source-language
type name is not proven.

Full action-decoder control flow establishes the serialized operand contract:

- the instruction's high byte, masked with `0x7f`, selects the descriptor;
- low-byte bit 7 embeds operand 0 in the remaining seven bits; otherwise the
  following halfword is operand 0;
- when descriptor metadata word 1 is not `0xff`, operand 1 is normally the
  next halfword;
- only an extended operand 0 combined with low-byte bit 6 embeds operand 1 in
  the remaining six low bits;
- metadata value `0xff` means no operand 1;
- metadata value `0xfe`, and value `0` in the applicable operand position,
  enable program-variable/result indirection rather than changing the stream
  size.

For either indirection-capable operand, bit `0x2000` selects a program
halfword. Bit `0x8000` chooses the parent program context rather than the
current one, and the low byte is the variable index. Exact value `0x1fff`
selects the VM result register.

Descriptor metadata word 0 value `0` additionally enables selector expansion
for operand 0:

| Encoded operand 0 | Runtime dispatch |
|---:|---|
| `0x1fff` | call once with the VM result register |
| `0x1ffe` | call once for every class-2 actor object |
| bit `0x4000` | call for every active class-5 object whose resolved selector matches the low byte |
| bit `0x8000` | call repeatedly for the encoded count, reading sequential current-program variables beginning at the low-byte index |
| otherwise | call once with the decoded value |

Metadata word 0 value `0xfe` retains program-variable indirection but does
not enable those object/list expansions. Other values pass the decoded
operand directly. This distinction is why descriptor metadata is part of the
bytecode ABI rather than merely documentation for a generic two-argument
call.

`sf_tool map-mission-script-actions` now emits both encoded operands and their
`inline7`/`inline6`/`extended16` sources. This is a lossless static map of
instruction boundaries; program variables and object selectors remain
runtime values and are not falsely resolved offline.

The non-action control words are also exact:

| High byte | Operation |
|---:|---|
| `0xff` | terminate the current event action stream |
| `0xfe` | evaluate the predicate beginning at `+4`; select the low-byte relative halfword offset or the alternate offset stored at `+2` |
| `0xfd` | text record; advance by the low-byte halfword length |
| `0xfc` | submit the embedded control payload, then advance by its low-byte halfword length |
| `0xfa` | skip by the low-byte halfword length |
| `0xf9` | process formatted text with fixed mode `0x50`, then advance by the low-byte halfword length |

Of the 192 aligned predicate/action descriptor slots, 169 retain identical
metadata between SF2 and SF3 and 23 change. Descriptor-slot alignment supplies
a deterministic handler crosswalk even where a handler changed too much for a
structural function hash.

The complete 128-action crosswalk is checked in as
[`research/sf2-sf3-script-actions.csv`](research/sf2-sf3-script-actions.csv).
It records both games' handler addresses and metadata plus whole-corpus
occurrence counts for every slot, including unused slots. Action metadata is
identical in 113 slots and differs in 15; together with the predicate table
this produces the 169/23 combined descriptor result above. The SF2 ordinary
action total is exactly 10,655 and the SF3 total is 16,366.

Confirmed examples include:

- predicate `0x17`: bounded retail random test;
- the handler at `0x800b2088` is shared by predicate slot `0x3b` and action
  slot `0x0b`, and directly calls `MissionScript_Activate`;
- the handler at `0x800b20bc` is shared by predicate slot `0x3c` and action
  slot `0x0c`, and directly calls `MissionScript_Deactivate`;
- shared script handler `0x800b13b4`: reaches both
  `MissionEvent_DispatchByObject` and actor event dispatch;
- multiple action handlers call `MissionScheduler_FrameEvent`, proving that
  authored scripts schedule shared engine work rather than relying on mission
  overlays.

The first implementation-grade action bindings are:

| Action | SF2 handler | SF3 handler | Recovered operation |
|---:|---:|---:|---|
| `0x0b` | `0x800b2088` | `0x800b4f48` | activate program by table index |
| `0x0c` | `0x800b20bc` | `0x800b4f7c` | deactivate program by table index |
| `0x1b` | `0x800b13b4` | `0x800b4168` | actor scripted activation/state change (strong) |
| `0x31` | `0x800b285c` | `0x800b563c` | night-vision state through player events `0x7f`/`0x80` (strong) |
| `0x32` | `0x800b0b3c` | `0x800b37fc` | grant an item and its table-scaled quantity to the current player |
| `0x55` | `0x800b0bac` | `0x800b386c` | dispatch event `0x81` or `0x82` to the current player object according to a Boolean operand |
| `0x68` | `0x800b198c` | `0x800b4740` | dispatch fixed object event `1` |
| `0x6a` | `0x800b26a0` | `0x800b5524` | stop/reset active scene-speech stream |
| `0x70` | `0x800b2934` | `0x800b56f8` | start a full-screen transition using the decoded preset/type |
| `0x75` | `0x800b19b0` | `0x800b4764` | dispatch decoded object/event pair |

Action `0x32` decodes item ID and quantity operands, selects the primary or
fallback quantity multiplier byte from the retail item table, multiplies the
authored quantity by it, and calls `InventoryItem_Acquire`. This is the
confirmed script-to-inventory boundary used for mission loadouts and pickups;
the authored operand is not a raw round count.

Action `0x70` calls a shared wrapper that constructs a `512 x 240`
full-screen transition descriptor and submits it to the application
transition path. The decoded operand is retained as a preset/type rather
than assigned a visual name whose retail enum is not yet recovered.

Only the two “strong” rows retain semantic qualification; the other eight
follow direct argument flow into confirmed native functions.

The mission objective/parameter family is now separated from the package
bitset above:

| Action | SF2 handler | Recovered operation |
|---:|---:|---|
| `0x08` | `0x800b1e9c` | reveal/announce one objective |
| `0x09` | `0x800b1ec0` | record and present one objective failure |
| `0x0a` | `0x800b1eec` | assign one objective's completed state |
| `0x1e` | `0x800b2000` | record and present one parameter failure |
| `0x1f` | `0x800b202c` | set one parameter-status bit |

This naming follows both direct state mutation and whole-corpus context:
AIRBASE `LEVEL` uses `0x08` to introduce its objectives, while guard/
constraint programs use `0x1e`; death and terminal paths use `0x09`.
The shared presentation routines maintain distinct objective and parameter
text tables and status bitsets.

SF3 changes the encoding rather than merely shifting these handlers. Its
action `0x08` handler at `0x800b4c9c` multiplexes five operations using the
first operand and the objective index in the second:

| SF3 `0x08` mode | Operation |
|---:|---|
| 0 | complete/present objective |
| 1 | clear objective completion state |
| 2 | fail/present objective |
| 3 | set the secondary objective-status bit |
| 4 | reveal/present objective |

SF3 action `0x1e` at `0x800b4df8` similarly uses modes 0/1/2 for parameter
status set, status clear, and parameter failure/presentation. This is a
confirmed bytecode-ABI delta: native code must version the descriptor-driven
operand contract, not bind action numbers to one blanket sequel operation.

The compact mutable-state family is also recovered far enough to implement
without mission-specific guesses:

| Action | SF2 handler | SF3 handler | Recovered operation |
|---:|---:|---:|---|
| `0x01` | `0x800b22a4` | `0x800b5164` | assign decoded value to a program halfword variable |
| `0x02` | `0x800b2838` | `0x800b5618` | assign decoded value to one program timer/counter halfword |
| `0x0d` | `0x800b20f0` | `0x800b4fb0` | set one bit in the mission-package bitset |
| `0x0e` | `0x800b2140` | `0x800b5000` | clear one bit in the mission-package bitset |
| `0x0f` | `0x800b2194` | `0x800b5054` | increment a program halfword variable |
| `0x10` | `0x800b21d8` | `0x800b5098` | decrement a program halfword variable |
| `0x5a` | `0x800b221c` | `0x800b50dc` | add a decoded value to a program halfword variable |
| `0x5b` | `0x800b2260` | `0x800b5120` | subtract a decoded value from a program halfword variable |
| `0x5c`, `0x61` | `0x800b22dc` | `0x800b519c` | store the VM result register in a program variable |
| `0x5d` | `0x800b2318` | `0x800b51d8` | store `rand() % bound` in a program variable |
| `0x23` | `0x800b2398` | `0x800b5258` | load an object's signed health into the VM result register |

Variable operand bit 15 selects the parent/nested program context; the low
byte indexes the program's `+0x0c` halfword table. The bitset pointer is
installed from mission-package header offset `+0x30`, is copied by the
mission save-state path, and is therefore deliberately named by storage role
rather than guessed as objectives, triggers, or completion flags.

Action `0x02` indexes the current program's `+0x10` timer table and stores its
second decoded operand directly. Together with event `0`'s decrement/expiry
path, this completes the script timer lifecycle without assigning a
mission-specific meaning to any timer index.

The highest-frequency remaining actor/object operations are also recovered
at their exact storage boundaries:

| Action | SF2 handler | SF3 handler | Confirmed effect |
|---:|---:|---:|---|
| `0x00` | `0x800b02f8` | `0x800b2f28` | set the selected bit in the shared application/script flag word |
| `0x03` | `0x800b0f70` | `0x800b3cf8` | assign actor-runtime word `+0x04` bit 0 from a Boolean operand |
| `0x04` | `0x800b0fd4` | `0x800b3d5c` | assign actor-runtime word `+0x04` bit 4 from a Boolean operand |
| `0x06` | `0x800b0c30` | `0x800b3918` | clear object byte `+0x27` bit 1 when signed health is positive or `-2` |
| `0x07` | `0x800b0c84` | `0x800b396c` | set object byte `+0x27` bit 1 and run the shared actor removal/deactivation path |
| `0x11` | `0x800b1a10` | `0x800b47c4` | set bit `0x8000` in a selected 16-byte runtime-table entry when its low 11 bits are nonzero |
| `0x12` | `0x800b1a30` | `0x800b47e4` | clear bit `0x8000` in a selected 16-byte runtime-table entry |
| `0x13` | `0x800b1030` | `0x800b3db8` | assign actor-runtime halfword `+0x10`, with the active-actor reset helper when required |
| `0x14` | `0x800b0e94` | `0x800b3c1c` | assign actor-runtime word `+0x0c` low mode nibble and update its associated active flags |
| `0x15` | `0x800b1100` | `0x800b3e88` | clear actor tracking/target state, set runtime halfword `+0x10` to `-1`, and clear the fixed actor-state bit |
| `0x19` | `0x800b1588` | `0x800b433c` | apply the shared actor state/flag transition helper |
| `0x1a` | `0x800b15c0` | `0x800b4374` | apply a fixed actor-runtime flag mask and assign byte `+0x15` |
| `0x1c` | `0x800b1544` | `0x800b42f8` | set actor-runtime byte `+0x16` to 20 and clear the fixed state-mask bits |
| `0x21` | `0x800b1320` | `0x800b40d4` | set actor-runtime word `+0x00` bit `0x20000` and clear bit `0x800000` |
| `0x22` | `0x800b0fac` | `0x800b3d34` | assign the selected object's signed health halfword directly |
| `0x26` | `0x800b1b8c` | `0x800b4940` | apply decoded scripted health/damage state through the shared actor reaction/death/event path |
| `0x27` | `0x800b1de4` | `0x800b4be4` | schedule the decoded event at priority 2 between the selected object and current player/source |
| `0x28` | `0x800b2854` | `0x800b5634` | decoder-special redispatch of event 11 to the current program with the decoded selector; the descriptor handler itself is an intentional no-op marker |
| `0x2a` | `0x800b1250` | `0x800b4004` | for a non-special actor state, set runtime bit `0x800` and assign byte `+0x14` |
| `0x2b` | `0x800b12ac` | `0x800b4060` | assign an actor's equipped/held item through the shared detach, held-model refresh, and event path |
| `0x2d` | `0x800b1cf0` | `0x800b4af0` | set or clear object byte `+0x27` bit 0 from a Boolean operand |
| `0x2e` | `0x800b1478` | `0x800b422c` | apply a fixed actor-runtime flag transition and assign byte `+0x15` or `0xff` |
| `0x30` | `0x800b1f90` | `0x800b4d88` | request successful mission transition/progress when the current mode and player state permit it |
| `0x33` | `0x800b0d90` | `0x800b3aec` | toggle actor-runtime bit `0x100000` through the shared room/event update path |
| `0x34` | `0x800b0db0` | `0x800b3b0c` | replace positive object health with `0x7fff` |
| `0x35` | `0x800b1a70` | `0x800b4824` | clear runtime-table bit `0x8000` and run the associated shared table-entry reset |
| `0x38` | `0x800b1c0c` | `0x800b49c0` | assign object byte `+0x25` |
| `0x3e` | `0x800b182c` | `0x800b45e0` | assign object-record word `+0x28` bits 4–7 |
| `0x3d` | `0x800b17e0` | `0x800b4594` | assign the object's handler-state halfword at object-record offset `+0x4a` |
| `0x40` | `0x800b28b0` | `0x800b5690` | pack two decoded halfwords into one shared script-state word |
| `0x42` | `0x800b0e20` | `0x800b3ba8` | apply the shared actor mode/state transition helper |
| `0x43` | `0x800b135c` | `0x800b4110` | clear actor-runtime word `+0x00` bit 31 and assign bit 30 from a Boolean operand |
| `0x44` | `0x800b12cc` | `0x800b4080` | clear actor-runtime word `+0x00` bit 30 and assign bit 31 from a Boolean operand |
| `0x4a` | `0x800b2694` | `0x800b5518` | assign the volume halfword consumed by the next scene-speech action |
| `0x54` | `0x800b1a50` | `0x800b4804` | assign bits 11–13 in a selected 16-byte runtime-table entry |
| `0x56` | `0x800b1644` | `0x800b43f8` | assign actor-runtime byte `+0x12` and reset active movement/target fields through the shared helper |
| `0x57` | `0x800afbf0` | `0x800b27e0` | set dormant bit 1 on every class-2 actor object |
| `0x58` | `0x800afc68` | `0x800b2858` | clear runtime-table bit `0x8000` on every populated 16-byte entry |
| `0x59` | `0x800b10c4` | `0x800b3e4c` | assign actor-runtime word `+0x0c` bits 6–7 |
| `0x5e` | `0x800b1198` | `0x800b3f20` | assign a decoded pair to the two shared script-state words at `0x8011f888`/`0x8011f88c` |
| `0x5f` | `0x800b1930` | `0x800b46e4` | assign object-record word `+0x28` bit 13 from a Boolean operand |
| `0x60` | `0x800b0c00` | `0x800b38c0` | apply a selected object/value interaction from the current player through shared geometry and mission callbacks |
| `0x62` | `0x800b167c` | `0x800b4430` | apply the four-mode actor event/state transition (`0x63`, `0x92`, `0x91`, or `0x5d`) |
| `0x66` | `0x800b2954` | `0x800b5718` | submit two decoded parameters to the shared low-level audio ramp/control path |
| `0x67` | `0x800b16f0` | `0x800b44a4` | assign actor-runtime word `+0x04` bit 24 from a Boolean operand |
| `0x69` | `0x800b1d64` | `0x800b4b64` | register the shared spatial/attachment update callback between two decoded objects |
| `0x71` | `0x800b1730` | `0x800b44e4` | activate the selected actor, clear its dormant flag, and add its display to the world list |
| `0x7b` | `0x800b1db8` | `0x800b4bb8` | assign object-record handler-state halfword `+0x4a` by direct object index |
| `0x7e` | `0x800afcb8` | `0x800b28a8` | assign object-display word `+0x14` bit 24 and the associated signed mode field |

These names deliberately stop at observable storage/helper effects. They
cover 5,949 reachable SF2 action occurrences, but the retail source-language
nouns for the actor mode bits are not inferred from frequency or animation
appearance.

Action `0x30` is the authored success boundary, not a host-side level warp.
It rejects multiplayer/special mode, dead players, and the disallowed player
class, starts the shared mission transition, and schedules the existing
campaign-progress callback. A native `.SS` interpreter should reach
`Campaign_AdvanceOrComplete` through this shell rather than incrementing a
mission index directly.

Counting the objective, mutable-state, inventory, speech, event, flag, actor,
and transition families above, 10,384 of the 10,655 reachable ordinary SF2
action occurrences (97.5%) now have an implementation-grade native effect
map. This is occurrence coverage, not a claim that 97.5% of source-language
verbs or full campaign behavior has been implemented.

The low-frequency tail has also been reduced to observable storage and
scheduler contracts. These operations account for 105 of the mapped
occurrences above:

| Action | SF2 handler | SF3 handler | Confirmed effect |
|---:|---:|---:|---|
| `0x05` | `0x800b0e58` | `0x800b3be0` | assign actor-runtime word `+0x04` bit 3 from a Boolean operand |
| `0x18` | `0x800afe50` | `0x800b2a74` | copy one 32-byte display/template record into a selected object's display state and set its fixed activation flags |
| `0x20` | `0x800b2058` | `0x800b4e90` | clear one decoded bit in the active package/runtime record word at `+0x20` |
| `0x2f` | `0x800b0d54` | `0x800b3ab0` | call `ActorHealth_ApplyScriptedState` with the fixed `0x7d00` mode and decoded object/value |
| `0x37` | `0x800b0df0` | `0x800b3b4c` | no operation |
| `0x39` | `0x800b0ccc` | `0x800b39b4` | set actor-runtime word `+0x04` bit 16 and clear the linked halfword |
| `0x3a` | `0x800b0d10` | `0x800b3a6c` | clear actor-runtime word `+0x04` bit 16 and clear the linked halfword |
| `0x3b` | `0x800b1770` | `0x800b4524` | assign the decoded byte to offset zero of the selected object record |
| `0x3c` | `0x800b1790` | `0x800b4544` | resolve the selected object's linked object-record index and assign `-1` to that record's handler-state halfword at `+0x4a` |
| `0x48` | `0x800b26e0` | `0x800b49e0` | assign the decoded value to selected object-record word `+0x04` |
| `0x49` | `0x800b2700` | `0x800b38f0` | schedule type `0x10`/`0x11` at priority 2 with fixed argument `0xfffe` |
| `0x4b` | `0x800b2748` | `0x800b5564` | schedule type `0x0f` at priority 2 with argument bit `0x8000` set |
| `0x4c` | `0x800b2784` | `0x800b55b4` | schedule type `0x0f` at priority 2 with argument bit `0x2000` set |
| `0x4e` | `0x800b27fc` | `0x800b3b54` | schedule type `0x0f` at priority 2 with argument bit `0x0800` set |
| `0x51` | `0x800b28e8` | `0x800b56ac` | when a tracked object index is valid, schedule type `0x0f` at priority 2 with the two decoded values packed into its argument |
| `0x52` | `0x800b0df8` | `0x800b3b80` | assign actor-runtime byte `+0x13` |
| `0x53` | `0x800b0f30` | `0x800b3cb8` | set actor-runtime word `+0x04` bit 27 and assign halfword `+0x10` |
| `0x63` | `0x800b1d30` | `0x800b4b30` | resolve two object positions, compute the retail approximate 3D distance (`largest + (middle + smallest) / 4`), divide it by 32, and store it in the VM result register |
| `0x64` | `0x800b11b4` | `0x800b3f3c` | assign bits 8–11 of the selected actor's 76-byte runtime-table record after subtracting the retail base value 7 |
| `0x65` | `0x800b16b4` | `0x800b4468` | read the selected actor/item quantity pair, sum it, and store the result in the VM result register |
| `0x6c` | `0x800b2974` | `0x800b5738` | schedule type `0x12` at priority 5 with the decoded object/value pair |
| `0x6d` | `0x800b29b0` | `0x800b5774` | reset the current global actor's five auxiliary resource handles |
| `0x6e` | `0x800b29d8` | `0x800b579c` | configure the two global actors' auxiliary resource state with the decoded signed mode and fixed callback |
| `0x72` | `0x800afa4c` | `0x800b25b8` | copy one selected object's position into another object's attachment/display transform while preserving its rotation and setting the fixed transform flags |
| `0x76` | `0x800b19d0` | `0x800b4784` | assign object-display word `+0x04` bit 26 from a Boolean operand |
| `0x7a` | `0x800b1210` | `0x800b3f98` | assign actor-runtime word `+0x04` bit 28 from a Boolean operand |
| `0x7f` | `0x800afb68` | `0x800b26d4` | select one of two indexed halfword tables from operand bit 15, then write either the fixed current scalar (mode 0) or an absolute object orientation component divided by 32 (modes 1/2) |

Four additional wrappers are narrowed but retain conservative names pending a
guest state probe: `0x2c` removes matching class-5/tracked objects through the
shared removal path; `0x47` invokes a large selected-object lifecycle helper;
`0x6b` submits a selected object and decoded value to the shared spatial
callback path; and `0x73` activates a selected object through display,
attachment, and scheduler services. Their exact source-language verbs are not
inferred from the helper shapes.

Action `0x79` is a seven-mode NPC/player-resource lifecycle multiplexer at
SF2 `0x800aff4c` / SF3 `0x800b2b70`. Its jump table is fully recovered:

| Mode | Observable retail operation |
|---:|---|
| 0 | invoke the current mission overlay's NPC-resource callback with the decoded index |
| 1 | build/load the selected NPC resource and then run the additional post-load callback |
| 2 | build/load the selected NPC resource without the mode-1 post-load callback |
| 3 | detach the selected NPC resource from either tracked slot and release matching runtime users |
| 4 | install the selected object as the active NPC/player resource and set active flags |
| 5 | detach and remove the currently installed NPC/player resource |
| 6 | install the selected object as the active resource without the mode-4 flag mutation |

The corpus context agrees with that storage behavior: modes 0–3 appear in
`SWAPPING_NPCS` and level-start programs, while modes 4/5 bracket authored
character/call sequences. The table is implementation-grade at the resource
lifecycle boundary, but it does not establish retail character names.

Action `0x29` (SF2 `0x800b24f4`, SF3 `0x800b53b8`) starts a scripted
player/object interaction. All 62 authored operand-0 values resolve to valid
object indices in their own mission; there are no counterexamples across
either disc. The shared path at `0x80041054`:

- arms an 80-tick completion state for that object;
- modes 0 and 1 queue distinct player events `0x5c` and `0x62`;
- both player-event modes clear/update the player's current interaction
  targeting state;
- mode 2 enters the shared special interaction/resource path;
- the completion callback schedules event 5 back to the selected object and
  clears the pending state.

This closes the object/control-flow contract required by a native
interpreter. The designer-facing names of modes 0–2 remain neutral because
their uses span level starts, objectives, torture, calls, and encounter
transitions.

Object flag operations are distinct from the package bitset. Actions `0x24`
and `0x25` clear/set object byte `+0x27` bit 1 and schedule events `0x10` and
`0x11`; `0x45`/`0x46` perform the same bit mutation without scheduling.
Actions `0x4f`/`0x50` set/clear a caller-selected bit in that byte. Action
`0x36` sets/clears bit 5 and, for most object classes, bit 4 through the
shared object-state helper. These are confirmed storage/event effects; their
source-language verbs remain unknown.

Actions `0x16`, `0x17`, `0x77`, and `0x78` are also confirmed as four
argument variants around the shared scene-speech start boundary. Their exact
source-language names and mode-flag spelling remain neutral.

The handler-call inventory emits 88 descriptor-associated rows for SF2: 82
unique physical direct calls from 67 physical handlers. SF3 emits 95 rows:
89 unique physical calls from 68 handlers. SF2's descriptor handlers reach
56 distinct direct callees. Aliased predicate/action slots deliberately
repeat an edge in the descriptor view. This map is an edge inventory: an
absent edge does not imply a leaf handler because computed calls and inline
state mutations do not use `jal`.

The complete neutral descriptor inventory is reproducible with:

```text
sf_tool map-mission-script-opcodes <game.cue> <output.csv>
sf_tool map-mission-script-handler-calls <game.cue> <output.csv>
sf_tool compare-mission-script-opcodes <sf2.cue> <sf3.cue> <output.csv>
```

The structural action walker follows both sides of predicate branches and
deduplicates loops without executing handler side effects:

```text
sf_tool map-mission-script-actions <game.cue> <output.csv>
sf_tool map-mission-script-strings <game.cue> <output.csv>
```

It validates every action edge against its enclosing event record. Across the
retail corpus it finds:

| Game | Event records | Reachable action/control words | Distinct action opcodes used |
|---|---:|---:|---:|
| SF2 (both discs) | 3,117 | 15,450 | 118 |
| SF3 | 4,573 | 23,837 | 108 |

SF2-only used action slots are `18 37 48 4a 51 55 5a 5b 66 6b 6c 70 76
7a`; SF3-only used slots are `10 41 4d 7c`. This is a concrete compatibility
delta: most of the language is shared, but SF3 is not merely an address-shifted
SF2 table.

### Program activation graph

The confirmed `0x0b`/`0x0c` handlers index the loaded program-pointer table
with their first decoded operand. `map-mission-script-actions` therefore also
emits `program_operation`, `target_program`, and `target_program_name`.
Across both SF2 discs, 361 reachable instructions perform 255 activations and
106 deactivations, producing 321 unique source/operation/target relationships.
SF3 has 595 such instructions (390 activation, 205 deactivation) and 527
unique relationships.

Two early missions demonstrate why this graph is the scalable port boundary:

- AIRBASE `LEVEL` activates `LOOKING_FOR_TROUBLE`, `BOXTRAP`,
  `COME_WITH_ME_DOCTOR`, and `OPEN_SESAME` on distinct events;
- HWAY `LEVEL` activates `CHANCE_PINNED`; that program activates
  `TUNNEL_STUB`, `TUNNEL_FIGHT`, and later `FLAMETHROWER_FIGHT`, which hands
  off through `OPEN_AREA_MOVIE` to `OPEN_AREA`.

These are retail control-flow relationships only. They do not prove the side
effects of the other action opcodes inside those programs.

The event-record inventory is reproducible with:

```text
sf_tool map-mission-script-events <game.cue> <output.csv>
```

## Control words in an action stream

The action decoder recognizes high-byte control forms before ordinary action
dispatch:

- `0xff`: end of stream;
- `0xfe`: evaluate a predicate expression through the predicate decoder and
  conditionally skip;
- `0xfd`: display/queue authored text;
- `0xfc`: invoke a shared control helper;
- `0xfa` and `0xf9`: alternate skip/text control paths.

Their exact source-language spelling is not yet known. The encoded behavior is
known well enough to build a structural decoder, but not yet enough to claim a
gameplay-complete native interpreter.

## Porting consequence

Mission-specific native fixes remain useful for bring-up, but the scalable
campaign implementation is:

1. retain and validate the `.SS` container and section layout;
2. decode event/action/predicate streams without inventing semantics;
3. bind confirmed opcode handlers to shared native systems;
4. compare the same program patterns across both SF2 discs and SF3;
5. replace mission-specific approximations only after deterministic parity
   probes demonstrate the shared path.

This prevents AIRBASE, I-70, later disc-2 missions, and SF3 from becoming
independent hard-coded rewrites.
