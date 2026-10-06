"""The particle set of the effects (chunk C3, GAME-LAYER.md §2.7): what the
Swift game draws with SCNParticleSystem (explosions, blasts, ground fire and
embers, dust, opal chips, ricochets, sparks, damage smoke and fire, the
jets, flames and plumes of the vehicles), drawn by
Source/Autocraft/AcParticles.cpp.

How they are drawn: every particle is one instance of a quad in an
instanced static mesh, one component per particle kind, written ONCE when
it is born (where, when, its velocity, size, colour) and never again. The
vertex shader flies it from that: position and velocity from the SceneKit
emitter's damping and acceleration in closed form, its size and colour from
the kind's keyframe ramps (SCNParticlePropertyController) over its life,
turned to the camera, stretched along its motion for sparks. Dead or unborn
particles collapse to a point. So a fight costs the CPU only the particles
born this frame, whatever is in the air.

Rerunnable: every asset is rebuilt in place.

    UnrealEditor-Cmd unreal/Autocraft.uproject -run=pythonscript \
        -script="$PWD/unreal/Tools/Editor/make_particle_materials.py" -unattended -nullrhi

or in the open editor: py "<repo>/unreal/Tools/Editor/make_particle_materials.py"

What it makes (all in /Game/Effects):

- SM_AcParticleQuad: the engine's Plane (100 cm, XY, normal +Z) with its
  bounds grown to a cube (±50 cm in Z too), so an instance's bounds (the
  quad's box times the instance scale, which the C++ sets to how far the
  particle can fly) are not flat and the GPU culls it only when its whole
  flight is off screen.
- MPC_AcParticles: `Time`, the game clock the particles were stamped with
  (the C++ writes it every frame; a paused game freezes them).
- M_AcParticleAdd: additive (SceneKit `.additive`): fire, sparks, jets; soft
  where it meets the ground (`SoftCm`, a richer look than SceneKit's cut).
- M_AcParticleSmoke: translucent (SceneKit `.alpha`), soft where it meets
  the ground (depth fade, `SoftCm`). Unused for now: every Swift system keeps
  SceneKit's default blend, `.additive`, smoke and dust included.

Both are unlit, like the Swift particles (`isLightingEnabled = false`).

Per instance (custom data, written by AcParticles.cpp):
  0 birth time (s, the MPC's clock), 1 life (s), 2-4 velocity (cm/s,
  world), 5 size (cm, SceneKit's particleSize: the quad is twice it wide), 6-8 colour (linear, times the particle
  intensity), 9 alpha, 10 a random number (the sprite's turn).
Per kind (material instance parameters, set by the C++ from its table):
  Damping (1/s, SceneKit dampingFactor), Accel (cm/s², world),
  SizeKeys/SizeTimes (four keys of the size ramp, times the size),
  Col0..Col3/ColTimes (four RGBA keys multiplying the colour; the opacity
  ramp is their alpha), Stretch (s: the quad reaches speed × Stretch ahead
  and behind along its motion, SceneKit `stretchFactor`), SpriteTex (lib.smoke or
  lib.spark: only its alpha is used), SoftCm (smoke only).
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_materials as MM  # noqa: E402
import make_resource_materials as RM  # noqa: E402

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

DIR = "/Game/Effects"
QUAD = DIR + "/SM_AcParticleQuad"
MPC = DIR + "/MPC_AcParticles"
M_ADD = DIR + "/M_AcParticleAdd"
M_SMOKE = DIR + "/M_AcParticleSmoke"
T_SMOKE = "/Game/Models/Textures/T_smoke"
F1 = unreal.CustomMaterialOutputType.CMOT_FLOAT1
F3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3
MADE_BY = "Tools/Editor/make_particle_materials.py"


def log(msg):
    unreal.log("[make_particle_materials] " + msg)


# --- the quad and the clock ------------------------------------------------------

def make_quad():
    mesh = MM.load(QUAD)
    if mesh is None:
        MM.ensure_dir(DIR)
        mesh = EAL.duplicate_asset("/Engine/BasicShapes/Plane", QUAD)
    mesh.set_editor_property("positive_bounds_extension", unreal.Vector(0, 0, 50))
    mesh.set_editor_property("negative_bounds_extension", unreal.Vector(0, 0, 50))
    EAL.set_metadata_tag(mesh, "AcMadeBy", MADE_BY)
    EAL.save_loaded_asset(mesh)
    log("quad %s" % QUAD)
    return mesh


def make_mpc():
    mpc = MM.load(MPC) or MM.create(MPC, unreal.MaterialParameterCollection,
                                    unreal.MaterialParameterCollectionFactoryNew())
    have = [str(x.get_editor_property("parameter_name")) for x in mpc.get_editor_property("scalar_parameters")]
    if have != ["Time"]:
        # Rewriting the list gives the parameter a new id (stale materials),
        # so only when it is not already right.
        s = unreal.CollectionScalarParameter()
        s.set_editor_property("parameter_name", "Time")
        s.set_editor_property("default_value", 0.0)
        mpc.set_editor_property("scalar_parameters", [s])
        EAL.set_metadata_tag(mpc, "AcMadeBy", MADE_BY)
        EAL.save_loaded_asset(mpc)
    log("collection %s" % MPC)
    return mpc


# --- the shaders -------------------------------------------------------------------

# A four-key ramp (CAKeyframeAnimation, linear) at life fraction `a`: keys K,
# times Tm (Tm.x = 0; unused keys repeat the last one with time 1).
RAMP = """
#define AC_RAMP(K, Tm, a) ((a) <= (Tm).y ? lerp((K).x, (K).y, saturate(((a) - (Tm).x) / max((Tm).y - (Tm).x, 1e-4))) \\
    : (a) <= (Tm).z ? lerp((K).y, (K).z, saturate(((a) - (Tm).y) / max((Tm).z - (Tm).y, 1e-4))) \\
    : lerp((K).z, (K).w, saturate(((a) - (Tm).z) / max((Tm).w - (Tm).z, 1e-4))))
