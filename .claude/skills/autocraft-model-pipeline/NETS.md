# Nets, decals and paint on meshes (parked)

Read this only when someone asks for painted texture on a UE model. Until then a UE model is geometry plus shared materials (MODELLING.md), and the painted turnaround is a proportion and colour reference (SKILL.md, steps 3 to 5).

## Why it is parked

The paint path was built on the Swift game: its stencil wrote a **skin plan** (which view paints each triangle) and its `nets`, `netspreview`, `install` and `install-nets` commands unfolded parts, put paint back on the model and loaded it. Those commands are gone. Nothing in UE writes a nets sheet, and nothing in UE projects paint onto a `SM_<part>__<material>` mesh. The existing painted skins (the models painted from nets or the turnaround) arrived in the UE game as baked textures from the old export; their team recolour is the material flag `PaintBase` (MODELLING.md).

To build, in this order, when the user wants it:
1. Projection in the model script's import: texture coordinates per triangle, from the view that sees it (the skin plan) or from the part's own box faces (nets), written into the static meshes' UVs.
2. A nets writer on the UE stencil: `nets.json`, `nets/nets.N.png` (blank clay), `nets.N.painted.png` (the turnaround's paint on the faces it saw square-on).
3. An importer that turns the painted sheets into `T_*` textures and an `MI_*` with the `PaintBase` flag.

## The idea

A **net** is one part alone, seen along its own six axes in orthographic clay and laid out as a cross: front in the middle, +Z, −Z, −X in the row, +Y above, −Y below, so faces that meet on the part meet on the sheet. Nothing hides anything and no face is seen at an angle. Grok paints the sheets (the grey faces are blank clay, the faces the turnaround saw come prefilled), the colours are fixed afterwards, and each triangle takes its paint from the net face its normal points at most. Twins built from one geometry share a net; parts under 0.03 units are left out.

## Tools in this folder

They read the files a nets writer must produce; none of those files can be made today.
- `legend.py <unit dir> <sheet>`: the `{LEGEND}` of `nets-rules.txt`, from `nets/nets.json` and `<unit>/parts.txt` (tree path, name, look; a part with no line stops it). The nets are shelf-packed, so a row is the nets sharing a top edge. `<unit>/noun.txt` names the unit for `{UNIT}` ("space Ranger" without one).
- `tint.py <unit dir>`: `nets.N.tinted.png`, the prefill with every clay face in the colour of the first material word of its `parts.txt` look (one colour per material, the median of the turnaround's paint on that material's nets; lights and the gold visor left out). Never send the tinted sheet to Grok: it reads tinted faces as finished.
- `recolour.py <unit dir> N <image>`: puts the prefill back on faces it covers at least half of (`PASTE_MIN`), eases Grok's paint into it at the edges, and moves Grok's grey paint to each part's material colour, keeping relative contrast. It keeps Grok's own colours on a net only when the part's `parts.txt` look names a paint colour (`PAINT_HUES`: deep blue, hazard, yellow, red) and Grok's median is that hue (`HUE_MAX` 40, `CHROMA_MIN` 8); a net whose look names none loses the team blue its prefill brought (`TEAM_HUE` 210). `--debug` prints each net's hue test.
- `netscheck.py <unit dir> N <image> [--given painted|tinted|blank]`: fits Grok's image back onto the sheet and prints outline IoU, how much the painted faces changed, how much of the unpainted faces Grok painted, local contrast and tone per material. Measure with it, not by eye: unfitted numbers were off by 2 to 4 times.
- `relayout.py <unit dir> <old dir> <new dir> [--fresh P]`: carries the approved paint of nets with unchanged paths onto a new layout, so only changed parts go back to Grok.
- `install_nets.py <unit dir> <pattern> <textures dir>`: writes `<unit>_nets_N.jpg`, `_nets_N_glow.jpg` and `<unit>_nets.json` for the game to load; the textures dir is now `unreal/Resources/Textures`, but UE has no loader for them yet. It fills every pixel off the nets with the nearest net colour (2 px outline included) so mipmaps do not bleed white.

## Painting nets with Grok

The prompt is `nets-rules.txt` (v9.1; log in PROMPTS.md). Call `mcp__orchestrator__grok_image` with `reference_images` = `[nets.N.painted.png, painted.jpg]`, the sheet's own `prompt.N.txt` as `prompt`, `name` `nets-<N>-v<version>`, `output_dir` `<unit>/nets/grok/v<version>/`; one call per sheet, all sheets can go one after another, each with `painted.jpg` attached again (no carry-over between sheets). The prompt names each image by file name; keep the names.

What Grok does:
- Never attach the blank `nets.N.png`: with nothing painted Grok draws free-standing objects from the legend's words (layout IoU 0.34, half its ink off the nets).
- It paints grey clay in full detail but all one light grey, whatever colour the legend names; `recolour.py` sets the colour afterwards.
- It leaves tinted faces nearly as given (8 to 16% of unpainted pixels changed on sheet 2), hence no tinted sheet.
- A prompt that ends on a "Done when …" checklist gets a text answer with no image (3 of 3); the last line asks for the painted sheet as one image. "Paint over the attached image" opens every prompt that made an image.
- A crowded sheet is painted thinly (48 nets: detail 12.9 after recolour). `--max-nets 12` (a nets-writer option) gives sheets of 150 to 1650 px per unit; the Ranger took 58 nets on 5 sheets.
- Spheres and thin shells unfold into discs and arcs that say little alone, so the painted turnaround goes along as the second image.
- Thin arm and strut nets are drawn wider than their outlines and a thin chrome cylinder became a coil spring (Firefly, layout 0.76 on that sheet against 0.91 to 0.97). `recolour` masks to the outline, so the part stays thin; look at the preview before re-rolling.
- Paint bands the recolour drops (Kestrel): a mostly silver net with blue flank bands came back all silver. `kestrel/nets/blueband.py` puts them back by a rule in the part's own coordinates, Grok's own deep blue scaled by brightness; bright cyan strips land on the glow sheet.
- A small hole in the paint is the clay's own paint colour (the shield's white star) when it differs from the blank sheet by at least `HOLE_DIFF` (3) grey levels; a big hole framed by a painted rim stays clay.
- Repainting one region: lay the changed parts out on sheets of their own, leave their prefill off (clay only), carry the rest with `relayout.py`, and swap the prompt's "painted faces are finished" paragraph for one saying what the sheet is (`comet/nets/prompt.4.head.txt`).
- A reshaped part keeps its tile rects when its bounds stay the same, so only sheets whose rects moved need Grok again (Comet wing cases: one new answer, three sheets reused).
- Rebuilt parts that reuse a tree path inherit the old part's painted frame; rename the moved entries before restencilling and relayout with `--fresh`.

