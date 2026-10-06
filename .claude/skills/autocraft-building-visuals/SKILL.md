---
name: autocraft-building-visuals
description: Visual status language for Autocraft buildings in the Unreal Engine 5 game (unreal/). Use whenever you create a new building or structure model, add production/research/activity to a building, or change how a building's lights and animations report its state (the building pose functions in AcPoseBuildings*.cpp, the lamp pool in AcLamps.cpp, `ac::Structure` in AutocraftCore).
---

# Autocraft building visuals

Every Autocraft building reports its state with the same light language, so
the player can read the whole base at a glance from the RTS camera. The
model only names its lamp parts; a pose function drives them every frame.

Where it lives (`unreal/Source/Autocraft/`):

- `AcPoseBuildings.h`: the shared helpers: `Lamp`, `Phase` (`time + id·0.37`, so
  buildings blink out of step), `Glow` (the yellow formula), `Beacon`,
  `FPart` / `FParts` (parts by name, numbered parts `name_0`, `name_1`…).
- `AcPoseBuildings.cpp`: `Common` (construction, weld, beacon) and
  `ProductionLamps` (blue progress and queue lamps), and the Citadel, Hab
  Dome, Garrison and Bastion poses.
- `AcPoseBuildings2.cpp`: the Foundry, Spacedock, Lab, Derrick and Sentinel.
- Each kind registers its pose: `FAcPoseRegistration RegX(ac::StructureKind::x, &PoseX);`.
- `AcLamps.cpp` (`UAcLampPool`): the real lights at night (apron pools,
  beacons).
- `AcBuildingFx.cpp`: construction weld sparks and light, steam, vapour.
- `AcEffectsBursts.cpp`: a hurt building's smoke and fire.

The two references for production lamps: `PoseCitadel` (a tower, so its
queue lamps form a ring) and `PoseGarrison` (no tower, so its queue lamps
are pips on the roof edge). Reuse `ProductionLamps` for every new production
building.

## Colour meanings

| Colour | Means | Behaviour |
|---|---|---|
| **Blue** | production state | Dark (emission 0.04) in standby. Lit only to report progress and queue (below). Never a decorative pulse. |
| **Yellow / orange** | the building is working | Steady (~1.2) while idle. While active it glows on and off with `AcBuildingPose::Glow(Phase, Offset)`: `0.05 + 2.6·b²`, `b = 0.5 + 0.5·sin((phase + offset)·2π / 1.3)`. Give groups offsets (0 and 0.65) so they alternate. |
| **Red** | beacon | One small part named `beacon`, shown 0.25 s of every 1.2 s once the building is complete (`Common` does it for every kind). |
| **Green** | passive status (Hab Dome) | Slow breathing, `0.5 + 0.5·(0.5 + 0.5·sin(phase·2.2))`. |

Parts that show a resource take the resource's look, not a status colour.
Metallic Hydrogen is mirror-silver liquid metal with a cold blue-white light
and pale vapour (`M_Mercury`, the `AcVapour` cue): the Derrick's throat, bay
light and steam. Stardust Ore is dark rock with opal shimmer (`M_Opal`). A
Derrick's green tank gauges are status lights, so they stay green.

While under construction every light sits dark (about 0.05–0.1): the
building rises inside its scaffold model (`scaffold_<kind>`) with weld
sparks (cue `AcWeld`), and lights come on only at completion.

## Blue production lamps

A building that produces units, research or upgrades gets both indicators.

1. **Progress lamps**: parts `progressLamps_0..3`, window lamps on the faces
   toward the camera, numbered screen left to right. Lamp `k` is on when
   `k < floor(progress·4)`. The next one blinks, on for 0.3 s of every 0.5 s.
   All four are dark in standby. On is 1.9.
2. **Queue lamps**: parts `queueLamps_0..4`, one per slot of
   `Rules::maxQueue` (5), one lit per item in the queue, the one in progress
   included. On is 2.0, off is 0.04.
   - With a tower: the segments are the tower's round window, spread across
     the side that faces the camera.
   - Without a tower: a row of 5 pips on the roof edge or front face, left
     to right, set apart from the progress lamps (different height or face).

The surrounding glass that is not a lamp uses the same blue material at
0.04, so off lamps read as dark glass and not as holes.

A building with no production (a Hab Dome, a wall) has no blue lamps. Its
lights are green or yellow. The Bastion (`PoseBastion`) is the reference for
one:

- **Green pips** count what it holds: one per crew slot
  (`Rules::bastionCapacity`, 4), in a row on the roof front, left to right. A
  full pip is 1.3; an empty one 0.05.
- **Yellow slits** are its "working" light: steady (1.2) while manned and
  quiet, 0.15 when empty, on and off (the formula above) while it fires.
  Each shot also flashes the slit that faces the target.
