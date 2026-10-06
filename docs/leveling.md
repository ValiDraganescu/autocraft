# Pilot leveling

The Prospector was designed first. Every other kind follows the same template,
with its own picks (see "Every kind"). Built: the record, XP for every
kind, picking, the effects framework and its building blocks, all 216
picks of the twelve kinds (the Kestrel and the Hailstorm since 2026-10-04,
the Peregrine, the Atlas and the Scorpion since 2026-10-05, see
[new-units.md](new-units.md)), and the tracking of the picks for balancing
(in the Unreal game since 2026-10-06: "In the Unreal game", below).
The tables below cover every kind; the catalogue is `Leveling.cpp`.
"How it fits the code" names the API.

## The idea

The more the player drives a kind of unit during a game, the better that
kind gets, but only while the player drives it. AI-run units never level.
The goal is to give the player a reason to drive the units that are dull
to play, the Prospector first.

## Decided

- **One game.** Every kind starts each game at level 1.
- **A pick at every level.** Each level-up offers two unlocks, and the
  player keeps one.
- **Always on.** There is no switch to turn it off.
- **Tracked.** The picks and the games they are made in go to a local
  SQLite database, for balancing (see "Tracking"). Window games write to
  it as they go; `make balance` reads it.

## What is there today

The driven unit is already a hero, at a flat rate (`Rules.hero*` in
`Sources/GameCore/State.swift`), and only while driven:

| Hero stat | Today | Plain Prospector |
| --- | --- | --- |
| Speed | ×1.2 | ×1 |
| Ore a trip | 25 | 5 |
| MH a trip | 20 | 4 |
| Build and repair rate | ×2 | ×1 |
| Weapon damage | ×1.5 | ×1 |
| Weapon range | ×1.25 | ×1 |

Level 1 is today's hero. Each level adds the unlock the player picked on
top of it.

## Rules

- **The level belongs to the pilot, not the machine.** It is kept for each
  kind (Prospector, Ranger, Firefly and so on) for the rest of the game. It
  applies to whichever unit of that kind the player takes over. When the
  driven Prospector dies, the level and the picks stay. Driving a Ranger for a
  while leaves the Prospector level where it was.
- **It is saved with the game.** Quitting and coming back to the same game
  keeps the levels. A new game, after a win, a loss or a map change, starts
  every kind at level 1 again.
- **XP comes from work done, not time spent.** Sitting in the cab earns
  nothing.
- **Only while driven.** XP is earned and unlocks apply only when the
  player is driving the unit. The wallpaper (nobody drives), the AI, the
  `simulate` batches and the tests that run without a pilot are left as
  they are. Unlocks that help the team (Foreman, Overtime and the rest)
  also work only while the player is driving the Prospector.

## Prospector: earning XP

1 XP is worth about one resource of work.

| Work | XP |
| --- | --- |
| Ore dropped off at a Citadel | 1 an ore |
| MH dropped off | 1.25 an MH |
| Welding a building | its cost × the share of its build time this Prospector welded |
| Repairing a building or a unit | its cost × the share of its hp mended × ½ |
| Damage dealt | the target's cost × the share of its hp taken (a kill's worth is its cost); buildings × ½ |

A driven Prospector at level 1 that only mines earns about 100 XP a minute.

## Prospector: levels

Reaching level L takes `30 × L × (L − 1)` XP in all. The cap is level 10.

| Level | XP in all | Minutes of mining at level 1 pace |
| --- | --- | --- |
| 2 | 60 | under 1 (three trips) |
| 3 | 180 | 2 |
| 5 | 600 | 6 |
| 7 | 1,260 | 13 |
| 10 | 2,700 | 27 |

Early levels come fast, so the first picks happen in the opening minutes.
Level 10 takes most of a long game spent mostly in the Prospector. Picks that
speed up mining make the later levels come a bit sooner. Tune the factor
of 30 against how long games really run.

## Prospector: the picks

Each level offers the same two unlocks every game, so a player learns the
pairs and can plan around them. Most picks help the team's Prospectors, so
driving the Prospector is about running the base more than being one strong
unit.

- **Nearby:** the team's Prospectors within 6 cells of the driven Prospector, the driven
  one included.
- **Every Prospector:** every Prospector of the team, anywhere on the map, the driven one
  included.

| Level | One | Or the other |
| --- | --- | --- |
| 2 | **Quick drill:** nearby Prospectors drill 10% faster | **Light frame:** every Prospector walks 50% faster |
| 3 | **Rig master:** every Prospector pumps MH 20% faster | **Plating:** the driven Prospector gets +20 hp and +1 armor |
| 4 | **Big haul:** the driven Prospector carries 35 ore or 28 MH a trip, up from 25 and 20, and nearby Prospectors carry 7 ore or 6 MH, up from 5 and 4 | **Fast welder:** the driven Prospector builds and repairs at ×3, up from ×2 |
| 5 | **Field welder:** the driven Prospector repairs machines, fast: a full repair takes half the machine's training time | **Group repair:** every friendly machine within 6 cells mends slowly, 1 hp a second |
| 6 | **Foreman:** nearby Prospectors drill 50% faster | **Site boss:** every Prospector builds 30% faster |
| 7 | **Rich vein:** every third ore trip of the driven Prospector carries double | **Cutting torch:** the driven Prospector's weapon does double damage |
| 8 | **Overtime:** every Prospector drills 10% faster | **Prefab:** buildings the driven Prospector places cost 10% less |
| 9 | **Cliff hop:** the driven Prospector jumps up and down cliffs like a Comet | **Supply chief:** the team's Hab Domes give 10 supply, up from 8 |
| 10 | **Union crew:** nearby Prospectors carry 10 ore or 8 MH, up from 5 and 4 | **Shield:** the driven Prospector gets a 20 hp shield that takes hits first and refills 2 a second after 7 s without a hit |

How picks combine:

- Speed-ups to the same thing add up. Quick drill, Foreman and Overtime
  together make nearby Prospectors drill 70% faster, so a drill takes 1/1.7 of the
  time.
- For a load, the biggest one counts. With Big haul and Union crew, nearby
  Prospectors carry 10 ore, not 12, and 8 MH. The driven Prospector keeps its 35
  and 28.
- Machines are what a Prospector mends: Prospectors, Fireflies, Longbows and
  Dropships. Rangers and Comets are not repaired.

Balance notes:

- Light frame helps every base at once. Walking is about half of each
  trip, so it is worth about a quarter more income for the whole team.
- Stacked, the picks are strong. With Light frame, Quick drill, Foreman,
  Overtime and Union crew, the Prospectors near the player bring in about three
  times as much. That base's patches also run out about three times
  sooner.
- Only the player levels, so leveling tips the game toward the player
  against the AI. That is the point, but watch how far it goes.

## Every kind

Each kind levels the way the Prospector does: its own level for the game, a pick
of one from two at levels 2 to 10, the same XP curve, and it applies only
while the player drives that kind. Help for the team also works only
while the player drives that kind.

- **Nearby:** the team's units of the same kind within 6 cells of the
  driven one, the driven one included.
- **Every:** every unit of that kind on the team, anywhere on the map, the
  driven one included.
- **Machines:** Prospectors, Fireflies, Longbows, Dropships, Kestrels,
  Hailstorms, Peregrines, Atlases and Scorpions. **Bio:** Rangers, Comets
  and Juggernauts.
- A pick in the tables below that names no one is for the driven unit.

### XP for every kind

1 XP is still worth about one resource of work. A unit's cost is its
ore plus its MH.

| Work | XP |
| --- | --- |
| Damage dealt | the target's cost × the share of its hp taken, so a kill is worth its cost; against buildings × ½ |
| Healing (Dropship) | the target's cost × the share of its hp healed × ½ |
| Carrying (Dropship) | 10% of a passenger's cost, when it is dropped 10 or more cells from where it boarded |
| Mining, building and repairing (Prospector) | as in the Prospector's table |

