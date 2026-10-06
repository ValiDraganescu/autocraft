# Kestrel concepts, round 1 (2026-10-04): the choice

The user was offline; the agent chose. Contact sheet: `../previews/concepts-c1.jpg`. Grok chat ids in `chats.txt`.

All six came back usable on the first try, 1168×784, deep blue on worn silver-grey with cyan strips and red wing-tip beacons, every one with two fat six-tube rocket pods facing forward. wasp printed a title block ("KESTREL … WASPS CLASS") in a corner.

**Chosen: `c1-gunship.jpg`.** Why:
- Read from the high camera. Its plan view is the most legible of the six: a long silver spine, straight blue-and-silver wings, two huge pods well clear of the hull, two long engine nacelles riding over the wing roots, and the twin booms with two tall fins and a blue tailplane. That H-shaped tail is a silhouette no other unit has, and it says "gunship" at a glance.
- Same army as the Ranger: mostly worn silver plates with the blue as bands (wing stripes, hull flanks), the Ranger's balance. falcon, manta and hotrod are mostly blue, which reads as a different faction next to the silver Rangers.
- It fits the game's hooks with the fewest changes: the blockout already has a body, a canopy, wings, vectored nacelles at the wing roots, a pod under each wing (the pods kick back on their rails) and twin booms. The model can follow the painting's proportions without moving any handle.

Runners-up: falcon (the name, but its hooked beak and forward-swept wings are lost from above, and it is nearly all blue); tiltjet (strong, but its wing-tip nacelles would have to swivel 90° to hover, a new pose; wings very wide for a 0.625-radius unit).

## What became of it

- Blockout rebuilt after c1-gunship in `Sources/Autocraft/Models+Kestrel.swift` (handles unchanged).
- Turnaround: five Grok answers; the one without `style.jpg` attached (`grok/painted.v1c.jpg`, now `painted.jpg`) kept the stencil's silhouettes, fitted IoU 0.87 / 0.96 / 0.88 / 0.95 / 0.83. With the concept attached Grok redrew the shape every time.
- Nets: three sheets, `nets/grok/v1/`, installed. The game paints the Kestrel from the nets.

## Polish (2026-10-04, agent; user offline)

- **Blue flank bands back.** recolour.py had turned the hull and nose nets silver (their medians are silver, so Grok's few blue wedges were not kept). `nets/blueband.py` repaints by rule in model space: the hull's and nose's flanks deep blue below a line 0.075 under the top edge, the outer part of their top faces blue (the nose keeps a silver ridge), a cyan strip on each flank (it glows). The blue is Grok's own tailplane blue scaled by each pixel's brightness, so panel lines and chips stay; it turns red for red. Chosen over a new Grok round: the concept's bands are simple geometry, and a rule puts them exactly where the concept has them. Installed from `nets/grok/v1b/`.
- **Canopy.** `Models.kestrelGlass`: dark smoked glass, deeper at the sills and teal-blue toward the crown, a sky rim at grazing angles, two glint streaks (a chevron over the upper panes) and a faint cyan instrument glow low at the front. No pilot: the look asks for the crew sealed out of sight. Metallic, because at metalness 0 the crown mirrored the sky pale teal from the high camera. Frame: the two bars keep their dark gunmetal (their nets painted them silver, a bright cross from above), plus sills along both lower edges, a bow at the back and a second cross bar, all added after the skin.
- Renders: `previews/polish-model.png` (yaw 0.8, 2.6, red, busy), `previews/polish-ingame.png` and `previews/polish-before-after.png` (playground, before left, after right).
