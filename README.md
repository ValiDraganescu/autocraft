<h1 align="center">Autocraft</h1>

<p align="center">
  <b>A real-time strategy game where the AI runs your army, and you can dive into any unit and fight it yourself.</b><br>
  macOS · Unreal Engine 5.8 · C++20 · built in public with AI agents · public domain (CC0)
</p>

<p align="center">
  <img src="docs/media/dive.gif" width="820" alt="The camera dives from the top-down view into a Ranger, who walks out in first person">
</p>

**▶ The trailer, with the game's sound** (27 s, all recorded in the game):

https://github.com/user-attachments/assets/cd9d5520-cd26-4cac-956c-d17d335f2d0f

On a frontier world, Prospectors drill Stardust Ore and pump Metallic
Hydrogen while armies of up to eight players clash over the deposits. You
don't micromanage. Your team's AI commander runs the army. You steer it
from above, and when a fight matters, you **take over a unit** and play it
like a shooter.

## How you play

You play in three ways, and switch between them whenever you like.

**1. Your buildings.** Click a building and press its buttons: train
units, research, build. Anything the AI can do for you is queued for your
team's commander instead of being refused.

<p align="center"><img src="docs/media/top.jpg" width="820" alt="The top-down view: the Citadel, its Stardust Ore line and two Metallic Hydrogen wells"></p>

**2. The command map (M).** Give your AI orders: a stance, objectives
(attack, defend, hold, man a Bastion), expansions and requests. It plans
the rest.

<p align="center"><img src="docs/media/command-map.jpg" width="820" alt="The command map: the bases of the map and the team's objectives"></p>

**3. Drive a unit.** Click any of your units and the camera dives in. WASD
walks, the mouse aims, a click fires, R uses its ability, V switches
between first and third person, Esc hands the unit back to the AI.

<p align="center"><img src="docs/media/fight.gif" width="820" alt="Two armies of twelve meet in the desert, seen from above as the camera sweeps in"></p>

## Every unit is playable

Twelve kinds, each with its own cockpit, weapon and ability.

| | | |
|:---:|:---:|:---:|
| <img src="docs/media/pilot-ore.jpg" width="270"><br>**Prospector**: mines ore, pumps MH, builds, repairs | <img src="docs/media/pilot-ranger-fire.jpg" width="270"><br>**Ranger**: rifle infantry | <img src="docs/media/pilot-comet-jump.jpg" width="270"><br>**Comet**: jump-pack raider, clears cliffs |
| <img src="docs/media/pilot-juggernaut-fire.jpg" width="270"><br>**Juggernaut**: heavy grenadier | <img src="docs/media/pilot-firefly-fire.jpg" width="270"><br>**Firefly**: fast flame buggy | <img src="docs/media/pilot-dropship-heal.jpg" width="270"><br>**Dropship**: healing transport |
| <img src="docs/media/pilot-longbow-anchored.jpg" width="270"><br>**Longbow**: a tank that anchors into artillery | <img src="docs/media/pilot-kestrel-fire.jpg" width="270"><br>**Kestrel**: rocket gunship | <img src="docs/media/pilot-hailstorm-fire.jpg" width="270"><br>**Hailstorm**: flak half-track |
| <img src="docs/media/pilot-peregrine.jpg" width="270"><br>**Peregrine**: air-superiority fighter | <img src="docs/media/pilot-atlas.jpg" width="270"><br>**Atlas**: heavy assault walker | <img src="docs/media/third-longbow.jpg" width="270"><br>**Third person** (V): any unit, over the shoulder |

The twelfth, the **Scorpion**, is a burrowing mine.

<p align="center">
  <img src="docs/media/comet.gif" width="405" alt="Driving a Comet in first person">
  <img src="docs/media/third.gif" width="405" alt="A Ranger walking in third person">
</p>

## Your units level up while you drive them

The longer you drive a kind of unit in a game, the better every unit of
that kind gets, but only while you're the one driving. Units the AI runs
never level. Every level offers two upgrades and you keep one: 216 of them
across the twelve kinds ("Every Prospector pumps MH 20% faster"). It gives
you a reason to drive the units that are dull to play, the Prospector
first.

<p align="center">
  <img src="docs/media/level-cards.jpg" width="820" alt="A level-up: two upgrade cards over the Prospector's cockpit">
</p>

<p align="center">
  <img src="docs/media/mining.gif" width="820" alt="A Prospector drilling Stardust Ore in first person">
</p>

## Day, night and the radio