A fighter earns its XP in bursts. Killing one enemy Ranger is worth 50
XP, about 30 seconds of Prospector mining, and a Longbow is worth 275.

### The template

Every kind's picks have the same shape as the Prospector's, level by level. The
**main job** is what the kind is for: mining for the Prospector, firing for a
fighter, healing for the Dropship.

| Level | One | Or the other |
| --- | --- | --- |
| 2 | Nearby: +10% at the main job | Every: moves faster |
| 3 | Every: +20% at a second job | The driven one: about +45% hp, and +1 armor |
| 4 | The driven one: +40% at the main job, and nearby a smaller share | The driven one: better at its second job |
| 5 | Strong help to one unit, or a new trick | Slow help to everyone within 6 cells |
| 6 | Nearby: a big boost to the main job | Every: +30% at another job |
| 7 | The driven one: every third action counts double | The driven one: a better weapon |
| 8 | Every: +10% at the main job | The kind costs 10% less |
| 9 | The driven one: a new way to move | The team: an economy boost |
| 10 | Nearby: the main job's biggest boost | The driven one: a shield of about 45% of its hp, refilling fully over 10 s after 7 s without a hit |

Fighters get smaller numbers than the Prospector at levels 6 and 10: firing 25%
faster rather than 50%, and 50% more damage rather than double the
yield. Damage snowballs, since more damage means fewer losses and so more
damage, while income is capped by the patches.

The ability key holds at most one pick per kind. The Longbow's key
anchors and the Dropship's loads, so their picks have no ability.

### Ranger

Main job: firing. Second job: staying alive.

| Level | One | Or the other |
| --- | --- | --- |
| 2 | **Drill sergeant:** nearby Rangers fire 10% faster | **Light kit:** every Ranger walks 20% faster |
| 3 | **Combat drill:** every Ranger gets +10 hp | **Flak vest:** +20 hp and +1 armor |
| 4 | **Hollow points:** +40% damage, and nearby Rangers +15% | **Long barrel:** +2 range |
| 5 | **Surge:** the ability key gives 8 s of +50% fire rate and speed, for 10 hp, every 15 s | **Field medic:** friendly bio units within 6 cells mend 1 hp a second |
| 6 | **Squad leader:** nearby Rangers fire 25% faster | **Quartermaster:** the team's Garrison train Rangers 30% faster |
| 7 | **Burst fire:** every third shot does double damage | **Armor-piercing:** shots ignore armor and do +5 against armored |
| 8 | **Esprit de corps:** every Ranger fires 10% faster | **Recruitment:** Rangers cost 10% less |
| 9 | **Jump pack:** jumps up and down cliffs like a Comet | **Bastion drill:** the team's Bastions hold 6, up from 4 |
| 10 | **Band of brothers:** nearby Rangers do 50% more damage | **Shield:** 20 hp |

### Comet

Main job: raiding. Second job: healing out of combat.

| Level | One | Or the other |
| --- | --- | --- |
| 2 | **Pack tactics:** nearby Comets fire 10% faster | **Fleet foot:** every Comet runs 15% faster |
| 3 | **Nanomeds:** every Comet heals out of combat 20% faster | **Padded suit:** +25 hp and +1 armor |
| 4 | **Twin magnums:** +40% damage, and nearby Comets +15% | **Long jump:** jumps reach 50% farther |
| 5 | **Pulse mine:** the ability key throws a grenade at the crosshair, 20 damage within 1.5 cells, every 14 s | **Spotter:** sees 50% farther, for the whole team |
| 6 | **Raid party:** nearby Comets fire 25% faster | **Demolition:** every Comet does 30% more damage to buildings |
| 7 | **Double tap:** every third volley does double damage | **Light killer:** +5 more against light, so +10 in all |
| 8 | **Hit and run:** every Comet fires 10% faster | **Cheap kit:** Comets cost 10% less |
| 9 | **Booster:** 3 s at +50% speed after each jump | **Bounty:** each of its kills gives the team 10% of the victim's cost |
| 10 | **Death squad:** nearby Comets do 50% more damage | **Shield:** 25 hp |

### Firefly

Main job: burning. Second job: the flame's reach.

| Level | One | Or the other |
| --- | --- | --- |
| 2 | **Hot burners:** nearby Fireflies fire 10% faster | **Turbo:** every Firefly drives 15% faster |
| 3 | **Fuel line:** every Firefly's flame reaches 20% farther | **Armor plates:** +40 hp and +1 armor |
| 4 | **Napalm:** +40% damage, and nearby Fireflies +15% | **Wide nozzle:** its flame is 50% wider |
| 5 | **Burn:** what its flame hits burns for 3 s, 4 damage a second | **Mechanic:** friendly machines within 6 cells mend 1 hp a second |
| 6 | **Pack:** nearby Fireflies fire 25% faster | **Spread:** every Firefly's flame is 30% wider |
| 7 | **Flashpoint:** every third flame does double damage | **Hot core:** its flame ignores armor and does +8 against bio |
| 8 | **Overdrive:** every Firefly fires 10% faster | **Cheap chassis:** Fireflies cost 10% less |
| 9 | **Boost:** the ability key gives 2 s at double speed, every 15 s | **Scrap:** every Firefly the team loses refunds 25% of its cost |
| 10 | **Firestorm:** nearby Fireflies do 50% more damage | **Shield:** 40 hp |

### Juggernaut

Main job: breaking armor. Second job: the slow.

| Level | One | Or the other |
| --- | --- | --- |
| 2 | **Shell drill:** nearby Juggernauts fire 10% faster | **Long stride:** every Juggernaut walks 20% faster |
| 3 | **Shock rounds:** every Juggernaut's slow lasts 20% longer | **Heavy plate:** +55 hp and +1 armor |
| 4 | **Breacher:** +40% damage, and nearby Juggernauts +15% | **Deep slow:** its slow drops a target to a third of its speed, not half |
| 5 | **Surge:** the ability key gives 8 s of +50% fire rate and speed, for 20 hp, every 15 s | **Bulwark:** friendly units within 6 cells take 10% less damage |
| 6 | **Siege breakers:** nearby Juggernauts fire 25% faster | **Behemoth:** every Juggernaut gets +40 hp |
| 7 | **Heavy volley:** every third grenade does double damage | **Fragment:** its grenades splash 1 cell around for half damage |
| 8 | **Shock troops:** every Juggernaut fires 10% faster | **Field kit:** Juggernauts cost 10% less |
| 9 | **Jump pack:** jumps up and down cliffs like a Comet | **Field research:** Garrison upgrades cost 25% less and finish 25% sooner |
| 10 | **Wrecking crew:** nearby Juggernauts do 50% more damage | **Shield:** 55 hp |

### Dropship

Main job: healing. Second job: energy. Its ability key loads and unloads.

| Level | One | Or the other |
| --- | --- | --- |
| 2 | **Triage:** nearby Dropships heal 10% faster | **Boosters:** every Dropship flies 20% faster |
| 3 | **Lifeline:** every Dropship regains energy 20% faster | **Armor plates:** +65 hp and +1 armor |
| 4 | **Surgeon:** heals 40% faster, and nearby Dropships 15% faster | **Cargo bay:** carries 12 slots, up from 8 |
| 5 | **Nanite beam:** heals machines too, at the same rate | **Healing field:** friendly bio units within 6 cells mend 1 hp a second |
| 6 | **Medical team:** nearby Dropships heal 25% faster | **Efficient:** every Dropship spends 30% less energy healing |
| 7 | **Double beam:** heals two units at once | **Combat drop:** units it drops get 4 s of +30% speed and fire rate |
| 8 | **Field hospital:** every Dropship heals 10% faster | **Cheap hull:** Dropships cost 10% less |
| 9 | **Cruise:** flies 50% faster while not healing | **Spacedock refit:** the team's Spacedocks build Dropships 25% faster |
| 10 | **Medical corps:** nearby Dropships heal 50% faster | **Shield:** 65 hp |

