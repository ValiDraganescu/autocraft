# Moving the Swift game out of the repo

Status, 2026-10-05: **done.** The Swift and SceneKit game left this repo. The
archive is `~/git/dev/autocraft-swift`: a copy of the whole repo (minus
`unreal/` and `.build/`), its `.git` and the uncommitted Swift work
included. Work is now only on the Unreal game in `unreal/`.

What was done, beyond the plan below:

- `unreal/` got its first commit before anything moved.
- Sounds and textures moved to `unreal/Resources/`, the fixtures to
  `unreal/core-tests/fixtures/`. The game, the tests and the tools read
  the new paths.
- The HUD and cursor bakes and `import_sounds.py` read copies of the Swift
  files they need, in `swift/` folders beside them.
- `art/`, `bench/`, `docs/`, `Tools/AudioGen` and `Tools/ImageGen` stayed.
- `unreal/Content-src/{cursors,fonts,hud,icons,launcher}` are now tracked.
  `Content-src/models` (430 MB) stays on disk, out of git.
- Both Unreal modules build with `bUseUnity = false`. Unity builds had
  never run: adaptive unity compiled every untracked file alone, and once
  the files were committed, unity blobs exposed clashing file-local names.
- The five Swift-era worktrees were removed (they were clean; their
  branches stay, here and in the archive). The three `autocraft-*` skills
  were rewritten for the Unreal game.

The plan as written before the move:

## Before anything moves

1. **`unreal/` is not in git.** No file under `unreal/` is tracked. Give
   it a first commit before the move, so the port has history.
2. **Uncommitted Swift work.** About 45 Swift files, skills and docs are
   modified and not committed (`git status`). Commit them first, so the
   archive holds them.
3. **Five worktrees** in `~/git/dev/autocraft-worktrees/` (leveling-core,
   leveling-hud, leveling-kinds, tracking-app, tracking-core), plus the
   branch `worktree-agent-a13236ce7fa06eb71`. They are all Swift-era. Merge
   or drop them, then `git worktree remove` each.
4. **The Unreal game still reads Swift files** (below). Fix those first.

## Move (Swift only)

| Path | Size / note |
|---|---|
| `Package.swift`, `Makefile`, `test.sh` | The root Makefile builds only Swift. |
| `Sources/Autocraft/` except `Resources/` | The SceneKit app. |
| `Sources/GameCore/` | Already ported to `unreal/Source/AutocraftCore`. |
| `Tests/GameCoreTests/` | Includes `GoldenDumpTests.swift`, which wrote the Unreal goldens. |
| `Tools/ImageGen/` | Writes Swift textures. |
| `Tools/Audio/` | Only a 118 MB extraction cache. Delete it rather than move it. |
| `art/` | 597 MB of concept art and turnarounds. Nothing in `unreal/` reads it. |
| `README.md` | Describes only the Swift game. A new root README replaces it. |
| `.claude/skills/ringshadow-model-pipeline/` | Runs `swift build` and `.build/release/Autocraft`. Needs an Unreal rewrite. |
| `.claude/skills/ringshadow-building-visuals/` | Refers to `Models.swift`. Needs an Unreal rewrite. |
| `.claude/skills/ringshadow-testing/` | XCTest and `make test`. Needs an Unreal rewrite. |
| `.idea/autocraft.dev.iml` | A CLion SwiftPM module. |
| `.build/`, `.build-log.txt`, `grid_bot.png`, `grid_top.png` | Git ignores these and they can be rebuilt. Delete them. |

## Stay, or copy into `unreal/` first

| Path | Why |
|---|---|
| `Sources/Autocraft/Resources/Sounds/` (117 MB) | `AcMusicPlayer.cpp:414` plays `../Sources/Autocraft/Resources/Sounds/music` at runtime, and `AcMp3LoaderTests.cpp:18` uses it. `Tools/Editor/import_sounds.py` reads `sfx/` and `ads/`. Move it into `unreal/`, then fix those paths. |
| `Sources/Autocraft/Resources/Textures/` (the 5 ground textures) | `unreal/Tools/terrain_textures.py:26`. |
| `Tests/Fixtures/` (3 JSON) | `core-tests/tests/StallTests.cpp:23` and `SessionTests.cpp:20`. |
| `bench/badlands-large.json` | Several core tests and `Tools/bench/bench.py`. |
| `Tools/AudioGen/` | The ElevenLabs manifests and comm scripts. They write into the sounds above. Keep them with whichever copy of the sounds is the master. |
| `docs/` | Game design. The Unreal code cites these files. |
| `AGENTS.md` | Split it. The machine notes stay. The build, test and log sections get rewritten for Unreal. |
| `.claude/skills/orc-elevenlabs/` and the other `orc-*`, `.claude/agents/`, `.mcp.json`, `.env`, `.gitignore`, `.orchestrator/` | Shared or Unreal. Split `.gitignore`'s Swift lines out. |
| `unreal/` | The game. |

## What stops working once Swift is gone

Nothing at runtime, if the paths above are fixed first. These tools can't
run again, though what they made stays:

- **Model export.** `export-models` wrote `unreal/Content-src/models/*.usda`.
  New units need a mesh path that doesn't go through Swift (see
  `docs/new-units.md`).
- **Goldens.** `unreal/core-tests/golden/*.json` came from the Swift tests.
  From now on, regenerate them from the C++ core.
- **Bakes.** `Tools/hud/bake_hud.sh`, `bake_icons.sh` and
  `Tools/cursors/bake_cursors.sh` compile or run Swift. A new unit icon
  needs another way to be made.
- **Comparisons.** The Swift-against-Unreal shots (`Tools/shots/compare.py`)
  and `bench.py --swift`.
- **`unreal/Content-src/` is ignored by git** (about 430 MB). Once the
  exporter is gone, its models, icons, HUD art and sounds can't be remade.
  Track it, or at least `launcher/` and `fonts/` (which the game reads at
  runtime), or back it up.

## Suggested order

1. Commit the Swift work in progress. Close the worktrees.
2. Copy the whole repo, `.git` included, to the archive folder (for
   example `~/git/dev/autocraft-swift`). That is the history copy.
3. Here: move the sounds, ground textures and fixtures into `unreal/`, and
   fix the runtime and test paths. Run `make -C unreal/core-tests` and the
   MP3 tests.
4. Here: `git rm` the "Move" paths. Add `unreal/` (minus the ignored
   build folders) in one commit. Write a new root README and AGENTS.md.
5. Rewrite the three `autocraft-*` skills for Unreal.
