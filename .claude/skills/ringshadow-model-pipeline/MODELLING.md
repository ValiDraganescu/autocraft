# Modelling a model in Unreal

How a model script builds the meshes and the catalog entry the game reads. Read it at steps 1, 2, 5 and 6 of [SKILL.md](SKILL.md). The kit and the stencil writer exist.

## Where things live

| What | Path |
|---|---|
| Model script, one per model | `unreal/Tools/Editor/models/<model>.py` (`peregrine.py`, `atlas.py`, `scorpion.py` first) |
| Shared helpers | `unreal/Tools/Editor/ac_modelkit.py` |
| Meshes | `/Game/Models/<model>/SM_<part>__<material>` (`<model>` is the base name, no team suffix) |
| Material instances | `/Game/Models/Materials/MI_<name>_<key>` |
| Master materials | `/Game/Materials/M_Hull`, `M_Emissive`, `M_Additive`; team palette `MPC_AcTeams` |
| Catalog, read at runtime by `FAcModelCatalog` | `unreal/Content/Models/ModelCatalog.json` |
| Pose code | `unreal/Source/Autocraft/AcPose*.cpp` |
| Ray code | `unreal/Source/Autocraft/AcRaysWorld.*` |
| Art | `art/models/<unit>/` |

Existing models came from a Swift export (USDA in `unreal/Content-src/models`, imported by `import_models.py` with `ac_models.py`). That path ends with the Swift move. `import_models.py` stays the reference for how a mesh becomes an asset: Geometry Script `GeometryScript_MeshEdits`, `GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh` and `GeometryScript_AssetUtils.copy_mesh_to_static_mesh`, collision off (the game casts its own rays), Nanite on for opaque meshes of at least `NANITE_MIN_TRIANGLES` (1000), asset metadata tags `AcSource` (a hash, so an unchanged mesh is skipped) and `AcMadeBy`.

## The model script

One script builds one model (a unit, building, scaffold or cockpit) from Geometry Script primitives: the `unreal.GeometryScript_*` Python classes (box, cylinder, sphere, extrude, revolve, mesh booleans, transforms). `import_models.py` already uses `MeshEdits`, `NewAssetUtils` and `AssetUtils`; look up the primitive, boolean and transform classes in the editor's Python API before using them (their names are not verified here).

The script:
1. Declares the parts as a tree: name, parent, rest transform, and the meshes it holds as (material, `DynamicMesh`) pairs.
2. Builds each part's meshes in the part's own frame, merged per material.
3. Writes one static mesh per (part, material) at `/Game/Models/<model>/SM_<part>__<material>`, skipping a mesh whose hash is unchanged (the `AcSource` tag).
4. Writes the model's catalog entry (below) and nothing else in the catalog.
5. Is rerunnable: same input, same assets.

All of that is `ac_modelkit.py`: `Model(base, category)`, `part(name, parent, at, rot, driver, hidden, ...)` (pivot and rotation in the model frame), `mesh(part, material, prims)` with the prims `box`, `cyl`, `cone`, `sphere`, `prism`, `loft`, `bar`, `material(name)` (reuse) or `material(name, parent=..., color=...)` (new), `write()`. `write()` also writes `art/models/<unit>/parts.txt`. `check_modelkit.py` (hidden run) checks it. The model script then holds only the shape of the model.

### Space and winding

