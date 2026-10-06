# Reference shots (F2)

Each scene of the Swift game's `windowshot` has an Unreal twin with the
same name. Both games render the scene headless, and a script puts the two
pictures side by side and gives a difference score. This file lists each
scene, how close the two pictures are, and every known difference, with the
chunk that owns it (`GAME-LAYER.md` §4).

## How to run

```sh
uv run unreal/Tools/shots/compare.py                  # every scene (~25 min)
uv run unreal/Tools/shots/compare.py top orders pilot-ranger
uv run unreal/Tools/shots/compare.py --group pilot    # top-down, command-map, leveling, pilot, third, playground
uv run unreal/Tools/shots/compare.py --only-score     # re-score the PNGs already there
uv run unreal/Tools/shots/compare.py --list
```

- The table is `unreal/Tools/shots/scenes.json`. For each scene it holds
  the Swift arguments and environment, and the Unreal flags (`"ue": null`
  means the scene has no Unreal staging yet), plus the map, zoom, camera
  point, warm-up, still and owners.
- In Unreal, `-AcScene=NAME` (`Source/Autocraft/AcShotScenes.*`) reads the
  same table and appends the scene's flags to the command line before the
  map loads. Pass the window size yourself:
  `-windowed -ForceRes -ResX=1600 -ResY=1000`. `-AcScene=list` logs every
  scene. `-AcWarm=S` steps the game S seconds at 1/30 before the first
  frame, as `GameController.warm` does (windowshot's `--warm 10`). With
  `-AcPaused` (`"still": true`) the game then holds still, as the Swift
  still does.
- Output goes to `unreal/Saved/Shots/` (or `--out DIR`): `swift/`, `ue/`,
  `side/NAME.png` (Swift | Unreal, with the ×3 difference map and a 50 %
  blend below), `logs/`, `report.md`, `report.json` and `sheet.png`.
- The script runs `make` first and uses the Swift binary only if `make`
  succeeds. Every Unreal run gets `-nosound`. The script runs 2 Unreal
  processes at a time (`--jobs`) and kills only the processes it started.
  If another agent is relinking the module, it waits and retries.
- Scores: both pictures at the Swift size, then at 1/4 scale with a light
  blur. **diff** is the mean |RGB| difference in % of full scale. **view**
  is the same over the 3-D view only. **hud** is the same over the
  234-point console strip. **edges** is the correlation of the two gradient
  maps, which measures layout and framing and ignores colour. **grade** is
  A (edges ≥ .80 and diff ≤ 6), B (≥ .65 and ≤ 10), C (≥ .45) or D.
  The scores rank the scenes. They do not replace looking at `side/`.

## Status (2026-10-05, 1600×1000 unless noted, HUD 1×)

| Scene | Swift | Grade | Diff | Edges | Owners |
|---|---|---|---|---|---|
| top | (defaults: Highlands · Medium, warm 10) | **A** | 3.9 | .86 | B6/C6 drill glow |
| top-badlands | `--map badlands --size large --zoom 40` | **A** | 3.6 | .85 | A3 |
| top-far | `--zoom 1` (clamped) | B | 3.6 | .70 | A3 |
| top-dusk | `AUTOCRAFT_HOUR=19` | **A** | 3.0 | .89 | B9 |
| top-night | `AUTOCRAFT_HOUR=23` | **A** | 1.8 | .89 | C6 |
| top-night-beacon | `AUTOCRAFT_HOUR=23`, warm 9.7 | **A** | 1.8 | .89 | B6, C6 |
| top-nofog | `AUTOCRAFT_FOG=off` | **A** | 4.0 | .87 | A3 |
| top-1080 | 1920×1080 | **A** | 3.6 | .86 | A3 |
| top-ultrawide | 5120×1378 | **A** | 3.0 | .85 | A3 |
| select-citadel | `--select citadel` | **A** | 4.0 | .87 | D6 (bar: see below) |
| hover-prospector | `--hover prospector` (badlands, 40) | **A** | 3.7 | .85 | D6 (bar: see below) |
| hover-citadel | `--hover citadel` | **A** | 3.6 | .85 | A3 |
| click-citadel | `--click citadel` | **A** | 3.7 | .86 | D6 (bar: see below) |
| music-paused | `AUTOCRAFT_MUSIC=paused` | **A** | 3.9 | .84 | D9 |
| music-ad | `AUTOCRAFT_MUSIC=ad` | **A** | 3.9 | .84 | D9 |
| command-map | `--command-map` | **A** | 1.0 | .94 | D8 |
| orders | `--orders` | **A** | 1.1 | .93 | D8 |
| orders-site | `--orders site` | **A** | 1.1 | .93 | D8 |
| orders-queue | `--orders --queue` | **A** | 1.1 | .93 | D8 |
| level-cards | `--pilot ore --level prospector:4 --picks 1` | B | 5.6 | .74 | B8, B9 |
| level-up | … `--levelup` | B | 5.7 | .74 | B8, B9 |
| level-map | `--command-map --level prospector:4,ranger:3 --picks 1` | **A** | 1.0 | .94 | E9, D8 |
| pilot-ore | `--pilot ore` | B | 5.7 | .73 | B8, B9 |
| pilot-mining | `--pilot mining` | B | 5.4 | .73 | C6 (drill light), B8 |
| pilot-build | `--pilot build` | B | 6.4 | .78 | E8 |
| pilot-menu | `--pilot menu` | **A** | 4.8 | .80 | E8 |
| pilot-ranger | `--pilot ranger` | B | 6.7 | .72 | E2, E4, B9 |
| pilot-comet | `--pilot comet` | **A** | 5.3 | .82 | B9, E4 |
| pilot-juggernaut | `--pilot juggernaut` | **A** | 4.6 | .84 | B9, E4 |
| pilot-firefly | `--pilot firefly` | **A** | 5.2 | .81 | B9, E4 |
| pilot-longbow | `--pilot longbow` | **A** | 5.7 | .80 | B9, E4 |
| pilot-dropship | `--pilot dropship` | **A** | 3.9 | .86 | B9 |
| pilot-kestrel | `--pilot kestrel` | B | 5.3 | .78 | B9, E4 |
| pilot-hailstorm | `--pilot hailstorm` | B | 7.4 | .74 | B9, E4 |
| third-ore | `--pilot ore --third` | C | 6.5 | .58 | E3 (heading: see below), B8 |
| third-ranger | `--pilot ranger --third` | C | 6.8 | .63 | E3, B9 |
| third-comet | `--pilot comet --third` | B | 6.2 | .72 | E3 |
| third-longbow | `--pilot longbow --third` | B | 6.4 | .72 | E3 |
| third-dropship | `--pilot dropship --third` | B | 4.7 | .78 | E3 |
| playground | `--playground` | D | 7.4 | .39 | F5 (column beside the view) |
| playground-citadel | `--playground --holding citadel` | D | 8.3 | .37 | F5 |
| playground-garrison | `--playground --holding garrison` | D | 7.9 | .37 | F5 |
| playground-drive | `--playground --drive ranger` | B | 6.9 | .70 | B9 |
| pilot-citadel | `--pilot citadel` | B | 9.1 | .76 | C6 (apron light), B8 |
| pilot-wide | `--pilot wide` | B | 7.2 | .79 | B9, B8 |
| pilot-enemy | `--pilot enemy` | **A** | 4.8 | .80 | B9 |
| pilot-ranger-fire | `--pilot ranger-fire` | B | 6.9 | .73 | E2 (held fire), E6 |
| pilot-ranger-far | `--pilot ranger-far` | B | 6.5 | .73 | B9 |
| pilot-ranger-miss | `--pilot ranger-miss` | B | 6.7 | .74 | B9 |
| pilot-ranger-minigun | `--pilot ranger-minigun` | B | 6.8 | .74 | B9 |
| pilot-ranger-minigun-fire | `--pilot ranger-minigun+fire` | B | 6.9 | .74 | E2 (held fire), B9 |
| pilot-comet-fire | `--pilot comet-fire` | **A** | 5.5 | .81 | B9 |
| pilot-comet-jump | `--pilot comet-jump` | B | 5.3 | .77 | B9 |
| pilot-juggernaut-fire | `--pilot juggernaut-fire` | B | 5.5 | .75 | B9 |
| pilot-firefly-fire | `--pilot firefly-fire` | B | 7.7 | .76 | E5/C1 (flame light gain) |
| pilot-longbow-fire | `--pilot longbow-fire` | B | 5.3 | .76 | E2 (held fire) |
| pilot-longbow-anchored | `--pilot longbow-anchored` | B | 6.8 | .69 | E4, B9 |
| pilot-longbow-anchoring | `--pilot longbow-anchoring` | B | 6.0 | .78 | B9 |
| pilot-longbow-close | `--pilot longbow-close` | B | 6.5 | .67 | E4 (eye height: min-range ring), E2 (target pose) |
| pilot-dropship-heal | `--pilot dropship-heal` | B | 4.5 | .78 | E2 (patient's heading) |
| pilot-dropship-cargo | `--pilot dropship-cargo` | **A** | 3.7 | .87 | — |
| pilot-kestrel-fire | `--pilot kestrel-fire` | B | 5.4 | .77 | E2 (held fire), B9 |
| pilot-hailstorm-fire | `--pilot hailstorm-fire` | B | 7.7 | .72 | B9 |

All 62 scenes render in both games (27 A, 27 B, 5 C, 3 D; before the
A3/D6/E3/E9/B10/F5 fix pass of 2026-10-05: 60 scenes, 14 A, 27 B, 15 C, 4 D). The 20
pilot variants were staged on 2026-10-05 (E2 staging, `AcPilotStage.cpp`);
`--group pilot` before: 12 of 33 scenes (6 A, 6 B), after: 33 of 33 (9 A,
22 B, 2 C).

## Dev staging without a Swift twin

- `-AcStageBuried=owner|enemy` (`AcWorldRenderer.cpp`): two buried Scorpions
  before the local main (`owner` adds a walking one; `enemy` is the first
  enemy's, shown by a reload). With `-AcCamAtArmy -AcZoom=60 -AcPaused`. Always
  add `-AcNew -AcNoSave -AcSaveDir=<tmp>`: a shot run otherwise writes the real
  window session. Used for `art/models/scorpion/previews/outline-owner.png` and
  `outline-enemy.png` (the outline pass, `AcOutline.h`).

## Known differences, by owner

**A3 (RTS camera): fixed 2026-10-05.** The top-down view was 1.11× too
close at any aspect but 16:9: the camera left the aspect constraint to the
player's default (MaintainYFOV), which read the horizontal FOV set in
`AAcRtsPawn::Apply` at the camera's 16:9 aspect. It now forces
`MaintainXFOV`, as the pilot camera does, so Swift's fixed vertical 30°
holds at any aspect: `top`, `top-1080` (1920×1080) and `top-ultrawide`
(5120×1378) all grade A (edges .85–.86).

**E2 (pilot staging):** every Swift `stagePilot`/`stageUnit` scene now has
an Unreal twin: `-AcPilot=KIND -AcPilotStage -AcPilotVariant=V`
(`AcPilotStage.cpp`; V is Swift's part after the dash, a Prospector's
`citadel`/`wide`/`enemy`). The stage steps exactly as Swift's
(`UAcSimSubsystem::StepThenPause`: 1/30 s after the set-up, 1/30 s per aim
round, then the act one 1/60 s step at a time until the driven unit's round
leaves). Every `-fire`/`-miss` scene fires on its first act step (log:
`pilot: stage fired after 1 steps`), and the tracer, muzzle flash and hit
spark leave the cockpit's muzzle (E5's muzzles, C1's tracers; the Ranger
rifle, minigun, Longbow cannon, Kestrel rockets and Hailstorm flak all
checked by eye). Remaining differences:
- The staged target holds its fire (cooldown 1 s) on purpose: a shot of its
  own would stay frozen on screen. In Swift its first shot lands, so the
  driven unit reads 39/45 (Ranger), 170/175 (Longbow), 134/140 (Kestrel)
  where Unreal reads full health, and Swift's XP bar is 5–10 XP higher
  (15 vs 10 for `ranger-fire`, 35 vs 25 for `longbow-fire`). The
  enemy's pose differs in `longbow-close` for the same reason.
- `pilot-dropship-heal`: the patient Ranger turns about 1 rad toward its
  side's rally facing over the 20 held steps (the core's idle turn, 3
  rad/s). Swift's patient still faces the Dropship after the same 1/3 s.
  Not explained (checked 2026-10-05): the patient is `idle` in the
  default branch of `stepSoldier` in both cores (log: `task 0`, heading
  −2.52 → 2.66), both games warm 301 steps to t=10.03, and Swift's
  `GameScene` poses a Ranger at `u.heading` with no easing. The stage has
  no difference left that touches it (the target's held cooldown does
  not reach this branch). The grade is unchanged (B).
- `pilot-mining` uses E6's `-AcPilotRun=2`, which plays 2 s for real
  (Swift steps 60 frames at 1/30), so the drill progress differs by about
  1 %.
- `pilot-hailstorm*`: fixed 2026-10-05. The enemy Kestrel was still
  lifting off: the renderer stamps `BornAt` on any unit that appears
  mid-game, and the still came 0.1 s later. Swift draws its last frame at
  `clock + 2` (`world.sync(time: clock + 2)`), past the 1.4 s lift. The
  stage now forgets `BornAt` for the units it puts down
  (`AAcPilotPawn::StageLifted`), but not for the driven flyer: its eye
  stays at the lift's start, as Swift's does (lifting it too made
  `pilot-dropship-heal` and `pilot-kestrel` worse).

**E5 (cockpits), seen in the new scenes:** fixed 2026-10-05, both in the
staging (`AcPilotStage.cpp`), since the cockpit code matched Swift's:
- `pilot-citadel`: Swift's `takeOver` poses the cockpit (and seeds
  `ForkMotion` shut) before `carrying = 5`, and windowshot's render clock
  stands still after that, so its fork stays shut and empty. The stage
  now puts the load aboard once the settle steps are done and the game
  holds, so the fork stays shut too.
