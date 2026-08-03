# SF2 consecutive campaign playtest

This is the manual release gate for Missions 8–21. Deterministic probes cover
machine state, timing, rendering packets, quick-state replay, combat stress and
retail checkpoint restoration; they do not prove that a human can complete
every objective or that authored choreography is perceptually correct.

## Test setup

- Build/commit:
- Disc revision: NTSC-U Disc 1 and Disc 2
- Resolution/aspect:
- Filtering/MSAA:
- GPU/driver:
- Input device:
- Starting save/mission:

Start from Mission 8 and continue through retail campaign flow. Do not use a
new `--mission` override after each completion unless documenting a recovery
from a blocker. Keep the generated log for any failure.

## Checks for every mission

Record pass/fail and a short note for each item:

1. opening FMV plays once and matches the mission;
2. briefing frame, text, gauge and continue prompt are composed correctly;
3. in-engine opening runs once and hands control over normally;
4. required actors, objectives, failure conditions and special mechanics work;
5. dialogue, music, weapon sounds and positional effects remain synchronized;
6. HUD, radar, target/danger bars, messages and mission-specific optics work;
7. one death/checkpoint restart restores gameplay and synchronized audio;
8. one F5/F9 cycle restores the same scene without doubled UI, stale pages or
   missing textures;
9. mission completion plays the ending FMV, save flow and next opening FMV in
   retail order; and
10. the next mission's inventory, health, difficulty and character ownership
    are correct.

## Campaign record

| Mission | Name | Complete | Restart | F5/F9 | Audio/UI | Flow | Notes |
|---:|---|:---:|:---:|:---:|:---:|:---:|---|
| 8 | C-130 Wreck Site |  |  |  |  |  | Parachute opening; Disc 1 ending |
| 9 | Pharcom Expo Center |  |  |  |  |  | Disc 1→2 seam and opening FMV |
| 10 | Morgan |  |  |  |  |  | Character/loadout ownership |
| 11 | Moscow Club 32 |  |  |  |  |  | Club music and combat dialogue |
| 12 | Moscow Streets |  |  |  |  |  | Outdoor streaming and radar |
| 13 | Volkov Park |  |  |  |  |  | Briefing→direct Lian control; no opening cinematic |
| 14 | Gregorov |  |  |  |  |  | Boss/special objective flow |
| 15 | Aljir Prison Break-in |  |  |  |  |  | Stealth/failure conditions |
| 16 | Aljir Prison Escape |  |  |  |  |  | Connected prison transition |
| 17 | Agency Bio-Lab |  |  |  |  |  | Mission-specific equipment/optics |
| 18 | Agency Bio-Lab Escape |  |  |  |  |  | Audio and checkpoint stress |
| 19 | New York Slums |  |  |  |  |  | Outdoor streaming and effects |
| 20 | New York Sewer |  |  |  |  |  | Room/collision transitions |
| 21 | Finale |  |  |  |  |  | Final boss, ending and credits |

## Cross-campaign acceptance

- Disc 1→Disc 2 selection occurs without a manual path change.
- Save slots show the correct SF2 mission names and reload the same mission.
- No inventory crosses between Gabe and Lian unless retail authors it.
- Normal difficulty remains stable across saves and transitions. Alpha 11 does
  not yet expose retail's difficulty selector, so Hard is outside this gate.
- The finale reaches the authored ending/credits instead of returning through
  an ordinary next-mission path.
- No unsupported STR error, FIFO-bound abort, stack overflow, renderer fault or
  retail `LEVEL` restart occurs.

The public build is not an end-to-end campaign candidate until every row has a
human result. A direct mission smoke test is useful diagnostics but does not
replace this consecutive-flow gate.