The sun crosses the sky, and at night the beacons and lamps come on.
The music plays as a radio station, **KSTR 88.7 Stardust**, with original
songs and in-world ads between them: Quicksilver Cola (pumped from
Metallic Hydrogen wells, "not a beverage"), Opal Glow face scrub (side
effects: levitation, being mined) and Citadel Timeshares (ash view
guaranteed).

<p align="center">
  <img src="docs/media/flyover.gif" width="820" alt="The camera pans across the base at dusk">
</p>

<p align="center">
  <img src="docs/media/top-night.jpg" width="820" alt="The base at night, its beacons lit">
</p>

## How it's made

**An engine-free core.** The whole simulation, from the economy and
pathing to combat and the AI commanders, is plain C++20 in
`unreal/Source/AutocraftCore`, with no Unreal in it. Its tests build with
clang and run in about 7 seconds (`make -C unreal/core-tests`). The Unreal
layer draws the frames, plays the sounds and takes the input.

**From concept to model.** Each unit starts as concept paintings. The one
picked becomes a six-view turnaround, and the in-engine model is built
part by part with Geometry Script and fitted against those views.

| Concept | Turnaround | In the engine |
|:---:|:---:|:---:|
| <img src="docs/media/atlas-concept.jpg" width="250"> | <img src="docs/media/atlas-turnaround.jpg" width="375"> | <img src="docs/media/atlas-model.jpg" width="250"> |

**Built with AI agents.** Autocraft is made with AI coding agents, and the
skills they work from are in [`.claude/skills/`](.claude/skills):

- [`autocraft-model-pipeline`](.claude/skills/autocraft-model-pipeline): concept art to an Unreal model.
- [`autocraft-building-visuals`](.claude/skills/autocraft-building-visuals): the buildings' light language.
- [`autocraft-testing`](.claude/skills/autocraft-testing): which tests a change needs and how to run them.
- [`autocraft-video`](.claude/skills/autocraft-video): records staged moments of the real game, hidden, at a fixed 30 fps with their sound, and cuts them into clips and narrated videos. Every GIF on this page came from it.

## Build and run

You need an Apple-silicon Mac and Unreal Engine 5.8 (from the Epic Games
launcher).

```sh
# The core's tests: no Unreal needed
make -C unreal/core-tests

# The game module
"/Users/Shared/Epic Games/UE_5.8/Engine/Build/BatchFiles/Mac/Build.sh" \
  AutocraftEditor Mac Development -Project="$PWD/unreal/Autocraft.uproject" -WaitMutex
```

Then open `unreal/Autocraft.uproject` and press Play. [AGENTS.md](AGENTS.md)
has the rest: hidden test runs, the tools and the rules of the repo.

## Repository layout

| Path | What |
|---|---|
| `unreal/` | The game: `Autocraft.uproject` (UE 5.8). |
| `unreal/Source/AutocraftCore/` | The simulation and the AI, engine-free C++20. |
| `unreal/Source/Autocraft/` | The Unreal game: rendering, camera, input, HUD, audio, effects. |
| `unreal/core-tests/` | The core's tests, built with plain clang++: `make -C unreal/core-tests`. |
| `unreal/Resources/` | Sounds (effects, voices, ads, music) and source textures. |
| `unreal/Tools/` | Editor import scripts, bakes, bench and shot tools ([README](unreal/Tools/README.md)). |
| `Tools/AudioGen/`, `Tools/ImageGen/` | Sound and image generation (ElevenLabs, OpenRouter). |
| `art/` | Concept art and turnaround sheets for the models. |
| `bench/` | Saved sessions the tests and the bench load. |
| `docs/` | Design: names, leveling, music, new units. |

[unreal/PORTING.md](unreal/PORTING.md) describes the core,
[unreal/GAME-LAYER.md](unreal/GAME-LAYER.md) the game layer, and
[AGENTS.md](AGENTS.md) how to build, test and work in this repo.

The game began as a Swift and SceneKit live wallpaper. That version was
retired on 2026-10-05 ([docs/swift-move.md](docs/swift-move.md)).

## Licence

Everything here is dedicated to the public domain under
[CC0 1.0](LICENSE): the code, the art, the models, the sounds, the music
and the docs. Use any of it for anything, with no credit needed.

These third-party files keep their own licences:

| Files | Licence |
|---|---|
| `unreal/Content-src/fonts/` (Barlow Condensed) | SIL Open Font License 1.1 ([OFL.txt](unreal/Content-src/fonts/OFL.txt)) |
| `unreal/Source/AutocraftCore/ThirdParty/json.hpp` (nlohmann/json) | MIT, in the file |
| `unreal/Source/Autocraft/ThirdParty/dr_mp3.h` | Public domain or MIT-0, in the file |

The game is built with Unreal Engine 5, which has its own licence from Epic Games.
