---
name: autocraft-testing
description: Running Autocraft's tests (Unreal Engine 5 game in unreal/) — which run a change needs, how long it takes, how to read the result. Use when verifying a change to the C++ core or the UE game module, running core tests or UE automation tests, running a flow test, debugging a failing test, after changing maps or pathing, or when writing a test.
---

# Autocraft tests

Three layers, cheapest first. Pick the run from the table, run it, read it.

1. **Core tests**: `unreal/core-tests`, plain clang++, no Unreal. They cover `unreal/Source/AutocraftCore` (rules, sim, commander, nav, terrain, leveling, vision, sessions).
2. **UE automation tests**: `unreal/Source/Autocraft/*Tests.cpp`, names `Autocraft.<Area>.<Case>`. They cover the game module (HUD, picking, cards, audio rules, rays, leveling UI).
3. **Flow tests**: `-AcFlowTest=STEPS` runs the real menu, save and new-game flow in a game process.

Every Unreal process runs hidden, silent and focus-free: see [Hidden runs](#hidden-runs). Never relaunch the editor or game the user has open.

## Pick the run

| Changed | Run | Takes |
|---|---|---|
| `Source/AutocraftCore` (rules, sim, nav, terrain, leveling, vision, session) | core tests | 4 s with the build, 0.03 s per filtered test once built |
| Commander or AI | core tests, filter `commander` / `Commander` first, then all | seconds |
| Maps or pathing (`WindowMaps`, `MapDefinition`, terrain, nav) | core tests with `AUTOCRAFT_MAP_TESTS=1` | 9 s |
| `Source/Autocraft` code with an automation test (HUD, cards, pick, leveling UI, audio rules, rays, music) | build, then that area's automation tests | build time plus an editor start |
| Menu, pause, save, new game, import | build, then the flow test | build time plus a game start |
| Rendering or look | `uv run unreal/Tools/shots/compare.py SCENE` (see `unreal/SHOTS.md`) | one scene a few minutes; all scenes about 25 min |
| Audio | offline to WAV with `-deterministicaudio`, then check the file; the rule tests are `Autocraft.Audio.*` | – |
| Only assets, materials or Python tools | no test; look at a render | – |

Perf (`unreal/Tools/bench/bench.py`, `make -C unreal/core-tests bench` then `simbench`) and `unreal/Tools/shots/compare.py` over every scene run only when the user asks.

## Core tests

```sh
make -C unreal/core-tests                      # builds what changed, runs all tests
make -C unreal/core-tests FILTER=noise         # only tests whose name contains "noise"
AUTOCRAFT_MAP_TESTS=1 make -C unreal/core-tests   # adds the slow map tests
```

- Done when the last line reads `N tests, … 0 failures in 0 tests: PASS` (the `in M tests` count is the failing tests) and the exit code is 0.
- A failure prints `FAIL file.cpp:line: expected …` under the test's name. `EXPECT_EQ` shows both values with doubles at 17 digits and compares doubles exactly; `ASSERT_TRUE` leaves the test, `EXPECT_*` carries on.
- The filter is a substring of the test name, and `main.cpp` takes only that one argument. Test names are the `TEST(name)` identifiers in `unreal/core-tests/tests/*.cpp`; the run prints each name before its failures.
- The build uses `-Werror` and `-O2`, so a warning fails it, and `assert()` is not a check: write the invariant as an `EXPECT_*`.
- Builds go to `unreal/core-tests/build/`. A stale object after an odd header edit: `make -C unreal/core-tests clean`, then run again.
- The slow tests are `WindowMode_basesAreReachableAndCountGrowsWithSize` (all 16 window maps) and `teams_squareMapsAreFairAndEveryBaseReachable` (every square map: small only without the variable). Without the variable the first prints `skipped` and passes.

### Goldens and fixtures

- `unreal/core-tests/golden/*.json` came from the Swift game. The C++ core is now its own reference, and nothing in `core-tests` regenerates a golden: they are frozen files. A golden test that fails after a rule change means the behaviour moved. Decide whether the move is intended; if so, edit the golden by hand to the new value, review the diff line by line, and say in the commit which numbers moved and why. A golden is never edited to make a red test green without that review.
- `golden::same` compares numbers exactly. The report names the JSON path of the first difference (`where`).
- Fixtures are session JSON files: `unreal/core-tests/fixtures/*.json` and `bench/badlands-large.json` at the repo root. `session_fixtures_round_trip` loads every one of them.

## UE automation tests

Build first, with the command sessions use (the module's own build, not the editor app):

```sh
cd "$(git rev-parse --show-toplevel)"
"/Users/Shared/Epic Games/UE_5.8/Engine/Build/BatchFiles/Mac/Build.sh" AutocraftEditor Mac Development \
  -project="$PWD/unreal/Autocraft.uproject" -waitmutex 2>&1 | grep -E "error|Result" | head -20
```

- Done when `Result: Succeeded` shows. `-waitmutex` queues behind another session's build. A build can fail on a busy machine or miss an edit made seconds earlier: `touch` the file and retry up to three times before treating it as broken.
- The editor the user has open does not load the new C++ until they restart it. That is their call; the hidden runs below load it fresh.

Run one area (the prefix is the name from `IMPLEMENT_SIMPLE_AUTOMATION_TEST`: `Autocraft.Pick`, `Autocraft.Leveling`, `Autocraft.Card`, `Autocraft.Audio`, `Autocraft.Hud`, `Autocraft.Rays`, `Autocraft.Space`, …; `Autocraft` runs them all):

```sh
BIN="/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditorBG.app/Contents/MacOS/UnrealEditor"
"$BIN" "$PWD/unreal/Autocraft.uproject" -nullrhi -unattended -nosplash -nosound \
  "-ini:Input:[/Script/Engine.InputSettings]:bCaptureMouseOnLaunch=False" -abslog=LOGPATH \
  -ExecCmds="Automation RunTests Autocraft.Pick; Quit" -TestExit="Automation Test Queue Empty" >/dev/null 2>&1
grep -E "Test Completed|Error:|Fail" LOGPATH
```

- Put `LOGPATH` in the scratchpad directory. Each test file's header repeats this command for its own prefix.
- Done when every `Test Completed` line reads `Passed` and no `Error:` line names a test. A failing expectation logs its message and file line under the test's name.
- An editor start dominates the time (not measured here); start it with `run_in_background` and a `timeout`. A run that writes no log lines at all means the module failed to load: rebuild.
- Automation tests run in the editor process without a world. A scenario needing a running game belongs in a flow test.

## Flow tests

`-AcFlowTest=STEPS` (`unreal/Source/Autocraft/AcGameFlow.cpp`) steps through the real menus and saves, then quits. Steps: `save`, `resume`, `import`, `autosave`, `fog`, `newgame`, `victory`, `menu`.

```sh
S=SCRATCHPAD/flow; mkdir -p $S
"$BIN" "$PWD/unreal/Autocraft.uproject" /Game/Maps/Battlefield -game -RenderOffscreen -windowed -ForceRes \
  -ResX=1280 -ResY=720 -unattended -nosplash -nosound \
  "-ini:Input:[/Script/Engine.InputSettings]:bCaptureMouseOnLaunch=False" -abslog=$S/flow.log \
  -AcSaveDir=$S/flow -AcMap=highlands-small -AcFlowTest=save,menu,newgame >/dev/null 2>&1
grep -E "flowtest|menu:" $S/flow.log
```

- Done when each step logs `flowtest: STEP: … ok` and the last line reads `flowtest: done, 0 failed`. `FAILED` marks a step.
- `-AcSaveDir=` keeps the run out of the user's real saves; the save checks need no `-AcNoSave`. `-AcImport=PATH` feeds the `import` step a session file.
- Staging flags for a look at one state (`-AcScene=NAME`, `-AcWarm=S`, `-AcPaused`, `-AcStage=N`) are in `unreal/SHOTS.md` and `unreal/GAME-LAYER.md`.

## Hidden runs

- Binary: `UnrealEditorBG.app` only (above): no Dock icon. If it is missing after an engine update, remake it as `unreal/Tools/README.md` ("Hidden test runs") says.
- Flags on every run: `-RenderOffscreen` (or `-nullrhi` when nothing is drawn), `-unattended -nosplash -nosound`, and the `bCaptureMouseOnLaunch=False` ini override.
- Log with `-abslog=PATH`; `-log` opens a console window.
- Kill only a process you started, by its pid.

## Writing a test

Each test is an isolated scenario: set up the state, step only as far as the behaviour needs, assert. A test where the AIs play a game, or several that add up to one, does not belong here; ask the user for a `simbench` or `bench.py` run instead.

- Rule or sim behaviour: a `TEST(name)` in `unreal/core-tests/tests/`, built from the helpers there (`SimHelpers.h`, `SquadHelpers.h`, `VisionHelpers.h`, `LevelingScene.h`).
- Game-module logic: `IMPLEMENT_SIMPLE_AUTOMATION_TEST(..., "Autocraft.Area.Case", Flags)` in `unreal/Source/Autocraft/Ac<Area>Tests.cpp`, with the headless command in the file header.
- A core test that takes more than a second runs only on request: gate it on `AUTOCRAFT_MAP_TESTS` as the map tests do, and add its row to the table above.
