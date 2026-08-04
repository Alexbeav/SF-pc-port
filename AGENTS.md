# Agent instructions — SF3-PC-Port

Authority and validation rules live in `docs/SF3_PC_PORT.md` (governing gates,
milestone definitions) and `CONTRIBUTING.md`. Read both before changing code.
Non-negotiables from those docs:

- Probe-first workflow: milestone claims require deterministic probe evidence
  (`sf_tool probe-*`, byte-identical stdout across two independent runs,
  SHA-256 transcript equality), not narrative. "Stable gameplay" in a commit
  subject means probe-stable; human validation is a separate, recorded step.
- No retail data in git: no retail executable, disc sector, or extracted asset.
  Handoff zips stay in ignored `build/`; their doc set is tracked under
  `docs/handoffs/`.
- SF3 has NO mouselook hook yet (milestone S4). The dual-path donors are SF1
  (`0x80037B08`) and SF2 (`0x80053464`, `src/game/sf2_runtime.cpp`); do not
  represent SF3 as a mouselook donor.
- Known gaps to close (2026-08-04 sweep): register the SF3 product-runtime
  replay as a rom-gated CTest (mirror the SF1 pattern in
  `cmake/SfRomProbes.cmake`); add the explicit "mouselook disabled => zero
  guest RAM writes" negative test; import the human-test texture-corruption
  result from `I:\Projects\PSX-Ports\_knowledge\projects\sf3.md` into this
  repo's docs.

## Mandatory corpus check (added 2026-08-05)

Before starting work, and again before diagnosing any new failure:

1. Read this project's section in
   `I:\Projects\PSX-Ports\_knowledge\reviews\2026-08-04-portfolio-sweep.md`
   and apply its action list. Rebut findings only with artifact evidence
   (mtimes, build strings, phys ranges); do not re-assert claims it disproves.
2. Search the shared corpus for your symptom BEFORE touching seeds, configs,
   or diagnostics:
   - `I:\Projects\PSX-Ports\_shared\FINDINGS_REGISTRY.md` (PSX-BUILD-001
     revised + PSX-BIOS-002 extended 2026-08-04 -- reread even if familiar)
   - `I:\Projects\PSX-Ports\_knowledge\FINDING_CANDIDATES.md`
   - `I:\Projects\PSX-Ports\_knowledge\failures\FAILURE_CATALOG.md`
   - `I:\Projects\PSX-Ports\_knowledge\regressions\REGRESSION_LEDGER.md`
3. Return new lessons as candidate rows in `FINDING_CANDIDATES.md`, not only
   the local devlog. Evidence rule: match every cited dump's
   frame_count/epoch to the symptom stage and check end-of-run heartbeat/exit
   state before using it -- a spin_freeze dump is a snapshot, not a terminal
   event.