### Longbow

Main job: shelling. Second job: anchoring. Its ability key anchors.

| Level | One | Or the other |
| --- | --- | --- |
| 2 | **Spotter:** nearby Longbows fire 10% faster | **Treads:** every Longbow drives 15% faster |
| 3 | **Quick anchor:** every Longbow anchors and unanchors 20% faster | **Reactive armor:** +80 hp and +1 armor |
| 4 | **Shaped charge:** +40% damage, and nearby Longbows +15% | **Long gun:** +2 range anchored |
| 5 | **Crew mechanic:** mends itself 3 hp a second after 5 s without a hit | **Mechanic:** friendly machines within 6 cells mend 1 hp a second |
| 6 | **Battery:** nearby Longbows fire 25% faster | **Heavy shells:** every Longbow's splash reaches 30% wider |
| 7 | **Double shot:** every third shell does double damage | **AP shells:** its shells ignore armor and do +10 against armored |
| 8 | **Drilled crews:** every Longbow fires 10% faster | **Cheap hull:** Longbows cost 10% less |
| 9 | **Hull down:** anchors and unanchors at once | **Heavy industry:** the team's Foundries build 20% faster |
| 10 | **Artillery park:** nearby Longbows do 50% more damage | **Shield:** 80 hp |

### Kestrel

Main job: rocket strikes on the ground. Second job: getting around (it
flies). Its ability key is free, so Afterburn takes it.

| Level | One | Or the other |
| --- | --- | --- |
| 2 | **Wingman:** nearby Kestrels fire 10% faster | **Afterburners:** every Kestrel flies 15% faster |
| 3 | **Keen optics:** every Kestrel sees 20% farther | **Armor plates:** +60 hp and +1 armor |
| 4 | **Hellfire:** +40% damage, and nearby Kestrels +15% | **Long rails:** +1 range |
| 5 | **Field repairs:** mends itself 3 hp a second after 5 s without a hit | **Mechanic:** friendly machines within 6 cells mend 1 hp a second |
| 6 | **Squadron:** nearby Kestrels fire 25% faster | **Worker hunter:** every Kestrel does 30% more damage to light units |
| 7 | **Double salvo:** every third salvo does double damage | **AP rockets:** its rockets ignore armor and do +4 against armored |
| 8 | **Flight drill:** every Kestrel fires 10% faster | **Cheap airframe:** Kestrels cost 10% less |
| 9 | **Afterburn:** the ability key gives 2 s at double speed, every 15 s | **Bounty hunter:** each of its kills gives the team 10% of the victim's cost |
| 10 | **Strike wing:** nearby Kestrels do 50% more damage | **Shield:** 60 hp |

### Hailstorm

Main job: flak at flyers. Second job: the burst's splash.

| Level | One | Or the other |
| --- | --- | --- |
| 2 | **Loaders:** nearby Hailstorms fire 10% faster | **Tracks:** every Hailstorm drives 15% faster |
| 3 | **Wide burst:** every Hailstorm's flak splashes 20% wider | **Armor plates:** +60 hp and +1 armor |
| 4 | **Proximity fuse:** +40% damage, and nearby Hailstorms +15% | **Long barrels:** +1 range |
| 5 | **Crew mechanic:** mends itself 3 hp a second after 5 s without a hit | **Mechanic:** friendly machines within 6 cells mend 1 hp a second |
| 6 | **Flak battery:** nearby Hailstorms fire 25% faster | **Sky watch:** every Hailstorm sees 30% farther |
| 7 | **Double burst:** every third burst does double damage | **AP flak:** its flak ignores armor and does +4 against armored |
| 8 | **Drilled gunners:** every Hailstorm fires 10% faster | **Cheap chassis:** Hailstorms cost 10% less |
| 9 | **Walker legs:** climbs up and down cliffs like a Comet | **Heavy industry:** the team's Foundries build 20% faster |
| 10 | **Flak wall:** nearby Hailstorms do 50% more damage | **Shield:** 60 hp |

### Peregrine

Main job: missiles at flyers. Second job: escorting. Its ability key is the afterburner.

| Level | One | Or the other |
| --- | --- | --- |
| 2 | **Wingmen:** nearby Peregrines fire 10% faster | **Thrusters:** every Peregrine flies 15% faster |
| 3 | **Long-range radar:** every Peregrine sees 20% farther | **Armor plates:** +40 hp and +1 armor |
| 4 | **Seeker heads:** +25% damage, and nearby Peregrines +15% | **Long rails:** +1 range |
| 5 | **Field repairs:** mends itself 3 hp a second after 5 s without a hit | **Escort:** friendly flyers within 6 cells take 10% less damage |
| 6 | **Pack hunters:** nearby Peregrines fire 25% faster | **Gunship killer:** every Peregrine does 30% more damage to armored units |
| 7 | **Double volley:** every third volley does double damage | **AP missiles:** its missiles ignore armor and do +4 against armored |
| 8 | **Flight school:** every Peregrine fires 10% faster | **Cheap airframe:** Peregrines cost 10% less |
| 9 | **Afterburn:** the ability key gives 2 s at double speed, every 15 s | **Bounty hunter:** each of its kills gives the team 10% of the victim's cost |
| 10 | **Air superiority:** nearby Peregrines do 50% more damage | **Shield:** a 40 hp shield takes hits first; it refills over 10 s after 7 s without a hit |

### Atlas

Main job: shelling. Second job: the Quake stomp, which the key (R) fires. Few Atlases stand together, so its picks lean on the driven one and on the army around it (`Scope::Around`).

| Level | One | Or the other |
| --- | --- | --- |
| 2 | **Autoloader:** fires 15% faster | **Servo legs:** every Atlas walks 15% faster |
| 3 | **Rangefinder:** every Atlas sees 20% farther | **Armor plates:** +100 hp and +1 armor |
| 4 | **Heavy shells:** +25% damage | **Long barrels:** +1 range |
| 5 | **Crew mechanic:** mends itself 5 hp a second after 5 s without a hit | **Mechanic:** friendly machines within 6 cells mend 2 hp a second |
| 6 | **Bulwark:** friendly units within 6 cells take 10% less damage | **Wide shells:** every Atlas's splash reaches 25% wider |
| 7 | **Double volley:** every third volley does double damage | **AP shells:** its shells ignore armor and do +6 against armored |
| 8 | **Aftershock:** its Quake stomp does 50% more damage and reaches 1 cell farther | **Cheap frame:** Atlases cost 10% less |
| 9 | **Heavy industry:** the team's Foundries build 20% faster | **Flak mount:** its cannons also fire at flyers, at half damage |
| 10 | **War march:** friendly units within 6 cells move 15% faster | **Shield:** a 150 hp shield takes hits first; it refills over 10 s after 7 s without a hit |

### Scorpion

Main job: the sting. Second job: staying hidden. Its ability key buries and digs out.

