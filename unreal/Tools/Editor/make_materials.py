"""Master materials, the team palette and one material instance per exported
material (chunk A5, GAME-LAYER.md §3.4). Rerunnable: existing assets are
rebuilt in place.

Run it (it also runs as the first step of import_models.py):

    UnrealEditor unreal/Autocraft.uproject -run=pythonscript \
        -script="$PWD/unreal/Tools/Editor/make_materials.py" -unattended -nullrhi

What it makes:

- /Game/Materials/MPC_AcTeams: the 8 player colours, three per player:
  `PaintN` (sRGB, what the skins' blue paint turns into), `TintN` (linear,
  the hull trim colour) and `GlowN` (linear HDR, team lamps). Player 0 is the
  Swift blue, player 1 the Swift red. C++ can change them at runtime.
  `EmissiveBoost` scales every model emission at once (exposure tuning).
- /Game/Materials/M_Hull (lit), M_Emissive (unlit, SceneKit's `constant`
  lighting) and M_Additive (unlit additive). One graph, no static switches,
  so all model materials share one shader per master.
- /Game/Models/Materials/MI_<name>_<key>: one instance per canonical exported
  material (blue and teamless exports; the red export only tells which
  materials change with the team).
- /Game/Models/Textures/T_*: the textures those instances use.

Per-primitive data (the same index for an ISM's per-instance custom data or
a plain component's custom primitive data):
  0 team index (0..7, the palette), 1 emission scale (default 1).
  Reserved for later chunks: 2 fade/alpha, 3 char (shatter embers).
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ac_models as A  # noqa: E402

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

# The skins are painted in this blue (MaterialLibrary.team, sRGB-ish).
HOME_PAINT = (0.14, 0.24, 0.72)

# Player colours. Paint is what SkinTextures.painted turns blue paint into
# (sRGB components, like NSColor's). Tint and Glow of players 0 and 1 are the
# exported values; the others follow the same curve.
PALETTE = [
    # name, paint (sRGB), tint (linear) or None, glow (linear HDR) or None
    ("Blue", (0.14, 0.24, 0.72), (0.02846, 0.09125, 0.55521), (0.10833, 0.30945, 1.7)),
    ("Red", (0.70, 0.09, 0.07), (0.53918, 0.02291, 0.00742), (1.7, 0.18849, 0.10833)),
    ("Green", (0.16, 0.56, 0.18), None, None),
    ("Gold", (0.86, 0.64, 0.10), None, None),
    ("Violet", (0.50, 0.20, 0.76), None, None),
    ("Teal", (0.06, 0.58, 0.62), None, None),
    ("Orange", (0.92, 0.40, 0.06), None, None),
    ("Pink", (0.88, 0.32, 0.60), None, None),
]


def _tint(paint):
    return tuple(c ** 1.8 for c in paint)


def _glow(paint):
    m = max(paint)
    return tuple(1.7 * (c / m) ** 1.6 for c in paint)


def palette():
    out = []
    for name, paint, tint, glow in PALETTE:
        out.append((name, paint, tint or _tint(paint), glow or _glow(paint)))
    return out


def log(msg):
    unreal.log("[make_materials] " + msg)


def ensure_dir(path):
    if not EAL.does_directory_exist(path):
        EAL.make_directory(path)


def load(path):
    return EAL.load_asset(path) if EAL.does_asset_exist(path) else None


def create(path, cls, factory):
    folder, name = path.rsplit("/", 1)
    ensure_dir(folder)
    return AT.create_asset(name, folder, cls, factory)


def lc(rgb, a=1.0):
    return unreal.LinearColor(rgb[0], rgb[1], rgb[2], a)


# --- the palette -------------------------------------------------------------

def make_mpc():
    mpc = load(A.MPC_TEAMS) or create(A.MPC_TEAMS, unreal.MaterialParameterCollection,
                                      unreal.MaterialParameterCollectionFactoryNew())
    vectors = []
    for i, (_, paint, tint, glow) in enumerate(palette()):
        for kind, rgb in (("Paint", paint), ("Tint", tint), ("Glow", glow)):
            p = unreal.CollectionVectorParameter()
            p.set_editor_property("parameter_name", "%s%d" % (kind, i))
            p.set_editor_property("default_value", lc(rgb))
            vectors.append(p)
    s = unreal.CollectionScalarParameter()
    s.set_editor_property("parameter_name", "EmissiveBoost")
    s.set_editor_property("default_value", 1.0)
    mpc.set_editor_property("vector_parameters", vectors)
    mpc.set_editor_property("scalar_parameters", [s])
    EAL.save_loaded_asset(mpc)
    return mpc


# --- master materials --------------------------------------------------------

RECOLOUR_HLSL = """
// SkinTextures.painted (Models+Prospector.swift), per pixel, in sRGB.
float3 lin = max(C, 0.0);
float3 s = lerp(1.055 * pow(lin, 1.0 / 2.4) - 0.055, lin * 12.92, step(lin, 0.0031308));
float3 home = float3(0.14, 0.24, 0.72);
float away = saturate((dot(abs(T - home), 1.0) - 0.01) * 1000.0);
float blueness = s.b - max(s.r, s.g);
float k = saturate((blueness - 0.03) / 0.12) * Enable * away;
float lum = dot(s, float3(0.3, 0.59, 0.11));
float lumT = dot(T, float3(0.3, 0.59, 0.11));
float3 q = saturate(T * (lum / 0.24) * (0.24 / max(lumT, 0.05)) * 0.9);
float3 o = lerp(s, q, k);
return lerp(pow((o + 0.055) / 1.055, 2.4), o / 12.92, step(o, 0.04045));
"""

PICK_HLSL = """
int i = clamp((int)round(I), 0, 7);
float3 c = C0;
if (i == 1) c = C1; else if (i == 2) c = C2; else if (i == 3) c = C3;
else if (i == 4) c = C4; else if (i == 5) c = C5; else if (i == 6) c = C6;
else if (i == 7) c = C7;
return c;
"""


class Graph:
    def __init__(self, mat):
        self.mat = mat
        self.y = 0

    def node(self, cls, x=-1200, **props):
        e = MEL.create_material_expression(self.mat, cls, x, self.y)
        self.y += 140
        for k, v in props.items():
            e.set_editor_property(k, v)
        return e

    def link(self, a, a_out, b, b_in):
        if not MEL.connect_material_expressions(a, a_out, b, b_in):
            raise RuntimeError("cannot connect %s.%s → %s.%s" % (a.get_name(), a_out, b.get_name(), b_in))

    def out(self, a, a_out, prop):
        if not MEL.connect_material_property(a, a_out, prop):
            raise RuntimeError("cannot connect %s.%s → %s" % (a.get_name(), a_out, prop))

    def scalar(self, name, value, group="Ac"):
        return self.node(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=value, group=group)

    def vector(self, name, rgb, group="Ac"):
        return self.node(unreal.MaterialExpressionVectorParameter, parameter_name=name, default_value=lc(rgb), group=group)

    def texture(self, name, default, sampler, uv):
        t = self.node(unreal.MaterialExpressionTextureSampleParameter2D, parameter_name=name,
                      texture=unreal.load_asset(default), sampler_type=sampler, group="Ac")
        self.link(uv, "", t, "UVs")
        return t

    def custom(self, desc, code, inputs, out_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3):
        c = self.node(unreal.MaterialExpressionCustom, x=-500, code=code, description=desc, output_type=out_type)
        ins = []
        for n in inputs:
            ci = unreal.CustomInput()
            ci.set_editor_property("input_name", n)
            ins.append(ci)
        c.set_editor_property("inputs", ins)
        return c

    def mpc(self, collection, name):
        return self.node(unreal.MaterialExpressionCollectionParameter, x=-1600, collection=collection, parameter_name=name)

    def binop(self, cls, a, b, a_out="", b_out=""):
        e = self.node(cls, x=-300)
        self.link(a, a_out, e, "A")
        self.link(b, b_out, e, "B")
        return e

    def custom_data(self, index, name, default):
        """Per-instance custom data `index` on an ISM; on a plain component the
        custom primitive data at the same index (a parameter feeding the
        default pin); else `default`."""
        prim = self.node(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=default,
                         group="CustomData", use_custom_primitive_data=True, primitive_data_index=index)
        pic = self.node(unreal.MaterialExpressionPerInstanceCustomData, data_index=index, const_default_value=default)
        self.link(prim, "", pic, "DefaultValue")
        return pic


def build_master(path, kind, mpc):
    """kind: 'hull' (lit), 'emissive' (unlit), 'additive' (unlit additive)."""
    mat = load(path) or create(path, unreal.Material, unreal.MaterialFactoryNew())
    MEL.delete_all_material_expressions(mat)
    if kind == "hull":
        mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    else:
        mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE if kind == "additive" else unreal.BlendMode.BLEND_OPAQUE)
    mat.set_editor_property("two_sided", False)
    g = Graph(mat)

    # UVs.
    tc = g.node(unreal.MaterialExpressionTextureCoordinate)
    uvs = g.vector("UVScale", (1, 1, 0))
    mask = g.node(unreal.MaterialExpressionComponentMask, r=True, g=True, b=False, a=False)
    g.link(uvs, "", mask, "")
    uv = g.binop(unreal.MaterialExpressionMultiply, tc, mask)

    color = unreal.MaterialSamplerType.SAMPLERTYPE_COLOR
    base_tex = g.texture("BaseColorTex", "/Engine/EngineResources/WhiteSquareTexture", color, uv)
    emis_tex = g.texture("EmissiveTex", "/Engine/EngineResources/WhiteSquareTexture", color, uv)
    base_tint = g.vector("BaseColorTint", (1, 1, 1))
    emis_color = g.vector("EmissiveColor", (0, 0, 0))

    # Team colour.
    team = g.custom_data(0, "TeamIndex", 0.0)
    emis_scale = g.custom_data(1, "EmissionScale", 1.0)
    picks = {}
    for kind_name in ("Paint", "Tint", "Glow"):
        c = g.custom("AcPick%s" % kind_name, PICK_HLSL, ["I"] + ["C%d" % i for i in range(8)])
        g.link(team, "", c, "I")
        for i in range(8):
            g.link(g.mpc(mpc, "%s%d" % (kind_name, i)), "", c, "C%d" % i)
        picks[kind_name] = c
    flag = {n: g.scalar(n, 0.0, "Team") for n in A.TEAM_FIELDS}

    def recolour(src, enable):
        c = g.custom("AcRecolour", RECOLOUR_HLSL, ["C", "T", "Enable"])
        g.link(src, "RGB", c, "C")
        g.link(picks["Paint"], "", c, "T")
        g.link(enable, "", c, "Enable")
        return c

    # Base colour: texture (recoloured where painted) × (tint or team tint).
    base = recolour(base_tex, flag["PaintBase"])
    tint = g.node(unreal.MaterialExpressionLinearInterpolate, x=-300)
    g.link(base_tint, "", tint, "A")
    g.link(picks["Tint"], "", tint, "B")
    g.link(flag["TeamTint"], "", tint, "Alpha")
    base_color = g.binop(unreal.MaterialExpressionMultiply, base, tint)

    # Emission: texture (recoloured) × (colour or team glow) × scale × boost.
    emis = recolour(emis_tex, flag["PaintEmissive"])
    ecol = g.node(unreal.MaterialExpressionLinearInterpolate, x=-300)
    g.link(emis_color, "", ecol, "A")
    g.link(picks["Glow"], "", ecol, "B")
    g.link(flag["TeamGlow"], "", ecol, "Alpha")
    e1 = g.binop(unreal.MaterialExpressionMultiply, emis, ecol)
    boost = g.mpc(mpc, "EmissiveBoost")
    e2 = g.binop(unreal.MaterialExpressionMultiply, e1, boost)

    opacity = g.scalar("Opacity", 1.0)
    op_tex = g.texture("OpacityTex", "/Engine/EngineResources/WhiteSquareTexture", color, uv)
    op = g.binop(unreal.MaterialExpressionMultiply, opacity, op_tex, b_out="R")

    if kind == "hull":
        e3 = g.binop(unreal.MaterialExpressionMultiply, e2, emis_scale)
        metallic, roughness = g.scalar("Metallic", 0.0), g.scalar("Roughness", 0.5)
        # B8: metals also mirror Swift's studio sky (make_resource_materials.hull_env).
        import make_resource_materials
        e3 = g.binop(unreal.MaterialExpressionAdd, e3, make_resource_materials.hull_env(g, base_color, metallic, roughness))
        g.out(base_color, "", unreal.MaterialProperty.MP_BASE_COLOR)
        g.out(metallic, "", unreal.MaterialProperty.MP_METALLIC)
        g.out(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
        g.out(e3, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        normal_tex = g.texture("NormalTex", "/Engine/EngineMaterials/FlatNormal", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, uv)
        flat = g.node(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(0, 0, 1, 0))
        nl = g.node(unreal.MaterialExpressionLinearInterpolate, x=-300)
        g.link(flat, "", nl, "A")
        g.link(normal_tex, "RGB", nl, "B")
        g.link(g.scalar("NormalStrength", 1.0), "", nl, "Alpha")
        g.out(nl, "", unreal.MaterialProperty.MP_NORMAL)
        g.out(op, "", unreal.MaterialProperty.MP_OPACITY)
    else:
        # SceneKit's constant lighting: the diffuse colour as is, plus emission.
        if kind == "additive":
            # The PNGs are straight alpha (SceneKit drew them premultiplied):
            # without this, a glow sprite adds its whole square (B8: the
            # well's rim halo).
            e2 = g.binop(unreal.MaterialExpressionMultiply, e2, emis_tex, b_out="A")
        total = g.binop(unreal.MaterialExpressionAdd, base_color, e2)
        scaled = g.binop(unreal.MaterialExpressionMultiply, total, emis_scale)
        g.out(scaled, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        g.out(op, "", unreal.MaterialProperty.MP_OPACITY)

    for usage in (unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES, unreal.MaterialUsage.MATUSAGE_NANITE,
                  unreal.MaterialUsage.MATUSAGE_STATIC_LIGHTING):
        if kind == "additive" and usage == unreal.MaterialUsage.MATUSAGE_NANITE:
            continue
        MEL.set_base_material_usage(mat, usage)
    MEL.layout_material_expressions(mat)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    log("master %s" % path)
    return mat


# --- textures ----------------------------------------------------------------

def import_texture(rel, role, clamp):
    """role: 'color', 'normal' or 'linear'."""
    path = A.texture_asset_path(rel)
    src = os.path.join(A.SRC, rel)
    stamp = A.file_hash(src) + "-" + role + ("-clamp" if clamp else "")
    tex = load(path)
    if tex is not None and EAL.get_metadata_tag(tex, "AcSource") == stamp:
        return tex
    folder, name = path.rsplit("/", 1)
    ensure_dir(folder)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", src)
    task.set_editor_property("destination_path", folder)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    AT.import_asset_tasks([task])
    tex = unreal.load_asset(path)
    if tex is None:
        raise RuntimeError("texture import failed: " + src)
    if role == "normal":
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("srgb", False)
        # SceneKit normal maps are +Y up (OpenGL); Unreal's are -Y (DirectX).
        tex.set_editor_property("flip_green_channel", True)
    elif role == "linear":
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    else:
        tex.set_editor_property("srgb", True)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
    addr = unreal.TextureAddress.TA_CLAMP if clamp else unreal.TextureAddress.TA_WRAP
    tex.set_editor_property("address_x", addr)
    tex.set_editor_property("address_y", addr)
    EAL.set_metadata_tag(tex, "AcSource", stamp)
    EAL.set_metadata_tag(tex, "AcMadeBy", "Tools/Editor/make_materials.py")
    EAL.save_loaded_asset(tex)
    return tex


# --- material instances ------------------------------------------------------

def master_for(mat):
    if mat["blend"] == "additive":
        return A.M_ADDITIVE
    if mat["lightingModel"] == "SCNLightingModelConstant":
        return A.M_EMISSIVE
    return A.M_HULL


def make_instance(mat, flags, masters):
    path = A.mi_asset_path(mat)
    mi = load(path) or create(path, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, masters[master_for(mat)])
    MEL.clear_all_material_instance_parameters(mi)
    clamp = mat.get("wrap") == "clamp"

    def vec(name, rgb):
        MEL.set_material_instance_vector_parameter_value(mi, name, lc(rgb))

    def sca(name, v):
        MEL.set_material_instance_scalar_parameter_value(mi, name, float(v))

    def tex(name, rel, role):
        MEL.set_material_instance_texture_parameter_value(mi, name, import_texture(rel, role, clamp))

    # Base colour: the untinted texture and the tint apart, so the team
    # recolour runs on the paint as in Swift.
    if "baseColorTexture" in mat:
        tex("BaseColorTex", mat.get("baseColorTextureUntinted", mat["baseColorTexture"]), "color")
        vec("BaseColorTint", mat.get("baseColorTint", (1, 1, 1)) if "baseColorTextureUntinted" in mat else (1, 1, 1))
    else:
        vec("BaseColorTint", mat.get("baseColor", (1, 1, 1)))
    sca("Metallic", mat.get("metallic", 0))
    sca("Roughness", mat.get("roughness", 0.5))
    if "normalTexture" in mat:
        tex("NormalTex", mat["normalTexture"], "normal")
        sca("NormalStrength", mat.get("normalStrength", 1))
    if "emissiveTexture" in mat:
        tex("EmissiveTex", mat["emissiveTexture"], "color")
        i = mat.get("emissiveIntensity", 1)
        vec("EmissiveColor", (i, i, i))
    elif "emissive" in mat:
        vec("EmissiveColor", mat["emissive"])  # intensity already folded in
    if "opacityTexture" in mat:
        # 'color' too: the same PNGs (smoke, spark) are also base colours.
        tex("OpacityTex", mat["opacityTexture"], "color")
    if "opacity" in mat:
        sca("Opacity", mat["opacity"])
    # `uvScale` (SceneKit contentsTransform) is already baked into the
    # exported st (ExportModels.swift): UVScale stays 1.
    for f in A.TEAM_FIELDS:
        if f in flags:
            sca(f, 1)

    over = mi.get_editor_property("base_property_overrides")
    over.set_editor_property("override_two_sided", bool(mat.get("doubleSided")))
    over.set_editor_property("two_sided", bool(mat.get("doubleSided")))
    translucent = mat["blend"] == "translucent"
    over.set_editor_property("override_blend_mode", translucent)
    over.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT if translucent else unreal.BlendMode.BLEND_OPAQUE)
    mi.set_editor_property("base_property_overrides", over)
    EAL.set_metadata_tag(mi, "AcMaterialKey", mat["key"])
    EAL.set_metadata_tag(mi, "AcMadeBy", "Tools/Editor/make_materials.py")
    MEL.update_material_instance(mi)
    EAL.save_loaded_asset(mi)
    return mi


def main(info=None):
    info = info or A.analyse(A.load_manifest())
    ensure_dir(A.MASTER_DIR)
    mpc = make_mpc()
    masters = {
        A.M_HULL: build_master(A.M_HULL, "hull", mpc),
        A.M_EMISSIVE: build_master(A.M_EMISSIVE, "emissive", mpc),
        A.M_ADDITIVE: build_master(A.M_ADDITIVE, "additive", mpc),
    }
    n = 0
    with unreal.ScopedSlowTask(len(info["materials"]), "Model materials") as task:
        for key, mat in sorted(info["materials"].items()):
            task.enter_progress_frame(1)
            make_instance(mat, info["flags"][key], masters)
            n += 1
    log("%d material instances" % n)
    # B8: M_Opal and M_Mercury, and the ore and pool instances re-parented
    # to them (else this run would leave them on M_Hull).
    import make_resource_materials
    make_resource_materials.main(info)
    # B5: the Kestrel's canopy glass (M_AcKestrelGlass), re-parented the same way.
    import make_kestrel_glass
    make_kestrel_glass.main(info)
    # B4: the Longbow's lock lights lit, its smoke ring on M_AcSmokeRing.
    import make_artillery_materials
    make_artillery_materials.main(info)
    # B2: the drill bit's and the Mini gun's glows lit, its smear on M_AcSmear.
    import make_infantry_materials
    make_infantry_materials.main(info)
    # B3: the Firefly's jet (M_AcFlame), sprites (M_AcSprite), tyre smear, hot lip.
    import make_flame_materials
    make_flame_materials.main(info)
    # C4: M_AcHullFade/M_AcEmissiveFade, M_Hull/M_Emissive masked with a fade dither (bodies, debris).
    import make_corpse_materials
    make_corpse_materials.main()
    # C5: M_AcEmber, the wrecks' burnt metal (M_Hull, fade dither, hot spots by custom data 3).
    import make_ember_material
    make_ember_material.main()
    return masters


if __name__ == "__main__":
    main()
