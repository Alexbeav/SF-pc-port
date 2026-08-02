# Verified SF3 findings

## Supported retail profile

| Field | Verified value |
| --- | --- |
| Game | *Syphon Filter 3* USA / NTSC-U |
| Serial | `SCUS-94640` |
| Executable | `SCUS_946.40` |
| Revision | 1.0 |
| Entry | `0x800FB368` |
| Text size | `0x001CC000` |
| Executable SHA-256 | `b4b32cc92e6b8634762893b637bc9a471442edbeb7569afcfb18eafbe82b9460` |

No address below is inherited from SF2. Each adopted boundary was checked
against SCUS-94640 runtime behavior, disassembly or the SF3 structural maps.

## Bootstrap and application stack

The initial apparent jump into zero was a probe-interpretation error. An
instruction-budget stop occurs before the interpreter fetches the next
instruction, so its reported instruction field can be zero even when guest RAM
contains valid code. The stable loop was:

```text
0x8016EA04  jal   0x800FEFFC
0x8016EA08  nop
0x800FEFFC  lw    v0,0x814(gp)
0x800FF000  jr    ra
0x800FF004  sltiu v0,v0,1
0x8016EA0C  beq   v0,zero,0x8016EA04
0x8016EA10  nop
```

With `gp=0x80121938`, the loop polls `0x8012214C`. Retail sets that word to one
at `0x800FEFD4`; the real SPU-DMA completion callback at `0x800FF728` clears it
at `0x800FF734`. Host code invokes the callback with a preserved CPU context;
it does not write the polled word.

Verified application boundaries:

| Meaning | Address |
| --- | --- |
| `Game_Main` | `0x80029ED8` |
| fourteen-state loop | `0x80029FB8` |
| state push | `0x8002C6EC` |
| state pop | `0x8002C728` |
| current state | `0x80121B88` |
| state depth | `0x80121B84` |
| state stack | `0x8010F304` |
| transition byte | `0x80121B8C` |

The complete sampled path into Mission 1 is:

```text
0 -> 1 -> 4 -> 6 -> 0 -> 9 -> 3 -> 12 -> 0 -> 1 -> 8 -> 0
```

The final stable gameplay gate is state `0`, depth `1`.

## Interrupt and callback ownership

| Meaning | Verified address |
| --- | --- |
| CD completion callback | `0x800F9DD8` |
| SPU-DMA completion callback | `0x800FF728` |
| task scheduler tick | `0x80103BE4` |
| task callback table | `0x8011FDA8` |
| safe interrupt stack | `0x8000B000` |

SF2's high mirrored callback stacks were not safe to reuse. `0x807F0000`
aliases physical `0x001F0000`, where SF3 bootstrap assets are live;
`0x807EF000` was later overlapped by `TITLE.HOG`. `0x8000B000` is below the
retail executable and outside both observed asset ranges.

An event callback at `0x801078A4` belongs to the sequel sound-service family,
but it is not the SPU-DMA completion callback and does not clear the bootstrap
poll word.

## Host-bound CD behavior

Two separate CD defects were found.

First, retail title substate 11 successfully issued `CdlGetstat`, but the
host-bound `CdControl` call consumed the controller IRQ without copying the
response buffer or publishing the PsyQ completion reason. Correct behavior is:

- copy the controller response to retail's supplied buffer;
- publish the actual completion reason (`CdlComplete == 2`);
- preserve async pending state only for commands with later completion; and
- keep the paired retail result buffers synchronized.

Second, a Mission 1 eight-sector retry completed six sectors, then reused stale
location `02:00:00` instead of requested `06:53:11`. The host-bound call had
skipped two wrapper side effects:

| Command | Retail effect | Address |
| --- | --- | --- |
| `CdlSetloc` (`2`) | copy four requested location bytes | `0x8011FEA8` |
| `CdlSetmode` (`0x0E`) | store active mode byte | `0x8011FEAC` |

The Setloc copy occurs in SCUS-94640 at `0x80106774`. Mirroring these values
restores the retry's cached location and lets initialization finish.

This was not the SF2 PSXRecomp CFG load-delay problem. The SF3 path uses the
R3000 interpreter, its focused ordinary-load delay test passes, and exact
branch/CD traces demonstrated a stale wrapper-state error.

## Retail title and movie shell

| Meaning | Address |
| --- | --- |
| title state frame | `0x8002A388` |
| title processed-PAD return | `0x801565B4` |
| TITLE2 processed-PAD return | `0x801597EC` |
| MENU processed-PAD return | `0x8014DB80` |
| title mode | `0x80157418` |
| title substate | `0x8015741C` |
| movie decoder/init boundary | `0x80147660` |
| movie update | `0x80147960` |
| movie completion | `0x8002C930` |

