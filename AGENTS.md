# Ringshadow

A real-time strategy game for macOS in Unreal Engine 5 (`unreal/`, UE 5.8): prospectors mining Stardust Ore and Metallic Hydrogen on a frontier world. [docs/naming.md](docs/naming.md) has the names of every unit, building and resource. [docs/music.md](docs/music.md) has the soundtrack, the music player and the Suno prompts. [README.md](README.md) has the layout. [unreal/PORTING.md](unreal/PORTING.md) and [unreal/GAME-LAYER.md](unreal/GAME-LAYER.md) describe the core and the game layer. [docs/new-units.md](docs/new-units.md) designs the next units. [docs/builds.md](docs/builds.md) packages the game for players: Windows and Linux on a Windows machine in AWS, the Mac build here.

The game was called Autocraft until 2026-10-07. Ringshadow is the huge ringed gas giant in its sky: the prospectors live in its shadow. Only what a player or a reader sees changed. The code keeps the old name (the `Autocraft` and `AutocraftCore` modules, `Autocraft.uproject`, the `Ac` class prefixes, the data and log folders, `AUTOCRAFT_*` variables, the repo folder), and they stay that way: Ringshadow is the marketing name, Autocraft the project's name inside ([docs/rename-ringshadow.md](docs/rename-ringshadow.md)). Write Ringshadow in prose, RINGSHADOW in uppercase labels, `ringshadow` as a slug. The GitHub repo was renamed to `ringshadow` on 2026-10-09; GitHub redirects the old `autocraft` URLs.

The original Swift and SceneKit game left this repo on 2026-10-05. It is archived, with its git history, in `~/git/dev/autocraft-swift` ([docs/swift-move.md](docs/swift-move.md)). Work happens only on the Unreal game; don't take facts from the Swift code. Until 2026-10-01 the repo lived in `~/git/dev/local-ai/autocraft`. Old logs under `art/` still show that path.

## Machine

- macOS on an M4 Max with 128 GB of unified memory.
- Python runs through `uv`: the pipeline scripts are self-contained `uv run` scripts. Never call pip or venv directly.
- Node runs through `pnpm`, never npm or yarn. nvm manages the Node versions.
- nix is the package manager. Never use brew.
- Keys live in `.env`, which git ignores: `OPENROUTER_API_KEY` and `ELEVENLABS_API_KEY`. The Orchestrator app keeps the ElevenLabs key in the Keychain (`com.orchestrator.elevenlabs.api-key`) and passes it only to terminals the Orchestrator opens itself. Sessions in IntelliJ's terminal don't get it, so they read it from `.env`.

## How to work here

- Core tests (engine-free C++, about 7 s): `make -C unreal/core-tests`.
- Build the game module: `"/Users/Shared/Epic Games/UE_5.8/Engine/Build/BatchFiles/Mac/Build.sh" AutocraftEditor Mac Development -Project="$PWD/unreal/Autocraft.uproject" -WaitMutex`. Builds can fail on a busy machine, so retry a few times before treating it as broken. The open editor needs a restart to load new C++.
- Never relaunch the game or the editor: the user restarts them. Every game run an agent starts is hidden and silent: the Dock-less `UnrealEditorBG.app`, `-nosound`, no `-log` ([unreal/Tools/README.md](unreal/Tools/README.md), "Hidden test runs").
- Tests are isolated scenarios. Never write or run a test where the AIs play a whole game, nor pieces that add up to one. Run game batches only when the user asks.
- Several sessions edit this repo at once. Make small in-place edits. Merge agent work with a three-way merge. Never copy whole files over live ones.
- Delete with `rm` and the exact full path only, never a wildcard.
- The game is open source (CC0, [LICENSE](LICENSE)), published as github.com/ValiDraganescu/ringshadow. Never name other games, their studios or their units in code, comments, docs, prompts or file names; describe the idea instead ("the classic RTS worker pull"). Never commit keys.
- Commits and PRs carry no AI attribution: no `Co-Authored-By: Claude` trailer, no "Generated with Claude Code" line. The author is the user.
- Sounds and textures live in `unreal/Resources/`. Some editor tools read copies of Swift files kept beside them (`unreal/Tools/hud/swift`, `cursors/swift`, `Editor/swift`); the exporters that needed the whole Swift game (models, icons, goldens) are retired.
- The game's log is `~/Library/Logs/Autocraft/autocraft.log`. Read it first when the user reports a freeze or a crash.

## Skills

- `.claude/skills/ringshadow-model-pipeline/`: turning concept art into an Unreal model (the Grok turnaround, a Geometry Script model). Record every hiccup in it as it happens.
- `.claude/skills/ringshadow-building-visuals/`: the light language of the buildings. Use it for every new building.
- `.claude/skills/ringshadow-testing/`: which tests a change needs and how to run them, the slow map test included.
- `.claude/skills/ringshadow-video/`: gameplay clips and narrated explainer videos for X. It records micro simulations of the game, hidden, at a fixed 30 fps.
- `.claude/skills/ringshadow-x-posts/`: X posts in the developer's voice, and the whole path of a post (WORKFLOW.md): the review page, the calendar in `docs/x-posts/`, the videos, posting.
- `.claude/skills/orc-elevenlabs/`: speech, sound effects and music. This copy has the Ringshadow sound notes, which the Orchestrator's stock copy lacks. Keep it. Like every `orc-*` skill and agent it is local only: git ignores them and they are never published.