- Author in UE axes: X forward, Y right, Z up, centimetres. A unit faces +X, a building faces +Y (SceneKit +Z). One map cell is 100 cm, a Ranger is about 160 cm tall.
- Meshes are stored in UE space in the part's frame, with the part's rest transform not baked in.
- Catalog transforms (`localTransform`, `position`, `eulerAngles`, `scale`, `boundsMin`/`Max`, `rays`) stay in SceneKit space: Y up, cells, row-vector 4×4 matrices with the translation in elements 12 to 14. `AcSpace` converts at load: SceneKit (x, y, z) is UE (x, z, y) × 100 cm. So the helper converts UE to SceneKit when it writes the entry. Check the conversion once by round-tripping an existing part (the Longbow's `hull` entry through the helper and back) before trusting it.
- Geometry Script primitives are already wound for UE. The index-order rule in `import_models.py` is for SceneKit data only.

### Parts

A part is a rigid piece with a name. Make a part of anything that:
- pose code moves (found by name through `FAcPartIndex`, so the name is the contract: copy the names the pose file will use, e.g. `hips_0`, `engines_0`, `glow`);
- starts hidden (flashes, spare frames), or changes visibility;
- carries a light or particle socket (a part with no mesh);
- the ray code or shatter code treats separately.

Everything rigid between two such parts merges into the parent per material. Aim for 5 to 15 moving parts per unit (`GAME-LAYER.md` §3.3): every part costs instance transforms per frame, per unit.

### Part and driver data

Each catalog part carries what the three readers need:
- `name`, `path` (`/<model>_blue/<parent>/<name>`), `parent`, `handles` (names the pose code may use; the Swift ones in old entries are history), `localTransform`, `position`, `eulerAngles`, `scale` (rest values the pose overwrites; euler x pitch, y yaw, z roll, applied roll then yaw then pitch), `hidden`, `opacity`.
- `driver`: `what` (plain words: walk cycle, turret aim, status lamps), `channels` (rotation, position, transform, scale, visibility, emission, material, particles), `functions` (the C++ function that moves it, `AcPoseAir.cpp:PoseKestrel`; the old entries still name Swift functions).
- `light` / `particles`: socket parameters, for lamps and Niagara.
- `rays`: the solid pieces the ray code tests, in the part's frame. Each piece is `{hi, lo, transform}` (bounds and a SceneKit matrix) plus `radius` and `round` (`sphere`, `cylinder`, `capsule`) for true round shapes. `AcRaysWorld` reads them, and "a shot between a Ranger's legs misses" is a rule, so every solid primitive gets a piece and gaps stay gaps. The kit derives pieces from the primitives (box, cylinder, sphere, flat prism boxes, a loft as one box per segment).
- `meshes`: one entry per mesh: `material`, `prim` (`<part>__<material>`), `mesh` (object path), `materialInstance`, `materialKey`, `blend` (`opaque`, `additive`, `translucent`), `team` (flags below), `triangles`.

The model entry: `name` (`<model>_blue`), `file` (omit or empty), `category` (`unit`, `building`, `construction`, `cockpit`, `resource`, `doodad`, `effect`), `facing`, `boundsMin`/`boundsMax` (rest pose, hidden parts left out), `triangles`, `team` (`blue`), `base` (`<model>`), `canonical` (`<model>_blue`), `notes`, `parts`. The renderer looks up `<base>_blue` and `scaffold_<base>_blue`; cockpits are `cockpit_<kind>_blue`. Only the blue entry is needed: red is the same meshes with team index 1.

A new kind also needs `AcPose::ModelBase(kind)` to return the base name; the pose function, cockpit and effects are game code (`docs/new-units.md`, "Where it lands in the code").

## Materials and team colour

Blue is canonical. The team colour is not baked: per-instance custom data slot 0 is the team index (palette of 8 in `MPC_AcTeams`), slot 1 the emission scale. A material takes the team through scalar flags on its instance (`make_materials.make_instance` and `ac_models.TEAM_FIELDS` are the source):
- `TeamTint` = 1: the hull trim takes the team's tint colour (the `team` material).
- `PaintBase` / `PaintEmissive` = 1: blue paint in the texture is recoloured (painted skins).
- `TeamGlow` = 1: the emission takes the team's lamp colour.

A model script reuses an existing `MI_<name>_<key>` where one fits (the shared hull materials: hull light, mid and dark, hazard, team, chrome, the glow family; list `Content/Models/Materials`) and asks the helper for a new one otherwise. The kit calls `make_materials.make_instance` for a material described in the script instead of in a manifest: parent `M_Hull`, `M_Emissive` (unlit lamps) or `M_Additive`; parameters `BaseColorTint`, `Metallic`, `Roughness`, `EmissiveColor`, `Opacity`, `UVScale`, the four team flags. Lamps are their own materials and their own parts, so each can pulse alone (the `ringshadow-building-visuals` skill has the light language for buildings). Keep one material key per look: the same key in two models means one instance.

## The stencil files

`split.py` reads three files from `art/models/<unit>/`; the UE writer (`stencil.py`) produces them in the format the Swift stencil used (`art/models/kestrel/stencil.json` is a live example):
- `stencil.png`, 1536×1024: six 512 px panels in a 3×2 grid, order FRONT, RIGHT, BACK, LEFT, TOP, 3/4. Grey clay, parts outlined, thick black lines between panels on white, panel names, a grey header line, dashed quarter-height guides and the ground line (the painting should drop all of these; split ignores them).
- `stencil.json`: `size`, `lineWidth`, `bounds`, `model`, and per panel `view`, `rect` (x, y, w, h from the top left), `right` and `up` (model axes along the panel), `toCamera`, `center`, `pixelsPerUnit`, `groundRow`, `orthographic`. A model point p lands at column w/2 + dot(p − center, right) × ppu, row h/2 − dot(p − center, up) × ppu. Axes and units are SceneKit's (Y up, cells, +Z the unit's right) so every existing tool keeps working; FRONT looks at the model from +X, so the unit's right is on screen left; TOP has the front at the top. The five orthographic panels share one scale, one ground line and one centre.
- `stencil.parts.png`: the same sheet with one flat colour per part (the exact silhouette and which part is where).

