# SF1 Phase 1 launcher-ready baseline

This branch standardizes the selected SF1 native source base
`c24ce313b1356da2e3d5615f3001f6000e399f99` without removing its existing
features.

The faithful first-run profile is 640x480, native 4:3, 20 Hz presentation,
vertical synchronization on, and MSAA, bilinear filtering, anisotropic
filtering, PGXP geometry, fullscreen, and chase-camera mouse-look off.
Controller input remains available with the retail action layout. Keyboard and
mouse bindings remain remappable. Optional features can be enabled after the
baseline launch.

This native runtime does not execute a PlayStation BIOS. It requires no BIOS,
packages no BIOS, and has no HLE/LLE BIOS selection. It also has no fast-boot
path: the runtime validates the exact `SCUS-94240` v1.1 retail executable before
starting title code. A missing or mismatched executable fails closed.

Use `tools/build_phase1_baseline.ps1` with a preverified vcpkg dependency
closure. The script disables vcpkg manifest installation, so configuration
cannot fetch or replace dependencies. Use
`tools/write_phase1_build_receipt.ps1` to bind the exact input, source,
toolchain, dependency closure, CMake configuration, and executable identities.
Then use `tools/package_windows_release.ps1` with that receipt. The package
contains `BASELINE_IDENTITY.txt`, which the integrated launcher displays through
the **BASELINE IDENTITY / POLICY** button.

This branch supports only a launcher-ready baseline claim. It makes no campaign,
mission-correctness, enhancement, full-game, or release-readiness claim.
