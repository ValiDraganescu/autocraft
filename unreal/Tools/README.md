# Tools: models from the Swift game

**Retired 2026-10-05.** The Swift game left the repo (archive:
`~/git/dev/autocraft-swift`, `docs/swift-move.md`). The export below, the
rays golden, `hud/bake_icons.sh`, the Swift side of `shots/compare.py` and
`bench/bench.py --swift` can no longer run here. What they made stays:
`Content-src/models` on disk (not in git) and the imported meshes in
`Content/Models`. New models are built in Unreal (the
`autocraft-model-pipeline` skill). `hud/bake_hud.sh`, `cursors/bake_cursors.sh`
and `Editor/import_sounds.py` still work from copies of the Swift files they
need, kept in `swift/` beside them.

The Swift game builds every model in code (`Sources/Autocraft/Models*.swift`).
`Autocraft export-models` writes them out for Unreal: one USDA file per
model, the textures as PNG, and `manifest.json`, which lists each model's parts
and what moves them.

## Run it

```sh
make                                     # builds only
.build/release/Autocraft export-models unreal/Content-src/models
```

This takes about 20 s. It writes 112 models, about 244 PNGs and the manifest,
about 430 MB in all. `unreal/Content-src/` is in `.gitignore`: run the export
again after a model changes.

Each part in the manifest also lists `rays`: the bounds (or true sphere,
cylinder, capsule) of every SceneKit geometry node baked into it, in the
part's frame, for the ray code (`AcRaysWorld.h`); `import_models.py` copies
them into the catalog. With `AUTOCRAFT_RAYS_GOLDEN=$PWD/unreal/core-tests/golden/rays.json`
set, the export also writes what the Swift ray code computes, for the
`Autocraft.Rays.*` tests (`Sources/Autocraft/ExportModels+Rays.swift`).

Options:

- `--only ranger_blue,ore_0` (or `--only ranger` for both teams): export only
  these models. The manifest then lists only them.
- `--all-visible`: leaves out `visibility = "invisible"` on parts that start
  hidden (muzzle flashes, spare tread frames, the Mini gun). Use it if the
  importer skips invisible prims. The manifest still marks them as `hidden`.
- `export-models --compare NAME FILE.usda OUT.png`: renders the model as the
  game builds it (left) next to the USDA loaded back into SceneKit (right),
  with the same camera. It reads the hidden parts from the manifest beside the
  file.

Only run the binary after `make` succeeds. An older binary does not know the
command and starts the game instead.

## What is exported

| Category | Models |
|---|---|
| `unit` | the 9 unit kinds, `_blue` and `_red` |
| `building` | the 9 building kinds, `_blue` and `_red`, built state |
| `construction` | `scaffold_<building>`, the construction scaffold at each building's size |
| `cockpit` | `cockpit_<unit>`, the first-person view models |
| `resource` | `ore_0..3` (the seed changes the shape), `well_0`, `well_3` |
| `doodad` | every `Doodad.Kind`; `_0..3` for the kinds whose shape depends on the variant. Border rocks are rock, boulder and spire doodads. |
| `effect` | `tracer`, `minigun_round`, `plasma_slug`, `impact_flash`, `grenade`, `anchor_shell` |

The team colour is baked into the materials (the `team` tint and the skins'
painted textures), so every unit, building and cockpit is exported once per
team. The two files share the same meshes and differ only in materials.

Buildings have no separate construction-stage models. While a building is
being built, its body rises out of the ground
(`position.y = -(1 - progress) * height * 0.92`) and its scaffold shows. Each
model's `notes` give the height.

## Format: hand-written USDA

`SCNScene.write(to: .usdz)` keeps the textures, but it drops SceneKit's
`multiply` tint, which most hull materials use. It also renames every
material `Material_N`, writes a bogus normal input, and in `.usda` form points
at textures in a temporary folder. ModelIO's export nests every node in a
`Scope` and loses the material bindings.

The exporter therefore writes USDA text itself:

- `UsdPreviewSurface` materials.
- `UsdUVTexture` reading PNGs from `textures/`.
- A metallic workflow.
- Indexed meshes with vertex normals and `primvars:st`.

UE 5.8's USD importer reads all of this.

- **Axes and units:** `upAxis = "Y"`, `metersPerUnit = 1`. One unit is one map
  cell, and a Ranger is about 1.6 tall. Units face +X (the scene sets
  root yaw = -heading). Buildings face +Z.
- **Hierarchy:** `/<model>` is the root part. Each part is an `Xform` with its
  rest transform relative to its parent part, as a `matrix4d`. It holds one
  `Mesh` per material, named `<part>__<material>`. Everything rigid under a
  part, down to the next part, is baked into the part's frame and merged by
  material. That gives one static mesh per (part, material), which is one
  instanced-static-mesh set per (kind, part, material).
- **What counts as a part:**
  - the root;
  - every node the model's Swift struct holds (the handles the scene moves,
    hides or relights);
  - nodes the scene looks up by name (`fan`, `glow`, `shadow`, `pulseN`...);
  - an ore deposit's `stage1..3` nodules, merged per stage;
  - nodes with a light, a particle system or a camera (sockets with no mesh);
  - nodes that start hidden.

  This is the same rule `Models.flattenStatic` uses to decide what to leave
  unmerged in the game.
