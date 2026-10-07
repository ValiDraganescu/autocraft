# New units

Three units join the roster: the **Peregrine** (an air-superiority
fighter), the **Atlas** (a heavy walker, the late-game capstone) and the
**Scorpion** (a burrowing mine). This document says what each one is for,
what it can do, how it levels, what gets tracked, and how it looks and
behaves.

Status, 2026-10-05: design agreed (see "Decisions"). The simulation side is
built (core, AI, tests); the models, cockpits, icons and sounds are not: the
game layer draws each kind as a neighbour (`unreal/Source/Autocraft/AcNewKinds.h`).
Every number here is a starting point for balance, not a final value.

Where the build still differs from the text below:

- The Quake stomp has no sound of its own: it plays the anchored Longbow's
  shell burst (`anchorhit`), the closest thud there is, until one is made.

**Scope: the Unreal game only.** The facts below come from the C++ in
`unreal/Source/AutocraftCore` and `unreal/Source/Autocraft`. The Swift
game is not a reference for this work, and it gets none of these units.

## Why these three

The current roster (`Rules::trains`, `unreal/Source/AutocraftCore/Private/Rules.cpp`):

| Building | Trains | Needs a Lab |
|---|---|---|
| Citadel | Prospector | |
| Garrison | Ranger, Comet, Juggernaut | Juggernaut |
| Foundry | Firefly, Longbow, Hailstorm | Longbow |
| Spacedock | Dropship, Kestrel | Kestrel |

The gaps this design fills:

1. **No air-superiority flyer.** The Kestrel carries a rail gun for air
   targets (`UnitStats::railgun`: 34 (+14 armoured) every 3.2 s), but it
   is a ground gunship that can duel, about 11 damage a second against the
   air. Every other answer to flyers shoots from the ground (Ranger,
   Hailstorm, Sentinel). Nothing flies out to clear the sky, so a Kestrel
   fleet can only be stopped where the defender already stands.
2. **No late-game anchor.** The most expensive unit is the Kestrel at
   150 ore and 100 MH. Long games, and eight-player games especially, have
   nothing big to build up to.
3. **Every unit fights in the open.** No unit hides, ambushes or holds
   ground on its own. Scouting and the order of an attack matter little.

Every unit today is either **light** or **armored**. Every flyer is
armored, so the Hailstorm's +8 against armored hits all of them. The
Peregrine is the first **light flyer**, which changes what counters it.

## The counter loop

```
Kestrel ──beats──▶ Atlas ──beats──▶ Longbow, Juggernaut, Firefly, Scorpion (when seen)
   ▲                                     
   │ beaten by                          
Peregrine ──beaten by──▶ Rangers, Sentinels (light target, cheap dps)
   │
   └─beats─▶ Dropship, Kestrel
Scorpion ──beats──▶ clumped light units, Prospectors, careless flyers
   ▲
   └─beaten by── anything that spots it first
                 (any unit within 2 cells, any Sentinel's sight)
```

## The units

### Peregrine: air-superiority fighter

A fast, light flyer from the Spacedock that fights only other flyers. It
hunts Kestrels and Dropships and cannot touch the ground.

| | Peregrine | For comparison |
|---|---|---|
| Built at | Spacedock, no Lab | Kestrel needs a Lab |
| Cost | 100 ore, 75 MH | Kestrel 150 / 100 |
| Train time | 28 s | Kestrel 43 s |
| Supply | 2 | Kestrel 3 |
| Hp, armour | 80, 0 | Kestrel 140, 0 |
| Speed | 5.6 | Kestrel 3.85, Dropship 3.5 |
| Radius | 0.5 | Kestrel 0.625 |
| Sight | 11 | |
| Traits | light, air, machine | |
| Weapon | **Seeker missiles**: 10 ×2 (+6 armoured), every 1.25 s, range 7, air only | |

Against a Kestrel that is about 25 damage a second, and the Kestrel's rail
gun does about 11 a second back (the Peregrine is light, so the rail gun's
+14 armoured does not apply). One Peregrine beats one Kestrel in about 5.5 s
and costs 70% as much. Rangers (about 10 a second each, unarmoured target)
and Sentinels (24 a second) shoot it down fast. The Hailstorm's bonus does
not apply to it, so flak alone is a weak answer: that is on purpose.