- **Red beacon** as usual once complete.

A building that shoots by itself (the Sentinel, `PoseSentinel`) has no pips:
its yellow lamp row round the bunker's top edge and the sensor slit share
one `workGlow`, steady while it watches the sky and on and off while it
tracks or fires. The turret (`head`, `pods`) turns from the simulation's
`aim`. Its missile noses are grey in dark-red bores: a bright red part reads
as a second beacon.

## Emission, materials and team colour

- A lamp is its own part with its own emissive material (`M_Emissive`
  parent), so the pose can set it alone: `P.Emission[Part]` (per-instance
  custom data 1) scales the material's built-in intensity. Use the `Lamp`
  helper in `AcPoseBuildings.h`, which divides by the exported intensity.
  A glow merged into a part with other meshes goes through
  `P.MeshEmission` instead. Never rely on a shared material to animate:
  everything that pulses on its own is its own part.
- A part is shown or hidden with `P.Visible[Part]` (the beacon, slit
  flashes).
- Team colour is per-instance custom data 0 (the 8-colour palette,
  `MPC_AcTeams`), on trim only. The light language never changes with the
  team: a red player's production lamps are still blue.
- Damage shows in the scene, not the model: smoke below two thirds of its
  hit points, fire below one third (`AcEffectsBursts.cpp`). Keep the roof
  clear enough for it to read.

## At night

After dark (`AAcDaylight::GetDark`), `UAcLampPool` lights what is round the
lamps. The yellow "working" lamps throw a pool of light onto the apron in
front of the door, following their brightness: steady while idle, on and off
while working, dark while under construction. The red beacon lights the
roof as it blinks. Blue and green signal lamps only glow.

A new building gets its pool in the `FPoolSpec` table in `AcLamps.cpp`
(`SocketsOf`): where it hangs in building space, how far it reaches, and the
yellow material it follows (`floodGlow`, `slitGlow`, `workGlow`) with that
material's exported intensity. Every part named `beacon` gets its red light
by itself. Check with a hidden shot at `-AcHour=23`.

## Placement rules

- Buildings face +Z in catalog (SceneKit) space, toward the camera; the RTS
  camera is pitched 56° down. Put every indicator on a camera-facing face
  (front or the ±45° diagonals) or on top. Side and back faces are grazing
  or hidden.
- Number indicator parts screen left to right (−X first), so index =
  reading order.
- Name the parts as the pose expects (`progressLamps_k`, `queueLamps_k`,
  `beacon`, `floodGlow`…). The part names are the contract between the
  model script and the pose function.
- The model never animates itself: only the pose function moves or lights
  parts, and only cues (`FAcPoseCue`) ask `UAcBuildingFx` for lights,
  sparks and steam.

## State behind the lights

- Production state lives in the core (`ac::Structure`,
  `unreal/Source/AutocraftCore/Public/Types.h`): `training` (seconds left),
  `line` (the queue, the unit in training first), `trainingProgress()`,
  `queueCount()`, and `research` / `researchLeft` on a Lab.
- Queue rules: the queue holds 5, and items are paid for and take supply
  when queued. The next item starts as soon as one finishes.
- New fields go at the end and are optional (`std::optional`) so old saved
  sessions still load.

A production building's door opens (shutter, hatch, iris) while its new unit
walks out. The yellow door floodlights and vents follow the "working" row
above. The AI places it so the ground in front of its door stays clear.

## Checklist for a new building

1. **The model**, built in Unreal by its model script (the
   `autocraft-model-pipeline` skill): the named lamp parts, its own
   emissive materials, and a `scaffold_<kind>` model for construction.
2. **The core**: the kind in `StructureKind` (append at the end), its cost,
   build time, hp, radius, sight and tech requirement in `Rules.cpp`, and
   any new state in `ac::Structure`, with a core test in
   `unreal/core-tests` (see the queue tests there).
3. **The pose**: a `PoseX` function that calls `Common` first, follows the
   tables above (`ProductionLamps` for production), and its
   `FAcPoseRegistration`. Extend `AcPoseBuildingsTests.cpp` if it adds a
   helper.
4. **Night**: its `FPoolSpec` row in `AcLamps.cpp` and a `beacon` part.
5. **The playground** lists every `StructureKind` by itself (`AcPlayground`).
6. **Check it**: build the module, then hidden shots of the building idle,
   busy and under construction, by day and at `-AcHour=23`, in two team
   colours (`/Game/Maps/ModelRow` with `-AcModelFocus`, or `-AcPlayground`;
   see `unreal/SHOTS.md` and the hidden-run rules in the
   `autocraft-testing` skill). Look at the PNGs side by side from the
   game's camera angle. The user checks the motion in the game by hand;
   never relaunch their editor or game.
