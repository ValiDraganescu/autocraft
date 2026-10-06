# Hailstorm concept: choice (2026-10-04)

Round 1: six directions (`prompts.md`), each in its own new Grok chat with the Longbow's `painted.jpg` attached; all six came back on the first try, 1168×784, each showing the vehicle at rest and firing. Contact sheet: `../previews/concepts-c1.jpg`.

**Chosen: `c1-boar.jpg`** (chosen by the agent; the user was offline).

Why:
- It is the clearest half-track of the six: two big tyred front wheels under flared fenders and a long track unit each side behind them. That is the blockout's running gear and what the game animates (front wheels roll; the turret turns; the cradle raises the barrels).
- An animal, like its siblings (Longbow rhino, Firefly scorpion, Juggernaut gorilla, Comet beetle): the wedge snout, two curved tusks low at the front, armoured shoulders and a bristly ridge of spikes down the back read as a boar at a glance, and as kin of the rhino's horn and hide plates without copying them.
- From the game's high camera the silhouette stays legible: a long wedge with a saw-tooth spine, a square turret on top with four thick barrels in two pairs and a radar dish behind. The barrels pitch steeply up in the firing pose, which is our `cradle` motion.
- The palette is already the army's: worn deep blue plates on dark gunmetal, amber lamps.

Runners-up: armadillo (banded shell is striking, but its front wheels are hidden under the snout and it reads as a full-track), owl (the most characterful face, but the owl head on the turret fights the turret turning and the barrels in its beak), shilka (the plain classic: right parts, no character). Porcupine's quills get lost at game scale; tortoise's dome hides the barrels' pairs.

Taken into the model: wedge snout with a brow plate and two amber headlamps, two tusks below it, high shoulder fenders over the front wheels, a hump hull behind with a spiked spine ridge, track units with road wheels, a square turret (blue side plates, dark ring) with four ribbed barrels in two side-by-side pairs, ammunition box at its back and a radar dish on a mast.

## Polish (2026-10-04, agent; user offline)

- **Radar.** The node is scaled to 0.8 and its foot sunk 0.08 into the ammunition box: its top is now about 0.52 over the turret's base instead of 0.67. Its parts keep their sizes, so their nets still fit. The amber lamp keeps it readable from the high camera.
- **Dish.** Its back rendered pale (the net's paint there is all highlight). The dish is now its own dark gunmetal metal on both sides (`ownMaterial`), as in the concept.
- **Tracks.** Cleats round each loop like the Longbow's (`treadGeometry`, now taking the loop's radius, length and width), six phases shown by stride so they roll as it drives. Built after the skin, so the paint plan is unchanged.
- Renders: `previews/polish-model.png` (yaw 0.6, 1.57, back, busy, red), `previews/polish-ingame.png`, `previews/polish-before-after.png` (playground, before left, after right).