- `pilot-comet-jump`: Swift steers its staged step from `takeOver`'s
  camera at the cliff's edge, so the crosshair point (and the pistols'
  aim, `Aim.inRig`) is cast from below the jump. From the lifted eye the
  pistols turn down out of view. The stage now takes over at the edge,
  keeps the crosshair cast from there (`StageCross`) and then puts the
  Comet half way across. The pistols are gone, as in Swift (C → B).
- `pilot-firefly-fire`: no flame shows in either still (the jet's head
  is at the nozzle at `since` 0). The real difference is that Unreal's
  flame light (0.3 × `FlameLight` × `ac.CockpitLightGain`, plus its
  world twin) washes the hull and the near ground white, where Swift's
  only warms them. E5 or C1 should tune that gain.

**E6 (hit marks):** fixed 2026-10-05. Swift's stage marks the hit at the
shot (`markPilotHit`). While a still is staged (`AAcPilotPawn::Staged`),
the overlay now marks on the driven unit's `Shot` rather than `Landed`,
and holds the mark fresh (`HeldMarkAge`), because the still is taken a
moment later. `longbow-fire` shows "-22.5" and `firefly-fire` the X and
"-21", as in Swift.

**E4 (range cues):** `pilot-longbow-close`: the minimum-range ring is
drawn (`-AcPilotPitch=-0.45` shows it in orange round the Longbow). In
Swift's still it sits right on the view's bottom edge. In Unreal it falls
just below, because near ground sits a little lower on screen. That
suggests an eye a few cm higher. Swift eases a machine's eye height by
render time, which stands still in windowshot, so its eye keeps the
height from `takeOver`. Not fixed.

