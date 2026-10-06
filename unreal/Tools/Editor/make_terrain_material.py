"""M_Terrain and its textures (GAME-LAYER.md §2.6 "Terrain", chunk A2).

Rerunnable: reimports the textures over the old ones and rebuilds the
material's graph from nothing each time.

Inputs: unreal/Content-src/terrain/*.png, made by
`uv run unreal/Tools/terrain_textures.py` (the Swift game's ground textures
plus baked normal maps).

Outputs:
  /Game/Terrain/Textures/T_GroundDirt, _Grass, _Highland, T_CliffRock,
      T_BasePlating, and each one's _N normal map
  /Game/Terrain/M_Terrain

The material is the Swift ground shader (`TerrainBuilder.surfaceShader`,
Sources/Autocraft/Terrain.swift) as one Custom HLSL node, so the two read
side by side. AAcTerrain (Source/Autocraft/AcTerrain.cpp) sets its
parameters: `Splat` (the RGBA splat map: grass, highland, plating, scorch),
`Bounds` (minX, minY, width, depth of the map, cells) and `Haze` (rgb, and
the border's width in cells). On top of the Swift look: normal maps on every
ground texture and on the cliff rock, roughness that follows the texture,
and a faint large-scale tint variation, all in the Swift palette.

Run it in the editor (Tools > Execute Python Script, or `py <path>` in the
console), or headless when the live editor does not have these assets open:
  UnrealEditor-Cmd Autocraft.uproject -run=pythonscript -script=<abs path>
"""
import os

import unreal

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SRC = os.path.join(ROOT, "Content-src", "terrain")
TEX_DIR = "/Game/Terrain/Textures"
MAT_DIR = "/Game/Terrain"
MAT_NAME = "M_Terrain"

ALBEDO = ["T_GroundDirt", "T_GroundGrass", "T_GroundHighland", "T_CliffRock", "T_BasePlating"]

mel = unreal.MaterialEditingLibrary
assets = unreal.EditorAssetLibrary


def import_textures():
    tasks = []
    for name in ALBEDO:
        for suffix in ("", "_N"):
            task = unreal.AssetImportTask()
            task.filename = os.path.join(SRC, name + suffix + ".png")
            task.destination_path = TEX_DIR
            task.destination_name = name + suffix
            task.replace_existing = True
            task.automated = True
            task.save = False
            tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    out = {}
    for name in ALBEDO:
        for suffix in ("", "_N"):
            path = f"{TEX_DIR}/{name}{suffix}"
            tex = unreal.load_asset(path)
            if tex is None:
                raise RuntimeError(f"import failed: {path}")
            if suffix == "_N":
                tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
                tex.set_editor_property("srgb", False)
            else:
                tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
                tex.set_editor_property("srgb", True)
            tex.set_editor_property("address_x", unreal.TextureAddress.TA_WRAP)
            tex.set_editor_property("address_y", unreal.TextureAddress.TA_WRAP)
            tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
            tex.set_editor_property("max_texture_size", 0)
            # Always on screen: no streaming (sharp from the first frame).
            tex.set_editor_property("never_stream", True)
            assets.save_asset(path, only_if_is_dirty=False)
            out[name + suffix] = tex
    return out


