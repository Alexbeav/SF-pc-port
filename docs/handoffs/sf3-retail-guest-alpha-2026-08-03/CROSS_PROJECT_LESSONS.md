# Cross-project lessons

These notes are intended for SF1/SF2 worktrees, PSX recompilation experiments
and other emulator-backed hybrid ports. They describe patterns, not universal
addresses.

## 1. Distinguish a pre-fetch budget stop from an invalid instruction

The SF3 bootstrap initially looked like an uninitialized code dispatch because
the execution result's instruction field was zero at `0x8016EA0C`. The
interpreter had stopped before fetching the next instruction. Guest RAM held a
valid backward branch.

When diagnosing a zero instruction:

1. Read guest RAM at the reported PC.
2. Capture the final bounded control-flow window.
3. Attribute `$ra` to its producer, not merely its current value.
4. Record the stop reason and whether fetch occurred.

Do not patch the PC or synthesize a function target until this distinction is
resolved.

## 2. Service the retail completion owner, not a nearby callback

SF3 opens a sound-family BIOS event callback at `0x801078A4`, but that callback
does not clear the SPU-DMA poll word. The exact DMA completion routine does.
Structural similarity is useful for locating candidates; only runtime state
effects establish ownership.

A good callback proof includes:

- the exact state word before and after;
- the guest instruction which writes it;
- preserved interrupt CPU context and stack;
- device interrupt cause; and
- a regression which fails if the callback is omitted.

## 3. PSX virtual addresses can alias live asset memory

An apparently high and safe interrupt stack can alias low physical RAM because
KSEG mirrors are masked by the PSX memory map. Both inherited SF2 stack choices
overlapped SF3 bootstrap assets.

Before selecting a callback stack, resolve its physical mirror and compare the
entire interrupt-frame range against:

- executable text/data;
- resident overlays;
- current archive/texture loads;
- heap/stack arenas; and
- later title/mission asset ranges, not just the first bootstrap snapshot.

## 4. HLE wrapper fidelity includes software mirrors

A host-bound CD command can drive the emulated device correctly and still
break retail code if it skips wrapper-owned RAM effects. SF3 retries consult
the software Setloc mirror rather than asking the device for its current
position.

For every HLE boundary, audit:

- return value;
- output/result buffer;
- completion reason;
- pending/active state;
- callback/event delivery;
- wrapper-maintained global mirrors; and
- command-specific synchronous versus asynchronous timing.

This pattern likely applies to PAD, GPU, SPU, memory-card and file wrappers as
well as CD-ROM.

## 5. Do not diagnose by analogy when exact traces disagree

The SF2 CFG load-delay report was a reasonable lead for a CD controller loop.
It was not the SF3 cause: the execution engine differed, its load-delay test
passed, and exact CD traces showed a stale Setloc mirror.

Cross-project reports should be used to generate falsifiable checks. Record why
a tempting hypothesis was rejected so later work does not repeatedly rediscover
it.

## 6. Own one archive layer at a time

SF3 failed when the host serviced a high-level resident/member request and the
guest low-level sector path continued for the same transfer. The correct
boundary supplies immutable metadata/bytes once and lets retail own parsing,
allocation and state transitions.

An archive bridge should explicitly state whether it owns:

- ISO path lookup;
- ISO sector transport;
- outer archive catalog delivery;
- virtual-member lookup; or
- member byte transfer.

Never implicitly own two matching layers for one request.

## 7. Retail file handles are identities, not permanent stream objects

SF3's five-slot handle pool reuses a handle for different members. Host state
keyed only by handle and monotonically increasing offset corrupted the next
file. Associate handle state with retail-described file identity and size;
reset it whenever either changes.

Split reads must still preserve per-file offsets. `VLF.RFF` is observed as
`0x20000 + 0x5C000`, while a later reuse starts a new file at offset zero.

## 8. Drive input by retail sample boundaries

Fixed host-render-frame pulses are unreliable because retail PAD sampling may
run at a different cadence. SF3 title choices and the loading confirmation were
made deterministic by counting visits to exact processed-PAD returns and
gating input on retail mode/substate.

For reproducible input:

- locate the consumer's processed record;
- publish neutral releases between edges;
- count guest samples, not wall time;
- gate on retail state/widget readiness; and
- clear stale frontend records before gameplay.

## 9. Capture the renderer call site, not a common callee

The GPU submission entry receives multiple kinds of work. The useful retail OT
is identified by both callee and return address. A capture boundary should
include caller/return provenance, application state, ordering-table root and a
minimum validity contract.

Reusing a packet decoder across games is safe only as a presentation transport
after the new game independently proves packet format and boundary ownership.

## 10. Keep deterministic probes after product integration

A desktop build is poor forensic evidence: frame pacing, audio callbacks and
human input add nondeterminism. SF3 retains a compact product-runtime probe and
the verbose bootstrap/title-shell probe after scene integration.

Useful gates include:

- two independent processes with byte-identical stdout;
- explicit guest state/depth;
- exact input-sample and frame counts;
- XA received/admitted/rejected counters;
- bounded CPU/device progress; and
- full Release tests alongside retail-disc probes.

## 11. Headless movie completion is a bridge, not final presentation

Completing a retail STR request without decoded frames is useful for reaching
later application states, but it proves only transition ownership. It does not
prove movie timing, audio synchronization, MDEC/DMA behavior or display.
Retain this distinction in milestones and release notes.

## 12. Refuse unsupported native fallback

Once a product claims a retail guest path, silently falling back to native
substitute gameplay for unsupported missions makes test reports ambiguous.
SF3 explicitly returns to title for Missions 2-19 until those guest paths are
implemented. Other projects should make ownership visible in logs and failure
behavior.