| Level | One | Or the other |
| --- | --- | --- |
| 2 | **Quick dig:** every Scorpion buries and digs out 50% faster | **Light frame:** every Scorpion crawls 15% faster |
| 3 | **Seismic sense:** every Scorpion sees 20% farther | **Armor plates:** +40 hp and +1 armor |
| 4 | **Heavy charge:** +25% damage, and nearby Scorpions +15% | **Long tail:** +1 range |
| 5 | **Field repairs:** mends itself 3 hp a second after 5 s without a hit | **Deep burrow:** buried, enemies see it only within 1 cell |
| 6 | **Quick reload:** every Scorpion reloads 20% faster | **Worker hunter:** every Scorpion does 30% more damage to light units |
| 7 | **Shaped charge:** its sting ignores armor and does +10 against armored | **Twin sting:** its strike hits a second target within 2 cells of the first |
| 8 | **Minefield drill:** every Scorpion reloads 10% faster | **Cheap shell:** Scorpions cost 10% less |
| 9 | **Sapper:** its sting also hits buildings, at half damage | **Bounty hunter:** each of its kills gives the team 10% of the victim's cost |
| 10 | **Nest:** nearby Scorpions do 50% more damage | **Shield:** a 40 hp shield takes hits first; it refills over 10 s after 7 s without a hit |

### What the picks are made of

Most picks are the same few effects with different numbers, so the code
needs a small set of building blocks rather than 63 one-offs:

- **Stat changes,** for the driven one, nearby or every: fire rate,
  damage, damage against a class (armored, light, bio, buildings), armor
  ignored, hp, armor, speed, range, sight, flame reach and width, splash
  size, anchor time, the slow's length and strength, drill and MH time,
  load, build, repair and heal rates, energy use and regrowth, healing out
  of combat, jump reach, cargo slots.
- **The team's economy:** training time of a kind, the cost of a kind, upgrade
  cost and time, supply per Hab Dome, Bastion size, a refund on a loss, a
  bounty on a kill, and the cost of the buildings it places.
- **Every third action counts double:** one counter on the driven unit.
- **Mending auras** within 6 cells: machines or bio, 1 hp a second. Field
  medic and Healing field are the same aura, and Mechanic and Group repair
  are the same aura for machines. Bulwark cuts the damage taken.
- **Ability-key bursts:** Sprint, Boost and Surge are one effect: for a few
  seconds, faster movement and maybe fire rate, sometimes for hp, then a
  cooldown.
- **Shield** and **self-mending** after some seconds without a hit.
- **Cliff jumping:** the Comet's jump, given to the Prospector, the Ranger and
  the Juggernaut.
- **One-offs:** Pulse mine, Burn, Fragment, Double beam, Nanite beam,
  Combat drop, Cruise, Booster, Hull down and Field welder.
- **For the Peregrine, the Atlas and the Scorpion** (54 picks, three new
  stats, one new filter and three new effects):
  - `Filter::air`: Escort is `Scope::Around(air)`, friendly flyers within 6
    cells take 10% less damage;
  - `Stat::Stomp`: Aftershock, the Quake stomp's damage (`.percent`) and
    reach (`.plus`);
  - `Stat::BuryTime`: Quick dig, a rate on burying and digging out;
  - `Stat::RevealRange`: Deep burrow, how close an enemy must come to see a
    buried Scorpion (`.plus`, on `Rules::revealRange`);
  - `Effect::HitsAir(share)`: Flak mount, the driven Atlas's cannons also
    fire at flyers;
  - `Effect::SecondTarget(radius)`: Twin sting, the sting hits a second enemy
    near the first;
  - `Effect::HitsBuildings(share)`: Sapper, the sting also hits buildings.
  The picks only work while the player drives the kind, like every pick: the
  AI's Atlases, Peregrines and Scorpions use the base numbers.

## On screen

- **Cab:** the cockpit console (FPS, and the flat cab in RTS and TPS)
  shows the level and an XP bar next to the unit's name.
- **Level-up:** a short banner, "Prospector level 4", with a sound. The two
  unlocks appear as cards on the console, each with an icon, a name and a
  one-line effect. They are picked with a click or a key, chosen from the
  keys that are free while driving (the digits are taken).
- **The game does not pause.** An unpicked choice waits on the console
  ("2 picks waiting") and the player picks when there is a moment. Its
  unlock only works once picked.
- **The command map (M)** lists this game's levels and picks for each kind
  the player has driven. A waiting pick can be made there with a click, for
  any kind.
- **Keys:** Z picks the first card and X the second. While cards are up,
  holding Option frees the pointer, so a card can be clicked; the click
  neither fires nor steers. With no cards up, Option leaves mouse-look
  alone.
- **AI-run units** show no level: their card stays as it is.
- **Sounds:** the level-up fanfare (`levelup`) and the latch of a pick
  (`pick`), in your ear. The model viewer's sound bar lists both for every
  kind, and the Pulse mine's blast for the Comet.

## Tracking

Which picks players take, and how the games they take them in turn out,
so the picks can be balanced. This section says what is recorded and how
it is built. The four steps of the plan below are built, and ported to the
Unreal game ("In the Unreal game").

### The two numbers

Games with "pick one of a few" unlocks (Dota 2's talent tree, Heroes of
the Storm's talents, Slay the Spire's cards) balance them on two numbers
per pick:

- **Pick rate:** of the games that reached the level with the kind, the
  share that took this card rather than the other.
- **Win rate when picked:** the share of those games that were won. It is
  compared with the other card of the same level, never across levels.

Everything else recorded makes those two trustworthy or explains them.

### What is recorded

**Per game:**

- the game id, the wall-clock start and end, and the length in game
  seconds;
- the result for the human team (`Pilot.player`): won, lost, drawn,
  abandoned (a new game replaced it before it ended), or open (not over
  yet: a game resumes across launches);
- the map (its style, size and version) and whether the team's AI was on;
- the balance signature: a hash of the catalogue, the XP curve and the
  hero numbers, so games from before a balance change are never mixed
  with games after it. Nothing is committed, so a git hash would not tell
  builds apart;
- the build (the app binary's date) and the macOS version;
- an install id, random, made once. The games before this one on the same
  install stand for the player's experience.

**Per player in the game:** whether it is the human team; its AI's draw
for the game (style, aggression, greed, Garrison per base, Comets,
Fireflies, drops, start base); whether it won; and at the end (or at the
last write) its standing and the units it lost. The enemy's draw is the
nearest thing to a difficulty setting.

A **standing** is a player's army value (what its units other than Prospectors
cost at list price, ore plus MH), workers, banked ore and MH,
supply used and cap, finished buildings and ore mined.

**Per level reached**, one row even when no card was taken:

- the kind, the level, the two cards on offer, and the card taken, or none
  before the game ended;
- when the level was reached and when the card was taken, in game seconds.
  The wait is how long the card stood first in line; a long wait means
  cards that are hard to tell apart, or both weak;
- the seconds driven with the kind up to that moment, so the driving after
  each pick is known;
- the standing at the level-up: the human's, and the other players' added
  up. It tells "picked while winning" from "won because of it".

The times, the driving and the standings are empty for a level with no
stamp: one set by hand, or reached in a session saved before tracking.

**Per kind driven, per game:**

- seconds driven (while the driven unit is alive), times taken over, XP
  and the level reached;
