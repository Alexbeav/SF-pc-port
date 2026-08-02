# PSX port knowledge report — Syphon Filter 3

- Date: 2026-08-03
- Handoff checkpoint: `531bbe8`
- Implementation checkpoint: `d84e475`
- Retail target: NTSC-U `SCUS-94640`, executable SHA-256 gated in the runtime
  profile
- Architecture lane: MIT retail-authoritative guest runtime

## Executive state

The retail executable reaches stable Mission 1 state `0/1`, loads the TOKYO
catalog and observed members, submits retail GPU work, produces SPU/XA PCM and
accepts processed PAD input. Two neutral 300-frame product routes match, a
600-frame forward route is stable, and the Release suite passed 21/21 at the
handoff.

This is a Mission 1 bootstrap alpha, not a campaign port. The frontend is
auto-operated, movie completion is headless, product GPU composition and
texture residency are incomplete, and human presentation/audio/input
validation was still pending when the handoff was packaged.

## Shared findings contributed

The private PSX ports knowledge repository records these normalized findings:

| Finding | SF3 evidence |
|---|---|
| `PSX-VAL-002` | Pre-fetch instruction-budget stop was distinguished from fetched zero code |
| `PSX-MEM-001` | Mirrored high callback stacks aliased live physical asset RAM |
| `PSX-HLE-001` | CD HLE needed response/reason and retail Setloc/Setmode software mirrors |
| `PSX-ASSET-003` | Double ownership of member and sector delivery faulted retail state |
| `PSX-ASSET-004` | Reused retail handle required identity/generation reset |
| `PSX-CTRL-003` | Processed-PAD sample boundaries made frontend/loading/gameplay input deterministic |
| `PSX-MDEC-005` | Headless movie completion proves transition ownership, not A/V support |

SF3 also independently reinforces caller-qualified GPU capture: a common GPU
entry receives unrelated work, so the useful OT needs caller/return provenance,
application state, root and a validity contract.

## Rejected hypotheses retained for other projects

- a zero instruction field necessarily means invalid guest code;
- a nearby sound-family callback owns SPU-DMA completion;
- inherited sequel callback stacks are safe because their virtual addresses
  are high;
- correct device transport is sufficient without HLE wrapper RAM effects;
- the SF2 captured-CFG load-delay defect caused the SF3 retry; and
- a retail file-handle slot permanently identifies one member stream.

## Highest-value incoming knowledge

1. SF2 hybrid persistent-page and complete same-tick OT composition evidence.
2. SF2 recomp authentic frontend and nested-call IRQ/JALR contracts.
3. Tenchu `PSX-CPU-001` safe-resume/context-redirect regression once generic.
4. Shared long-run SPU/XA timing and quick-state derived-state schemas.

These are leads until independently reproduced against the SCUS-94640 profile.
No sibling address or mission behavior may be imported by analogy.

## Next decisive experiment

Run the visible Mission 1 product and record the first authored frame, both
display pages, input/pause, XA/music/dialogue and ten-minute stability. Then
instrument all OT submissions belonging to one retail tick and remove native
texture seeding in favor of guest-authoritative VRAM/residency evidence.

## Portfolio return checklist

- Refresh the normalized SF3 project snapshot at each committed milestone.
- Add source-owned regressions for physical aliases, layered-I/O ownership,
  recycled handles and pre-fetch stop reporting.
- Promote only address-free invariants; keep the executable profile local.
- Record contradictions and rejected sibling analogies explicitly.
- Prepare generic upstream contributions under the community policy only after
  a focused test, license audit and explicit user approval.
