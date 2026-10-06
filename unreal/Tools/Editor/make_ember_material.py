"""The ember master for chunk C5's wrecks (GAME-LAYER.md §2.7; the C++ is
Source/Autocraft/AcShatter.cpp and AcEffectsShatter.cpp). Rerunnable:
rebuilt in place.

Swift's `Effects.char` (Effects+Shatter.swift:37) swaps every non-glowing
material of a wreck for a copy multiplied by white 0.6 (SceneKit's
`multiply`: the whole lit fragment) whose emission is the `emberImage` hot
spots (Effects.swift:507, on the mesh's own texture coordinates, repeated),
at an intensity that flickers and cools. The lamps only dim (C4's fading
masters do those).

- /Game/Materials/M_AcEmber: M_Hull's graph (make_materials.build_master,
  the same parameter names, so a model instance's values copy onto a
  dynamic instance of it), masked with C4's fade dither (custom data 2),
  plus custom data 3 (`AcModelData::Char`):
    < 0  intact: drawn as M_Hull (a Dropship falling before it bursts);
    >= 0 charred: the tint of a textured material gone (`CharUntint`,
         Swift's multiply replaced), base colour and emission × 0.6, the model's own emission
         gone (the C++ writes custom data 1 = 0) and `EmberTex` × custom
         data 3 × EmissiveBoost added (the hot spots' glow).
  `EmberTex` defaults to black; the C++ makes the hot-spot texture at run
  time from `ac::Noise` (seed 77), as Swift does.

Run in a commandlet (no open editor has the asset):
  UnrealEditor-Cmd unreal/Autocraft.uproject -run=pythonscript \
      -script="$PWD/unreal/Tools/Editor/make_ember_material.py" -unattended -nullrhi
or from make_materials.py (it calls `main()` at its end).
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ac_models as A  # noqa: E402
import make_corpse_materials as MC  # noqa: E402
import make_materials as MM  # noqa: E402

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

M_EMBER = A.MASTER_DIR + "/M_AcEmber"

BASE_HLSL = "float charred = saturate(C * 10000.0 + 1.0);\nreturn Base * lerp(1.0, 0.6, charred);"
# SceneKit's `multiply` is the hull materials' tint, and char replaces it:
# charred, a textured material's tint goes to white (`CharUntint` 1, set by
# the C++ on textured materials; a plain material's tint is its colour).
UNTINT_HLSL = "float charred = saturate(C * 10000.0 + 1.0);\nreturn lerp(T, float3(1, 1, 1), charred * U);"
EMIS_HLSL = (
    "float charred = saturate(C * 10000.0 + 1.0);\n"
    "return (Emis + Ember * max(C, 0.0) * Boost) * lerp(1.0, 0.6, charred);"
)


def main():
    mpc = MM.load(A.MPC_TEAMS)
    if mpc is None:
        raise RuntimeError("no %s: run make_materials.py first" % A.MPC_TEAMS)
    # M_Hull's graph, masked with the fade dither (saved once by fade()).
    mat = MC.fade(M_EMBER, "hull", mpc)
    base_in = MEL.get_material_property_input_node(mat, unreal.MaterialProperty.MP_BASE_COLOR)
    emis_in = MEL.get_material_property_input_node(mat, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    if base_in is None or emis_in is None:
        raise RuntimeError("M_AcEmber: no base colour or emissive input to wrap")
    g = MM.Graph(mat)
    g.y = 4000
    char = g.custom_data(3, "Char", -1.0)
    uv = g.node(unreal.MaterialExpressionTextureCoordinate)
    ember = g.texture("EmberTex", "/Engine/EngineResources/Black", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, uv)
    boost = g.mpc(mpc, "EmissiveBoost")
    # base_in is M_Hull's texture × tint (build_master): put the untint on
    # its tint input, so the studio-sky reflection (hull_env) follows too.
    ins = MEL.get_inputs_for_material_expression(mat, base_in)
    if len(ins) < 2 or ins[1] is None:
        raise RuntimeError("M_AcEmber: base colour is not texture x tint")
    untint = g.custom("AcCharUntint", UNTINT_HLSL, ["T", "C", "U"])
    g.link(ins[1], "", untint, "T")
    g.link(char, "", untint, "C")
    g.link(g.scalar("CharUntint", 0.0), "", untint, "U")
    g.link(untint, "", base_in, "B")
    base = g.custom("AcCharBase", BASE_HLSL, ["Base", "C"])
    g.link(base_in, "", base, "Base")
    g.link(char, "", base, "C")
    emis = g.custom("AcCharEmission", EMIS_HLSL, ["Emis", "Ember", "C", "Boost"])
    g.link(emis_in, "", emis, "Emis")
    g.link(ember, "RGB", emis, "Ember")
    g.link(char, "", emis, "C")
    g.link(boost, "", emis, "Boost")
    g.out(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(emis, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.layout_material_expressions(mat)
    MEL.recompile_material(mat)
    EAL.set_metadata_tag(mat, "AcMadeBy", "Tools/Editor/make_ember_material.py")
    EAL.save_loaded_asset(mat)
    MM.log("ember master %s" % M_EMBER)
    return mat


if __name__ == "__main__":
    main()
