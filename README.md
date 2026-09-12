# Syphon Filter PC — Mouse-Look Fork

An independent, unofficial Windows PC runtime for the NTSC-U v1.1 release of
*Syphon Filter* (`SCUS-94240`), focused on modern mouse and keyboard controls.

This project is based on
[Madxbio97/SF-pc-port](https://github.com/Madxbio97/SF-pc-port) at commit
`f5f10ad0c7aa76a599ec723fbed8a0e5804e5651`. That project supplied the completed
campaign integration, native renderer, PGXP geometry, high-refresh
presentation, audio, frontend and FFmpeg-backed STR movie player that make this
work possible. See [UPSTREAM.md](UPSTREAM.md) for provenance and synchronization
policy.

The fork is not affiliated with or endorsed by Madxbio97, Sony Interactive
Entertainment, Bend Studio or any other rights holder.

## Status

The current branch is the sanitized upstream baseline. It intentionally has no
gameplay changes yet. The first feature series will add the control work proven
in the archived
[Syphon Filter Redux](https://github.com/Alexbeav/syphon_filter_redux)
prototype:

- focus-safe SDL relative-mouse input;
- configurable horizontal and vertical sensitivity;
- normal-play third-person horizontal and vertical free-look;
- mouse control in manual-aim and first-person views;
- movement and strafing while aiming;
- clean camera ownership around scripts, menus, FMVs and focus changes; and
- controller behaviour that remains available and unchanged.

Once the first game passes a complete campaign validation with these controls,
the same native platform and input architecture will be evaluated for
*Syphon Filter 2* and then *Syphon Filter 3*.

## Inherited runtime

The upstream foundation already provides:

- the complete 20-mission campaign, checkpoints and saves;
- native Windows rendering and platform integration;
- arbitrary internal resolution, MSAA and texture filtering;
- PGXP-backed, perspective-correct, Z-buffered geometry;
- original 4:3 and adaptive widescreen modes;
- original-rate through high-refresh presentation;
- native OpenAL audio and FFmpeg-backed PS1 STR playback;
- clean FMV skipping;
- remappable keyboard/mouse and gamepad bindings; and
- a launcher for selecting the user-owned game image.

## Required game data

This repository does not contain a PlayStation BIOS, disc image, original
executable, music, speech, FMV, models, textures, translated game assets or
character artwork.

Running the game requires a legally obtained BIN/CUE image of:

| Field | Required value |
| --- | --- |
| Region | USA / NTSC-U |
| Serial | `SCUS-94240` |
| Revision | v1.1 |
| Format | BIN/CUE |
| PS-X EXE SHA-256 | `bac292061ad5bc718ce137ef5b43d3d7e9b1b65248fb0d52229f328ccfe4ab4e` |

Keep every BIN file beside its CUE file. The launcher remembers the selected
path but does not copy the image into the repository or installation.

## Building on Windows

Requirements:

- Windows 10 or 11 x64;
- Visual Studio with the Desktop development with C++ workload;
- CMake 3.24 or newer;
- Git; and
- vcpkg with `VCPKG_ROOT` configured.

```powershell
git clone https://github.com/Alexbeav/SF-pc-port.git
cd SF-pc-port
cmake --preset windows-psycross
cmake --build --preset windows-psycross-app-release
```

The executable is written to:

```text
build/windows-psycross/Release/syphon_filter.exe
```

Run it, browse to the supported CUE file and choose **DEPLOY**. See
[Building](docs/BUILDING.md), [Controls](docs/CONTROLS.md) and
[Troubleshooting](docs/TROUBLESHOOTING.md) for the inherited detailed
documentation.

## Repository policy

Never commit or distribute:

- BIOS, BIN, CUE, ISO or extracted executable data;
- translated game text or code;
- files extracted from a game image;
- game textures, maps, fonts, audio, video or models;
- character portraits or franchise artwork;
- saves, memory cards, logs or diagnostic captures containing game data; or
- generated localization packs.

Public releases must be reproducible without proprietary or translated game
content.

## License and attribution

Project-owned source from the upstream repository is provided under its
[MIT License](LICENSE). Vendored and dynamically linked dependencies retain
their own licenses; see [THIRD_PARTY.md](THIRD_PARTY.md).

The MIT license applies only to material its contributors had the right to
license. It grants no rights to *Syphon Filter*, PlayStation, game data,
characters, artwork or trademarks.

## Development history

Older source and separate candidates are preserved as fixed tags. See [the archive and recovery instructions](docs/BRANCH_ARCHIVE.md).
