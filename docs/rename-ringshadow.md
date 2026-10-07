# Renaming the inside to Ringshadow

The game was renamed from Autocraft to Ringshadow on 2026-10-07. Phase 1
changed everything a player or a reader sees: the menu title (RINGSHADOW),
`ProjectName` and `Description` in `DefaultGame.ini`, the uproject
description, every doc, the post drafts and the five skills (now
`ringshadow-*`). This file is phase 2, a plan only: the internal names that
still say Autocraft, what renaming each one means, and what to do. Nothing
below is done.

`git grep -n -i autocraft` still finds these groups (the counts are from
2026-10-07):

| Group | Where | Hits |
|---|---|---|
| A. Unreal modules and project | `Autocraft`, `AutocraftCore`, `Autocraft.uproject`, `*.Target.cs`, `*.Build.cs` | about 330 source files |
| B. Class and log names | `Ac` / `SAc` / `FAc` / `UAc` prefixes, `AUTOCRAFT_API`, `LogAutocraft`, `Category = "Autocraft"` | 252 `AUTOCRAFT_API`, 6 category files |
| C. Automation test names | `Autocraft.<Area>.<Case>` | 87 tests, 15 areas |
| D. Data and log folders | `~/Library/Application Support/Autocraft`, `~/Library/Logs/Autocraft` | 10 source lines, 8 docs |
| E. Environment variables | `AUTOCRAFT_*` | 12 names |
| F. ElevenLabs voice names | `"Autocraft Atlas"` and ten more, in `Tools/AudioGen` | 11 voices |
| G. Repo folder, worktrees, GitHub repo | `~/git/dev/autocraft*`, `github.com/ValiDraganescu/autocraft` | 12 files with the URL |
| H. Leftovers | the Swift archive's names, imported-asset metadata, benchmark records | see the end |

## A. The Unreal modules and the project file

**What it means.** Rename the `Autocraft` module to `Ringshadow` and
`AutocraftCore` to `RingshadowCore`: the two source folders, the
`*.Build.cs` and `*.Target.cs` files and the classes in them
(`AutocraftTarget`, `AutocraftEditorTarget`), `IMPLEMENT_PRIMARY_GAME_MODULE`
in `Autocraft.cpp`, `IMPLEMENT_MODULE` in `AutocraftCoreModule.cpp`, the
`Modules` list and file name of `Autocraft.uproject`, the
`GetModuleFilename(TEXT("Autocraft"))` call in `AcTracker.cpp`, and every
`#include` path that names the module folder (the core is included as
`AutocraftCore/...` or by plain header names through its `Public` folder;
check which). The build target becomes `RingshadowEditor`, so the build
command in AGENTS.md, README.md and the testing skill changes.
`unreal/core-tests/Makefile` has `CORE := ../Source/AutocraftCore`.

**The `/Script/Autocraft` references.** There are fewer than the 413 the brief
guessed. Only `Config/DefaultEngine.ini` (`GameUserSettingsClassName`,
`GlobalDefaultGameMode`), the two maps `Content/Maps/Battlefield.umap` and
`ModelRow.umap`, `AcPlayground.cpp` (a config section), `AcSettings.h` (a
comment) and `make_battlefield.py` / `make_model_row.py` name
`/Script/Autocraft.*`. The other 410 `.uasset` files mention Autocraft only in
their import-source metadata (for example
`Sources/Autocraft/Resources/Sounds/ads/ash_away.wav`), which nothing loads.
Redirects for the two maps and the config:

```ini
[CoreRedirects]
+PackageRedirects=(OldName="/Script/Autocraft",NewName="/Script/Ringshadow")
+PackageRedirects=(OldName="/Script/AutocraftCore",NewName="/Script/RingshadowCore")
```

in `DefaultEngine.ini`, then open the two maps in the editor and resave them so
the redirect can be dropped. The player's `GameUserSettings.ini` keeps its
volumes under `[/Script/Autocraft.AcSettings]`: the redirect does not cover a
config section name, so either add a one-line migration that reads the old
section or accept that the volume sliders reset once.

**Files touched.** All of `unreal/Source/` (folders and the `AUTOCRAFT_API` and
`AUTOCRAFTCORE_API` export macros, which follow the module name),
`Autocraft.uproject` (renamed to `Ringshadow.uproject`), `Config/*.ini`, the two
maps, `core-tests/Makefile`, `Tools/**` (about 60 scripts and the shot
comparer pass `Autocraft.uproject` and look for "module Autocraft"), the
build commands in AGENTS.md, README.md and three skills, and `.idea/`.

**Risk.** High and wide, but mechanical. The first build after the rename is a
full rebuild; the editor's `Binaries`, `Intermediate` and `DerivedDataCache`
for the old name go stale; an editor open during the rename breaks (the user
restarts it). Other sessions editing the live repo conflict on every file, so
do it in one short window with the other sessions paused, on a branch, with a
`git mv` of the folders first so history follows. The `Binaries/Mac/*Autocraft*`
files are not tracked.