**E3 (third person):** the VIEW switch now lights **3RD** in third person
(the pilot pawn sets `SAcConsole::SetView` each frame, driving or not, as
`hud.showView(third:)`, and a click on the switch is V). `third-ranger`
reads 132° against Swift's 131°: a staged aim round now turns the unit to
`pilotFacing` (toward the crosshair's point), not to the camera's yaw.
`third-ore` still reads 274° against Swift's 318°. Unreal's is the live
game's value. Swift's comes from its stage: `stagePilot` calls `takeOver`
(which places the chase camera) before it moves the Prospector and turns
the view, and its 3 steps steer without `followPilot`. So the crosshair is
cast from the old camera, and the unit turns toward that point. The view
itself matches (the same rock in the middle). The Ranger also sits farther
off, as E3 noted.

**B8 (opal):** fixed in part (2026-10-05). `M_Opal` now reads the
normal-mapped pixel normal and the reflection in the camera's basis (Swift's
view-space `_surface.normal`), has no night glow (`NightGlow` 0: Swift's
emission is the same by day and night) and a finer pattern (`PatternScale`
0.04). Close up the hue now mixes teal, gold and violet over more dark rock,
as in Swift, instead of purple blotches. The night opal is as bright as
Swift's. The streaks (2026-10-05 polish): the graph fed the vertex normal,
not the normal-mapped one, to the hue; it now transforms the rock normal map
to world space, so the bumps break the film into finer teal/violet/gold
streaks inside the patches as in Swift. Left: Swift's grey sheen on the bare
rock, and the deposits' placement (some lie turned against Swift's).