- **Textures:**
  - Base colour textures have the material's tint baked in, because readers
    differ on `UsdUVTexture.scale`. The manifest also names the untinted
    texture and gives the tint.
  - Normal maps use scale and bias.
  - The emissive intensity is passed as `scale` on the emissive texture.
  - SceneKit's V is flipped into USD's st.

## manifest.json

- **Top level:** the `units`, `axes` and `teamColour` conventions, the
  `models`, and the `warnings`.
- **Each model:**
  - `file`, `category`, `team`;
  - `factory`: the Swift function that builds it, as `file:function`;
  - `animators`: every function that moves one of its parts;
  - `facing`, `notes`;
  - `boundsMin` and `boundsMax`: the rest pose with the hidden parts left out;
  - `triangles`, `parts`, `materials`.
- **Each part:**
  - `name`, `path` (the USD prim) and `parent`.
  - `handles`: Swift paths into the model struct, such as `legs[0]`,
    `shins[0].lower[2].node` or `fork.tines[1]`. This is how the scene reaches
    the part, so search the Swift source for it.
  - `sceneKitName`.
  - `localTransform`: 16 floats, column-major, relative to the parent.
  - `position`, `eulerAngles` and `scale`: the SceneKit rest values that pose
    code overwrites. The euler angles are x pitch, y yaw, z roll, and SceneKit
    applies them roll first, then yaw, then pitch.
  - `pivot`, only if it is not the identity.
  - `hidden` and `opacity`.
  - `meshes`: the prim, material, triangle count and vertex count of each.
  - `light` or `particles`: the main parameters of a socket's light or
    emitter.
  - `driver`, described below.
- **`driver`**, which says what moves the part:
  - `what`: a plain-words label (walk cycle, turret aim, anchoring, muzzle
    flash, status lamps, wheel roll, tread frames...);
  - `channels`: rotation, position, transform, scale, visibility, emission,
    material or particles;
  - `functions`: the Swift `file:function` that animates it;
  - `lines`: the source lines that touch it, so you can port the motion line
    by line.

  The labels come from the handle names, and the pointers from scanning
  `Sources/Autocraft` when the export runs. A part with a handle but no
  `driver` is held by the struct but not moved per frame. It might be used
  only at build time or by death effects.
- **Each material:**
  - `name` and `key`. The same key in two models means the same material, so
    one Unreal material can serve both.
  - `lightingModel`.
  - Base colour: `baseColor` or `baseColorTexture`, plus `baseColorTint` and
    `baseColorTextureUntinted`.
  - `metallic`, `roughness`, `normalTexture` and `normalStrength`.
  - `emissive`, `emissiveTexture` and `emissiveIntensity`.
  - `opacity` and `opacityTexture`.
  - `blend`: opaque, additive or translucent. Also `writesDepth` and
    `doubleSided`.
  - `animatedBy`: the struct handle of a material the scene relights each
    frame, which becomes a per-instance parameter in Unreal.
  - `shaderModifiers` and `notes`.

## Not exported faithfully

- **SceneKit shader modifiers** export only their base values:
  - the opal veins of Stardust Ore (`oreRock`: ore, Prospector loads,
    cockpits);
  - the Firefly's flame materials;
  - the Derrick's throat glow;
  - the Kestrel's cockpit glass;
  - the MH well pool's heaving liquid silver.

  Rebuild these as Unreal materials from the Swift source
  (`Materials.swift`, `Models+Firefly.swift`, `Models.swift:wellPool`).
- **Particle systems** (steam, thruster plumes, jet flames, sparks, ore
  glitter, the MH vapour) are only sockets. The manifest lists their birth
  rate, life span, size and colour, for Niagara.
- **Lights** are sockets that carry their type, colour, intensity and
  attenuation.
- **Animated materials** (lamps, bay glows, mini gun heat) are exported at
  their built intensity. `animatedBy` and the driver lines say how they
  change.
- **Firefly `multiply` textures** are written but not wired into the USD
  material (see that material's `notes`).
- **Emissive intensity above 1** depends on the importer honouring `scale` on
  the emissive texture.
- **SceneKit pivots:** no part in this export has one (the Kestrel's pivoted
  tail fins are rigid and baked in). If a future part does, its `pivot` is in
  the manifest; compose transform × pivot⁻¹ when you rotate it.

## Hidden test runs

Agents' game runs must never show in the Dock, take focus or move the mouse:

- Run `/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditorBG.app/Contents/MacOS/UnrealEditor`, a copy of `UnrealEditor.app` with `LSUIElement=true`, bundle id `com.epicgames.UnrealEditor.headless`, ad-hoc signed. Remake it after an engine update:
  `ditto UnrealEditor.app UnrealEditorBG.app`, PlistBuddy `Add :LSUIElement bool true`, then `codesign --force --deep -s - UnrealEditorBG.app`.
- Pass `-RenderOffscreen` (or `-nullrhi`), `-unattended -nosplash -nosound`, and `"-ini:Input:[/Script/Engine.InputSettings]:bCaptureMouseOnLaunch=False"`.
- Never pass `-log`, which opens a console window; use `-abslog=PATH`.
- With `-RenderOffscreen`, `-nullrhi` or `-AcHeadless`, the pawns never capture or lock the mouse (`AcHeadless.h`).
