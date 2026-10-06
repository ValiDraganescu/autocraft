"""The resource materials (chunk B8, GAME-LAYER.md §2.6 "Ore and wells"):
the Swift shader modifiers on Stardust Ore and Metallic Hydrogen rebuilt as
Unreal materials. Rerunnable: every asset is rebuilt in place.

Run it (make_materials.py also runs it as its last step, so the instances it
re-parents never fall back to M_Hull):

    UnrealEditor-Cmd unreal/Autocraft.uproject -run=pythonscript \
        -script="$PWD/unreal/Tools/Editor/make_resource_materials.py" -unattended -nullrhi

What it makes:

- /Game/Materials/MPC_AcWorld: world-wide values the C++ sets every frame.
  `Dark` (0 by day, 1 at night: AAcDaylight::GetDark, written by AAcResources);
  `HullEnv` (0.6), how much of the Swift studio sky M_Hull's metals mirror
  on top of Lumen (`hull_env`, the chrome fix; 0 turns it off).
- /Game/Materials/M_Opal: Stardust Ore (`MaterialLibrary.opalOre`,
  Sources/Autocraft/Materials.swift). Near-black basalt with broad opal
  patches whose thin-film hue (teal, violet, gold) follows the view angle,
  brightest at grazing angles, and twinkling dust glints, as in Swift. Richer
  than SceneKit: the opal patches get a clear coat (a glossy film over the
  rock, so they catch the real sky and sun in Lumen reflections). The film's
  hue reads the normal-mapped pixel normal and the reflection in the
  camera's basis, as Swift's view-space `_surface.normal`; `NightGlow` (0,
  as Swift) can lift it at night. Patterns are in the instance's
  own space (`PatternScale` maps cm to the Swift node space), so a carried
  chunk keeps its opal.
- /Game/Materials/M_Mercury: Metallic Hydrogen (`MaterialLibrary.mercury`,
  `mercuryLook`, `ModelLibrary.wellPool`). Mirror silver with the Swift
  studio sky faked in emission (`EnvGain`, dimmed at night) and a cold sheen
  at grazing angles. `Pool` = 1 adds the well pool's motion: the surface
  heaves and rings run out (world position offset, upper half only), and the
  normals ripple with three wave trains; each well's phase comes from where
  it stands.
- /Game/Materials/M_AcPuff and its instances MI_AcVapour, MI_AcGlint: an
  additive sprite (a soft puff, or a star-shaped glint) for the CPU
  particles of AAcResources (the well's vapour, the ore's glitter); colour
  and alpha per instance (custom data 0-3).

It then re-parents the model instances made by make_materials.py: every
`oreRock` material (the deposits, a Prospector's carried chunk, the cockpit)
to M_Opal, and the well pool (`plain_494e59` with a geometry shader
modifier) to M_Mercury. Their parameters keep the names M_Hull uses
(BaseColorTint, Roughness, Metallic, NormalTex, NormalStrength), so
make_materials.py fills them as for any other instance.
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ac_models as A  # noqa: E402
import make_materials as MM  # noqa: E402

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

MPC_WORLD = A.MASTER_DIR + "/MPC_AcWorld"
M_OPAL = A.MASTER_DIR + "/M_Opal"
M_MERCURY = A.MASTER_DIR + "/M_Mercury"
M_PUFF = A.MASTER_DIR + "/M_AcPuff"
MI_VAPOUR = A.MASTER_DIR + "/MI_AcVapour"
MI_GLINT = A.MASTER_DIR + "/MI_AcGlint"

F1 = unreal.CustomMaterialOutputType.CMOT_FLOAT1
F3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3


def log(msg):
    unreal.log("[make_resource_materials] " + msg)


def is_opal(mat):
    return mat["name"].endswith("oreRock")


def is_pool(mat):
    return mat["name"].startswith("plain_494e59") and "SCNShaderModifierEntryPointGeometry" in mat.get("shaderModifiers", [])


# --- the world MPC -------------------------------------------------------------

def make_world_mpc():
    mpc = MM.load(MPC_WORLD) or MM.create(MPC_WORLD, unreal.MaterialParameterCollection,
                                          unreal.MaterialParameterCollectionFactoryNew())
    wanted = (("Dark", 0.0), ("HullEnv", 0.6))
    have = [(str(x.get_editor_property("parameter_name")), x.get_editor_property("default_value"))
            for x in mpc.get_editor_property("scalar_parameters")]
    if [n for n, _ in have] == [n for n, _ in wanted]:
        # Rewriting the list gives the parameters new ids, which would leave
        # every material compiled against the old ones stale.
        return mpc
    scalars = []
    for name, value in wanted:
        s = unreal.CollectionScalarParameter()
        s.set_editor_property("parameter_name", name)
        s.set_editor_property("default_value", value)
        scalars.append(s)
    mpc.set_editor_property("scalar_parameters", scalars)
    EAL.set_metadata_tag(mpc, "AcMadeBy", "Tools/Editor/make_resource_materials.py")
    EAL.save_loaded_asset(mpc)
    return mpc


# --- shared graph helpers ----------------------------------------------------

def custom(g, desc, code, inputs, outputs=(), out_type=F3):
    """A Custom node with `inputs` [(name, node, output pin)] wired and extra
    `outputs` [(name, type)]."""
    c = g.custom(desc, code, [n for n, _, _ in inputs], out_type)
    extra = []
    for name, kind in outputs:
        o = unreal.CustomOutput()
        o.set_editor_property("output_name", name)
        o.set_editor_property("output_type", kind)
        extra.append(o)
    c.set_editor_property("additional_outputs", extra)
    for name, src, pin in inputs:
        g.link(src, pin, c, name)
    return c


def new_master(path, shading, blend=unreal.BlendMode.BLEND_OPAQUE):
    mat = MM.load(path) or MM.create(path, unreal.Material, unreal.MaterialFactoryNew())
    MEL.delete_all_material_expressions(mat)
    mat.set_editor_property("shading_model", shading)
    mat.set_editor_property("blend_mode", blend)
    mat.set_editor_property("two_sided", False)
    return mat, MM.Graph(mat)


def finish(mat, path, usages):
    for usage in usages:
        MEL.set_base_material_usage(mat, usage)
    MEL.layout_material_expressions(mat)
    MEL.recompile_material(mat)
    EAL.set_metadata_tag(mat, "AcMadeBy", "Tools/Editor/make_resource_materials.py")
    EAL.save_loaded_asset(mat)
    log("master %s" % path)


def local_position(g):
    """The position in the instance's own space (cm)."""
    return g.node(unreal.MaterialExpressionLocalPosition)