**C6 / B6 (lights):** fixed in part. The Citadel's door light (training)
now lights the apron yellow by day and at night: `UAcBuildingFx` lights use
the lamp pool's units (unitless, no inverse square, `ac.LampFalloff`,
`ac.BuildingLightGain` 500). Unlit night ground matches Swift within ~3 %
(`ac.Daylight.NightAmbient` 3.5, which lifts the sky light with the sun 8-18°
or more below the horizon). In `pilot-mining` the struck crystal is lit gold,
from a world-space twin of each cockpit light (`ac.CockpitWorldLightGain`,
AcCockpit.cpp). The top-down drill glow now matches Swift's size day and
night (`ac.DrillLightGain` 30, `ac.DrillLightReach` 1.8). **Beacons: fixed
2026-10-05.** The blink is the same (`fmod(t, 1.2) <= 0.25`, every building
in step). Poses now run on a render clock (`FAcFrame::Clock`: game time plus
the real time spent paused, as Swift's `GameController.clock`), so a paused
game keeps blinking; shot runs hold it, as `windowshot` never ticks. The red
beacon light reddens the Citadel dome as in Swift (`ac.BeaconLightGain` 40;
scene `top-night-beacon`, warm 9.7 s so every beacon is lit: A, 1.8 %).
**Firefly flame (pilot-firefly-fire): fixed 2026-10-05.** The cockpit flame
light was Swift's sRGB colour used as linear light at 0.3 of `flameLight`,
which washed the hull and the near ground white. It is now the colour in
linear light at `ac.CockpitFlameLight` 0.15: B 7.7 % → A 5.2 %.

**D6 (life bars): not a defect.** Unreal shows a bar over a selected or
hovered thing at full health (`select-citadel`, `click-citadel`,
`hover-prospector`). This is Swift's `showLifeBars` rule
(`hp < full - 0.5 || barFocus.contains(id)`), and the live Swift game draws
the bar too. The Swift still has none because `windowshot` never runs a
frame after `select`/`click`/`hovers`, and `barFocus` is set in the
per-frame update (`GameController.swift`, `world.barFocus = focus`).

**B9 (sky and haze):** the Unreal sky has volumetric clouds over a deeper
blue (the richer look the user asked for). Fixed 2026-10-05: the clouds ran
to their 50 km tracing distance and stopped on a flat line just over the
horizon; they now thin out toward it as Swift's (`M_AcCloud`: extinction ×
`smoothstep(0.05, 0.3, d.y)`). The far mountains showed as a pale outline
because the height fog greyed the sky dome but not the pilot veil's sky;
the fog now stops short of the dome (`FogCutoffDistance` 20 km) and both
the dome and the veil take Swift's haze rising from the horizon, so the far
ground melts into a hazy horizon as in Swift. pilot-ranger 6.7 → 6.1 %,
third-ranger 7.8 → 6.5 %. Left: the sky over 20° is a deeper blue than
Swift's.

