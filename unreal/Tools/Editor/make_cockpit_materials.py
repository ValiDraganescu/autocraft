"""The cockpits' animated materials (chunk E5, GAME-LAYER.md §2.12; the poses
are Source/Autocraft/AcCockpitKinds.cpp). Rerunnable.

The Firefly, Longbow and Hailstorm cockpits carry the unit's own model, and
the exporter renames its materials `model_<name>` (`model_lockGlow`,
`model_flameMaterials_0`, `model_heatGlow`, `model_radarGlow`...), so their
instances miss what the pose chunks' scripts did to the unit's
(make_artillery_materials.py, make_flame_materials.py: lock lights lit,
the jet on M_AcFlame/M_AcSprite, the hot lip lit, the smoke ring). The
Comet's jet halos and the Firefly's tyre smear have their own names in the
cockpit. This copies, from each unit material instance (the twin) onto the
cockpit's: the parent, the blend-mode override and every parameter value.

Run it after make_materials.py and the pose chunks' scripts:
    UnrealEditor unreal/Autocraft.uproject -run=pythonscript \\
        -script="$PWD/unreal/Tools/Editor/make_cockpit_materials.py" -unattended -nullrhi
"""

import json
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ac_models as A  # noqa: E402

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
MADE_BY = "Tools/Editor/make_cockpit_materials.py"

# Cockpit materials whose twin has another name: (cockpit model, material) →
# (unit model, material).
TWINS = {
    ("cockpit_comet_blue", "plain_000000"): ("comet_blue", "plain_000000_2"),
    ("cockpit_firefly_blue", "plain_040302_2"): ("firefly_blue", "plain_040302_2"),
}


def log(msg):
    unreal.log("[make_cockpit_materials] " + msg)


def instances(model):
    """Material name → instance path of a catalog model."""
    out = {}
    for part in model["parts"]:
        for m in part["meshes"]:
            out.setdefault(m["material"], m["materialInstance"].split(".")[0])
    return out


def copy(src, dst):
    MEL.set_material_instance_parent(dst, src.get_editor_property("parent"))
    over = src.get_editor_property("base_property_overrides")
    dst.set_editor_property("base_property_overrides", over)
    for name in MEL.get_scalar_parameter_names(src):
        MEL.set_material_instance_scalar_parameter_value(dst, name, MEL.get_material_instance_scalar_parameter_value(src, name))
    for name in MEL.get_vector_parameter_names(src):
        MEL.set_material_instance_vector_parameter_value(dst, name, MEL.get_material_instance_vector_parameter_value(src, name))
    for name in MEL.get_texture_parameter_names(src):
        t = MEL.get_material_instance_texture_parameter_value(src, name)
        if t:
            MEL.set_material_instance_texture_parameter_value(dst, name, t)
    try:
        for name in MEL.get_static_switch_parameter_names(src):
            MEL.set_material_instance_static_switch_parameter_value(
                dst, name, MEL.get_material_instance_static_switch_parameter_value(src, name))
    except AttributeError:
        pass  # no static switches in this engine's Python API (the masters use none)
    EAL.set_metadata_tag(dst, "AcMadeBy", "Tools/Editor/make_materials.py + " + MADE_BY)
    MEL.update_material_instance(dst)
    EAL.save_loaded_asset(dst)


def main():
    with open(A.CATALOG_FILE) as f:
        catalog = {m["name"]: m for m in json.load(f)["models"]}
    done = 0
    for name, model in sorted(catalog.items()):
        if not (name.startswith("cockpit_") and name.endswith("_blue")):
            continue
        unit = name[len("cockpit_"):]
        mine = instances(model)
        theirs = instances(catalog[unit]) if unit in catalog else {}
        for mat, path in sorted(mine.items()):
            twin = None
            if (name, mat) in TWINS:
                umodel, umat = TWINS[(name, mat)]
                twin = instances(catalog[umodel]).get(umat) if umodel in catalog else None
            elif mat.startswith("model_"):
                twin = theirs.get(mat[len("model_"):])
            if not twin or twin == path:
                continue
            src, dst = EAL.load_asset(twin), EAL.load_asset(path)
            if not src or not dst:
                log("missing %s or %s" % (twin, path))
                continue
            copy(src, dst)
            done += 1
            log("%s %s ← %s" % (name, mat, twin))
    log("%d instances" % done)


if __name__ == "__main__":
    main()