def instance_origin(g):
    """The instance's origin in world space (cm): local (0,0,0) → world."""
    zero = g.node(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(0, 0, 0, 0))
    t = g.node(unreal.MaterialExpressionTransformPosition,
               transform_source_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL,
               transform_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
    g.link(zero, "", t, "")
    return t


# --- M_Hull's chrome (called by make_materials.build_master) -------------------

HULL_ENV_HLSL = """
// The Swift game lit every model with a sky map (MaterialLibrary.
// skyEnvironment, lightingEnvironment 0.75): brown ground, a bright warm
// horizon, blue sky. Its metals mirror it, which keeps chrome light grey from
// the top-down camera; under Lumen they mirror the dark ground and the parts
// beside them and come out darker. This adds that sky back to metals, by
// gloss, fading at night. Gain: MPC_AcWorld.HullEnv (0 turns it off).
float3 R = reflect(-normalize(V), normalize(N));
float t = asin(clamp(R.z, -1.0, 1.0)) / 3.14159265 + 0.5;
float3 c0 = float3(0.36, 0.28, 0.2), c1 = float3(0.55, 0.45, 0.36), c2 = float3(0.93, 0.83, 0.7);
float3 c3 = float3(0.55, 0.66, 0.82), c4 = float3(0.3, 0.42, 0.66);
float3 c = t < 0.42 ? lerp(c0, c1, t / 0.42) : t < 0.5 ? lerp(c1, c2, (t - 0.42) / 0.08)
         : t < 0.65 ? lerp(c2, c3, (t - 0.5) / 0.15) : lerp(c3, c4, (t - 0.65) / 0.35);
c = pow(c, 2.2);
float gloss = 1.0 - saturate(Rough);
return Base * saturate(Metal) * c * gloss * Gain * (1.0 - 0.85 * Dark);
"""


def hull_env(g, base_color, metallic, roughness):
    """The emission M_Hull adds for the Swift studio sky on metals."""
    mpc = make_world_mpc()
    return custom(g, "Swift sky map on metals", HULL_ENV_HLSL, [
        ("Base", base_color, ""), ("Metal", metallic, ""), ("Rough", roughness, ""),
        # Swift's _surface.normal carries the rock normal map: its streaks
        # break the film's hue into thin sheens (the vertex normal gave
        # broad blotches close up).
        ("N", g.node(unreal.MaterialExpressionPixelNormalWS), ""),
        ("V", g.node(unreal.MaterialExpressionCameraVectorWS), ""),
        ("Dark", g.mpc(mpc, "Dark"), ""),
        ("Gain", g.mpc(mpc, "HullEnv"), ""),
    ])


# --- M_Opal ------------------------------------------------------------------

OPAL_HLSL = """
// MaterialLibrary.opalShader (Sources/Autocraft/Materials.swift), in the
// instance's own space. Swift's `op` is a nodule's node space; here it is
// the deposit's, scaled to the same pattern size by PatternScale.
struct AcOre
{
    float Hash(float3 p)
    {
        p = frac(p * 0.3183099 + 0.1);
        p *= 17.0;
        return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
    }
    float Noise(float3 x)
    {
        float3 i = floor(x), f = frac(x);
        f = f * f * (3.0 - 2.0 * f);
        return lerp(lerp(lerp(Hash(i), Hash(i + float3(1, 0, 0)), f.x),
                         lerp(Hash(i + float3(0, 1, 0)), Hash(i + float3(1, 1, 0)), f.x), f.y),
                    lerp(lerp(Hash(i + float3(0, 0, 1)), Hash(i + float3(1, 0, 1)), f.x),
                         lerp(Hash(i + float3(0, 1, 1)), Hash(i + float3(1, 1, 1)), f.x), f.y), f.z);
    }
    float3 Opal(float t)
    {
        float3 teal = float3(0.05, 0.95, 0.8), violet = float3(0.62, 0.28, 1.0), gold = float3(1.0, 0.72, 0.16);
        t = frac(t) * 3.0;
        float k = smoothstep(0.0, 1.0, frac(t));
        return t < 1.0 ? lerp(teal, violet, k) : t < 2.0 ? lerp(violet, gold, k) : lerp(gold, teal, k);
    }
};
AcOre o;
float3 n = normalize(N);
float3 v = normalize(V);
float ndv = saturate(dot(n, v));
float3 op = LP * PatternScale;
// Opal: broad patches over the dark rock, flecked inside with brighter play
// of colour.
float big = o.Noise(op * 2.2 + 3.0);
float opal = smoothstep(0.44, 0.58, big);
float fleck = o.Noise(op * 7.0 + 17.0);
// Thin film: the hue follows the view angle and the surface's turn.
float3 r = reflect(-v, n);
// Swift's r is in view space (x right, y up): the camera's basis from the
// view direction (left-handed, Z up: right = up x forward).
float3 fwd = -v;
float3 right = cross(float3(0, 0, 1), fwd);
right = dot(right, right) > 1e-4 ? normalize(right) : float3(0, 1, 0);
float3 up = cross(fwd, right);
float hue = big * 1.2 + fleck * 0.3 + (1.0 - ndv) * 1.1 + dot(r, up) * 0.35 + dot(r, right) * 0.25 + T * 0.02;
float3 film = o.Opal(hue);
float grazing = pow(1.0 - ndv, 2.0);
float flash = 0.6 + 0.8 * smoothstep(0.4, 0.85, fleck);
float3 e = film * film * opal * flash * (1.0 + 1.4 * grazing);
// A faint iridescent sheen over the bare rock at grazing angles.
e += o.Opal(hue + 0.4) * pow(1.0 - ndv, 3.0) * 0.4 * (1.0 - opal);
// Swift's emission is the same day and night (NightGlow 0, as Swift).
e *= OpalGain * (1.0 + NightGlow * Dark);
// Dust glints: sparse points that twinkle on and off.
float3 g = op * 5.0;
float3 cell = floor(g);
float h = o.Hash(cell + 0.37);
float3 at = cell + 0.5 + (float3(o.Hash(cell + 1.7), o.Hash(cell + 3.1), o.Hash(cell + 5.3)) - 0.5) * 0.5;
float twinkle = pow(saturate(sin(T * (1.4 + 2.6 * h) + h * 40.0)), 10.0);
float glint = step(0.6, h) * (1.0 - smoothstep(0.08, 0.26, length(g - at))) * twinkle;
e += lerp(float3(1.0, 0.95, 0.85), o.Opal(h * 5.0), 0.35) * glint * GlintGain;
Rough = lerp(R, 0.15, opal);
// Bare basalt takes little of the sky, so it stays near black.
AO = lerp(0.35, 1.0, opal);
Coat = opal * CoatAmount;
return e * Scale;
"""


def build_opal(mpc_world):
    mat, g = new_master(M_OPAL, unreal.MaterialShadingModel.MSM_CLEAR_COAT)
    # Clear coat has no MaterialProperty in Python: everything goes through
    # one MakeMaterialAttributes node.
    mat.set_editor_property("use_material_attributes", True)
    attrs = g.node(unreal.MaterialExpressionMakeMaterialAttributes, x=0)
    g.out(attrs, "", unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES)
    pins = {
        unreal.MaterialProperty.MP_BASE_COLOR: "BaseColor",
        unreal.MaterialProperty.MP_METALLIC: "Metallic",
        unreal.MaterialProperty.MP_NORMAL: "Normal",
        unreal.MaterialProperty.MP_EMISSIVE_COLOR: "EmissiveColor",
        unreal.MaterialProperty.MP_ROUGHNESS: "Roughness",
        unreal.MaterialProperty.MP_AMBIENT_OCCLUSION: "AmbientOcclusion",
        "coat": "ClearCoat",
        "coatRoughness": "ClearCoatRoughness",
    }

    class Attr:
        def __init__(self, graph):
            self.graph = graph

        def __getattr__(self, name):
            return getattr(self.graph, name)

        def out(self, node, pin, prop):
            self.graph.link(node, pin, attrs, pins[prop])

    g = Attr(g)
    tc = g.node(unreal.MaterialExpressionTextureCoordinate)
    color = unreal.MaterialSamplerType.SAMPLERTYPE_COLOR
    base_tex = g.texture("BaseColorTex", "/Engine/EngineResources/WhiteSquareTexture", color, tc)
    tint = g.vector("BaseColorTint", (0.00841, 0.00541, 0.00375))
    base = g.binop(unreal.MaterialExpressionMultiply, base_tex, tint, a_out="RGB")
    g.out(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.scalar("Metallic", 0.0), "", unreal.MaterialProperty.MP_METALLIC)

    normal_tex = g.texture("NormalTex", "/Engine/EngineMaterials/FlatNormal",
                           unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, tc)
    flat = g.node(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(0, 0, 1, 0))
    nl = g.node(unreal.MaterialExpressionLinearInterpolate, x=-300)
    g.link(flat, "", nl, "A")
    g.link(normal_tex, "RGB", nl, "B")
    g.link(g.scalar("NormalStrength", 1.0), "", nl, "Alpha")
    g.out(nl, "", unreal.MaterialProperty.MP_NORMAL)

    # Swift's `_surface.normal` is the normal-mapped one (rockNormal,
    # strength 3): its fine bumps break the thin film's hue into the thin
    # rainbow streaks inside the patches. PixelNormalWS cannot be read in a
    # material that writes Normal, so the same normal goes to world space here.
    n_ws = g.node(unreal.MaterialExpressionTransform, x=-300,
                  transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_TANGENT,
                  transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    g.link(nl, "", n_ws, "")
    time = g.node(unreal.MaterialExpressionTime)
    scale = g.custom_data(1, "EmissionScale", 1.0)
    c = custom(g, "Swift opal (opalOre)", OPAL_HLSL, [
        ("LP", local_position(g), ""),
        ("N", n_ws, ""),
        ("V", g.node(unreal.MaterialExpressionCameraVectorWS), ""),
        ("T", time, ""),
        ("R", g.scalar("Roughness", 0.8), ""),
        ("Dark", g.mpc(mpc_world, "Dark"), ""),
        ("PatternScale", g.scalar("PatternScale", 0.04), ""),
        ("OpalGain", g.scalar("OpalGain", 1.0), ""),
        ("GlintGain", g.scalar("GlintGain", 7.0), ""),
        ("NightGlow", g.scalar("NightGlow", 0.0), ""),
        ("CoatAmount", g.scalar("CoatAmount", 1.0), ""),
        ("Scale", scale, ""),
    ], outputs=[("Rough", F1), ("AO", F1), ("Coat", F1)])
    g.out(c, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    g.out(c, "Rough", unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(c, "AO", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    g.out(c, "Coat", "coat")
    g.out(g.scalar("CoatRoughness", 0.06), "", "coatRoughness")
    finish(mat, M_OPAL, (unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES, unreal.MaterialUsage.MATUSAGE_NANITE))
    return mat


# --- M_Mercury ---------------------------------------------------------------

POOL_WPO_HLSL = """
// ModelLibrary.wellPool, geometry: the pool heaves slowly and rings run out
// over it. The pool is a sphere of radius 0.5 squashed to 0.15 at 0.575 up
// the well (cells); only its upper half moves.
float2 q = LP.xy / 100.0;
float r = saturate(length(q) / 0.5);
float ph = dot(O.xy / 100.0, float2(0.37, 0.61));
float heave = (0.5 + 0.5 * sin(T * 0.7 + ph)) * 0.2 * (1.0 - r * r);
float ring = 0.05 * sin(r * 10.0 - T * 1.3 + ph) * (1.0 - r);
return float3(0, 0, (heave + ring) * 0.15 * 100.0 * step(57.5, LP.z) * Pool);
"""

MERCURY_HLSL = """
// ModelLibrary.wellPool (surface) and MaterialLibrary.mercuryLook, in world
// space (Z up).
float3 v = normalize(V);
float3 n = normalize(N);
float2 q = LP.xy / 100.0;
float r = length(q);
float2 dir = q / max(r, 1e-3);
float ph = dot(O.xy / 100.0, float2(0.37, 0.61));
float fadeOut = 1.0 - smoothstep(0.3, 0.5, r);
// The slope of three small wave trains: rings running out from the middle
// and two crossing swells.
float2 slope = dir * 0.2 * cos(r * 24.0 - T * 1.6 + ph)
    + float2(11.0, 6.0) * 0.006 * cos(dot(q, float2(11.0, 6.0)) + T * 0.9 + ph)
    + float2(-4.0, 14.0) * 0.005 * cos(dot(q, float2(-4.0, 14.0)) - T * 1.2);
slope *= fadeOut * Pool * step(0.0, n.z);
n = normalize(n + float3(-slope.x, -slope.y, 0.0));
Nrm = n;
// A cold studio sky mirrored in it (dark low down, silver-white overhead, a
// bright band across), and a blue-white sheen at grazing angles. Lumen
// reflects the real sky too, so the fake one is weaker than in Swift and
// fades at night.
float3 R = reflect(-v, n);
float up = smoothstep(0.72, 0.92, R.z);
float band = smoothstep(0.93, 0.99, sin(R.x * 5.0 + R.y * 2.0 + 1.0)) * smoothstep(0.5, 0.8, R.z);
float3 env = lerp(float3(0.05, 0.06, 0.08), float3(0.72, 0.78, 0.86), up) + band * float3(0.9, 0.95, 1.0);
float sheen = pow(1.0 - saturate(dot(n, v)), 3.0);
float day = 1.0 - 0.85 * Dark;
return (env * 0.7 * EnvGain * day + float3(0.55, 0.75, 1.0) * sheen * 0.8 * lerp(1.0, 0.5, Dark)) * Scale;
"""


def build_mercury(mpc_world):
    mat, g = new_master(M_MERCURY, unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    mat.set_editor_property("tangent_space_normal", False)
    tint = g.vector("BaseColorTint", (0.2867, 0.30921, 0.35234))
    g.out(tint, "", unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.scalar("Metallic", 1.0), "", unreal.MaterialProperty.MP_METALLIC)
    g.out(g.scalar("Roughness", 0.05), "", unreal.MaterialProperty.MP_ROUGHNESS)
    time = g.node(unreal.MaterialExpressionTime)
    lp = local_position(g)
    origin = instance_origin(g)
    pool = g.scalar("Pool", 0.0)
    wpo = custom(g, "Swift well pool (geometry)", POOL_WPO_HLSL, [
        ("LP", lp, ""), ("O", origin, ""), ("T", time, ""), ("Pool", pool, ""),
    ])
    g.out(wpo, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    c = custom(g, "Swift mercury (wellPool, mercuryLook)", MERCURY_HLSL, [
        ("LP", lp, ""), ("O", origin, ""), ("T", time, ""), ("Pool", pool, ""),
        ("N", g.node(unreal.MaterialExpressionVertexNormalWS), ""),
        ("V", g.node(unreal.MaterialExpressionCameraVectorWS), ""),
        ("Dark", g.mpc(mpc_world, "Dark"), ""),
        ("EnvGain", g.scalar("EnvGain", 0.6), ""),
        ("Scale", g.custom_data(1, "EmissionScale", 1.0), ""),
    ], outputs=[("Nrm", F3)])
    g.out(c, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    g.out(c, "Nrm", unreal.MaterialProperty.MP_NORMAL)
    # WPO keeps Nanite off for the pool mesh's sake (it is < 1000 triangles).
    finish(mat, M_MERCURY, (unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES, unreal.MaterialUsage.MATUSAGE_NANITE))
    return mat


# --- M_AcPuff ----------------------------------------------------------------

PUFF_HLSL = """
// A sprite on the engine plane (UV 0-1), turned to the eye by the C++.
// Star = 0: a soft round puff (lib.smoke); Star = 1: a glint, a small bright
// core with four thin rays (MaterialLibrary.glint).
float2 p = (UV - 0.5) * 2.0;
float d = length(p);
float soft = pow(saturate(1.0 - d), Sharp);
float rays = saturate(1.0 - abs(p.x) * 10.0) * saturate(1.0 - abs(p.y))
           + saturate(1.0 - abs(p.y) * 10.0) * saturate(1.0 - abs(p.x));
float star = max(pow(saturate(1.0 - d * 2.5), 2.0), rays * rays);
return C.rgb * C.a * lerp(soft, star, Star);
"""


def build_puff():
    mat, g = new_master(M_PUFF, unreal.MaterialShadingModel.MSM_UNLIT, unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property("two_sided", True)
    rgba = []
    for i, d in enumerate((1.0, 1.0, 1.0, 1.0)):
        rgba.append(g.node(unreal.MaterialExpressionPerInstanceCustomData, data_index=i, const_default_value=d))
    a3 = g.node(unreal.MaterialExpressionAppendVector)
    g.link(rgba[0], "", a3, "A")
    g.link(rgba[1], "", a3, "B")
    a4 = g.node(unreal.MaterialExpressionAppendVector)
    g.link(a3, "", a4, "A")
    g.link(rgba[2], "", a4, "B")
    a5 = g.node(unreal.MaterialExpressionAppendVector)
    g.link(a4, "", a5, "A")
    g.link(rgba[3], "", a5, "B")
    c = custom(g, "Sprite", PUFF_HLSL, [
        ("C", a5, ""),
        ("UV", g.node(unreal.MaterialExpressionTextureCoordinate), ""),
        ("Sharp", g.scalar("Sharp", 2.0), ""),
        ("Star", g.scalar("Star", 0.0), ""),
    ])
    g.out(c, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(mat, M_PUFF, (unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES,))
    for path, sharp, star in ((MI_VAPOUR, 1.6, 0.0), (MI_GLINT, 1.0, 1.0)):
        mi = MM.load(path) or MM.create(path, unreal.MaterialInstanceConstant,
                                        unreal.MaterialInstanceConstantFactoryNew())
        MEL.set_material_instance_parent(mi, mat)
        MEL.set_material_instance_scalar_parameter_value(mi, "Sharp", sharp)
        MEL.set_material_instance_scalar_parameter_value(mi, "Star", star)
        EAL.set_metadata_tag(mi, "AcMadeBy", "Tools/Editor/make_resource_materials.py")
        MEL.update_material_instance(mi)
        EAL.save_loaded_asset(mi)
    return mat


# --- re-parenting --------------------------------------------------------------

def reparent(info, masters):
    n = 0
    for key, mat in sorted(info["materials"].items()):
        target = M_OPAL if is_opal(mat) else M_MERCURY if is_pool(mat) else None
        if target is None:
            continue
        mi = MM.load(A.mi_asset_path(mat))
        if mi is None:
            log("no instance yet for %s (%s): run make_materials.py" % (mat["name"], key))
            continue
        MEL.set_material_instance_parent(mi, masters[target])
        if target == M_MERCURY:
            MEL.set_material_instance_scalar_parameter_value(mi, "Pool", 1.0)
        EAL.set_metadata_tag(mi, "AcMadeBy", "Tools/Editor/make_materials.py + make_resource_materials.py")
        MEL.update_material_instance(mi)
        EAL.save_loaded_asset(mi)
        log("%s → %s" % (A.mi_asset_path(mat), target))
        n += 1
    return n


def tint_well_halo(info):
    """The well's rim halo: SceneKit multiplies its emission by (0.62, 0.8, 1)
    (`ModelLibrary.well`, `halo.multiply`), which the export leaves out, so
    the halo came out white."""
    keys = set()
    for m in info["canonical"]:
        if m["name"].startswith("well_"):
            keys |= {x["key"] for x in m["materials"] if x["blend"] == "additive"}
    for key in sorted(keys):
        mat = info["materials"][key]
        mi = MM.load(A.mi_asset_path(mat))
        if mi is None:
            continue
        i = mat.get("emissiveIntensity", 1)
        MEL.set_material_instance_vector_parameter_value(mi, "EmissiveColor", MM.lc((0.62 * i, 0.8 * i, 1.0 * i)))
        MEL.update_material_instance(mi)
        EAL.save_loaded_asset(mi)
        log("%s: halo tint" % A.mi_asset_path(mat))


def main(info=None):
    info = info or A.analyse(A.load_manifest())
    MM.ensure_dir(A.MASTER_DIR)
    mpc = make_world_mpc()
    masters = {M_OPAL: build_opal(mpc), M_MERCURY: build_mercury(mpc)}
    build_puff()
    log("%d model instances re-parented" % reparent(info, masters))
    tint_well_halo(info)


if __name__ == "__main__":
    main()
