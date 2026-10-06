"""The flame and sprite materials of the Comet and the Firefly (chunk B3,
GAME-LAYER.md §2.6): what the export could not carry (SceneKit shader
modifiers, `multiply` textures and colours, billboard constraints, node
opacity) rebuilt for the poses in Source/Autocraft/AcPoseVehicles.cpp.
Rerunnable: every asset is rebuilt in place.

Run it after make_materials.py (which makes the instances this re-parents;
make_materials.py also runs it as its last step):

    UnrealEditor-Cmd unreal/Autocraft.uproject -run=pythonscript \
        -script="$PWD/unreal/Tools/Editor/make_flame_materials.py" -unattended -nullrhi

or in the open editor: py "<repo>/unreal/Tools/Editor/make_flame_materials.py"

What it makes:

- /Game/Materials/M_AcFlame: the Firefly's jet (`FireflyFX.flameMaterial`,
  Models+Firefly.swift:514). Unlit, additive, two-sided: the colour ramp
  along the jet (EmissiveTex × EmissiveColor) times a noise mask (NoiseTex,
  the export's `firefly_blue_multiply.png`) that scrolls out along the jet by
  time (`Scroll` = the Swift contentsTransform's speeds per layer), times
  facing² (|N·V|², soft at grazing angles so a cone reads as a volume), times
  the emission scale (custom data 1, the pose's flicker and fade).
- /Game/Materials/M_AcSprite: a camera-facing additive square (SceneKit's
  SCNBillboardConstraint on a plane): the whole part turns to face the
  camera about its own origin in the vertex shader (world position offset),
  keeping its scale, so it faces every view, top-down and cockpit alike.
  Emission as M_Additive (texture × alpha × EmissiveColor × custom data 1).
  The Firefly's splash ball and flare, the Comet's jet halos.

The Firefly's tyre smear (`tyreSmear`, a Lambert drum over the lugs with
node opacity) uses B2's /Game/Materials/M_AcSmear (make_infantry_materials.py:
translucent, lit, custom data 1 is its OPACITY; the pose writes the smear
there).

and fixes instances made by make_materials.py:
- flameMaterials_0..2 → M_AcFlame (ramp kept; noise and scroll set);
- flameMaterials_3..5 and the Comet's halo → M_AcSprite, with the
  `multiply` colours the export dropped (the ball's hot core and outer fire,
  the flare) folded into EmissiveColor;
- the tyre smear → M_AcSmear;
- heatGlow (the Firefly's nozzle lip, exported at intensity 0 because the
  pose heats it): glow orange at intensity 1, so the pose's 2.6·heat² is an
  absolute intensity.
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ac_models as A  # noqa: E402
import make_materials as MM  # noqa: E402
import make_resource_materials as RM  # noqa: E402

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

M_FLAME = A.MASTER_DIR + "/M_AcFlame"
M_SPRITE = A.MASTER_DIR + "/M_AcSprite"
NOISE = "textures/firefly_blue_multiply.png"
F1 = unreal.CustomMaterialOutputType.CMOT_FLOAT1
F3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3
MADE_BY = "Tools/Editor/make_flame_materials.py"


def log(msg):
    unreal.log("[make_flame_materials] " + msg)


def calibrated(rgb):
    """NSColor(calibratedRed:…) components → linear (the export's curve: γ 1.8)."""
    return tuple(c ** 1.8 for c in rgb)


def finish(mat, path, usages):
    for usage in usages:
        MEL.set_base_material_usage(mat, usage)
    MEL.layout_material_expressions(mat)
    MEL.recompile_material(mat)
    EAL.set_metadata_tag(mat, "AcMadeBy", MADE_BY)
    EAL.save_loaded_asset(mat)
    log("master %s" % path)


USAGES = (unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES, unreal.MaterialUsage.MATUSAGE_STATIC_LIGHTING)


# --- M_AcFlame -----------------------------------------------------------------

FLAME_HLSL = """
// FireflyFX.flameMaterial: emission ramp × multiply noise, then the shader
// modifier `_output.color.rgb *= facing * facing`.
float facing = abs(dot(normalize(N), normalize(V)));
return Ramp * Col * Noise * facing * facing * Scale * Boost;
"""


def build_flame(mpc):
    mat, g = RM.new_master(M_FLAME, unreal.MaterialShadingModel.MSM_UNLIT, unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property("two_sided", True)
    color = unreal.MaterialSamplerType.SAMPLERTYPE_COLOR
    uv = g.node(unreal.MaterialExpressionTextureCoordinate)
    ramp = g.texture("EmissiveTex", "/Engine/EngineResources/WhiteSquareTexture", color, uv)
    # The noise scrolls by time: SceneKit's (s + a, t + b) is Unreal's
    # (u + a, v − b) (the import flips v).
    scroll = g.vector("Scroll", (0.37, -2.6, 0))
    mask = g.node(unreal.MaterialExpressionComponentMask, r=True, g=True, b=False, a=False)
    g.link(scroll, "", mask, "")
    time = g.node(unreal.MaterialExpressionTime)
    moved = g.binop(unreal.MaterialExpressionMultiply, time, mask)
    frac = g.node(unreal.MaterialExpressionFrac)
    g.link(moved, "", frac, "")
    nuv = g.binop(unreal.MaterialExpressionAdd, uv, frac)
    noise = g.texture("NoiseTex", "/Engine/EngineResources/WhiteSquareTexture", color, nuv)
    c = RM.custom(g, "AcFlame", FLAME_HLSL, [
        ("Ramp", ramp, "RGB"),
        ("Col", g.vector("EmissiveColor", (1, 1, 1)), ""),
        ("Noise", noise, "RGB"),
        ("N", g.node(unreal.MaterialExpressionVertexNormalWS), ""),
        ("V", g.node(unreal.MaterialExpressionCameraVectorWS), ""),
        ("Scale", g.custom_data(1, "EmissionScale", 1.0), ""),
        ("Boost", g.mpc(mpc, "EmissiveBoost"), ""),
    ])
    g.out(c, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(mat, M_FLAME, USAGES)
    return mat


# --- M_AcSprite ----------------------------------------------------------------

BILLBOARD_HLSL = """
// SCNBillboardConstraint on a plane (SceneKit x, y; normal +z = local X, Z;
// normal local Y here): the part turns about its origin to face the camera.
float3 F = Cam - O;
float lf = length(F);
F = lf > 1e-3 ? F / lf : float3(0, 0, 1);
float3 Up = abs(F.z) > 0.999 ? float3(1, 0, 0) : float3(0, 0, 1);
float3 R = normalize(cross(Up, F));
float3 U = cross(F, R);
float3 p = O + R * (L.x * length(AX)) + U * (L.z * length(AZ)) + F * (L.y * length(AY));
return p - W;
"""

SPRITE_HLSL = """
return Tex.rgb * Tex.a * Col * Scale * Boost;
"""


def axis(g, x, y, z):
    v = g.node(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(x, y, z, 0))
    t = g.node(unreal.MaterialExpressionTransform,
               transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL,
               transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    g.link(v, "", t, "")
    return t


def build_sprite(mpc):
    mat, g = RM.new_master(M_SPRITE, unreal.MaterialShadingModel.MSM_UNLIT, unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property("two_sided", True)
    color = unreal.MaterialSamplerType.SAMPLERTYPE_COLOR
    uv = g.node(unreal.MaterialExpressionTextureCoordinate)
    tex = g.texture("EmissiveTex", "/Engine/EngineResources/WhiteSquareTexture", color, uv)
    c = RM.custom(g, "AcSprite", SPRITE_HLSL, [
        ("Tex", tex, "RGBA"),
        ("Col", g.vector("EmissiveColor", (1, 1, 1)), ""),
        ("Scale", g.custom_data(1, "EmissionScale", 1.0), ""),
        ("Boost", g.mpc(mpc, "EmissiveBoost"), ""),
    ])
    g.out(c, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    wpo = RM.custom(g, "AcBillboard", BILLBOARD_HLSL, [
        ("L", RM.local_position(g), ""),
        ("O", RM.instance_origin(g), ""),
        ("AX", axis(g, 1, 0, 0), ""),
        ("AY", axis(g, 0, 1, 0), ""),
        ("AZ", axis(g, 0, 0, 1), ""),
        ("Cam", g.node(unreal.MaterialExpressionCameraPositionWS), ""),
        ("W", g.node(unreal.MaterialExpressionWorldPosition), ""),
    ])
    g.out(wpo, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    finish(mat, M_SPRITE, USAGES)
    return mat


# --- the instances -------------------------------------------------------------

def materials_of(info, model):
    m = info["models"].get(model)
    return {x["name"]: info["materials"][x["key"]] for x in (m["materials"] if m else [])}


def instance(mat):
    mi = MM.load(A.mi_asset_path(mat))
    if mi is None:
        log("no instance yet for %s (%s): run make_materials.py" % (mat["name"], mat["key"]))
    return mi


def save(mi, what):
    EAL.set_metadata_tag(mi, "AcMadeBy", "Tools/Editor/make_materials.py + " + MADE_BY)
    MEL.update_material_instance(mi)
    EAL.save_loaded_asset(mi)
    log("%s: %s" % (mi.get_path_name(), what))


def fix_instances(info, flame, sprite, smear):
    ff = materials_of(info, "firefly_blue")
    comet = materials_of(info, "comet_blue")
    noise = MM.import_texture(NOISE, "color", False)
    # The jet's three layers: ramps and glow as exported; the noise scrolls
    # at (0.37·(i+1), 2.6 + 0.9·i) a second (Models+Firefly.swift:792).
    for i in range(3):
        mat = ff.get("flameMaterials_%d" % i)
        mi = mat and instance(mat)
        if not mi:
            continue
        MEL.set_material_instance_parent(mi, flame)
        MEL.set_material_instance_texture_parameter_value(mi, "NoiseTex", noise)
        MEL.set_material_instance_vector_parameter_value(mi, "Scroll", MM.lc((0.37 * (i + 1), -(2.6 + 0.9 * i), 0)))
        over = mi.get_editor_property("base_property_overrides")
        over.set_editor_property("override_blend_mode", False)
        mi.set_editor_property("base_property_overrides", over)
        save(mi, "flame layer %d" % i)
    # Sprites: the splash ball (hot core, outer fire), the flare; their
    # `multiply` colours × intensity (FireflyFX.glow).
    tints = {"flameMaterials_3": (1, 0.62, 0.2), "flameMaterials_4": (0.85, 0.2, 0.03), "flameMaterials_5": (1, 0.9, 0.65)}
    for name, rgb in tints.items():
        mat = ff.get(name)
        mi = mat and instance(mat)
        if not mi:
            continue
        i = mat.get("emissiveIntensity", 1)
        MEL.set_material_instance_parent(mi, sprite)
        MEL.set_material_instance_vector_parameter_value(mi, "EmissiveColor", MM.lc(tuple(c * i for c in calibrated(rgb))))
        save(mi, "sprite")
    # The Comet's jet halos (additive planes with a billboard constraint).
    for name, mat in comet.items():
        if mat["blend"] == "additive" and name.startswith("plain_"):
            mi = instance(mat)
            if mi:
                MEL.set_material_instance_parent(mi, sprite)
                save(mi, "comet halo sprite")
    # The tyre smear (the Lambert copy of the tyre colour).
    mat = ff.get("plain_040302_2")
    mi = mat and instance(mat)
    if mi:
        MEL.set_material_instance_parent(mi, smear)
        over = mi.get_editor_property("base_property_overrides")
        over.set_editor_property("override_blend_mode", False)
        mi.set_editor_property("base_property_overrides", over)
        MEL.set_material_instance_vector_parameter_value(mi, "BaseColorTint", MM.lc(mat.get("baseColor", (0.016, 0.014, 0.011))))
        save(mi, "tyre smear")
    # The nozzle lip: glow orange (Models.swift:38) at intensity 1.
    mat = ff.get("heatGlow")
    mi = mat and instance(mat)
    if mi:
        MEL.set_material_instance_vector_parameter_value(mi, "EmissiveColor", MM.lc(calibrated((1, 0.55, 0.15))))
        save(mi, "heat glow at intensity 1")


def main(info=None):
    info = info or A.analyse(A.load_manifest())
    MM.ensure_dir(A.MASTER_DIR)
    mpc = MM.load(A.MPC_TEAMS) or MM.make_mpc()
    flame = build_flame(mpc)
    sprite = build_sprite(mpc)
    import make_infantry_materials
    smear = MM.load(make_infantry_materials.M_SMEAR) or make_infantry_materials.build_smear()
    fix_instances(info, flame, sprite, smear)


if __name__ == "__main__":
    main()
