"""Fading masters for chunk C4's deaths (GAME-LAYER.md §2.7; the C++ is
Source/Autocraft/AcEffectsDeaths.cpp). Rerunnable: rebuilt in place.

The Swift falls fade a whole body out (`root.opacity`, 1 → 0 over the last
two seconds) and debris fades the same way. The model masters are opaque,
so a fading body is drawn with these instead:

- /Game/Materials/M_AcHullFade: M_Hull's graph (make_materials.build_master,
  the same parameter names, so a model's MaterialInstanceConstant's values
  copy onto a dynamic instance of it), masked, both sides, with the opacity
  mask a screen-space dither of 1 - custom data 2 (`AcModelData::Fade`, 0 =
  solid, 1 = gone; TSR smooths the dither).
- /Game/Materials/M_AcEmissiveFade: M_Emissive's graph, the same way.

Run in the editor (`py "<repo>/unreal/Tools/Editor/make_corpse_materials.py"`)
or in a commandlet:
  UnrealEditor-Cmd unreal/Autocraft.uproject -run=pythonscript \
      -script="$PWD/unreal/Tools/Editor/make_corpse_materials.py" -unattended -nullrhi
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ac_models as A  # noqa: E402
import make_materials as MM  # noqa: E402

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

M_HULL_FADE = A.MASTER_DIR + "/M_AcHullFade"
M_EMISSIVE_FADE = A.MASTER_DIR + "/M_AcEmissiveFade"

# Keep - a per-pixel threshold in [0, 1) that turns each frame (interleaved
# gradient noise): kept where > 0, so Keep 1 draws all, Keep 0 nothing.
DITHER_HLSL = (
    "float2 p = Parameters.SvPosition.xy + 5.588238 * float(View.StateFrameIndexMod8);\n"
    "float n = frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715))));\n"
    "return Keep - n;"
)


def fade(path, kind, mpc):
    mat = MM.build_master(path, kind, mpc)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("opacity_mask_clip_value", 0.0001)
    g = MM.Graph(mat)
    f = g.custom_data(2, "Fade", 0.0)
    keep = g.node(unreal.MaterialExpressionOneMinus, x=-500)
    g.link(f, "", keep, "")
    d = g.custom("AcFadeDither", DITHER_HLSL, ["Keep"], unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    g.link(keep, "", d, "Keep")
    g.out(d, "", unreal.MaterialProperty.MP_OPACITY_MASK)
    MEL.layout_material_expressions(mat)
    MEL.recompile_material(mat)
    EAL.set_metadata_tag(mat, "AcMadeBy", "Tools/Editor/make_corpse_materials.py")
    EAL.save_loaded_asset(mat)
    MM.log("fade master %s" % path)
    return mat


def main():
    mpc = MM.load(A.MPC_TEAMS)
    if mpc is None:
        raise RuntimeError("no %s: run make_materials.py first" % A.MPC_TEAMS)
    fade(M_HULL_FADE, "hull", mpc)
    fade(M_EMISSIVE_FADE, "emissive", mpc)


if __name__ == "__main__":
    main()
