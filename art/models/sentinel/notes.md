# Sentinel: model notes (2026-10-04)

The user was offline; every choice below was made by the agent, with the reason.

## Concept round 1

Six directions (`concepts/prompts.md`, one `prompt.<slug>.txt` each), each in
its own new Grok chat, with `concepts/base.jpg` attached (the Bastion, Hab Dome
and Garrison as `Autocraft preview` renders them). All six came back usable on
the first answer, five 1168×784 and the tall `radar` 784×1168. Contact sheet:
`previews/concepts-c1.jpg`.

**Chosen: `c1-yoke.jpg`.** Why:
- From the game's high camera the two box launchers are the first thing you
  see. Raised toward the sky they turn their tube faces (six red missile noses
  each) up toward the camera, which says "anti-air missiles" at wallpaper size.
- Its square bunker with clipped corners fills the 2x2 pad the way the Hab
  Dome and Garrison fill theirs: square blocks, silver-grey panels, blue team
  trim, hazard-striped plinth. The round ones (pillbox, dome) looked like a
  smaller Bastion, and a Sentinel standing next to a Bastion should not be
  mistaken for one.
- The yellow lamp pips along the bunker's top edge are the "working" light the
  building-visuals skill asks for, already in the concept.
- The yoke and trunnions give the existing animation hooks a real mechanism:
  the head turns on the turntable (`head.eulerAngles.y`), the pods pitch on
  their trunnions (`pod.eulerAngles.z`).

Not chosen: `radar` (tall and thin, small pods: weak from above), `tripod`
(open frame reads as clutter when small), `rails` (four launchers, more than
the design's two pods), `pillbox` (close to the blockout, too much like the
Bastion), `dome` (good, but round like the Bastion).

No second round: the yoke concept already answered the question (shape,
materials, lights), and the model follows it in primitives.

## Approach: procedural, not the turnaround pipeline

The model is a refined procedural model (`Sources/Autocraft/Models+Sentinel.swift`),
built from the concept, using the shared building materials. The turnaround
and texture pipeline was not used. Why:
- Every other building (Citadel, Hab Dome, Garrison, Bastion, Derrick, Foundry,
  Spacedock, Lab) is procedural with the shared panelled hull materials
  (`hullLight`, `hullMid`, `hullDark`, `team`, `hazard`). A painted skin would make
  the Sentinel the one building with a hand-painted look, which stands out in a
  base far more than in an army of distinct units.
- The light language needs its lamps as separate materials driven by
  `GameScene.animate` (yellow pips, sensor slit, beacon). A painting bakes
  lights into the albedo, so they would have to be cut back out anyway.
- The concept is made of boxes, prisms and cylinders: primitives with the hull
  texture match it closely without paint.
- The pipeline is built around units (it turns buildings to face +X, and the
  painting and nets steps cost several Grok rounds per model). For a building
  the concept step (step 0) is what pays: it settled the shape in one round.

## The model

- Plinth: dark clipped-corner slab, hazard lip on all four edges.
- Bunker: clipped-corner block (`Models.clippedSquare`, texture in metres so the
  hull panels keep one size), dark skirt, blue team plates on the four corner
  faces and one low on the front, a dark recess round the top edge with five
  yellow pips per face (`workGlow`), a lighter cap.
- Turntable: fixed dark ring on the roof; the head's disk turns on it.
- Head (`head`, turns about Y): yoke cross-beam, a light spine with a blue
  lower plate, the yellow sensor slit on its front, the mast and red beacon on
  top; a trunnion each side.
- Pods (`podLeft`, `podRight`, pitch about their Z): box launchers with a blue
  outer plate, a dark rear cap, a light tube frame and six tubes (two columns,
  three rows), each with a matte red missile nose (paint, not a light: the
  only glowing red is the beacon). The top row's two tubes are the muzzles.
- Hooks unchanged: `Models.Sentinel` keeps `root`, `beacon`, `head`, `pods`,
  `muzzles` (2 per pod), `flashes` (1 per pod), `workGlow`; `GameScene`,
  `NightLamps`, `ModelCatalog` and `ModelViewer` use it as before.

- `GameScene`: the Sentinel's scaffold is 1.6 high and its `height` 1.7 (were
  1.9 and 2.0): the model is about 1.66 to the beacon's top.
- Night: `NightLamps` first hung the pool at (0, 0.9, 1.2), reach 2.6, from the
  blockout's waist ring; it washed the roof and the launchers yellow
  (`previews/v3-night-crop.png`). Moved (polish, 2026-10-04) to (0, 0.55, 1.15),
  reach 2.4: just out from the front row of pips (y 0.42, face at z 0.82), so the
  pool lies on the apron and the head stays in its own light
  (`previews/polish-night.png`, `polish-night-before-after.png`; the red on the head
  there is the beacon's blink, caught lit). README's "Buildings show what they are
  doing" line now describes this model.

## Iterations (renders in `previews/`)

- `blockout.png`, `ingame-blockout.png`: the old blockout, before this work.
- `concepts-c1.jpg`: the six concepts.
- `v1-idle.png`, `v1-busy.png`: first build. The hazard lips were wide and
  flat on top and glared; the missile noses were glossy red and read as lights
  (red is the beacon's colour only).
- `v2-*`: hazard strips moved to the plinth's faces, grey noses in dark-red
  bores (as in the concept), blue edge on the cap and blue bands on the pods.
  In the game (`v2-ingame.png`) it was darker and smaller than the Hab Dome.
- `v3-sheet.png` (idle, busy, in game beside the Hab Dome), `v3-idle.png`,
  `v3-busy.png`, `v3-ingame.png`, `v3-night.png`: body in the light hull, cap in
  the mid hull, a lighter turntable, launchers 10% bigger. Kept.

## Stencil test

`model.sh stencil sentinel` was run to see whether the pipeline takes a
building: `stencil.png`, `stencil.json`, `stencil.parts.png` and
`stencil.skin.json` are here. It works (98 parts), but the skin plan sees only
29% of the surface (69% nearby pixel). They are left here in case the Sentinel
is ever painted; nothing reads them now.
