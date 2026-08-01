# SF2 deterministic input replays

## `mission3-truck-route.sf2pad`

- Game: Syphon Filter 2 USA, Disc 1
- Mission: 3, Colorado Interstate 70 (`HWAY`, zero-based resource index 2)
- Guest-update samples: 5,500 (about 92 seconds)
- SHA-256: `2DCEB537B5B578B55CF5CB363F2BA22D0327C17A7D391ED4A1075D42BB6BF1F7`
- Recorded on 2026-07-31 from a human-driven run with diagnostic health
  pinning enabled.

The trace reaches the truck, acquires the M-16 and bulk equipment grant, and
reproduces the intermittent black-world flicker and duplicated UI reported by
the playtester. The initial sparse-frame composition experiment did not remove
all user-visible flicker or duplicate UI and must remain classified as
unverified/failing until a complete replay comparison passes.

Replay from the repository root in PowerShell:

```powershell
$env:SF2_REPLAY_INPUT = (Resolve-Path ".\tests\data\sf2\mission3-truck-route.sf2pad").Path
$env:SF2_PIN_HEALTH = "1"
.\build\windows-psycross\Release\syphon_filter.exe `
  --no-launcher --mission=3 --scene-test `
  "<path-to-syphon-filter-2-disc-1.cue>"
```

The same transcript can be run without opening a product window. This path
retains the runtime's audio, XA, dialogue, collision, renderer, and script
diagnostics while omitting host presentation and audio-device playback:

```powershell
.\build\windows-psycross\Release\sf_tool.exe `
  probe-sf2-product-runtime `
  "<path-to-syphon-filter-2-disc-1.cue>" `
  5500 replay 2 `
  ".\tests\data\sf2\mission3-truck-route.sf2pad"
```

The replay file contains only synthesized PS1 pad samples. It does not contain
disc data, executable code, screenshots, audio, or other retail assets.