## Decals: flat designed faces

A flat face with a design (the Ranger's shield: a glowing ring and a star) takes its paint from two or three views at an angle, and the image AI rarely puts the design where the geometry is (the ring sat 14% off centre on the first painting). Such a face keeps its own texture coordinates and its own texture: list it in `<unit>/decals.json` (fields in `decal.py`'s header; the Ranger's shield is the example) and run `model.sh decal <unit>`. It reads the whole face from one view, puts the ring at a fixed inset (`ring`), the emblem on the centre line (`centre`) and, for a symmetric design with one clean half, mirrors it (`mirror: "right"`). Look at `decals/<name>.check.png`. `decal.py` needs `paint-pose.json` (each part's frame when painted) from the stencil writer.

## Projection facts

- A shell that curves smoothly from facing out to facing sideways crosses the 45° tile boundary on sliver triangles and paints a sawtooth of two views. Build curved shells with creases where the paint changes: a dome under 40° on a folded lip, each face its own vertices (the Comet's wing cases). A sphere unfolds into six ragged tiles; a head that needs clean paint is six patches, each built from its own normals.
- Mirrored shells are not twins (a band built with its range mirrored is different geometry and needs its own net and `parts.txt` line); same-shape parts placed mirrored share a net.
- Hoses, glass and spinning parts keep their own material and stay out of the paint; swappable gear is built after the painted parts as the last child, so every painted part keeps its tree path.
- The skin plan's split on the Ranger was 41% of the surface seen by a view, 56% nearby pixel, 3% swatch. A squat building's stacked tiers hide each other's undersides (29% seen, 69% nearby pixel for the Sentinel), which is why buildings are modelled from the concept, not painted.
- Glass seen at a grazing angle under the high camera mirrors the sky pale (the Kestrel's canopy went light teal): keep it metallic with a dark tinted base.