"""

WPO_HLSL = RAMP + """
// One particle on the engine plane (local XY, ±50 cm): fly it from its
// birth (the instance origin O) by the SceneKit emitter's physics, in closed
// form: dv/dt = A - k·v.
float age = T - Birth;
float a = age / max(Life, 1e-4);
if (a < 0.0 || a > 1.0 || Size <= 0.0) return O - W;
float3 Ac = Acc.xyz;
float3 p, v;
if (Damp > 1e-3)
{
    float e = exp(-Damp * age);
    float3 term = Vel - Ac / Damp;
    p = Ac / Damp * age + term * (1.0 - e) / Damp;
    v = Ac / Damp + term * e;
}
else
{
    p = Vel * age + 0.5 * Ac * age * age;
    v = Vel + Ac * age;
}
float3 C = O + p;
// SceneKit draws a particle 2 × particleSize wide (measured).
float s = 2.0 * Size * AC_RAMP(SizeK, SizeT, a);
// Face the camera (SceneKit's particles are screen-aligned billboards).
float3 F = Cam - C;
float lf = length(F);
F = lf > 1e-3 ? F / lf : float3(0, 0, 1);
float3 Up = abs(F.z) > 0.999 ? float3(1, 0, 0) : float3(0, 0, 1);
float3 R = normalize(cross(Up, F));
float3 U = cross(F, R);
float2 c = L.xy / 50.0;
float hx = 0.5 * s, hy = 0.5 * s;
float3 X, Y;
float3 vs = v - dot(v, F) * F;
float lv = length(vs);
if (Stretch > 0.0 && lv > 1e-3)
{
    // stretchFactor: drawn along its motion, reaching speed × stretch
    // ahead of and behind its centre (measured on SceneKit), in 3D so a
    // spark flying at the camera foreshortens.
    float3 Xd = vs / lv;
    Y = cross(F, Xd);
    X = Xd * (0.5 * s) + v * Stretch;
    hx = 1.0;
}
else
{
    float ang = Seed * 6.2831853;
    float cs = cos(ang), sn = sin(ang);
    X = R * cs + U * sn;
    Y = U * cs - R * sn;
}
return C + X * (c.x * hx) + Y * (c.y * hy) - W;
"""

COLOUR_HLSL = RAMP + """
// Colour × the kind's RGBA ramp over life; the sprite's alpha shapes it.
float a = saturate((T - Birth) / max(Life, 1e-4));
float4 k = float4(AC_RAMP(float4(C0.r, C1.r, C2.r, C3.r), Tm, a), AC_RAMP(float4(C0.g, C1.g, C2.g, C3.g), Tm, a),
                  AC_RAMP(float4(C0.b, C1.b, C2.b, C3.b), Tm, a), AC_RAMP(float4(C0.a, C1.a, C2.a, C3.a), Tm, a));