**Recommendation.** Do it, but last and in its own window, after B to E are
decided. Nothing player-visible depends on it. If the cost never seems worth
it, leaving the modules named Autocraft is a defensible end state: a module
name is not shown anywhere a player looks. Do the module rename together with
the class prefix question below, never in separate passes.

## B. Class prefixes, the log category and editor categories

- **`Ac` / `SAc` / `FAc` / `UAc` / `AAc` prefixes.** A new prefix (`Rs`) means
  renaming about 150 files and every use. `Ac` stays readable as "Ringshadow
  core", so keep it. **Recommendation: keep.**
- **`LogAutocraft`** is read by the pilot probe
  (`Tools/Audio/pilot_probe.py` greps `LogAutocraft: audio:`) and by
  `AcLog.cpp`. Renaming to `LogRingshadow` is a one-line change plus the
  probe and any log-reading skill text. Cheap; do it with A.
- **`Category = "Autocraft"`** in six headers is an editor details-panel
  label. Cheap; do it with A.
- **`AUTOCRAFT_API`** follows the module name (see A).

## C. Automation test names

`IMPLEMENT_SIMPLE_AUTOMATION_TEST(..., "Autocraft.Audio.Rules.Weapons", ...)`
and 86 more, plus the header comments with their `-ExecCmds="Automation
RunTests Autocraft.<Area>"` lines and the testing skill's table. The names are
only labels, so renaming is a find and replace of `"Autocraft.` inside the
test files, and of the run commands in the testing skill, SHOTS.md and the
header comments. Risk: none beyond a stale command somewhere.
**Recommendation:** do it with A, as `Ringshadow.<Area>.<Case>`.

## D. Data and log folders

