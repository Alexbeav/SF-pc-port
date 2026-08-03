# SF2 cross-project handoff package — 2026-08-03

This package records the state of the MIT-licensed `sf-pc-port` SF2 hybrid
runtime at the end of the post-Alpha-10 hardening and briefing-differential
pass. It provides a clean exchange boundary for the independent PSXRecomp
feasibility lab and a parking checkpoint for the next session.

The intended readers are the maintainers of both projects and future PS1
bring-up efforts. This is not a release announcement. It distinguishes:

- automated evidence from interactive observations;
- retail-compatible porting from modern presentation work;
- generic framework findings from SF2-specific facts; and
- locally verified facts from reports supplied by the companion project.

## Read in this order

1. [Hybrid runtime status](HYBRID_STATUS.md) — what works, what was proven,
   what is currently modified, and what remains before Alpha 11.
2. [Cross-project exchange](CROSS_PROJECT_EXCHANGE.md) — a direct comparison
   with the recompilation lab, useful facts to exchange, and the next joint
   measurements.
3. [Bring-up retrospective](../../SF2_BRINGUP_RETROSPECTIVE.md) — what this
   project caught, missed, and nearly diagnosed correctly.
4. [Consecutive campaign gate](../../SF2_CONSECUTIVE_PLAYTEST.md) — the human
   Missions 8–21 acceptance record that still must be completed.
5. [Alpha 11 candidate notes](../../releases/0.1.0-sf2-guest-alpha.11.md) —
   proposed public-build changes and installation notes.
6. [Runtime architecture](../../GAME_RUNTIME_ARCHITECTURE.md) and the
   [chronological SF2 devlog](../../devlogs/2026-07-28-sf2-bring-up.md) — the
   deeper implementation record.

## Executive assessment

The hybrid runtime is in late single-player hardening, not initial bring-up.
All 21 mission packages reach retail gameplay; campaign/FMVs/saves, both-disc
selection, retail HUD/UI, keyboard/mouse, controller input, high-resolution
widescreen projection, and in-session quick states exist. Missions 1–7 have
substantial interactive coverage, and individual routes beyond them have also
been exercised. The current exact binary has passed all 63 automated mission
routes—21 completion flows, 21 exact quick-state routes, and 21 combat routes—
when the interrupted run and its validated tail are taken together.

It is not yet an end-to-end release claim. Save and Quit now reaches the
centered slot UI, saves, returns to title, and reloads successfully; Mission 6
NVG is also confirmed wide and correct. Restart Mission still needs an exact
route confirmation, and Missions 8–21 plus the ending/credits need a
consecutive human playthrough. The native title shell also omits the retail
`New Game -> One Player / Two Players` hierarchy, the retail difficulty
selector, and two-player arenas.

The briefing is deliberately parked as unresolved. Text, reveal order, grid,
gauge, and frame geometry can be reconstructed, but the authentic animated
mottled surface is not. Exact packet analysis disproved the earlier Gouraud-
surface hypothesis, and both direct-16 and corrected indexed-texture upload
experiments failed to reproduce retail. The current native fallback is not an
accepted fidelity result. The recommended destination is to execute retail
frontend state 8 (`INIT.OVL`) and present its GPU packets through the existing
retail-owned two-page path, just as the accurate pause menu already does.

The recompilation lab has independently demonstrated two deterministic
Mission 1 routes, authentic retail memory-card save/load, the complete retail
menu hierarchy, accurate PS1 presentation, and the authentic 989 -> Eidetic ->
legal -> `ZINTRO` -> TITLE sequence with fewer game-specific lifecycle bridges.
Its generic OpenGL 24-bit VRAM and CD seek/read ownership defects are now fixed.
It has not yet supplied equivalent Mission 3, all-mission, campaign, modern
input, or modern presentation evidence. It also remains a PolyForm
Noncommercial experiment, so its code cannot simply be merged into this MIT
project.

The current lab return is checkpointed at clean commit `7613d6f`; its
registered framework suite passes 43/43. This package records that identity in
the cross-project exchange rather than copying the sibling documentation.

The two projects should remain independent implementations and become
differential oracles for one another. Share reproducible facts, minimal generic
tests, trace schemas, and upstreamable framework corrections—not generated
retail code, captured overlays, memory cards, disc data, or license-incompatible
source.

## Parking note

The local tree remains intentionally uncommitted and contains broader Alpha 11
work. The briefing `prepare()`/texture experiments are diagnostic scaffolding,
not a completed feature. Preserve them only long enough to inform the retail
state-8 handoff; do not publish their current flat fallback as authentic retail
presentation.