# The Swift surface shader, in Unreal's axes: Swift's ground plane (x, z) is
# Unreal's (X, Y) and its height y is Unreal's Z; everything below is in
# cells (Unreal centimetres / 100).
HLSL = r"""
float3 wp = WP / 100.0;
float3 wn = normalize(VN);
float2 g = wp.xy;
float2 suv = (g - Bounds.xy) / Bounds.zw;
float4 splat = Texture2DSampleLevel(Splat, SplatSampler, suv, 0);

// Two scales per texture break up visible tiling.
float2 t1 = g / 7.0;
float2 t2 = float2(g.x * 0.7 - g.y * 0.7, g.x * 0.7 + g.y * 0.7) / 17.0 + 0.37;
float2 tp = g / 3.5;
float3 dirt = lerp(Texture2DSample(Dirt, DirtSampler, t1).rgb, Texture2DSample(Dirt, DirtSampler, t2).rgb, 0.4);
float3 grass = lerp(Texture2DSample(Grass, GrassSampler, t1 * 1.3).rgb, Texture2DSample(Grass, GrassSampler, t2).rgb, 0.35);
float3 high = lerp(Texture2DSample(High, HighSampler, t1 * 0.9).rgb, Texture2DSample(High, HighSampler, t2).rgb, 0.3);
high = lerp(pow(max(high, 0.0), 1.5), dirt * 1.15, 0.3) * float3(0.8, 0.7, 0.6);
float3 plate = Texture2DSample(Plate, PlateSampler, tp).rgb;

dirt *= float3(0.78, 0.7, 0.64);
grass *= float3(0.85, 0.9, 0.8);
float3 c = dirt;
c = lerp(c, high, splat.g);
c = lerp(c, grass, splat.r);
c = lerp(c, plate, splat.b);
c *= 1.0 - splat.a * 0.5;

// Richer look: a faint tint variation over tens of cells, so wide plains do
// not read as one repeated tile (kept within +-6%, the palette stays).
float macroV = Texture2DSample(High, HighSampler, g / 61.0 + 0.23).g;
c *= lerp(0.94, 1.06, saturate(macroV * 1.6 - 0.3));

// Cliff rock on steep faces, projected sideways (triplanar on X and Y).
float slope = 1.0 - wn.z;
float rockW = smoothstep(0.22, 0.45, slope);
float2 ux = float2(wp.y, -wp.z * 1.4) / 5.0;
float2 uy = float2(wp.x, -wp.z * 1.4) / 5.0;
float3 rx = Texture2DSample(Rock, RockSampler, ux).rgb;
float3 ry = Texture2DSample(Rock, RockSampler, uy).rgb;
float ax = abs(wn.x), ay = abs(wn.y);
float3 rock = (rx * ax + ry * ay) / max(ax + ay, 1e-3);
c = lerp(c, rock * 0.95, rockW);

// Darken low ground a touch and cliff bases, like baked AO.
c *= lerp(0.88, 1.0, saturate(wp.z / 2.4));

// Past the map's edge (the border scenery, out of play): wild highland and
// rock, dimmer, fading into the haze further out, with a faint dark seam
// along the edge itself. Haze.w is how far the border reaches.
float2 lo = Bounds.xy, hi = Bounds.xy + Bounds.zw;
float past = length(max(max(lo - g, g - hi), 0.0));
float inside = min(min(g.x - lo.x, hi.x - g.x), min(g.y - lo.y, hi.y - g.y));
float edge = past > 0.0 ? past : max(inside, 0.0);
c *= 1.0 - 0.3 * (1.0 - smoothstep(0.0, 0.9, edge));
float fade = 0.0;
if (past > 0.0) {
    float3 wild = lerp(lerp(dirt, high, 0.65), rock * 0.95, rockW);
    c = lerp(c, wild, smoothstep(0.0, 1.5, past));
    c *= lerp(1.0, 0.72, smoothstep(0.0, 2.0, past));
    fade = smoothstep(Haze.w * 0.3, Haze.w * 0.9, past);
    splat = lerp(splat, float4(0, 0, 0, 0), smoothstep(0.0, 1.5, past));
}

// Normals (world space). Ground: the textures' normal maps, projected from
// above (tangent +X, bitangent +Y), blended as the colours are.
float3 nd = Texture2DSample(DirtN, DirtNSampler, t1).xyz * 2.0 - 1.0;
float3 nh = Texture2DSample(HighN, HighNSampler, t1 * 0.9).xyz * 2.0 - 1.0;
float3 ng = Texture2DSample(GrassN, GrassNSampler, t1 * 1.3).xyz * 2.0 - 1.0;
float3 np = Texture2DSample(PlateN, PlateNSampler, tp).xyz * 2.0 - 1.0;
float2 dn = nd.xy;
dn = lerp(dn, nh.xy, splat.g);
dn = lerp(dn, ng.xy, splat.r);
dn = lerp(dn, np.xy, splat.b);
float3 nTop = normalize(wn + float3(dn, 0.0) * Detail);
// Cliffs: the rock's normal map on the same sideways projections (U along
// +Y or +X, V down the face).
float3 nrx = Texture2DSample(RockN, RockNSampler, ux).xyz * 2.0 - 1.0;
float3 nry = Texture2DSample(RockN, RockNSampler, uy).xyz * 2.0 - 1.0;
float3 dRock = (float3(0.0, nrx.x, -nrx.y) * ax + float3(nry.x, 0.0, -nry.y) * ay) / max(ax + ay, 1e-3);
float3 nRock = normalize(wn + dRock * Detail * 1.3);
float3 n = normalize(lerp(nTop, nRock, rockW));
Nrm = normalize(lerp(n, wn, fade));

// Roughness: the Swift value (plating is smoother and a little metallic),
// varied by the texture's own light and dark.
float lum = dot(c, float3(0.3, 0.59, 0.11));
Rough = saturate(lerp(0.92, 0.55, splat.b) + (0.35 - lum) * 0.25);
Metal = splat.b * 0.35;
Emis = Haze.rgb * fade * HazeGain;
return c * (1.0 - fade);
"""