float3 rgb = float3(R, G, B) * k.rgb;
float alpha = saturate(Alpha * k.a * Tex.a);
Opacity = alpha;
return Add > 0.5 ? rgb * alpha * Boost : rgb;
"""


def data(g, index, default=0.0):
    return g.node(unreal.MaterialExpressionPerInstanceCustomData, data_index=index, const_default_value=default)


def vec4(g, name, value):
    return g.node(unreal.MaterialExpressionVectorParameter, parameter_name=name,
                  default_value=unreal.LinearColor(*value), group="Ac")


def build(path, additive, mpc):
    blend = unreal.BlendMode.BLEND_ADDITIVE if additive else unreal.BlendMode.BLEND_TRANSLUCENT
    mat, g = RM.new_master(path, unreal.MaterialShadingModel.MSM_UNLIT, blend)
    mat.set_editor_property("two_sided", True)
    time = g.mpc(mpc, "Time")
    birth, life = data(g, 0), data(g, 1, 1.0)
    vel = g.node(unreal.MaterialExpressionAppendVector)
    vxy = g.node(unreal.MaterialExpressionAppendVector)
    g.link(data(g, 2), "", vxy, "A")
    g.link(data(g, 3), "", vxy, "B")
    g.link(vxy, "", vel, "A")
    g.link(data(g, 4), "", vel, "B")

    wpo = RM.custom(g, "AcParticleFly", WPO_HLSL, [
        ("L", RM.local_position(g), ""),
        ("O", RM.instance_origin(g), ""),
        ("W", g.node(unreal.MaterialExpressionWorldPosition), ""),
        ("Cam", g.node(unreal.MaterialExpressionCameraPositionWS), ""),
        ("T", time, ""),
        ("Birth", birth, ""),
        ("Life", life, ""),
        ("Vel", vel, ""),
        ("Size", data(g, 5), ""),
        ("Seed", data(g, 10), ""),
        ("Damp", g.scalar("Damping", 0.0), ""),
        ("Acc", g.vector("Accel", (0, 0, 0)), ""),
        ("SizeK", vec4(g, "SizeKeys", (1, 1, 1, 1)), "RGBA"),
        ("SizeT", vec4(g, "SizeTimes", (0, 1, 1, 1)), "RGBA"),
        ("Stretch", g.scalar("Stretch", 0.0), ""),
    ])
    g.out(wpo, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)

    uv = g.node(unreal.MaterialExpressionTextureCoordinate)
    tex = g.texture("SpriteTex", T_SMOKE, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, uv)
    colour = RM.custom(g, "AcParticleColour", COLOUR_HLSL, [
        ("T", time, ""),
        ("Birth", birth, ""),
        ("Life", life, ""),
        ("R", data(g, 6, 1.0), ""),
        ("G", data(g, 7, 1.0), ""),
        ("B", data(g, 8, 1.0), ""),
        ("Alpha", data(g, 9, 1.0), ""),
        ("C0", vec4(g, "Col0", (1, 1, 1, 1)), "RGBA"),
        ("C1", vec4(g, "Col1", (1, 1, 1, 1)), "RGBA"),
        ("C2", vec4(g, "Col2", (1, 1, 1, 1)), "RGBA"),
        ("C3", vec4(g, "Col3", (1, 1, 1, 1)), "RGBA"),
        ("Tm", vec4(g, "ColTimes", (0, 1, 1, 1)), "RGBA"),
        ("Tex", tex, "RGBA"),
        # A scalar of our own, not MPC_AcTeams.EmissiveBoost: a material
        # saved by a commandlet binds a collection parameter by its id, and
        # make_materials.make_mpc gives the collection new ids on each run.
        ("Boost", g.scalar("Boost", 1.0), ""),
        ("Add", g.node(unreal.MaterialExpressionConstant, r=1.0 if additive else 0.0), ""),
    ], outputs=[("Opacity", F1)])
    if additive:
        # Soft where it meets the ground too (SceneKit cut it off hard).
        fade = g.node(unreal.MaterialExpressionDepthFade)
        g.link(g.node(unreal.MaterialExpressionConstant, r=1.0), "", fade, "Opacity")
        g.link(g.scalar("SoftCm", 20.0), "", fade, "FadeDistance")
        g.out(g.binop(unreal.MaterialExpressionMultiply, colour, fade), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    else:
        g.out(colour, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    if not additive:
        # Soft particles: smoke fades where it meets the ground or a hull.
        fade = g.node(unreal.MaterialExpressionDepthFade)
        g.link(colour, "Opacity", fade, "Opacity")
        g.link(g.scalar("SoftCm", 40.0), "", fade, "FadeDistance")
        g.out(fade, "", unreal.MaterialProperty.MP_OPACITY)
    for usage in (unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES,):
        MEL.set_base_material_usage(mat, usage)
    MEL.layout_material_expressions(mat)
    MEL.recompile_material(mat)
    EAL.set_metadata_tag(mat, "AcMadeBy", MADE_BY)
    EAL.save_loaded_asset(mat)
    log("master %s" % path)
    return mat


def main():
    MM.ensure_dir(DIR)
    make_quad()
    mpc = make_mpc()
    build(M_ADD, True, mpc)
    build(M_SMOKE, False, mpc)


if __name__ == "__main__":
    main()