Behaviour:
- Flies over cliffs like the Dropship and the Kestrel. It follows the
  ground ahead and banks into turns like the other flyers.
- The missiles fly (`Rules::flight`: about `0.1 + d / 30`), so a fast
  target can outrun a salvo that is already in the air. Damage lands on
  arrival, like the Kestrel's rockets.
- It cannot attack the ground, so a Peregrine on attack-move ignores
  ground units and buildings, and only flies on toward flyers it sees.
- It can escort: given a follow order on a friendly flyer, it holds
  formation and engages flyers that come within 7 cells of the one it
  follows. With no order of its own (a mission), whoever owns it, it
  follows the closest friendly flyer that is not a Peregrine (a Kestrel, a
  Dropship, the one the player drives) by itself. It keeps that leader until
  it is gone or another flyer is less than half as far, then picks the
  closest again; any order replaces it, and it starts again once the
  Peregrine has none. With no friendly flyer it stays as it is
  (`Simulation::autoEscort`, `Unit::autoFollow`).

### Atlas: heavy assault walker

A slow, armoured two-legged walker from the Foundry. Its twin cannons
smash ground armies and buildings. It cannot shoot at the air, which is
its weakness.

| | Atlas | For comparison |
|---|---|---|
| Built at | Foundry with a Lab | Longbow needs a Lab too |
| Cost | 300 ore, 200 MH | Longbow 150 / 125 |
| Train time | 60 s | Longbow 32 s |
| Supply | 6 | Longbow 3 |
| Hp, armour | 500, 2 | Longbow 175, 1 |
| Speed | 2.2 | Longbow 3.15 |
| Radius | 1.25 | Longbow 0.875 |
| Sight | 11 | |
| Traits | armored, machine | |
| Weapon | **Twin siege cannons**: 25 ×2 (+15 armoured), every 2.0 s, range 7, ground only | |
| Splash | full damage to 0.5 cells, half to 1.0, enemies only | |
| Ability | **Quake stomp**: 30 damage to every enemy on the ground within 2 cells, which also slows them to half speed for 1.07 s; every 20 s | |

Against armoured targets with 1 armour that is about 39 damage a second,
plus the splash. It has no minimum range: the stomp is how it deals with
Fireflies and Comets that run under its guns.

Behaviour:
- The stomp fires by itself (AI and unpiloted units) when three or more
  enemy ground units stand within 2 cells, and on the ability key (R) when
  the player drives it.
- It walks; it cannot jump cliffs, and it cannot board a Dropship (it
  needs more slots than the Dropship has).
- Its splash spares friends, unlike the anchored Longbow's shell. The
  Atlas fights in the front line, so friendly fire would hurt every push.
- It turns slowly: the cannons sit on a torso that turns at 3 rad/s, and
  the legs turn the whole body at 1.5 rad/s.

### Scorpion: burrowing mine

A small, fast crawler from the Foundry. It is harmless while moving.
Buried, it is invisible to the enemy and fires one heavy strike at the
first enemy that comes close, ground or air, then reloads.

| | Scorpion | For comparison |
|---|---|---|
| Built at | Foundry, no Lab | Firefly |
| Cost | 75 ore, 25 MH | Firefly 100 / 0 |
| Train time | 21 s | Firefly 21 s |
| Supply | 2 | Firefly 2 |
| Hp, armour | 90, 0 | Firefly 90, 0 |
| Speed | 3.9 | Firefly 5.95 |
| Radius | 0.5 | |
| Sight | 8 | |
| Traits | light, machine | |
| Bury / dig out | 2.0 s each (`Rules::anchorTime` is 2.7 for the Longbow) | |
| Weapon (buried only) | **Sting**: 60 (+30 armoured), range 5, ground and air; 1.0 s to lock on, then 25 s to reload | |
| Splash | full damage to 0.5 cells, half to 1.25, enemies only | |

