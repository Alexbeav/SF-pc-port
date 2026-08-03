# Release process

This checklist prevents local game data, saves, cheat markers and developer
artifacts from entering a public build.

## 1. Prepare the source tree

- Update `CHANGELOG.md`, the package-script default version and Windows resource
  metadata.
- Confirm `git status` contains only intended source/documentation changes.
- Confirm no BIN/CUE, save, log, dump, object, PDB or credential is staged.
- Confirm no character artwork, dossier pages or generated localization assets
  are present.
- Commit the validated source and documentation before packaging. The package
  script refuses tracked working-tree changes so `BUILD_INFO.txt` always names
  the revision which actually produced the archive.

## 2. Build and validate

```powershell
cmake --preset windows-psycross
cmake --build --preset windows-psycross-release
ctest --preset windows-psycross-release
git diff --check
```

ROM probes are optional and require an explicitly configured legal image. Do not
run the interactive game as part of automated packaging.

For an SF2 guest alpha, the legal two-disc release gate is required before the
human checklist. It runs both MENU lifecycle callbacks, all selected
completion shells (including authored destination-state rejection), and the
quick-state/combat survival matrix:

```powershell
.\tools\run_sf2_validation_matrix.ps1 `
  -Disc1Cue "D:\PS1\Syphon Filter 2 (USA) (Disc 1).cue" `
  -Disc2Cue "D:\PS1\Syphon Filter 2 (USA) (Disc 2).cue"
```

`-SkipCompletionFlow` exists only for focused development reruns; do not use it
for release evidence.

## 3. Package

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File tools/package_windows_release.ps1 `
  -Version 0.1.0-sf2-guest-alpha.11 `
  -Configuration Release `
  -Sf2GuestAlpha
```

Expected outputs:

```text
dist/SyphonFilterPC-0.1.0-sf2-guest-alpha.11-win64/
dist/SyphonFilterPC-0.1.0-sf2-guest-alpha.11-win64.zip
dist/SyphonFilterPC-0.1.0-sf2-guest-alpha.11-win64.zip.sha256
```

The script fails instead of overwriting any existing output.

## 4. Audit the archive

- Verify the archive checksum and the internal `SHA256SUMS.txt`.
- Verify the packaged executable hash matches the built executable.
- Verify the archive contains no generated localization pack, character
  artwork, dossier screens or data extracted from a game image.
- Verify the archive contains no `syphon_filter_cheats`, save, BIN, CUE, CMD,
  log, dump, PDB, LIB or EXP file.
- Test only by manually extracting to a clean folder; never execute from `dist`.

## 5. Publish

- Create an annotated tag named `v<version>`.
- Push the default branch and tag.
- Create a GitHub Release from the tag.
- Upload both the ZIP and `.zip.sha256` sidecar.
- Put the supported disc revision, legal image requirement, archive SHA-256 and
  major changes in the release notes.

Release archives are intentionally not committed to Git history; GitHub Releases
is the canonical binary distribution channel.
