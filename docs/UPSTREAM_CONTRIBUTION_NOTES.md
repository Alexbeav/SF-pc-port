# Upstream contribution candidates

This file records reusable findings which may warrant a focused contribution
to the upstream SF1 port. It is not an instruction to publish automatically.
Before submission, reproduce the need against the current upstream branch,
follow its contribution guide, and audit the exact patch for game data,
game-specific addresses, private paths, and license-incompatible material.

## PSXRecomp captured-CFG load-delay correction

The companion recompilation lab isolated a generic defect in PSXRecomp's
captured-overlay CFG emitter: ordinary MIPS-I loads were committed before the
immediately following instruction instead of after it. Its two-file fix and
focused regression are suitable for upstreaming independently of any SF2
material. The interpreter used by this port was cross-audited against that
finding: it already carries separate current/next delayed-load state, covers
`LB`, `LBU`, `LH`, `LHU`, `LW`, `LWL`, and `LWR`, and has a focused regression
which proves the immediate consumer observes the old register value and a
register write cancels an incoming load. The regression now also covers an
adjacent unaligned `LWL`/`LWR` pair: the pair merges through pending state while
its immediate consumer still sees the old architectural register. No runtime
change is required here.

The generic recompiler correction and its focused regression were submitted as
[PSXRecomp PR #93](https://github.com/mstan/psxrecomp/pull/93). As of
2026-08-03 it remains open and GitHub reports it mergeable. Its public diff
contains only `recompiler/src/code_generator.cpp` and
`recompiler/tests/recompiler_patch_test.cpp`; an explicit scan found no SF2
name, serial, address, workstation path, or diagnostic material.

Upstream boundary: the recompiler code-generator change and its focused test
only. Diagnostic environment switches and all SF2-specific material stay out.

## Non-`$ra` JALR regression

The companion lab found that SF2 executes descriptor trampolines with forms
such as `jalr $a1,$t0`, and also demonstrated why rewriting `rd=0` to `$ra`
fabricates a continuation. This port's interpreter already implements the
architectural rule: it writes `pc+8` to the encoded `rd`, while register zero
remains immutable. No runtime correction was needed.

`tests/r3000_runtime_tests.cpp` now carries a focused, game-independent
regression. One synthetic program links through `$a1`, returns through that
register, and proves the delay slot. A second uses `jalr $zero,$t0`; an invalid
fallthrough word makes the test fail if an implementation invents an `$ra`
link. This test is suitable for an upstream contribution if the current
upstream suite lacks equivalent coverage; the SF2 descriptor addresses and
captured code are not required.

## Legacy VM clock ownership

### Reusable change

The common legacy VM now distinguishes three execution contracts:

1. normal execution, where retired guest instructions advance emulated
   hardware and pending guest interrupts may be delivered;
2. clock-neutral execution, where a native owner advances hardware at an
   external presentation boundary; and
3. hardware-clocked, guest-interrupt-suppressed execution, for a synchronous
   native-owned device wait whose CPU and hardware must progress without
   entering an exception/callback dispatcher that the native owner has not
   installed.

The SF2 owner deliberately does not use its available semantic `VSync(-1)`
counter for scheduling. During one host-boundary search it records PCs at
exhausted instruction quanta; recurrence of the same PC proves cyclic guest
execution and advances all devices by retired guest cycles. This recognizes
neither the recurring address's identity nor an API, return address, overlay,
MMIO register, mission state, or display event. The generally reusable lesson
is the separation of clock-neutral execution from hardware-clocked execution;
the bounded recurrence detector remains an owner policy until another upstream
consumer demonstrates the same need.

### Evidence in this branch

- Focused R3000 runtime tests independently verify clock-neutral execution and
  hardware-clocked execution without guest IRQ delivery.
- The SF1 retail VM gate still completes its math cases and `SUBWAY.OVL`
  bootstrap.
- The SF2 raw-CD synchronization probe crosses its synchronous wait without a
  PC- or operation-specific scheduler rule and returns normally.
- All 21 SF2 missions pass one consolidated 3,000-frame quick-state and combat
  matrix with exact device/audio time. Ordinary combat crosses authentic
  retail checkpoint restores; checkpoint-free parachute openings reconstruct
  the mission after an early death.

### Upstream status

Current `upstream/main` has clock-neutral execution but no known upstream
consumer requiring the third contract. Do not submit this as speculative
infrastructure. When an upstream synchronous wait or callback-ownership fault
reproduces, extract only the generic changes in:

- `include/sf/game/legacy_gameplay_vm.hpp`;
- `src/game/legacy_gameplay_vm.cpp`; and
- the isolated cases in `tests/r3000_runtime_tests.cpp`.

Validate that focused patch against upstream's current test suite and describe
the upstream reproduction without referring to SF2 executable addresses.

## Companion-lab generic candidates

The recompilation lab has four further framework-level corrections on its
PolyForm Noncommercial branch. They are recorded here as coordination leads,
not code to transplant into this MIT repository:

- `61d3667`: honor the encoded link register for dirty-interpreter `JALR`,
  including linkless `rd=0`;
- `dc873fc`: permit IRQ delivery inside native call units while deferring only
  a requested cooperative thread switch;
- `09be64b`: make OpenGL CPU/FBO VRAM ownership coherent across depth-24 movie
  uploads and fills; and
- `485b79b`: make `SeekL`/`SeekP` cancel an active `ReadN`/`ReadS` stream and
  pending data-ready ownership before entering seek.

The lab's clean deterministic checkpoint is `7613d6f`, with 43/43 registered
framework tests. Each candidate needs an independently reviewed minimal diff,
title-neutral regression, provenance audit, and upstream authorization. The
hybrid runtime supplies behavioral controls but should not duplicate fixes
whose failure mode does not exist in its execution architecture.

## Not upstream candidates

The following changes are intentionally project-local:

- SF2 initial checkpoint timing and object-event/camera handoff policy;
- SF2 auxiliary-primitive widescreen ownership;
- SF2 presentation digest and 21-mission validation matrix; and
- any executable, overlay, mission, archive, dialogue, or asset mapping.

Their general lessons belong in the external porting playbook, but their code
contracts are defined by this game and should not be presented as generic
hardware or upstream SF1 fixes.
