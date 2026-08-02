# Reproduction guide

## Requirements

- Windows x64 with Visual Studio/CMake prerequisites used by the repository.
- The `windows-psycross` CMake preset and its dependencies.
- A user-owned *Syphon Filter 3* USA cue/bin image.
- Disc executable SHA-256 exactly
  `b4b32cc92e6b8634762893b637bc9a471442edbeb7569afcfb18eafbe82b9460`.

The examples use a local cue path. Do not copy disc content into the worktree.

## Build and tests

```powershell
cd I:\Projects\SF3-PC-Port
cmake --preset windows-psycross-local
cmake --build --preset windows-psycross-local-release
ctest --preset windows-psycross-local-release --output-on-failure
```

For the existing configured tree:

```powershell
cmake --build build\windows-psycross --config Release `
  --target syphon_filter sf_tool --parallel
ctest --test-dir build\windows-psycross -C Release --output-on-failure
```

Expected configured suite at this checkpoint: 21/21.

## Disc validation

```powershell
$sf3 = "Z:\Emulators\PS1 Games\Syphon Filter 3 (USA).cue"
$tool = ".\build\windows-psycross\Release\sf_tool.exe"

& $tool inspect $sf3
& $tool inspect-mission-archive $sf3 TOKYO
```

Reject the image if serial, executable name, revision or executable hash does
not match the supported profile.

## Named bootstrap probe

```powershell
& $tool probe-sf3-guest-bootstrap $sf3 5000000
```

The default regression must reach `Game_Main`, the application loop and TITLE
state `4`, depth `2`, with SPU-DMA completion and retail GPU submissions. The
historical two-process stdout SHA-256 at the stable TITLE checkpoint is:

```text
961714DD79B26CCB72E7B0D1E7F238A16CF8902ADCDDDEA11598ADF79E93CA6C
```

## Title/Mission transition probe

```powershell
& $tool probe-sf3-title-shell $sf3 500000000
```

This forensic probe emits title modes/substates, card/CD operations, file
requests, archive members, state pushes/pops, PAD calls, movie handoffs, GPU
submissions and XA admission. Its success gate depends on the budget and
current milestone; inspect the final state and requests rather than relying on
a truncated excerpt.

Stable historical signatures:

```text
Mission 1 request boundary:
8B67C9B4D8A050A5A53F80B61055ACEA58019667622BD0404CDCAB8E722E6A8F

TOKYO member bring-up:
422814CF91CCA5C6B3B294FC903CD6A8BCD361F9754677BBC808449D860E243C

Stable gameplay entry:
89828E78A5CDE5CED232DA820A2B7C6DDD1D30D359A820A3A040B4DE235303A7
```

If output format changes, establish a new signature only after comparing the
semantic diagnostics rather than accepting a hash change by itself.

## Reusable product-runtime probe

This is the recommended fast gate and does not create a window or audio
device. PCM is drained into memory.

```powershell
& $tool probe-sf3-product-runtime $sf3 300 neutral
& $tool probe-sf3-product-runtime $sf3 600 forward
```

Expected neutral result:

```text
SF3 product runtime: mode=neutral requested=300 state=0/1 guest-frames=301 input=301 gpu=4408 presentation=4167 xa=2/2 pcm=208203 final=0/0x801859A0/882/869
```

Expected forward result:

```text
SF3 product runtime: mode=forward requested=600 state=0/1 guest-frames=601 input=601 gpu=5308 presentation=4908 xa=4/4 pcm=424805 final=0/0x801859A0/651/640
```

Run the neutral command in two clean processes and require identical complete
stdout, not merely the same final state.

## Product alpha

```powershell
$exe = ".\build\windows-psycross\Release\syphon_filter.exe"
& $exe --no-launcher --mission=1 --scene-test $sf3
```

Expected behavior:

1. Product constructs the Mission 1 package for texture residency.
2. The retail guest auto-traverses title, card prompts and loading.
3. Hotel Fukushima state-0 gameplay is presented.
4. Configured PAD controls affect the retail player.
5. `P` sends retail Start and should enter state-7 pause.
6. Escape returns to the host title.

Capture the log and a screenshot if any presentation, input, audio or pause
issue occurs. Report elapsed time, visible frame, input device, and whether
audio continued; those details distinguish guest stalls from backend faults.

## Quiet/headless policy used for this checkpoint

All automated probes were command-line, headless and silent. The product was
not launched visibly because the user was studying. `SDL_VIDEODRIVER=offscreen`
with dummy audio remained alive but stalled before PsyCross/OpenGL guest scene
initialization; it is not a valid product-presentation test on this Windows
backend. That isolated process was stopped and no diagnostic process remained.

## Clean-boundary checks

Before sharing a commit or documentation package:

```powershell
git status --short
git ls-files | Select-String -Pattern '\.(bin|cue|iso|img|ram|sav)$'
git diff --check
```

No proprietary file should be tracked. Disc paths may appear in local command
examples, but disc bytes, extracted members and runtime dumps must not.
