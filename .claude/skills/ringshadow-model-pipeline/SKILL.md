---
name: ringshadow-model-pipeline
description: Build or restyle a Ringshadow unit or building model for the Unreal Engine 5 game (unreal/) — concept art and turnaround sheets with mcp__orchestrator__grok_image, a Geometry Script modelling script per model, catalog entries, render review. Use for a new unit (Peregrine, Atlas, Scorpion), a restyle from artwork, an image-AI prompt for a model, or checking a painted sheet against its model.
---

# Ringshadow model pipeline (Unreal)

The game is the UE 5.8 project `unreal/Autocraft.uproject`. A model there is a tree of **rigid parts**, one static mesh per (part, material), named `/Game/Models/<model>/SM_<part>__<material>`, plus a **catalog** entry in `unreal/Content/Models/ModelCatalog.json`. Three readers depend on the part names and data: the pose code (`unreal/Source/Autocraft/AcPose*.cpp`, finds a part by name), the ray code (`AcRaysWorld`, reads each part's `rays`) and the renderer (one instanced-mesh set per kind, part and material; team colour from per-instance custom data and the team flags on the material). `unreal/GAME-LAYER.md` §2.6, §3.3 and §3.4 are the contract; the field-by-field catalog entry is in [MODELLING.md](MODELLING.md).

Art lives in `art/models/<unit>/` (the repo keeps it). A **turnaround** is one sheet, six panels (FRONT, RIGHT SIDE, BACK, LEFT SIDE, TOP, 3/4) split by black lines on white. Grok paints over a grey clay **stencil** of the model, so the views agree by construction; the painting is split and fitted back onto the stencil before anything reads it, because AI image models keep silhouettes but stretch the figure (the first Ranger sheet came back 8% taller in every view).

Save every render you judge by to `art/models/<unit>/previews/` and tell the user the path as you go.

## Status: what exists, what is to build

Steps 0 to 4, 6 and 7 run today; step 5 is by hand. Build a helper when a step needs one, in `unreal/Tools/Editor/` or `.claude/skills/ringshadow-model-pipeline/`, and move it from this list to "exists" in the same edit.

To build:
- Paint onto UE meshes (skin projection and nets): parked, see [NETS.md](NETS.md).

Exists: `model.sh stencil`, `stencil.py` and `-AcModelView` / `-AcModelPaint` in `AAcModelRow` (with `make_stencil_material.py`), `unreal/Tools/Editor/ac_modelkit.py` (parts, materials, rays, catalog merge, checked by `check_modelkit.py`), `models/peregrine.py`, `atlas.py`, `scorpion.py` (blockouts), the catalog merge (`ac_modelkit.merge_entry`; `import_models.py` still rewrites the whole catalog, never run it), `import_models.py` and `ac_models.py` (Swift-export path, `Content-src/models`, which will be gone), `make_materials.py` (the master materials and one `MI_<name>_<key>` per material), `make_model_row.py` and `AAcModelRow` (`/Game/Maps/ModelRow`), `-AcShot`, `-AcShotScenes`, `AcPlayground`, `AcPoseSheet` (`unreal/SHOTS.md`), `model.sh prompt|split|decal` and the Python tools in this folder.

## Steps

Each step ends on its done line. A step is done when its line holds, and the next step starts from the files it names.

### 0. Concept

Settle a new look with concept art: one `mcp__orchestrator__grok_image` call per design direction. `art/models/prospector/concepts/prompts.md` is the model: a shared text with a `{DIRECTION}` slot plus one short direction per variant, one `prompt.<slug>.txt` per direction next to it.

- The shared text names the unit's jobs as body parts (drill arm, gripper, rail gun, pilot visible): the pose code moves exactly those parts.
- Attach the army's reference as `reference_images`: a neighbour's `painted.jpg` for a unit (the Ranger's), the neighbouring buildings' renders side by side for a building (`<unit>/base.jpg`). Ask for a chunky silhouette that reads from the game's high camera, on plain light grey.
- Call: `prompt` = shared text with the direction filled in, `reference_images`, `aspect_ratio` `3:2` (or `1:1`), `count` 1 per direction (or up to 4 variations of one), `name` `c<round>-<slug>`, `output_dir` `art/models/<unit>/concepts/`. Each image takes about 20 to 40 s; the images come back inline, so look at each one and say when it misses the brief. `output_dir` keeps the files (the run folder is pruned) and never overwrites: a repeated `name` gets a `-2` suffix. Leave `enhance` off so the prompt is sent word for word.
- Put a labelled contact sheet in `previews/concepts-c<round>.jpg` for the user.

Done when the user has picked a concept and `concepts/c<round>-<slug>.jpg` is in the unit's folder. What earlier rounds showed (what Grok softens, copies, ignores) is in [HICCUPS.md](HICCUPS.md); read its "Concept rounds" section before writing a new direction.

### 1. Blockout

Write `unreal/Tools/Editor/models/<model>.py` as a **blockout**: primitives at the right size, parts named for what moves them (`hips_0`, `turret`, `engines_0`; the existing pose files show the naming), parents and pivots where the joints go. Same script and same output as the final model (see [MODELLING.md](MODELLING.md)), so the stencil carries the proportions and the part tree from the start. Units are cells (a Ranger is about 1.6 tall), units face +X, buildings +Z.

Done when the script runs headless (hidden run, below), the meshes are under `/Game/Models/<model>/` and `ModelCatalog.json` lists the model with every part.

### 2. Stencil

Render the model's grey clay in the format `split.py` reads: five orthographic views at one scale on a shared ground line plus a 3/4 view, six 512 px panels on a 1536×1024 sheet, with part outlines. Spec: MODELLING.md, "The stencil files". Parts that are hidden at rest, glow halos and particle sockets stay out of the clay.

Run `model.sh stencil <unit>`. Done when `stencil.png`, `stencil.json` and `stencil.parts.png` are in `art/models/<unit>/` and you have looked at `stencil.png`: every panel shows the model, open shells show their inside.

### 3. Prompt and painting

Write `look.txt` in the unit's folder: materials, colours, emblems, glows, part by part (`art/models/ranger/look.txt` is the model). End it with a paragraph that names what each panel shows, as the stencil draws it (FRONT head-on, RIGHT SIDE a flat profile, only 3/4 at an angle): a wide, low vehicle comes back as six 3/4 views without it (`longbow/look.txt` is the example).

`model.sh prompt <unit>` prints the **prompt template** (`sheet-rules.txt`, versioned) followed by `look.txt` and saves the text as `<unit>/prompt.txt`. With a `<unit>/style.*` image (a panel cropped from a painting or concept whose look the user liked) the prompt asks Grok to match its look and take every shape from the stencil.

Paint with one `mcp__orchestrator__grok_image` call: `prompt` = the saved `prompt.txt` text, `reference_images` = `[<unit>/stencil.png, <unit>/style.jpg]` (the prompt names the images by file name, so the file names stay as they are), `aspect_ratio` `3:2` (the stencil's; a single-image edit keeps its input's ratio), `name` `painted-v<N>`, `output_dir` `art/models/<unit>/grok/`. The output is JPEG; take the answer you keep and copy it to `<unit>/painted.jpg`. Send two prompts at once (two calls, or `count` 2): one may come back as a single 3/4 illustration with no panels, which costs a round only when it is the only answer.

The painting becomes a **base colour** reference: the game's lights make the shading, so the template asks for flat, unlit colour (highlights painted in get lit twice).

Done when `<unit>/painted.jpg` is a six-panel sheet.

### 4. Split and check

`model.sh split <unit>` finds the dividing lines, maps each painted panel onto its stencil panel, fits it (stretch and shift matched to the bounding boxes, then tuned for overlap) and writes `views/<view>.png`, `<view>.mask.png` and `compare.png` (magenta: painted only, cyan: stencil only). It prints each view's IoU before and after fitting, the stretch and the width, depth and height each view shows.

Look at `compare.png` and report per view what changed shape: the parts, which way, how much. Look at the painting for baked light. Add a row to the results table in [PROMPTS.md](PROMPTS.md): prompt version, unit, stretch, fitted IoUs, spread, what went wrong.

Done when every orthographic view's fitted IoU is at least 0.8, each size's spread between views is at most 3%, and the PROMPTS.md row is written. A view below that goes back to Grok for a re-roll, naming the view and what drifted. A painting that redesigns a part on purpose is the user's call: ask whether the model follows the painting.

### 5. Refine the model to the painting

Read the fitted views against the blockout: proportions from `views/`, details and glow strips from the painting. Edit the model script (small in-place edits), rerun it, restencil when a part moved a lot. The painting's detail goes into the model as geometry and as material choice (hull light, mid, dark, hazard, team, glow materials); painted texture on the meshes is parked (NETS.md). Small 3D details the painting also draws double up: draw them in one place only.

Done when the model script reproduces the painting's silhouette in every view (re-run steps 2 and 4 on the refined model and the IoUs hold) and every part that moves has its driver entry (MODELLING.md, "Part and driver data").

### 6. Import into the game

Run the model script (hidden run, below). It writes the static meshes, the material instances it needs and its catalog entry. The pose function for the unit's kind (`AcPose*.cpp`), the cockpit (`AcCockpitKinds`, model `cockpit_<kind>`) and the effects are game code; new kinds are listed in `docs/new-units.md`, "Where it lands in the code".

Done when `/Game/Models/<model>/` holds one `SM_<part>__<material>` per catalog mesh, the catalog parses (the game starts and `AAcModelRow` shows the model) and the blue and red teams both draw.

### 7. Review renders

Render with the model row (hidden run): the focused model in its 3/4 view, both team colours, and the pilot view for a unit that can be driven. The existing scene runs (`unreal/SHOTS.md`: `-AcScene=NAME`, `-AcPilot=KIND -AcPilotStage`, `-AcPlayground`) show the model in the game's own cameras. Look at the PNGs, compare them with the painting, and save the ones you judge by to `<unit>/previews/`.

Done when the previews show the painting's shapes on the right parts in both team colours and the log (`-abslog=` file) has no catalog or asset-load error for the model.

## Hidden runs (every UE run)

Never relaunch the user's game or editor windows. Every UE process you start runs hidden: the Dock-less copy `/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditorBG.app/Contents/MacOS/UnrealEditor` (`unreal/Tools/README.md`, "Hidden test runs", has how it is made), with `-unattended -nosplash -nosound`, `-RenderOffscreen` (or `-nullrhi` for scripts), `"-ini:Input:[/Script/Engine.InputSettings]:bCaptureMouseOnLaunch=False"` and `-abslog=<path>` (never `-log`). From the repo root:

```sh
BG="/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditorBG.app/Contents/MacOS/UnrealEditor"
# a model script (editor, no window)
"$BG" unreal/Autocraft.uproject -run=pythonscript -script="$PWD/unreal/Tools/Editor/models/<model>.py" \
  -unattended -nullrhi -nosound -abslog=/abs/model.log
# a review render of one model (the model row level, 16:9, crop the centre square)
"$BG" unreal/Autocraft.uproject /Game/Maps/ModelRow -game -RenderOffscreen -ResX=1600 -ResY=900 \
  -unattended -nosplash -nosound -AcNoSave -AcNoAI -AcPaused -AcModelFocus=<model>_blue [-AcModelTeam=N] \
  -AcShot=/abs/out.png -abslog=/abs/shot.log "-ini:Input:[/Script/Engine.InputSettings]:bCaptureMouseOnLaunch=False"
```

With the editor already open, `unreal-mcp` reads and edits assets (`StaticMeshTools`, `MaterialInstanceTools`); running a whole model script through it is untested, so prefer the hidden run. Another session may be building the module: if a run fails on a link or a locked file, wait and retry.

## Hiccups

Record every hiccup and fact you meet in this pipeline as it happens, in [HICCUPS.md](HICCUPS.md) (image generation and art) or MODELLING.md (UE modelling), with the date and the unit. A hiccup that changes a step goes into the step as well, and out of the log.

## Files

- [MODELLING.md](MODELLING.md): the model script contract, catalog entry, material and team-colour rules, stencil files, helpers to build.
- [HICCUPS.md](HICCUPS.md): what Grok did on each unit's concept and painting rounds.
- [PROMPTS.md](PROMPTS.md): the template version log and the results table. `sheet-rules.txt` and `nets-rules.txt` are the templates.
- [NETS.md](NETS.md): nets, decals and paint onto meshes (parked, with the tools that survive).
- [ROADMAP.md](ROADMAP.md): planned stages.
- `model.sh` (`prompt`, `split`, `decal`), `split.py`, `decal.py` and the nets tools (`tint.py`, `legend.py`, `recolour.py`, `relayout.py`, `netscheck.py`, `install_nets.py`).
- Other sessions edit the repo at once: small `Edit`s on live files, never copy a file over one. Delete with `rm` and the exact full path.
