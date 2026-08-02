# SF3 retail guest alpha handoff

Date: 2026-08-03  
Repository: `I:\Projects\SF3-PC-Port`  
Branch: `research/sf3-full-bringup`  
Baseline: `001b1e8` (`v0.1.0-sf2-guest-alpha.10`)  
Current implementation checkpoint: `d84e475`

## Executive status

The NTSC-U retail *Syphon Filter 3* executable, `SCUS-94640`, now runs
continuously through its own title/application stack, selects Mission 1, loads
the `TOKYO.FOG` catalog and observed nested assets, dismisses its loading shell,
and remains in retail state-0 gameplay under host PAD input. The PC product
selects this runtime for Mission 1 and publishes retail GPU ordering tables and
SPU/XA PCM through existing platform backends.

This is a retail-owned hybrid runtime, not native substitute gameplay. The
guest executable and overlays still own application state, mission scripts,
actors, AI, combat, camera, objectives, timing and audio commands. Host code
owns bounded PSX services, immutable disc delivery, processed PAD transport,
presentation and PCM output.

A packaged Mission 1 alpha exists locally at:

```text
I:\Projects\SF3-PC-Port\build\SF3-Mission1-Guest-Alpha.zip
```

That binary was built at checkpoint `d84e475`. Its ZIP SHA-256 is:

```text
B5BB9DC717EB5AC143746956A2A15B4E449379A4CFE689552C6826853AF24852
```

Two independent 300-frame neutral runs of the reusable product runtime were
identical, a 600-frame forward-input run remained stable, and the configured
Release suite passed 21/21. Product presentation still needs human validation;
SDL's Windows offscreen backend cannot initialize the PsyCross/OpenGL path, so
the actual window was deliberately not opened during the quiet test session.

## What changed

The bring-up progressed through four checkpoints:

| Commit | Result |
| --- | --- |
| `2faaadb` | Retail title shell selects Mission 1 and requests `TOKYO.FOG`/`SLF.RFF`. |
| `0704870` | Retail parses TOKYO and loads the observed Mission 1 archive members. |
| `f845828` | Correct CD retry state; retail enters stable Mission 1 gameplay. |
| `d84e475` | Reusable continuous runtime is connected to the Release product. |

The most important technical outcomes are:

- a separate, independently verified `Sf3GuestRuntimeProfile`;
- correct SPU-DMA and CD-completion callback service;
- a non-aliasing SF3 interrupt stack;
- host-bound `CdControl` response and wrapper-state fidelity;
- optional deterministic formatted-memory-card BIOS service;
- exact title/menu/loading/gameplay processed-PAD boundaries;
- immutable ISO/FOG archive transport with handle-reuse correctness;
- retail ordering-table capture at the correct renderer call site;
- continuous retail SPU/XA production; and
- a product scene path which refuses to fall back to native SF3 gameplay.

## Current user-facing behavior

The alpha command is:

```powershell
.\syphon_filter.exe --no-launcher --mission=1 --scene-test `
  "Z:\Emulators\PS1 Games\Syphon Filter 3 (USA).cue"
```

The runtime currently auto-operates the retail frontend and loading shell,
then gives control to the player in Hotel Fukushima. `P` is mapped to retail
Start; Escape returns to the host title. Only SF3 Mission 1 is accepted by this
guest product path. Other SF3 missions return to title instead of entering the
older native scene implementation.

## Package contents

- `VERIFIED_FINDINGS.md` — exact discoveries and rejected hypotheses.
- `IMPLEMENTATION_MAP.md` — source changes, ownership and runtime flow.
- `CROSS_PROJECT_LESSONS.md` — findings likely to transfer to SF1/SF2 or other
  PSX recompilation/emulation projects.
- `REPRODUCTION.md` — build, probe, test and expected-output commands.
- `OPEN_WORK.md` — prioritized remaining work and acceptance gates.
- `runtime-profile.json` — machine-readable SCUS-94640 boundary map.
- `MANIFEST.sha256` — integrity hashes for the documentation files.

The fuller chronological record remains in
`docs/devlogs/2026-08-02-sf3-bring-up.md`; the governing workstream contract is
`docs/SF3_PC_PORT.md`.

## Evidence and authority rules

All addresses in this package refer only to USA revision 1.0, executable
SHA-256
`b4b32cc92e6b8634762893b637bc9a471442edbeb7569afcfb18eafbe82b9460`.
Do not apply them to another region or revision without revalidation.

`I:\Projects\PSX-References` was used read-only as research material. An SF2
relationship was treated only as a lead; adopted behavior was independently
checked against SF3 disassembly, runtime traces or exact guest state. The
earlier SF2 CFG load-delay report was investigated and explicitly ruled out as
the cause of the SF3 CD retry failure.

## Proprietary-data boundary

This package contains no retail executable, disc sector, extracted asset, RAM
dump, movie, speech, music, save image or private research artifact. Reproduction
requires a user-owned USA retail cue/bin image. The source reads immutable
bytes from that image at runtime; generated diagnostics and package binaries
remain ignored build artifacts.
