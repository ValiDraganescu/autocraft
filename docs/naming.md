# Names

The names of every unit, building and resource in Autocraft, their short
forms, and the identifiers the code uses for them. The name set is
"Stellar prospectors": a mining frontier on a far world.

## Resources

|                | Name              | Short form | Code        |
|----------------|-------------------|------------|-------------|
| Resource       | Stardust Ore      | Ore        | `ore`       |
| Map feature    | Ore deposit       | deposit    | `OreDeposit` |
| A row of them  | Ore line          |            |             |
| Resource       | Metallic Hydrogen | MH         | `hydrogen`  |
| Map feature    | MH well           | well       | `well`      |
| Building on it | Derrick           |            | `derrick`   |

Verbs: ore is **mined** (the "Drill ore" card). MH is **pumped** ("Pump
MH", "PUMPING MH"). Cargo reads "5 ore" or "4 MH". Metallic Hydrogen is a
liquid metal, like mercury.

### How they look

- **Stardust Ore**: rough nodules of dark rock. Opal shimmers run over
  them and shift colour with the view angle (teal, violet, gold), and
  tiny glints of dust sparkle on the surface. A faint glitter drifts off
  the deposit. Deposits shrink in three stages as they are mined out.
- **MH well**: a rock mound around a pool of mirror-silver liquid metal
  that heaves slowly. A cold blue-white light shines at its rim, and a
  pale vapour boils off it.
- **Derrick**: built over a well, its throat and vapour silver and cold
  blue.
- **Carried cargo**: a Prospector carries a dark ore chunk with an opal
  glint, or a silver MH canister.
- **HUD icons**: a dark nodule with an opal glint for ore, a silver
  droplet for MH.

## Units

| Name       | Role                                         | Code         |
|------------|----------------------------------------------|--------------|
| Prospector | Mines ore, pumps MH, builds, repairs         | `prospector` |
| Ranger     | Rifle infantry                               | `ranger`     |
| Comet      | Jump-pack raider with pistols, clears cliffs | `comet`      |
| Juggernaut | Heavy grenadier                              | `juggernaut` |
| Firefly    | Fast flame buggy                             | `firefly`    |
| Dropship   | Healing transport                            | `dropship`   |
| Longbow    | Tank that anchors into artillery             | `longbow`    |
| Kestrel    | Rocket gunship, hits the ground only         | `kestrel`    |
| Hailstorm  | Flak half-track, hits the air only           | `hailstorm`  |
| Peregrine  | Air-superiority fighter                      | `peregrine`  |
| Atlas      | Heavy assault walker                         | `atlas`      |
| Scorpion   | Burrowing mine                               | `scorpion`   |

The Longbow **anchors** into artillery mode: `anchor`, `anchored`,
`unanchor`, `anchorTime`, and so on. [new-units.md](new-units.md) designs
the Peregrine, the Atlas and the Scorpion.

## Buildings

| Name      | Short form | Code        |
|-----------|------------|-------------|
| Citadel   |            | `citadel`   |
| Hab Dome  | Hab        | `habDome`   |
| Garrison  |            | `garrison`  |
| Bastion   |            | `bastion`   |
| Derrick   |            | `derrick`   |
| Foundry   |            | `foundry`   |
| Spacedock |            | `spacedock` |
| Lab       |            | `lab`       |
| Sentinel  |            | `sentinel`  |

The Kestrel, the Hailstorm and the Sentinel bring air attack and
anti-air. Their weapons are the Kestrel's **twin rockets**, the
Hailstorm's **flak** and the Sentinel's **missile pods**.

"Supply" and "rally" are ordinary RTS words and stay.

## Upgrades, abilities and weapons

| Name              | Unit       | Code                         |
|-------------------|------------|------------------------------|
| Mini gun          | Ranger     | `minigun`                    |
| Aegis shield      | Ranger     | `aegisShield`                |
| Surge             | Ranger     | `rangerSurge` (a pick)       |
| Coil rifle        | Ranger     |                              |
| Twin pistols      | Comet      |                              |
| Pulse mine        | Comet      | `cometPulseMine` (a pick)    |
| Breacher grenades | Juggernaut | `breacher`                   |
| Shock rounds      | Juggernaut | (comments only)              |
| Nova igniters     | Firefly    | `novaIgniters`               |
| Lifeline reactor  | Dropship   | `lifelineReactor`            |
| Boosters          | Dropship   | `dropshipBoosters` (a pick)  |
| Rail cannon       | Longbow    |                              |

The mini gun's sound is called `minigun` too.

Leveling picks are named after their unit: `prospectorBigHaul`,
`rangerSurge`, `juggernautBreacher`, and so on ("Every Prospector pumps
MH 20% faster").

## Map, voice and music

- **Map**: **Ashfall Reach**.
- **Base voice**: the **Oracle**. Its warnings never say its name.
- **Music**: original tracks made with ElevenLabs Music, in
  `unreal/Resources/Sounds/music/` ([music.md](music.md)). The game plays
  them plus anything the player drops into the Audio folder's `music/`.

## Sound files

The per-unit stems carry the unit's name: `v<unit>` (reports in),
`<unit>death`, `<unit>move`. The Juggernaut's grenades are `breacher`;
the Longbow's artillery sounds are `anchorshot`, `anchorhit`, `anchorset`
and `anchorlift`. Generic stems keep plain names: `gun`, `pistol`,
`flame`, `drill`, `weld`, `deposit`, `hum`, `alert`, `heal`, `cannon`,
`step`, `breath`. The manifests in `Tools/AudioGen/` follow.

## Writing the names

Unit names are capitalised everywhere, in running text too: "two
Rangers", "the Comet jumps". Comet, Ranger, Firefly, Longbow, Kestrel,
Hailstorm, Sentinel, Peregrine, Atlas and Scorpion are ordinary words, so
lower case would read as the word, not the unit.
Building names are already capitalised.

In the game's own text (cards, prompts, the help line, the console and
the messages on screen) unit and building names are also highlighted:
drawn in an accent colour, so they stand out from the words around them.
