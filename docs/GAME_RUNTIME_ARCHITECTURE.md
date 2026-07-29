# Game Runtime Architecture

## Decision

Syphon Filter 1, 2, and 3 are separate gameplay runtimes built on one shared
technical foundation. Similar disc formats or sequel data structures do not
establish identical gameplay behavior.

```text
Shared foundation
  assets / disc / PSX / rendering / audio / input / math
                  |
        explicit runtime selection
          /          |          \
       SF1          SF2          SF3
```

The repository remains unified so proven low-level fixes do not drift between
games. Game behavior must nevertheless have an explicit owner.

## Current boundary

`GameRuntimeProfile` is the selection boundary. The three profile
implementations live in:

- `src/game/sf1_runtime.cpp`
- `src/game/sf2_runtime.cpp`
- `src/game/sf3_runtime.cpp`

The initial split owns:

- EMD and HMD decode strides;
- player collision dimensions;
- first-person camera height;
- legacy guest-runtime selection;
- native mission-item and interaction opt-in;
- HUD-atlas identity and relocation ownership;
- SF1 environment-atlas ownership.

SF2 item translation and AIRBASE delayed-actor policy are owned by
`sf2_runtime`. SF3 deliberately does not inherit that translation or SF2's
native mission scaffolding.

## Dependency rules

1. Shared code must not infer compatibility from `game != SF1`.
2. A feature shared by two games still requires two explicit profile opt-ins.
3. Mission-specific source indices and retail item IDs belong to that game's
   runtime module.
4. SF1 behavior is frozen behind regression tests while SF2 is brought to
   parity.
5. SF3 receives only decoding and presentation support required for verified
   bootability until its own runtime work begins.
6. Code moves into the shared foundation only after identical behavior is
   demonstrated in at least two games.

## Next extraction slices

1. Move SF2 mission-start loadouts, objectives, timers, and source-index
   actions out of `GameplaySession` into an `Sf2MissionRuntime`.
2. Split combat policy: hit zones, headshots, damage, hit markers, reactions,
   drops, and ammunition translation.
3. Split actor-class interpretation and AI scheduling.
4. Split camera and player-action policy beyond the initial numeric profile.
5. Give each game a presentation profile for HUD composition, scope behavior,
   feedback markers, and mission overlays.
6. Replace remaining direct game-ID branches with explicit runtime calls or
   genuinely shared primitives.

The immediate product milestone remains a complete SF2 Mission 3 combat
vertical slice. Architecture work should serve that milestone rather than
delay it.