- what the driver did: hp taken off enemy units and off enemy buildings,
  units killed, buildings razed, hp lost, what the pilot shield took,
  deaths, flyers killed (`airKills`), Quake stomps and the units they hit
  (`stomps`, `stompHits`), stings and the units they hit (`strikes`,
  `strikeHits`), seconds buried and unseen by every enemy
  (`hiddenSeconds`), ore and MH delivered, buildings put up (in hp: a
  building's full hp times the share of its build time welded), hp
  repaired (buildings, and machines with Field welder), hp healed,
  passengers carried 10 or more cells (the carries that earn XP), burn
  damage and Pulse mine hits. These are the stats the picks change,
  counted where XP is earned and only for the driven unit. Hp counts
  leave out overkill. Burn damage is in the hp taken off units too.

Later, once the plain numbers are in: each pick's own share, such as the
extra ore Big haul brought or the damage Shield took. It needs a hook
wherever a boost is applied (`boost`, `teamBoost`), so it waits until the
plain numbers prove too noisy.

### What is worked out

Views in the database, each per balance signature. A win rate counts only
games that are over: won, lost or drawn, and a draw is not a win.

- `perk_balance`: per pick, the games it was on offer (`offered`), the
  games that took a card at its level (`took`), the times it was picked,
  its pick rate within its pair (picked over took), the finished games it
  was picked in and their wins, its win rate when picked, the other
  card's (`other_*`), the gap, and a 95% margin on its win rate,
  `1.96 × √(p(1 − p)/n)`.
- `level_funnel`: per kind and level, the share of the games the kind was
  driven in that reached it. It tunes the XP curve and shows which late
  picks are too rare to judge.
- `pick_paths`: per kind, each game's picks in level order
  (`prospectorQuickDrill > prospectorPlating`), the games that took that run, and its
  win rate. Picks that work together show up here, not in single picks.
- `drive_share`: each kind's share of the seconds driven. Leveling is
  there to get the Prospector driven; this says whether it does.

Reading a pick off its two numbers:

| | High win rate | Low win rate |
| --- | --- | --- |
| **High pick rate** | Too strong: tone it down | A trap: feels good, doesn't help |
| **Low pick rate** | Strong but overlooked, or niche: make it clearer | Dead: rework it |

### Traps

- **Survivorship:** winners reach higher levels, so late picks always look
  strong. Compare only the two cards of one level.
- **Skill:** good players settle on the same card. Split by experience and
  by the enemy's draw.
- **Sample size:** a 5-point win-rate gap between two cards needs about
  800 picks of each. Every rate shows its count. A split past about 70/30
  usually means one card is a must-pick or a dead one. On one machine the
  samples stay small for a long time; the per-kind stats are useful long
  before the win rates are.
- **Versions:** never mix balance signatures.
- **Only real games:** window-mode games. The wallpaper (no one drives),
  the headless renders (`windowshot`, `snapshot`), `simulate` and the tests
  never write.

### Where it goes

A local SQLite database, `~/Library/Application Support/Autocraft/tracking.sqlite`,
next to `sessions/`. macOS has SQLite built in (`import SQLite3`), so
there is no new dependency. `make balance` prints `perk_balance` with the
`sqlite3` that ships with macOS; `VIEW=` picks another view and `DB=`
another file. The same file holds the performance tables (README,
"Performance"): `Database` (GameCore) opens it, migrates each set of
tables on its own and does every write and read, for the leveling's
`TrackingStore` and the app's `PerfStore` alike.

| Table | Key | Holds |
| --- | --- | --- |
| `meta` | key | the install id, and each set's version: `version.tracking` (these tables), `version.perf` |
| `games` | game | the per-game list, with the install id |
| `players` | game, player | the per-player list; the standing as `army`, `workers`, `ore`, `hydrogen`, `supply_used`, `supply_cap`, `buildings`, `mined` |
| `kinds` | game, kind | the per-kind list, a column for each count |
| `levels` | game, kind, level | the per-level list; the standings as `own_*` and `enemy_*` |

Times are game seconds; dates are ISO 8601 text, UTC. Every write is an
upsert of the whole game as it stands, in one transaction: the `games`
row, and that game's `players`, `kinds` and `levels` rows in place of the
ones there. Writing the same game twice changes nothing, and a crash
loses at most the last few seconds. The database is in WAL mode, and the
views are made again each time it is opened, so a change to them
applies.

### Building it (plan)

The sim keeps the facts and the app copies them to the database. The
sim's record is saved with the session, so the facts survive a quit and a
resume, and a game's rows can always be written again from its session.

1. **The facts, in GameCore.** Built.
   - `KindRecord` gains a `tally` (the per-kind counts) and `stamps` (per
     level: when reached, the standing then, the seconds driven by then,
     when picked). Its tolerant decoder reads old sessions without them.
   - The counts are added where XP is earned (`earn`, `earnHit` and their
     callers in `Simulation.swift`, `Pilot.swift` and `Pilot+Kinds.swift`),
     plus kills, damage taken and deaths in `hit`, seconds driven in
     `stepLeveling`, and take-overs in `take`.
   - `keepPick` stamps the pick; the level-up stamps the level and the
     standing.
   - `Simulation.standing(of:)`: a player's army value, workers, bank,
     supply, buildings and ore mined.
   - `Player.unitsLost`, counted where `.died` is raised (`bury`).
   - `Leveling.signature`: a stable hash (FNV-1a, not Swift's `Hasher`,
     which changes every launch) of the catalogue, the XP table and
     `Rules.hero*`.
   - `Session.game`: a game id, new whenever a game starts (a new session,
     `restart`, and the next game after a victory lap).
   - Tests, as isolated scenarios: a driven Ranger hitting a target, a Prospector
     delivering one load, a pick stamped at the sim's time, an old session
     decoding.
2. **The store, in GameCore**, beside `SessionStore` (which already writes
   files from GameCore). Built. `Tracking.swift` builds the rows from a session
   and its sim, as plain values. `TrackingStore.swift` has the tables and
   views (a `Database.Schema`) and upserts a game; `Database.swift` opens
   the file and migrates it. Tests: a
   temporary database, the same game written twice, and the views run on
   rows written by hand, never on games the AIs play.
3. **The wiring, in the app** (`GameController`), window mode only. Built.
   `AppDelegate` hands the store in, and the snapshot paths, which make
   their own controllers, leave it out. Writes go on a serial background
   queue: at the 15-second save, after a level-up and after a pick, at a
   victory (the result), before a new game or a restart replaces a running
   one (abandoned), and at quit.
4. **Reading it:** `make balance`, and the README and this doc brought up
   to date. Built.

Two orc workers, one after the other: one for 1 and 2 (GameCore and its
tests), then one for 3 and 4 (the app), each merged into live with a
three-way merge.

### As built

The four steps as they came out, where the plan left room. Steps 1 and 2:

- **No gameplay change.** The counts and stamps sit beside the XP; no
  number the game plays by was moved or recomputed. With no one driving,
  only `Player.unitsLost` is added to the state.
- **The counts follow the XP.** They go on during a victory lap, as XP
  does, so a level can be reached after the end; its stamp's time says so.
- **Deaths** are counted in `bury`, where the driven unit is taken off.
- **Game ids:** `SessionStore.open` gives every session it hands out a
  game id: a new one, and an old one through `fillGame`, which takes the
  session's `created` for the start. `Session.restart` and `nextGame`
  make a new one. `GameState` and `Simulation` know none of it, so they
  stay deterministic.
- **The end, wall clock:** for a game that is over, the write's time less
  the game seconds since the end. Writing it again later gives the same
  end, unless the app was quit in between.
- **Result:** won or lost from `state.winner`; drawn when it ended with no
  winner; abandoned only when the caller says so and it was not over;
  open otherwise.
- **An old session's AI draw:** the nils are written as the defaults they
  stand for (bio, greed 0, 2 Garrisons a base, 1 Comet, 0 Fireflies, no
  drops).
- **The signature** hashes, per pick, its name, kind, level, slot, title,
  effect text and `String(describing: effects)`; then
  `Leveling.xp(toReach:)` for 2 to 10; then `cap`, `radius`, the XP
  numbers (`oreXP`, `hydrogenXP`, `buildingXP`, `mendXP`, `carryDistance`,
  `carryXP`) and `healing`; then `Rules.hero*`.
- **SQLite:** the one built into macOS (3.51 on macOS 26), which has
  `sqrt` and `group_concat(… ORDER BY …)`, both used by the views. A
  SQLite before 3.44 could not make `pick_paths`.

Steps 3 and 4:

- **One tracker.** `Tracker` wraps the database and its serial queue, for
  the leveling and the performance samples alike. The
  app delegate opens it with the first window game and hands it to every
  window controller after that. If the database won't open, the log says
  so once and the games go on untracked. Nothing else makes one, so the
  wallpaper, the headless renders, `simulate`, the model viewer and the
  tests never open the file.
- **Rows on the main thread, writes on the queue.** The rows are a copy
  of the game as it stands, so the sim can step on while they are
  written. A failed write is logged once per message, not every 15 s.
- **The build** is the app binary's modification date when the tracker
  opens: a rebuild while the game runs is not the code that runs.
- **The victory lap:** the finished game is written, then `sim.newGame()`
  and `session.nextGame()`, then the save writes the next game as open.
- **Abandoned:** a new game (⌘N) or a restart (⌘R) writes the running
  game as abandoned before the new session is saved. The wallpaper's
  Restart World does too, though the wallpaper has no tracker.
- **`make balance`** shows rates as whole percents, and leaves out the
  `signature` column (the line above the table names it) and `other`
  (the two cards of a level are on adjacent rows). The latest signature
  is that of the game that ended last; an open game's end is its last
  write, so in practice it is the game written last. It opens the file
  read-write, though it only reads: a WAL database the app has closed
  won't open read-only.

### In the Unreal game

The store is ported (2026-10-06); the tables, the views, the rules of
"What is recorded" and "What is worked out" hold as above. Where the C++
differs from the text, or the text left room:

- **Where:** `~/Library/Application Support/Autocraft/Unreal/tracking.sqlite`,
  next to the Unreal game's `Unreal/sessions/` (the "next to `sessions/`" of
  "Where it goes"; the Swift game's file, in the folder above, is not
  touched). With `-AcSaveDir=DIR` it is `DIR/tracking.sqlite`, so a test
  never writes the player's file. `make balance DB=…` reads another.