Hidden: a buried Scorpion is invisible to every enemy player except:
- an enemy unit or building within 2 cells of it sees it;
- an enemy Sentinel's whole sight sees it (the Sentinel becomes the
  game's detector);
- for 3 s after each strike, and while it reloads, it is visible to all.

Behaviour:
- It cannot attack unless buried. Buried, it cannot move.
- It locks onto the first enemy unit (not a building) to come within 5
  cells. The lock-on second is the victim's chance: a unit that leaves
  range in time is safe. The strike is a fast projectile, about 0.2 s.
- Units hit by a Scorpion they cannot see already go for where the shot
  came from (`Rules::unseenChase`, 3 s). They then see it once they come
  within 2 cells, so the existing chase rule is the natural answer.
- Its splash spares its own side, so Scorpions can sit inside a friendly
  ore line.
- A Dropship carries it (one slot; the beam does not heal it): the player's
  Dropship loads one it looks at, and the harass AI drops them at the
  enemy's ore lines.
- A Scorpion one strike from death is still a threat: it keeps its reload
  timer while digging out and moving, so moving it is no free reload.

## Leveling

The new units use the leveling that exists (`Leveling`, in
`unreal/Source/AutocraftCore/Public/Types.h` and `Private/Leveling.cpp`):

- Levels 1 to 10 per kind, per game, earned only while the player drives a
  unit of that kind. XP to reach level L is `30 × L × (L − 1)`.
- Level 1 is the hero bonus (`Rules::hero*`: ×1.5 damage, ×1.25 range,
  ×1.2 speed).
- Levels 2 to 10 each offer two picks, the same two every game (18 picks
  per kind). The pair at each level is two consecutive rows of the
  catalogue.
- XP comes from damage dealt (the victim's worth times the share of its hp
  taken; buildings earn half), as for every combat unit.

The picks below follow the pattern of the existing kinds (compare the
Kestrel and Hailstorm rows): a squad pick and a mobility pick at 2, sight
and plates at 3, damage and range at 4, self-repair and an aura at 5, and
so on, ending with a big squad bonus or a shield at 10. **NEW** marks an
effect the leveling cannot express today. Each needs a new `Stat`, `Filter`
or `Effect` case and its handling in `Simulation+Leveling.cpp`.

### Peregrine picks

| Level | One | Other |
|---|---|---|
| 2 | **Wingmen**: Nearby Peregrines fire 10% faster. | **Thrusters**: Every Peregrine flies 15% faster. |
| 3 | **Long-range radar**: Every Peregrine sees 20% farther. | **Armor plates**: +40 hp and +1 armor. |
| 4 | **Seeker heads**: +25% damage, and nearby Peregrines +15%. | **Long rails**: +1 range. |
| 5 | **Field repairs**: Mends itself 3 hp a second after 5 s without a hit. | **Escort**: Friendly flyers within 6 cells take 10% less damage. **NEW**: a `Filter::air` for `Scope::Around`. |
| 6 | **Pack hunters**: Nearby Peregrines fire 25% faster. | **Gunship killer**: Every Peregrine does 30% more damage to armored units. |
| 7 | **Double volley**: Every third volley does double damage. | **AP missiles**: Its missiles ignore armor and do +4 against armored. |
| 8 | **Flight school**: Every Peregrine fires 10% faster. | **Cheap airframe**: Peregrines cost 10% less. |
| 9 | **Afterburn**: The ability key gives 2 s at double speed, every 15 s. | **Bounty hunter**: Each of its kills gives the team 10% of the victim's cost. |
| 10 | **Air superiority**: Nearby Peregrines do 50% more damage. | **Shield**: A 40 hp shield takes hits first; it refills over 10 s after 7 s without a hit. |

### Atlas picks

Few Atlases stand together, so its picks lean on the driven unit and on
the army around it (`Scope::Around`) rather than on "nearby Atlases".