def build_material(tex):
    path = f"{MAT_DIR}/{MAT_NAME}"
    mat = unreal.load_asset(path) if assets.does_asset_exist(path) else None
    if mat is None:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            MAT_NAME, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    mel.delete_all_material_expressions(mat)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    mat.set_editor_property("tangent_space_normal", False)
    mat.set_editor_property("two_sided", False)

    def node(cls, x, y):
        return mel.create_material_expression(mat, cls, x, y)

    custom = node(unreal.MaterialExpressionCustom, -300, 0)
    custom.set_editor_property("code", HLSL)
    custom.set_editor_property("description", "Swift ground shader")
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    outputs = []
    for name, kind in (("Rough", "CMOT_FLOAT1"), ("Metal", "CMOT_FLOAT1"),
                       ("Emis", "CMOT_FLOAT3"), ("Nrm", "CMOT_FLOAT3")):
        o = unreal.CustomOutput()
        o.set_editor_property("output_name", name)
        o.set_editor_property("output_type", getattr(unreal.CustomMaterialOutputType, kind))
        outputs.append(o)
    custom.set_editor_property("additional_outputs", outputs)

    # Inputs, in order: (input name, source node).
    wp = node(unreal.MaterialExpressionWorldPosition, -900, -400)
    vn = node(unreal.MaterialExpressionVertexNormalWS, -900, -340)
    sources = [("WP", wp), ("VN", vn)]
    y = -260

    def tex_param(input_name, param, texture, sampler):
        nonlocal y
        t = node(unreal.MaterialExpressionTextureObjectParameter, -900, y)
        y += 70
        t.set_editor_property("parameter_name", param)
        t.set_editor_property("texture", texture)
        t.set_editor_property("sampler_type", sampler)
        sources.append((input_name, t))

    color = unreal.MaterialSamplerType.SAMPLERTYPE_COLOR
    normal = unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL
    linear = unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR
    tex_param("Splat", "Splat", unreal.load_asset("/Engine/EngineResources/Black"), linear)
    tex_param("Dirt", "Dirt", tex["T_GroundDirt"], color)
    tex_param("Grass", "Grass", tex["T_GroundGrass"], color)
    tex_param("High", "Highland", tex["T_GroundHighland"], color)
    tex_param("Rock", "Rock", tex["T_CliffRock"], color)
    tex_param("Plate", "Plating", tex["T_BasePlating"], color)
    tex_param("DirtN", "DirtNormal", tex["T_GroundDirt_N"], normal)
    tex_param("GrassN", "GrassNormal", tex["T_GroundGrass_N"], normal)
    tex_param("HighN", "HighlandNormal", tex["T_GroundHighland_N"], normal)
    tex_param("RockN", "RockNormal", tex["T_CliffRock_N"], normal)
    tex_param("PlateN", "PlatingNormal", tex["T_BasePlating_N"], normal)

    def vec_param(name, value):
        nonlocal y
        v = node(unreal.MaterialExpressionVectorParameter, -900, y)
        y += 70
        v.set_editor_property("parameter_name", name)
        v.set_editor_property("default_value", unreal.LinearColor(*value))
        return v

    def scalar_param(name, value):
        nonlocal y
        s = node(unreal.MaterialExpressionScalarParameter, -900, y)
        y += 70
        s.set_editor_property("parameter_name", name)
        s.set_editor_property("default_value", value)
        return s

    # Defaults: a 200 x 128 map centred on the origin, the Swift haze.
    sources.append(("Bounds", vec_param("Bounds", (-100.0, -64.0, 200.0, 128.0))))
    sources.append(("Haze", vec_param("Haze", (0.27, 0.22, 0.19, 90.0))))
    sources.append(("Detail", scalar_param("DetailNormal", 0.55)))
    sources.append(("HazeGain", scalar_param("HazeGain", 1.0)))

    inputs = []
    for name, _ in sources:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", name)
        inputs.append(ci)
    custom.set_editor_property("inputs", inputs)
    for name, src in sources:
        # Vector parameters: the RGBA output (index 0 is RGB).
        out = ""
        if isinstance(src, unreal.MaterialExpressionVectorParameter):
            out = "RGBA"
        if not mel.connect_material_expressions(src, out, custom, name):
            raise RuntimeError(f"could not connect {name}")

    P = unreal.MaterialProperty
    for out, prop in (("", P.MP_BASE_COLOR), ("Rough", P.MP_ROUGHNESS), ("Metal", P.MP_METALLIC),
                      ("Emis", P.MP_EMISSIVE_COLOR), ("Nrm", P.MP_NORMAL)):
        if not mel.connect_material_property(custom, out, prop):
            raise RuntimeError(f"could not connect output {out or 'main'}")

    mel.recompile_material(mat)
    assets.save_asset(path, only_if_is_dirty=False)
    return mat


def main():
    tex = import_textures()
    mat = build_material(tex)
    unreal.log(f"make_terrain_material: built {mat.get_path_name()}")


main()
