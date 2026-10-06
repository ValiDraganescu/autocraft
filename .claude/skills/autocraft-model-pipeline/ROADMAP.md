# Roadmap: after the fitted views

Agreed with the user on 2026-09-30, for the Swift game; reread for Unreal on 2026-10-05 (the paint path is parked, NETS.md). None of these exist yet. Open texture issues: seams between views are hard cuts (no blending); a surface no view sees takes one nearby pixel per small triangle, so neighbours alternate colours in a checker (the Ranger's pauldron undersides; 56% of its surface is filled this way); and team colour still comes from repainting blue pixels rather than a mask. Flat designed faces are handled by decals (SKILL.md, Decals).

1. **Module file.** A JSON per unit (`art/models/<unit>/modules.json`): each module's name, purpose, parent, pivot, mirror pair, material or texture region, animation role (the part names the `AcPose*.cpp` pose code finds: `gun`, `head`, legs…), and primitive. The model script's part tree (MODELLING.md) is its first draft; `stencil.parts.png` says which pixels belong to which part.
2. **Per-part comparison.** Score each part, not only each view: the part's pixels in `stencil.parts.png` against the painted mask around them. A view's IoU misses a part that changes shape inside the same outline (the Ranger's boxy front pauldrons).
3. **Data-driven builder with hot reload.** `ac_modelkit.py` reads the module file (box, panel, cylinder, sphere patch, outline slab) and builds the meshes, so a model is data instead of a script; each try is one hidden editor run.
4. **Animation check.** A strip of frames from `AcPoseSheet` (walk, aim, fire, death; `unreal/SHOTS.md`) to catch pivots in the wrong place before the game.

Option noted, not chosen: Hunyuan3D-2mv makes a mesh from four views, which could serve as a third reference for proportions; it gives no parts, rig or team colour.

## Proposed: bake a texture atlas instead of projecting (2026-09-30)

The user asked to rebuild the skin from the painting rather than project it. Measured on the v4 Ranger painting (clear pixels 3 px inside each part, 70 of 96 parts): silver 45%, gunmetal 35%, blue paint 19%, glow 1%, gold 1%; only 13 parts are 90% or more one material. The gunmetal share is mostly the style itself (dark outlines round plates, shaded seams, grime), so one flat material per part would lose the look the user wants. Rebuild the skin per part in its own texture space instead:

1. **Charts.** Unwrap each part: triangles grouped by their main axis (a box projection), one rectangle per group in a per-unit atlas.
2. **Bake.** Each texel takes every view that sees its point, weighted by how square-on the view is and how far the point is from an occluding edge: soft seams instead of hard cuts.
3. **Fill.** Texels no view sees (57% of the Ranger's surface today, the pauldron checker) are filled from the same part's painted texels by patch-based synthesis, not one nearest pixel per triangle; optionally the image AI inpaints the atlas's holes.
4. **Material maps.** The material classes above give a team mask (the team colour applied properly instead of repainting blue), a glow mask, and metalness and roughness per class.
5. The atlas is an ordinary image: it can be touched up by hand or by the image AI, and decals become regions of it.