| Level | One | Other |
|---|---|---|
| 2 | **Autoloader**: Fires 15% faster. | **Servo legs**: Every Atlas walks 15% faster. |
| 3 | **Rangefinder**: Every Atlas sees 20% farther. | **Armor plates**: +100 hp and +1 armor. |
| 4 | **Heavy shells**: +25% damage. | **Long barrels**: +1 range. |
| 5 | **Crew mechanic**: Mends itself 5 hp a second after 5 s without a hit. | **Mechanic**: Friendly machines within 6 cells mend 2 hp a second. |
| 6 | **Bulwark**: Friendly units within 6 cells take 10% less damage. | **Wide shells**: Every Atlas's splash reaches 25% wider. |
| 7 | **Double volley**: Every third volley does double damage. | **AP shells**: Its shells ignore armor and do +6 against armored. |
| 8 | **Aftershock**: Its Quake stomp does 50% more damage and reaches 1 cell farther. **NEW**: a stomp stat. | **Cheap frame**: Atlases cost 10% less. |
| 9 | **Heavy industry**: The team's Foundries build 20% faster. | **Flak mount**: Its cannons also fire at flyers, at half damage. **NEW**: a pick that adds a target class. |
| 10 | **War march**: Friendly units within 6 cells move 15% faster. | **Shield**: A 150 hp shield takes hits first; it refills over 10 s after 7 s without a hit. |

Flak mount is the one pick that patches the Atlas's weakness, and only for
the driven one: a choice between a stronger army and a safer hero.

### Scorpion picks

| Level | One | Other |
|---|---|---|
| 2 | **Quick dig**: Every Scorpion buries and digs out 50% faster. **NEW**: a bury-time stat. | **Light frame**: Every Scorpion crawls 15% faster. |
| 3 | **Seismic sense**: Every Scorpion sees 20% farther. | **Armor plates**: +40 hp and +1 armor. |
| 4 | **Heavy charge**: +25% damage, and nearby Scorpions +15%. | **Long tail**: +1 range. |
| 5 | **Field repairs**: Mends itself 3 hp a second after 5 s without a hit. | **Deep burrow**: Buried, enemies see it only within 1 cell. **NEW**: a reveal-distance stat. |
| 6 | **Quick reload**: Every Scorpion reloads 20% faster. | **Worker hunter**: Every Scorpion does 30% more damage to light units. |
| 7 | **Shaped charge**: Its sting ignores armor and does +10 against armored. | **Twin sting**: Its strike hits a second target within 2 cells of the first. **NEW**: an effect. |
| 8 | **Minefield drill**: Every Scorpion reloads 10% faster. | **Cheap shell**: Scorpions cost 10% less. |
| 9 | **Sapper**: Its sting also hits buildings, at half damage. **NEW**: an effect. | **Bounty hunter**: Each of its kills gives the team 10% of the victim's cost. |
| 10 | **Nest**: Nearby Scorpions do 50% more damage. | **Shield**: A 40 hp shield takes hits first; it refills over 10 s after 7 s without a hit. |

The driven Scorpion is the one unit where the player fires by hand: the
fire button stings (once buried) at the crosshair's target, and the ability
key buries or digs out, the way the Longbow's anchors.

### New leveling cases, together

| Case | For |
|---|---|
| `Filter::air` | Escort (Peregrine) |
| `Stat::Stomp` (damage and reach of the Quake stomp) | Aftershock (Atlas) |
| `Effect::HitsAir { share }` | Flak mount (Atlas) |
| `Stat::BuryTime` | Quick dig (Scorpion) |
| `Stat::RevealRange` | Deep burrow (Scorpion) |
| `Effect::SecondTarget { radius }` | Twin sting (Scorpion) |
| `Effect::HitsBuildings { share }` | Sapper (Scorpion) |

Adding 54 picks changes `Leveling::balance()` and so
`Leveling::signature()`. That is expected: games tracked before and after
fall under different signatures.

## Tracking

Each pick's worth is measured from what the player does with the driven
unit. The record exists in the core: `Tally` per kind, stamps per level
reached (`Simulation::stamp`), and the `Standing` of every player at each
stamp (`unreal/Source/AutocraftCore/Public/Types.h`). The new kinds get all
of it for free: seconds driven, takeovers, damage to units and buildings,
kills, buildings razed, damage taken and shielded, deaths.