- **Code:** `Tracking.h/.cpp` (engine-free: `TrackedGame::make` and the
  `worth::` maths) and `TrackingStore.h/.cpp` (SQLite's C API: the game
  links the engine's `SQLiteCore` 3.47.1, core-tests the system's
  `libsqlite3`) are in the core. `AcTracker.*` is the writer thread in the
  game, and `UAcSimSubsystem::Track` builds the rows.
- **When it writes:** `Track` runs at the game's start or resume, at every
  autosave (15 s) and `Save`, at a level-up event, after a pick that took,
  at the victory, before `NextGame` replaces the ended game, with
  `abandoned` before a new game, a restart or an import replaces a running
  one, and at quit. Nothing for a fixture, the playground, or `-AcNoSave`
  (every hidden shot and test run, and `-AcRunFor`). The rows are built on
  the game thread; the upsert runs on one worker, in the order queued. A
  failed open is logged once and the run goes untracked.
- **Columns the text leaves open:** `games(game, install, session, started,
  ended, length, result, map, map_version, ai, signature, build, os)`;
  `players(game, player, human, style, aggression, greed, garrison_per_base,
  comets, fireflies, drops, start_base, won, army, workers, ore, hydrogen,
  supply_used, supply_cap, buildings, mined, units_lost)`; `kinds(game, kind,
  seconds, takeovers, xp, level` and a snake_case column for every `Tally`
  field, the six new ones included`)`; `levels(game, kind, level, one,
  other, picked, reached, picked_at, waited, driven, own_*, enemy_*)`.
  `waited` is worked out when the row is made: from the later of the level
  being reached and the lower level's card being taken, to the card being
  taken. A kind's level rows run from 2 to the higher of the level its XP
  reaches and its highest card.
- **Worked out as written:** `length` is the game seconds to the end for a
  game that is over, else so far; the result is the human team's (allies
  of player 0 win and lose with it); `enemy_*` and the standings of a
  stamp add up every player but the human's, allies included.
- **Views:** `perk_balance(signature, kind, level, perk, offered, took,
  picked, pick_rate, games, wins, win_rate, margin, other, other_games,
  other_wins, other_win_rate, gap)`, `level_funnel(signature, kind, level,
  driven, reached, share)` (levels 2 to 10, zero where none got there),
  `pick_paths(signature, kind, path, games, finished, wins, win_rate)` and
  `drive_share(signature, kind, seconds, share)`. They use `sqrt` and
  `group_concat(… ORDER BY …)`: the engine's SQLite (3.47.1) has the second
  and not the first, so the views are made on open there and only read
  with the system `sqlite3` (`make balance`).
- **WAL:** the store asks for WAL mode and takes what SQLite grants. The
  engine's SQLite is built for a custom platform (`ZERO_MALLOC`,
  `MUTEX_NOOP`, no shared memory), where a WAL request stays in the
  rollback journal: the game's file is `journal_mode = delete`. A write
  holds the file for about a millisecond, so `make balance` waits for it
  (`.timeout`). The system SQLite (core-tests) does enter WAL.
- **Version:** `meta.version.tracking` is 1. A file below it runs the
  steps after it in one transaction; a file above it is not opened.
- **Not ported:** the performance tables (`PerfStore`, `version.perf`).

## How it fits the code

`Sources/GameCore/Leveling.swift` holds the numbers, the record and the
catalogue. `Simulation+Leveling.swift` holds the sim's side. Tests are in
`Tests/GameCoreTests/LevelingTests.swift` (the framework and the Prospector) and
`LevelingKindsTests.swift` (the other six kinds).

### The record

- `Player.pilot: PilotRecord?` on the human player (`Pilot.player`, 0).
  It is saved with the session. Old sessions load with it nil, and
  `GameState.new` (a new game, `Session.restart`, `Simulation.newGame`)
  starts without one.
- `PilotRecord.kinds: [Unit.Kind: KindRecord]` holds each kind driven
  this game. `take` adds a kind at level 1.
- `KindRecord` has `xp`, `picks` (lowest level first) and `level`, plus
  the blocks' state: `count` (the every-third counter), `burstUntil` and
  `burstReady` (the ability key's clock, the grenade's too), and
  `healedAt` (Cruise), and for tracking `tally` and `stamps` (see
  "Tracking" below). It decodes tolerantly.

### The curve

- `Leveling.xp(toReach: L)` is `30 × L × (L − 1)`. `Leveling.level(xp:)`
  gives the level. `Leveling.cap` is 10.

### Queries for the HUD

- `sim.levels` is the record, empty before any driving.
- `levels.level(kind)`, `levels.xp(kind)`. For the bar, use
  `Leveling.xp(toReach: level)` and `Leveling.xp(toReach: level + 1)`.
- `levels.pending(kind)` gives `[PilotRecord.Offer]` (`level`, `one`,
  `other`), lowest level first. Only the first can be picked now.
- `levels.picks(kind)`, `levels.has(perk)`, and `levels.driven` (the kinds
  driven this game, for the command map).
- `levels.refusal(perk)` says why a pick can't be made now, or nil.
- `sim.active(perk)` is true when the pick is made and the player drives
  its kind now.

### The catalogue

- `Perk` is an enum of 216 cases: 12 kinds × 9 levels × 2, named
  `prospectorQuickDrill`, `rangerSurge`, `longbowShield` and so on. The
  three newest kinds' picks (`peregrine…`, `atlas…`, `scorpion…`) are
  appended at the end, so saved games keep loading.