Render them with `model.sh stencil <unit>`: twelve hidden runs of `AAcModelRow` (`-AcModelView=front|right|back|left|top|3q`, `-AcModelPaint=clay|parts`, 512 px, orthographic for the five), then `stencil.py` composes the sheets. The clay is lit grey (`M_AcStencil`, a fixed key and fill light, no sky), like the Kestrel's sheet; the part-colour shot gives the outlines and `stencil.parts.png`. Opaque meshes only (hidden parts, halos and glass stay out). A 3/4 panel is for the painter only.

## Review

`-AcModelFocus=<model>_blue [-AcModelTeam=N] -AcShot=/abs/out.png` in `/Game/Maps/ModelRow` (SKILL.md, "Hidden runs"). `AAcModelRow` builds from the catalog, so a new entry shows up with no other change. Use a 16:9 `-ResX/-ResY` and crop the centre square (`AcShot` crops other aspects). The row also has a palette row (the Ranger and the Citadel in all 8 colours) for a team-colour check.

## Facts and hiccups (UE modelling)

Add one line per fact: date, model, what happened, what to do instead.

- 2026-10-05, kit: hidden runs need the absolute `.uproject` path (a relative one makes the launcher look under the engine folder). `-run=pythonscript` models take about 15 s.
- 2026-10-05, kit: Geometry Script lists (`GeometryScriptVectorList`) have no `.list`; use `GeometryScript_List.convert_vector_list_to_array` / `convert_triangle_list_to_array`. `append_revolve_polygon` takes (radius, height) points and offsets them by its `radius` argument.
- 2026-10-05, kit: a Geometry Script box's triangles have (b-a)x(c-a) pointing into the solid; the hand-made loft copies that. `Rotator(roll, pitch, yaw)` positional; `Transform.multiply(b)` is a then b. All checked by `check_modelkit.py`.
- 2026-10-05, Longbow hull round trip: its SceneKit ray pieces through `sk_to_ue` bound the hull's static meshes to within 0.3 cm.
- 2026-10-05, catalog merge: `json.dump(indent=1)` reproduces the existing file byte for byte, so replacing one entry leaves the others identical.
- 2026-10-05, Peregrine: docs/new-units.md says "about half the length of a Kestrel"; the task said about 320 cm. The blockout is 327 cm (the Kestrel is 226). Decision (2026-10-05, user): keep 327 cm for now, adjust after gameplay.
- 2026-10-05, a hidden run failed with "game module could not be found" while another session was linking the module; wait and retry.
- 2026-10-05, stencil: the model row's sky and atmosphere show at the horizon of a side view; `-AcModelView` hides them and lights the clay with two directional lights. Clay colour 0.30 linear matches the Kestrel sheet's grey. Self-test: `split.py` with the Atlas's own `stencil.png` as the painting gives IoU 0.99 to 1.00 in all six views and the sizes (2.24 x 2.14 x 2.99 cells) agree with the catalog bounds; the json has the Kestrel's keys.
- 2026-10-05, stencil: a shot takes about 20 s on an idle machine and a minute on a busy one; a sheet is 12 shots. A shot fails with "game module 'Autocraft' could not be found" while another session links the module; wait for `unreal/Binaries/Mac/libUnrealEditor-Autocraft.dylib` and rerun.
- 2026-10-05, stencil self-test (`split.py` with each unit's own `stencil.png` as the painting): Atlas 0.99 to 1.00 in all views; Scorpion 0.98 to 1.00 except 3/4 0.91 (thin legs); Peregrine 0.91 to 0.99 (front and back 0.91: `split.py` opens away thin horizontal lines, and the Peregrine's 4 cm wings and wing-tip pods read as such in FRONT and BACK; right/left lose 2% of length for the same reason with the needle tip). A real painting of a very thin wing will hit the same limit: the stencil is right, the mask opening is the cause.
- 2026-10-05, stencil: the 3/4 camera fits the bounding sphere at 0.55 of the half FOV (0.72 cut the Peregrine's wing tip).
- 2026-10-05, step 5 (Peregrine, Atlas, Scorpion): `Model.mesh(..., solid=False)` marks decoration (rivets, rings, light strips, slats, panel lines): drawn, but no ray piece (every solid prim otherwise adds one to the catalog `rays`). Cockpit copies are all `solid=False`.
- 2026-10-05, Peregrine: fitted IoU after the refinement: front 0.53, right 0.95, back 0.57, left 0.95, top 0.94 (blockout 0.52 / 0.96 / 0.54 / 0.95 / 0.94). The stencil's height grew 3% when the missile fins hung 3 cm below the old lowest point; split fits by bounding box, so a thin detail that sets a new extreme (a fin, a pitot, a spike) shifts the whole fit. Keep the extremes of a silhouette where the blockout had them, add detail inside. A pitot thinner than about 1 cm radius vanishes in the 512 px stencil (mask opening): the loft still ends at 195 cm with half-width 0.6 and the pitot is a dark cone laid over its last 14 cm.
- 2026-10-05, Atlas: refined IoU 0.96 / 0.96 / 0.97 / 0.96 / TOP 0.94 against 0.98 / 0.97 / 0.98 / 0.97 / 0.93 for the blockout. Leg pistons on the inner face of a leg and a longer heel toe showed as stencil-only strips (cyan) in compare.png: put them on the leg's centre line and inside the old foot outline.
- 2026-10-05, Scorpion: the painting is fatter than the blockout everywhere (tail, launcher, legs, belly). Thickening to it raised the fitted IoUs from 0.82 / 0.82 / 0.82 / 0.83 / 0.84 to 0.89 / 0.89 / 0.89 / 0.90 / 0.85 (front, right, back, left, top).
- 2026-10-05, cockpits: the contract (AcCockpitKinds.cpp, Longbow pattern) is rig > level > model_root > model_<unit part> (the unit's parts copied with the same pivots and parents, so the unit's pose function drives them) plus cockpit-only geometry on `rig` and muzzle sockets. `unreal/Tools/Editor/ac_cockpitkit.py` runs the unit's script with its `m.write()` taken out and copies the chosen parts, so nothing is modelled twice; `models/cockpit_<kind>.py` says which parts, what to leave out of a part (the Peregrine's canopy, most of the Atlas torso) and the `rig` frame. `rig` is authored in the camera frame, UE X right, UE Y behind, UE Z up (`cam(ahead, right, up)`); the `model_*` parts keep the unit's model frame (the pose code turns `model_root`). The eye points (AcPilotCamera::Eye) are model-frame cells: Peregrine (0.45, hover + 0.15) with the body pivot lifted to the hover by the pose, Atlas (0.55, 2.2), Scorpion (0.7, 0.62).
- 2026-10-06, cockpits: the first-person cockpit is drawn at `ac.CockpitScale` (0.5) and the pilot camera's vertical field is 62 degrees (`FAcPilotCamera::FovDegrees`), so rig geometry authored in the camera frame shows about 30 percent narrower than `atan(size/ahead)` says: aim wider than the window you want, then look at a shot. The unit's own parts are in the model frame (cm, +X forward, body pivot) and the eye is `AcPilotCamera::Eye` in cells from the unit root: put the eye inside the part you want to look out of (Peregrine: front of the canopy, 21 cm over the fuselage axis) and filter out the unit prims that lie right under it (copy_parts `keep`).
- 2026-10-06, cockpit_scorpion: the unit's tail rises over its own back, so from any eye on the body it is overhead and behind. A cockpit may move parts that way: shift the part `at` AND every prim (a Loft's `sections` too) of tail_0..3, launcher and tailLamp by the same vector; the children keep their rests, tail_0's differs and `FState.Adjust` carries the unit's pose over. Parts whose prims are all filtered out stay in the tree as empty pivots.
- 2026-10-06, cockpit_atlas: a unit pose that pitches a gun at a close crosshair point sends the barrels out of the slit; the cockpit pose takes a part of the pitch (`Slerp(rest, posed, 0.3)`, as the Longbow's cannon does) and leaves the recoil whole.