**B10 (world beacons in fog): not a defect.** The additive objective
beacons (D8) are greyed where they stand on unexplored ground. Swift does
the same: its fog is an `SCNTechnique` pass over the finished colour that
finds each pixel's ground from the depth, and the beacons write no depth
(`GameScene+Beacons.swift`, `writesToDepthBuffer = false`).

**B10 / D7 (explored fog on maps): fixed 2026-10-05.** `--orders` adds
buildings by hand, and Swift's `fogSnapshot` calls `sim.lookNow()` before
the picture, so their sight explores. D8's `-AcOrders` staging now calls
`lookNow()` too. The explored area on `orders*` matches (diff 1.1 %).

**E9 / D1 (level banner): fixed 2026-10-05.** The title was drawn with
unit-name tinting on, which turned "PROSPECTOR" magenta. It is now one
amber line, as in Swift.

**F5 (playground):** fixed 2026-10-05:
- The 5000/5000 ore was not a saved setting. Unlimited is on by default in
  both games, and Unreal topped up each frame of the shot run, while Swift's
  `windowshot` never ticks. `TopUp` now skips shot runs (65 ore and 19/23
  supply, as in Swift). No stale playground save or saved unlimited setting
  existed (the Unreal sessions folder holds only `window.json`, and
  `GameUserSettings.ini` has no `AcPlayground` section), so nothing was
  deleted. `-AcPlaygroundTest` now uses a scratch store
  (`$TMPDIR/autocraft-test-sessions`, `AcSaves::Directory`) and never
  reads or writes the remembered unlimited setting.