New `Tally` fields, so the new picks can be judged:

| Field | Counts | Judges |
|---|---|---|
| `airKills` | Flyers killed (a share of `kills`) | Peregrine picks; Atlas Flak mount |
| `stomps`, `stompHits` | Quake stomps used, and units they hit | Aftershock |
| `strikes`, `strikeHits` | Stings fired, and units they hit (splash included) | Scorpion picks |
| `hiddenSeconds` | Seconds buried and unseen by any enemy | Deep burrow; how often Scorpions get spotted |

**First:** the tracking database is ported (2026-10-06): `unreal/PORTING.md`
("Database.swift, Tracking*.swift") says where it lives. Every window game
writes its tallies and stamps, the six new fields included
(`kinds.air_kills`, `stomps`, `stomp_hits`, `strikes`, `strike_hits`,
`hidden_seconds`), and `make balance` reads them. The new picks can be
balanced from it.

## Looks

All three follow the game's existing look: hard-surface frontier
machinery, rigid parts (no skeletons), team colour through the mask
channel and the 8-colour palette (`GAME-LAYER.md` §3.4), lamps on the
emissive material so they bloom. Each comes as a hull model, a cockpit
model for first-person driving, a scaffold-free training reveal, an icon
and a death (shatter and debris). Aim for 5 to 15 moving parts each
(`GAME-LAYER.md` §3.3).

Concepts chosen 2026-10-05 (`art/models/<unit>/concepts/prompts.md`):
Peregrine `dart`, Atlas `foundry`, Scorpion `scorpion`. Where a concept
differs from the text below, the concept wins.

### Peregrine

A narrow dart of a fighter: a very long needle nose, swept-back wings with
two missiles on a rail under each, two tall tail fins canted outward, and
two engines side by side at the back that glow cold blue (the `dart`
concept). About 327 cm long, longer than the Kestrel's 226 cm, for the
dart's needle nose (the user's call, 2026-10-05: keep it, tune after
playing). Its cockpit is a small dark
canopy behind the nose.

- Moving parts: two wing flaps, two canted fins, the engine glow (scale
  and brightness with speed), the two missile rails (a missile vanishes
  from its rail on launch and reloads in view).
- Flight: banks harder than the other flyers (up to 60°), with a vapour
  trail off the wingtips in hard turns and at top speed.