`~/Library/Application Support/Autocraft/` holds the saves (`sessions/`,
`Unreal/sessions/`), `tracking.sqlite` (and its `-shm`/`-wal`),
`Unreal/tracking.sqlite`, `Unreal/music.jsonl` and `Audio/music` (the user's
own MP3 soundtrack, 117 MB of it in the repo's archive too).
`~/Library/Logs/Autocraft/` holds `autocraft.log` (the Swift game's),
`unreal.log` and Unreal's own `AutoSDKInfo` files (Unreal names that folder
after the project, so it moves with A without any code).

**What it means.** Change the paths in `AcSaves.cpp` (three lines),
`AcMp3Loader.cpp`, `AcMusicPlayer.cpp`, `AcLog.cpp`, `Session.cpp` in the core
and the Makefile's `DB` default, and the tests that assert the paths
(`AcMp3LoaderTests.cpp`, `AcTrackerTests.cpp`). The user's existing data has
to come along: a one-time migration at startup that, if the new folder is
missing and the old one exists, moves the old folder (a `rename` on the same
volume, atomic, no copy) and leaves a note in the log. `tracking.sqlite` has
WAL files, so move the whole folder only while the game is closed, never
file by file. A fallback that reads the old folder when the new one is empty
is the safer variant: it never loses data if the move fails halfway.

**Files touched.** The six sources above, two core-test files, the Makefile,
AGENTS.md (the log path), docs/music.md, docs/leveling.md, GAME-LAYER.md,
the AudioGen shell scripts and the music skill text, plus any script that
reads `autocraft.log`.

**Risk.** The only item here that can lose the user's data: saves, the
tracking history that feeds the leveling stats, the votes on the music, and
the user's soundtrack folder. Test the migration on a copy first and keep the
old folder until the user says to delete it.

**Recommendation.** Rename last, after A, and only with the migration and a
backup. A shipped game would want it before release; for now the folders are
invisible to players who never open `~/Library`. A cheaper middle path:
make new data go to `Ringshadow/` and read the old folder as a fallback
forever.

## E. Environment variables and command-line flags

`AUTOCRAFT_HOUR`, `AUTOCRAFT_FOG`, `AUTOCRAFT_MUSIC`, `AUTOCRAFT_TIP`,
`AUTOCRAFT_MAP_TESTS` are read by the game or the core tests and are set by
`Tools/shots/scenes.json` (7 hits), the testing skill and SHOTS.md. The
`AUTOCRAFT_PREVIEW_*`, `AUTOCRAFT_ICON_DUMP`, `AUTOCRAFT_RAYS_GOLDEN`,
`AUTOCRAFT_SIM_HASH` and `AUTOCRAFT_GOLDEN_OUT` belong to the retired Swift
exporters and only appear in comments and docs about them. The `-Ac…` command
line flags do not carry the name.

**What it means.** Read `RINGSHADOW_*` first and fall back to `AUTOCRAFT_*`
for a release, then drop the fallback. `Tools/shots/scenes.json`,
`core-tests/tests/TeamTests.cpp`, `WindowModeTests.cpp`, `AcFog.cpp`, the
daylight, tip and music code and the docs change. The retired Swift variables
stay as history.

**Risk.** Low. A forgotten variable silently falls back to the default (the
slow map test is skipped; the music scene shots lose their forced state), so
grep after.

**Recommendation.** Do it with A, five variables only.

## F. ElevenLabs voice names

The voices were made with Voice Design and saved under names like
`Autocraft Comet`, `Autocraft Tank`, `Autocraft KSTR Announcer` and
`Autocraft Oracle`. The manifests in `Tools/AudioGen/*.manifest.json` and
docs/music.md name them, and `design_voice.py save` takes the name. The
sounds already generated do not depend on the name, only a regeneration does.
**What it means.** Rename the 11 voices in the ElevenLabs account (by hand in
the dashboard) and the strings in the manifests and docs; or leave the account
alone. **Risk:** a manifest that names a voice that no longer exists fails the
next generation. **Recommendation:** leave them; the names are internal to the
user's account and nobody sees them. Rename a voice only when it is
regenerated anyway.

## G. The repo folder, the worktrees and the GitHub repo

- **GitHub repo** `ValiDraganescu/autocraft` to `ValiDraganescu/ringshadow`:
  GitHub redirects the old URL for the web, `git clone` and `git push`, so
  every link in 12 files (the post replies and benchmark briefs that point at
  `/tree/main/.claude/skills/ringshadow-*` and `docs/*.md`, README, the
  sample posts) keeps working until someone creates a new repo of the old
  name. The skill folder renames in phase 1 already broke the old
  `/tree/main/.claude/skills/autocraft-*` links on GitHub once merged, so
  rename the repo first or soon after the merge. Update the remote
  (`git remote set-url origin ...`) in the main checkout and every worktree
  (they share one config, so once). Posted tweets keep their old links
  (redirects cover them). Check the repo's description, topics and the
  website field by hand.
- **Repo folder** `~/git/dev/autocraft` to `~/git/dev/ringshadow`, and the
  archive `~/git/dev/autocraft-swift`: the folder name is in the Orchestrator's
  project list, IntelliJ's `.idea/autocraft.iml` and `modules.xml`, every
  worktree's `.git` pointer file (`git worktree repair` fixes them), the
  Claude Code project memory directory
  (`~/.claude-profiles/personal/projects/-Users-...-autocraft/`, which is keyed
  by the path: a moved folder starts with an empty memory, so move that
  directory too), absolute paths in docs (`docs/swift-move.md`, AGENTS.md
  mention `~/git/dev/autocraft-swift` and `~/git/dev/local-ai/autocraft`) and
  the `autocraft-worktrees` folder. Do it while no session runs in the repo.
- **Local names** such as `/tmp/autocraft-bake-*`, `autocraft-tracker-*.sqlite`
  and `autocraft-test-sessions` in `$TMPDIR` are throwaway; rename them with
  A if the touched file is open anyway.

**Risk.** Low for the GitHub rename (reversible, redirected); medium for the
local folder (it breaks every tool keyed by the path, including the
Orchestrator's own records). **Recommendation:** rename the GitHub repo soon
after the merge; leave the local folder until it hurts (it is invisible to
players), or do it in the same window as A.

## H. Left on purpose

- The archived Swift game and everything that describes it
  (`docs/swift-move.md`, `unreal/Tools/Editor/swift/*`, `Sources/Autocraft`
  paths in `docs/`, `art/` and Source comments, `Autocraft preview`,
  `Autocraft export-models`, `Autocraft bench`, `Autocraft netspreview` and
  `Autocraft stencil` as binary names). They describe a program that was
  called Autocraft and no longer exists here.
- The 410 `.uasset` files with an import path inside.
  Reimporting a texture or sound rewrites it; there is no reason to touch
  them.
- `.claude/skills/ringshadow-x-posts/bench/rounds/1.json` and `2.json`:
  the benchmark's recorded posts with their votes, which say Autocraft.
- `.claude/skills/ringshadow-video/kit/gallery/sample-frame.jpg`, a rendered
  frame whose footer reads Autocraft. Re-render the gallery
  (`scripts/gallery.sh`) to refresh it.
- The longbow post's text in `docs/x-posts/longbow.md`, as posted.
- `.idea/autocraft.iml`, the IntelliJ module.

## Suggested order

1. Merge phase 1. Rename the GitHub repo.
2. One window, one branch: A, B (`LogRingshadow`, categories), C and E
   together, with the other sessions paused. Build, run the core tests and
   the automation tests, open both maps once and resave.
3. D with its migration, tested on a copy of `~/Library/Application Support/Autocraft`.
4. G's local folder, if wanted. F never, unless a voice is redone.