- `-AcPlaygroundLayout` now warms after the layout (`UAcShotWarm::Warm`),
  as Swift lays out and then warms. The armies have closed in as in Swift.
- `playground-drive` (`-AcPilotYaw=0`, `drivePlayground`'s
  `pilotYaw = 0`) faces east, 090° against Swift's 092°.
- The top-down view now starts right of the 180-point palette column
  (`AAcRtsPawn::SetViewInset`: the local player's viewport, view points
  and `FreeView` size), as Swift's stage sits beside its list. While
  driving, the view is the whole window again (the cockpit overlay is laid
  out on it), and the column is hidden (2026-10-05), so it no longer
  covers the cockpit's left.

The playground scenes still grade D/C. Swift's `windowshot` has no list,
so the column and the inset view shift the picture 180 points. The
scoreboard also shows 8 sides against Swift's 2.

**HUD (all scenes):** the console strip differed by 1.2–3.0 % everywhere.
The dark tones were about +6/255 too bright (Slate on the Mac writes
pow(1/2.2)); fixed 2026-10-05 (`FAcHudStyle::Srgb`, Gamma22 HUD textures,
GAME-LAYER.md Progress): HUD % select-citadel 0.8, music-* 0.6-0.7. The perf panel shows numbers in Unreal, while Swift's headless
panel is empty (expected). Barlow is wider than DIN Condensed (D1).

## Notes for other chunks

- To add a scene, add one entry to `scenes.json`. C++ and the script pick
  it up with no rebuild. Put the scene's own flags in `ue`. They come
  before the defaults, so they win where a chunk reads the first match
  (`FParse::Value`).
- F3 (bench) can reuse `-AcWarm` and the `-AcScene` mechanism for its 9
  shots.
- A scene that stays `null` is a gap, not a bug in the table: stage it in
  the owner's code, then fill in `ue`. None is `null` now.
- `UAcSimSubsystem::StepThenPause(N)` runs exactly N fixed steps, then
  pauses. Use it for any still that must match Swift's hand-stepped stage.