- Sound: the flyby should recall the Wraith Darts of Stargate Atlantis
  (the user's wish): an eerie, rising, almost organic shriek as it passes,
  made new with ElevenLabs, not taken from the show. A sharp "fssh" per
  missile, and a voice line set in the style of the other units
  (`v<kind>`, `<kind>death`, `<kind>move`).

### Atlas

A two-legged walker about twice a Longbow's height: legs with
forward-bending knees and broad three-toed feet (the `foundry` concept), a
riveted boiler torso with hazard stripes, cable bundles and two exhaust
stacks, round shoulder pods, and two long cannons on top. A searchlight under the cockpit slit, and amber warning lamps on
the knees. Heat vents on the back glow after each volley.

- Moving parts: hips, knees and ankles of both legs (procedural walk
  cycle from distance walked, like the Ranger's), the torso yaw, the two
  cannon pitches and recoils, the vents' glow.
- Walk: heavy and slow, each footfall shakes the camera slightly when it
  is close, and leaves a dust puff and a footprint decal that fades.
- Quake stomp: it rises on one leg and drives it down; a ring of dust
  and a shock ring spread to 2 cells; a deep thud.
- Death: it drops to its knees, then topples forward and shatters.

### Scorpion

A low, six-legged crawler the size of a Prospector, with a segmented
armoured back and a raised tail ending in a single charge launcher.

- Moving parts: six legs (an insect gait, three legs at a time), the tail
  (curls up to aim, snaps forward to fire), the back plates.
- Burying: its legs dig, it sinks into the ground in 2 s, and a mound of
  disturbed soil is left. To its owner it shows as the mound plus a faint
  team-coloured outline. To an enemy who sees it, the mound and a red
  outline.
- Sting: the tail rises out of the ground for the lock-on second with a
  rising chirp, a red laser line from the tail to the target, then the
  shot and a burst on impact.
- Reloading: a tail lamp blinks slowly while it reloads, and steadily when
  ready.

## How the AI uses them

The Commander picks its army in `Commander::counters` and by style
(`Player::Style`: bio, mech, harass), in
`unreal/Source/AutocraftCore/Private/Commander+Intel.cpp` and
`Commander.cpp`.

- **Peregrines** answer armed enemy flyers: about one Peregrine per armed
  enemy flyer seen, up to 8, once a Spacedock stands. They escort the
  AI's own Dropships and Kestrels, and they never join a ground push.
- **Atlases**: the mech style adds one Atlas per 40 supply of army once a
  Foundry has a Lab, up to 3. The other styles build one late in the game
  when the bank is high. Enemy Atlases make the AI want Kestrels (the
  existing `kestrels` counter grows when it sees one).
- **Scorpions**: a defending AI buries two at each of its ore lines and
  one at each choke it holds: the foot of each ramp on its side of each
  of its bases (the ramp's end on the base's level, nearer to that base than
  to any enemy base) and each point of its `hold` objectives
  (`Commander::chokes`). The harass style drops them at the enemy's
  ore lines from Dropships. When an AI is hit by an unseen shooter it
  brings a Sentinel or a unit to the spot (the existing unseen-chase rule
  does part of this).

Each of these needs isolated scenario tests in `unreal/core-tests` (an
Atlas against a Longbow line, a Peregrine against a Kestrel, a buried
Scorpion and a walking Ranger squad), never full AI-against-AI games.

## Where it lands in the code

Append the new kinds at the **end** of `UnitKind` and the new picks at the
end of `Perk`. A case without a payload is stored as its index, so
appending keeps saved games loading.

- Core (`unreal/Source/AutocraftCore`): `Rules.h` (`UnitKind`, the size of
  `UnitStats::byKind` from 9 to 12, the stats, `slots`, `needsLab`,
  `flight`, `vision`, `trains`), `Types.h` (`Perk`, `Tally`, the new
  leveling cases), `Leveling.cpp` (the catalogue and the effects),
  `Simulation.cpp` (bury, hidden, sting, stomp, air-only targeting),
  `Vision.cpp` (hidden units and Sentinel detection), `Pilot.cpp` and
  `Pilot+Kinds.cpp` (the abilities and firing when driven), the
  `Commander*` files.
- Game (`unreal/Source/Autocraft`): the files that name every kind today,
  found with `grep -l hailstorm`: the pose files (`AcPose*`), the cockpits
  (`AcCockpitKinds`), effects (`AcEffectsShots`, `AcEffectsDeaths*`,
  `AcEffectsShatter*`), audio (`AcAudioDirector`), the console and pilot
  text (`AcConsoleInfo`, `AcPilotText`, `AcPilotCamera`), the playground
  (`AcPlayground`) and the minimap (`SAcMinimap`).
- Golden tests: `unreal/core-tests/golden/*.json` were computed by the
  Swift game. The new kinds change the leveling golden and some others.
  From here the C++ core is its own reference: regenerate the goldens from
  it and stop comparing with Swift.
- Models: the existing meshes came from the Swift export
  (`Content-src/models/*.usda`, imported by `Tools/Editor/import_models.py`),
  which is retired. The new units are modelled in Unreal: an editor Python
  script per model builds its parts with Geometry Script and writes the same
  `SM_<part>__<material>` meshes and catalog entries the renderer, the pose
  code and the ray code read (the `ringshadow-model-pipeline` skill).

## Decisions

The user's answers, 2026-10-05:

1. **Names:** Peregrine, Atlas and Scorpion.
2. **Meshes:** modelled in Unreal with Geometry Script (above).
3. **Atlas cap:** none.
4. **Friendly fire:** none. The Scorpion's splash spares its own side, so
   the Safe fuse pick became Sapper.
5. **Detection:** the Sentinel is the only detector.
6. **Tracking:** done (2026-10-06): the SQLite tracking store is ported to
   the Unreal game; see "Tracking" above.