Retail's authored title playlist selects catalog indices 21 through 25:
`DEMO1.STR`, `DEMO2.STR`, `SCEA.STR`, `TITLE.STR`, and `ZINTRO.STR`. Headless
bring-up yields at the decoder boundary and invokes retail completion while
preserving the live continuation. This is a diagnostic movie presenter, not a
replacement for final STR decoding and display.

Title input must be gated by retail state, not wall-clock frame. The probe
waits for the mode-0 widget state to become four, then follows the story path
through title substates `0 -> 10 -> 12 -> 11`. Other observed state-10 choices
load multiplayer paths (`MPTITLE.HOG`, `MPTITLE2.HOG`, or
`FOG2/ARENA01.FOG`) and were rejected for Mission 1 bring-up.

The title path also requires memory-card behavior. A deterministic formatted
blank 128 KiB card is attached only when requested by the SF3 runtime. It
supports the observed A0 information/load calls, B0 sector reads/writes and
completion events. It is in-memory and is not persisted into the repository.

## Archive and overlay ownership

The exact platform boundaries are:

| Meaning | Address |
| --- | --- |
| `CdSearchFile` | `0x800FA688` |
| resident file read | `0x80026C7C` |

Host code supplies immutable ISO metadata and exact bytes. Retail still parses
catalogs, allocates memory, selects overlays and manages file handles.

Delivering the first 2 KiB catalog sector of `TOKYO.FOG` lets retail build its
own 20-entry virtual file table. Therefore `\TOKYO\SLF.RFF;1` is not an ISO
search once TOKYO is mounted; it is a retail virtual-member lookup.

Observed Mission 1 member sequence:

```text
SLF.RFF      0x84800
TOKYO.DAT    0x01000
GENERIC.OVL  0x00800
VLF.RFF      0x7C000 (0x20000 + 0x5C000)
DLF.RFF      0x34800
WLDEMD.HOG   0x00800 catalog read
TOKYO.SS     0x04800
NPC.HOG      0x00800 catalog read
```

The retail five-slot handle pool reuses handles for unrelated members. A host
bridge must reset its member association when retail changes the described
file. Treating `TOKYO.SS` as the next sequential `WLDEMD.HOG` chunk produced a
deterministic unaligned pointer and was rejected.

Do not service both a host-owned high-level member read and the matching guest
low-level sector path. That double ownership caused the first post-title fault.

## Input boundary

The generic processed-PAD function is `0x80022A60`. Mission 1's controller at
`0x80050D48` obtains a 60-byte processed record and returns through
`0x80050DA8`. Its button word is at record offset `+4`.

Stale frontend data left Start `0x0800` in this record and caused a false push
of application state 7. Publishing a neutral record at the exact gameplay
return removes the false pause. Host input is now sampled at this return;
retail still decides movement, camera and pause behavior.

The state-8 loading/briefing shell consumes processed PAD at `0x8002A4AC`.
One Cross sample dismisses it; retail tears down and pops state 8 through return
`0x8002A67C`.

## GPU and presentation boundary

SF3 uses GPU-submission entry `0x800F5B94`, but the relevant retail renderer
submission is specifically the call returning to `0x800F458C`. Capturing every
entry observes unrelated GPU traffic and produces incorrect frame ownership.

The best early TITLE frame was rooted at `0x801F8C94` with 25 packets, 54 GP0
words and eight draws. A later Mission 1 capture reached 640 packets, 2,537 GP0
words and 625 draws. The reusable runtime publishes state-0 frames with at
least 100 draw commands and state-7 menu frames.

The current product reuses the independently selected sequel DMA-chain
decoder and persistent two-page PsyCross submission path. That is a transport
reuse only; no SF2 address or gameplay behavior participates.

## Deterministic signatures

| Gate | Repeated result |
| --- | --- |
| Default TITLE bootstrap stdout SHA-256 | `961714DD79B26CCB72E7B0D1E7F238A16CF8902ADCDDDEA11598ADF79E93CA6C` |
| Title-to-Mission-1 request stdout SHA-256 | `8B67C9B4D8A050A5A53F80B61055ACEA58019667622BD0404CDCAB8E722E6A8F` |
| TOKYO member bring-up stdout SHA-256 | `422814CF91CCA5C6B3B294FC903CD6A8BCD361F9754677BBC808449D860E243C` |
| Stable gameplay-entry stdout SHA-256 | `89828E78A5CDE5CED232DA820A2B7C6DDD1D30D359A820A3A040B4DE235303A7` |

Reusable product-runtime neutral result, reproduced twice:

```text
mode=neutral requested=300 state=0/1 guest-frames=301 input=301
gpu=4408 presentation=4167 xa=2/2 pcm=208203
final=0/0x801859A0/882/869
```

Forward-input result:

```text
mode=forward requested=600 state=0/1 guest-frames=601 input=601
gpu=5308 presentation=4908 xa=4/4 pcm=424805
final=0/0x801859A0/651/640
```