- `perk.info` is a `Perk.Info`: `kind`, `level`, `slot` (`.one` or
  `.other`), `title` ("Quick drill") and `effect` ("Nearby Prospectors drill 10%
  faster.").
- `Perk.offer(kind, level:)` gives the pair offered at a level.
- `perk.effects` lists what it does: its rows in `Perk.built`. Every pick
  has some.

### Picking and the event

- `Command.pick(player:, Perk)`, or `sim.pick(perk)` for short. Like any
  order it returns false and changes nothing unless the pick is one of the
  two at its kind's lowest pending level.
- `GameEvent.leveledUp(kind:level:)` fires once for each level crossed, at
  the end of the step. The game does not pause.
- `GameEvent.blast(unit:at:radius:)` fires when the driven unit's pulse mine
  charge goes off. The scene shows a burst as wide as the radius and a
  scorch mark (`GameScene.blast`), with the blast sound
  (`AudioDirector.grenade`).
- `GameEvent::Stomp{unit, at, radius}` (the last case) fires when an Atlas's
  Quake stomp goes off, driven or not. The scene draws a ring of dust and two
  shock rings out to the radius (`UAcEffects::Stomp`), the sound is the
  anchored Longbow's shell burst (`FAcAudioRules::Stomp`), and cameras close
  to it shake a little (`UAcEffects::StompShake`: the driven unit's view, and
  the top-down view when zoomed in to 30 cells or less).

### XP

XP is added in the sim, only when the source is the driven unit
(`earn(_:by:)`):

- `prospectorDeposit`: 1 an ore, 1.25 an MH.
- `stepStructures`, while welding: the building's worth × the share of its
  build time welded.
- `prospectorBusy` and `prospectorMend`, repairing: worth × share mended × ½.
- `hit(_:_:slow:by:)`, damage: worth × share of hp taken. Overkill doesn't
  count, nor does damage to its own side. Buildings earn ×½.
- `heal`, the Dropship: worth × share healed × ½.
- `unload`, the Dropship: 10% of a passenger's worth when it is dropped 10
  or more cells from where it boarded (`Unit.boardedAt`).

### Tracking

The facts, in the sim (saved with the session):

- `KindRecord.tally: Tally`, the per-kind counts: `seconds`, `takeovers`,
  `unitDamage`, `buildingDamage`, `kills`, `razed`, `damageTaken`,
  `shieldAbsorbed`, `deaths`, `ore`, `hydrogen`, `built`, `repaired`,
  `healed`, `carried`, `burn`, `blastHits`. Added through
  `sim.tally(unit) { … }`, which does nothing unless the unit is driven:
  in `hit`, `stepLeveling` (seconds and burns), `take`, `bury`,
  `prospectorDeposit`, `stepStructures`, `prospectorBusy`, `prospectorMend`, `heal`, `unload`
  and `throwGrenade`.
- `KindRecord.stamps: [Stamp]`, one per level reached: `level`, `at`,
  `driven`, `standing` (every player's) and `picked`. `earn` stamps the
  levels it crosses (`stamp(_:levels:)`); `keepPick` sets `picked`.
- `sim.standing(of: player) -> Standing` (and `GameState.standing(of:)`):
  `army`, `workers`, `ore`, `hydrogen`, `supplyUsed`, `supplyCap`,
  `buildings`, `mined`. `+` adds two up.
- `Player.unitsLost: Int?`, counted in `bury`.
- `Leveling.signature`: 16 hex digits, FNV-1a 64 (`Leveling.fnv1a`) of
  `Leveling.balance`.
- `Session.game: String?` and `Session.gameStarted: Date?`. Set by
  `SessionStore.open`, `Session.restart(on:)` and
  `session.nextGame()` (call it after `sim.newGame()`).
  `session.fillGame()` fills them in when nil.

The store:

- `TrackedGame(session:state:build:os:now:abandoned:)` builds a game's
  rows: `TrackedGame` (`id`, `session`, `started`, `ended`, `length`,
  `result`, `map`, `mapVersion`, `ai`, `signature`, `build`, `os`),
  with `players: [TrackedPlayer]`, `kinds: [TrackedKind]` and
  `levels: [TrackedLevel]`. `TrackedGame.Result`: `won`, `lost`, `drawn`,
  `abandoned`, `open`. Each has a memberwise `init` for rows made by hand.
- `TrackingStore(db)` on an open `Database`, or `TrackingStore(url:) throws`
  to open one with only these tables (tests and tools);
  `TrackingStore.schema`, `store.write(game) throws`.
- `Database(url:schemas:) throws` (`Database.defaultURL`), shared with
  the performance tables: `db.installID`, `db.version(of:)`,
  `db.transaction { }`, `db.insert(table, columns, values, upsert:)` or
  `db.insert(table, [Column], row, upsert:)`, and `db.rows(sql, values)`
  to read. `Column.definitions(columns)` makes a table's column list,
  `Column.additions(to: table, columns)` a later step's added columns. A `DatabaseError` carries SQLite's message. One database, one
  serial queue.
- Tests: `Tests/GameCoreTests/TrackingTests.swift`,
  `Tests/GameCoreTests/DatabaseTests.swift`.

The app (window mode only):

- `Tracker` (`Sources/Autocraft/GameController+Tracking.swift`): the
  database on a serial queue, with every set of tables
  (`Tracker.schemas`). `Tracker.open()` (nil, logged, when the file won't
  open), `write { db in … }` (in the background, a failure logged once),
  `record(session, state, abandoned:)`, `record(summary, game:time:units:)`
  for performance, and `finish()`, which waits for the writes queued so
  far.
- `AppDelegate.tracker`, opened lazily in `openWindowGame` and set on
  each window `GameController` as `tracker`; nil means no tracking.
- `GameController.track(abandoned:)` writes the game as it stands. It is
  called in `save()` (every 15 s, and on quit), `leveledUp`, `pickPerk`
  (a pick that took), at `.victory`, before `sim.newGame()` at the end
  of a victory lap, in `restartWorld`, and from `AppDelegate.beginGame`
  (abandoned). `applicationWillTerminate` saves, then calls `finish()`.
- `make balance [VIEW=…] [DB=…]` prints a view for the latest balance
  signature with `/usr/bin/sqlite3 -box`.

### Effects

- A pick is a list of `Effect`s:
  - `.stat(Stat, Scope, Change)`;
  - `.everyThird(Action)`;
  - `.aura(Aura)`;
  - `.burst(Burst)`;
  - `.shield(Shield)`;
  - `.selfMend(hpPerSecond:delay:)`;
  - `.cliffJump`;
  - `.machineRepair(share:)`;
  - the one-offs: `.grenade(Grenade)` (Pulse mine), `.burn(perSecond:seconds:)`,
    `.fragment(radius:share:)`, `.doubleBeam`, `.naniteBeam`,
    `.dropRush(Rush)` (Combat drop), `.cruise(speed:)` and
    `.afterJump(Rush)` (Booster), and, for the newest kinds,
    `.hitsAir(share:)` (Flak mount), `.secondTarget(radius:)` (Twin sting) and
    `.hitsBuildings(share:)` (Sapper).
- `Scope` is `.driven`, `.nearby` (same kind within `Leveling.radius`, 6
  cells, the driven one included), `.every`, `.around(Filter)` (any kind
  in `Filter` within 6 cells; `Filter` is `machines`, `bio`, `all` or, for
  Escort, `air`) or `.team` (economy).
- `Change`:
  - `.percent` adds up (speed-ups);
  - `.times` and `.atLeast`: the biggest counts (a factor, a load);
  - `.plus` adds up (hp, armor, range).
- `sim.boost(stat, of: unit) -> Boost` is the one place that answers what
  a unit's stat is right now. It adds the scoped picks of the kind the
  player drives, and level 1's hero for the driven unit
  (`Leveling.hero`, from `Rules.hero*`). `Boost.apply(base)` is
  `max(base × times, atLeast) × (1 + percent) + plus`, and `Boost.rate` is
  `apply(1)`. With no one driving it is `.none`, and every number stays
  exactly as it was, so the AI, the wallpaper and `simulate` are
  unchanged.
- `sim.teamBoost(stat, owner:)` covers the `.team` stats.
- Helpers: `load(of:hydrogen:)`, `unitCost(_:owner:)`, `buildCost(_:owner:)`,
  `habDomeSupply(owner:)`, `bastionCapacity(owner:)`, `cargoSlots(_:)` and
  `upgradeCost(_:owner:)`.
- Wired stats:
  - `speed` (`move`, `pilotWalk`, `pilotFly`);
  - `drill` and `hydrogen`, as rates;
  - `oreCarry` and `hydrogenCarry`;
  - `build` and `repair`;
  - `hp` (`maxHP`; current hp follows through `syncBonusHP`) and `armor`
    (`target`);
  - `damage`, `damageVs(TargetClass)` and `ignoresArmor` (`damage`);
  - `fireRate` and `range` (`weapon`);
  - `damageTaken` (`hit`);
  - `buildingCost`, `training(kind)` (as a rate, so the training bar stays
    right), `unitCost(kind)` and `supplyPerHabDome`;
  - `sight` (`look`, through `Vision.sight`'s `farther`);
  - `flameReach` (`weapon`: a Firefly's range is its reach) and
    `flameWidth` (`land`);
  - `splash`, a factor on `Rules.splash`'s radii (`land`);
  - `anchor`, a rate (`upkeep`), and `anchorRange` (`weapon`, anchored);
  - `slowTime` and `slow` (`hit`; the target keeps `Unit.slowedTo` of its
    speed, read in `move` and `pilotWalk`);
  - `heal`, a rate, and `healEnergy`, the hp an energy point heals (`heal`);
  - `energy` and `regen`, rates (`upkeep`);
  - `jumpReach` (`pilotJump`);
  - `cargo` (`cargoSlots`: `board`, `pilotAbility`);
  - `bounty` (`hit`, on a kill) and `refund(kind)` (`bury`);
  - `production(Structure.Kind)`, a rate on top of `training(kind)`
    (`stepStructures`);
  - `upgradeCost(Structure.Kind)` (the `.research` order) and
    `research(Structure.Kind)`, a rate (`stepStructures`), both keyed by
    the building the upgrade is researched at (`Upgrade.at`);
  - `bastionSize` (`bastionCapacity`: the `.load` order, `stepInBastion`);
  - `stomp` (`quake`: `.percent` of the damage, `.plus` cells of reach),
    `buryTime`, a rate (`upkeep`), and `revealRange`
    (`hiddenFrom`, `Vision::sight`: how close an enemy sees a buried Scorpion).
- The blocks:
  - `thirdTime(_:by:)`: the counter. Ore trips are counted in
    `prospectorBusy`; attacks in `land`, where the whole volley doubles.
  - `keyAbility`, `startBurst` and `throwGrenade`: on the ability key,
    through `PilotAbility.Action.burst` and `.grenade`. The title is the
    pick's ("Surge", "Boost", "Pulse mine"). The Longbow's and the Dropship's
    picks have none, so their key still anchors and loads.
  - `levelUpkeep`: the shield (`Unit.shield`, taken first in
    `shieldTakes`) and self-mending.
  - `stepLeveling`: units on fire (`Unit.burning`), then the auras.
  - `Unit.rush` and `rushUntil`: Booster and Combat drop, read in
    `boost`.
  - `effect(of:_:)`: a one-off's numbers for the driven unit.
  - `pilotJumps`: cliff jumping.
  - `machineRepair(by:)`: the Prospector's `PilotTarget.repair(id)` also takes a
    unit's id.
- `Rules.heroRange` and the other `Rules.hero*` constants remain as level
  1. The app's three readers of `heroRange` still compile, but they don't
  see range picks. `sim.weapon(unit).range` does.
- Tests can try effects on a pick that isn't built yet through
  `sim.trialEffects[perk]`.

### Interpretations

- Hero factors and picks multiply. Light frame makes the driven Prospector
  1.2 × 1.5.
- Cutting torch doubles on top of the hero: ×3 of a plain Prospector.
- Fast welder's ×3 replaces the hero's ×2. Site boss's +30% multiplies on
  that.
- A Field welder repair runs at its own rate: maxHP over half the training
  time. Fast welder doesn't speed it up.
- A shield starts empty when it is picked or the pilot gets in, and fills
  over its 10 s.
- An hp pick raises current hp by as much when it comes on, as Combat
  Shield does. When it goes off, hp only drops to the new full.
- XP keeps adding up past level 10.
- Handing back a loaded Dropship sets its cargo down, and that drop can
  earn carry XP.

The other six kinds:

- "+40% damage, and nearby +15%" (Hollow points and the rest): the
  driven one gets 40% in all, the others nearby 15%. Nearby includes the
  driven one, so its own row is 25%. Surgeon is the same for healing.
- Demolition's +30% against buildings adds to the damage picks' percent,
  as speed-ups add up.
- Light killer and AP shells add their bonus to each hit: a Comet's two
  pistols get +5 each.
- Long gun's +2 comes after the hero's ×1.25: a driven Longbow reaches
  13 × 1.25 + 2 anchored.
- Fuel line works on the Firefly's range, which is its flame's reach. It
  reaches and targets 20% farther, and the hero's ×1.25 comes on top.
- Spotter is the driven Comet's own sight, ×1.5. The team sees what any
  of its units sees.
- Nanomeds speed up a Comet's healing out of combat. That healing now
  goes up to `maxHP`, so a Padded suit's 25 hp grow back too.
- Shock rounds makes every Juggernaut's slow last ×1.2. Deep slow leaves the
  target a third of its speed, kept on the target (`Unit.slowedTo`).
- Field research makes upgrades cost ¾ when ordered. "Finish 25% sooner"
  is ¾ of the time, so the research runs at 4/3 while a Juggernaut is
  driven. The bar stays right because it is a rate.
- Efficient's "30% less energy" means an energy point heals 10/7 as much.
- Hull down's anchor rate is infinite. The key is read after the step's
  upkeep, so the Longbow is anchored at the next step.
- Heavy industry and Spacedock refit speed up a kind of building
  (`production`), on top of any `training(kind)` pick.
- Pulse mine lands on what the sight is on (the crosshair, or the line
  ahead) if that is within the weapon's reach, else at that reach toward
  it, else straight ahead. It does a flat 20 to enemy ground units and
  buildings within 1.5 cells: no armour, no hero factor. It raises
  `.blast`, so the scene shows it.
- Burn: 4 a second for 3 s on enemy units the driven Firefly's flame hits,
  buildings not. No armour, no hero factor. A new hit starts the 3 s
  again. A burn runs its 3 s out even once no one drives
  (`Simulation.burnsLeft` keeps `stepLeveling` going).
- Fragment hits enemy ground units within 1 cell of the target for half
  of each one's own grenade damage. It doesn't slow them.
- Bounty and Scrap count the list cost, ore and MH, rounded. Bounty
  counts buildings as well as units. Scrap counts the driven Firefly too.
- Double beam's second patient is the most hurt one, by share of hp, in
  heal reach. It costs energy like the first.
- Nanite beam heals machines but doesn't load them: boarding stays bio
  only.
- Combat drop's rush stays on the units set down for its 4 s, whatever
  the player drives meanwhile. Handing back a loaded Dropship drops its
  cargo, and that counts.
- Cruise: "not healing" means no heal in the last 0.25 s
  (`Leveling.healing`).
- Booster's 3 s start on landing.
- Bastion drill and Cargo bay change the sim, and the app reads them
  through `sim.bastionCapacity(owner:)` and `sim.cargoSlots(_:)`:
  - the Bastion card's "x / 6 Rangers inside";
  - two more crew pips round the Bastion's front corners, shown only
    while it holds 6;
  - the Dropship card's cargo count;
  - a second row of 4 cargo lamps in the Dropship's cockpit, shown only
    with 12 slots.

  The AI's planning still reads `Rules`, so it fills a Bastion with 4.
