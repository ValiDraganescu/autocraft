"""Driving a unit: the pilot's actions and IMC_Pilot (chunk E2, GAME-LAYER.md §3.5).

Outputs, under /Game/Input (read by AAcPilotPawn, AcPilotPawn.h):
  IA_PilotMove     Axis2D  W A S D and the arrows; x right, y back (as IA_Pan)
  IA_PilotLook     Axis2D  the mouse (Mouse XY); AAcPilotPawn turns y up into Swift's y down
  IA_PilotAct      Bool    left button, E, Space (press and hold)
  IA_PilotAbility  Bool    R
  IA_PilotView     Bool    V
  IA_PilotNext     Bool    Tab
  IA_PilotLeave    Bool    Esc, F
  IA_PilotKey      Axis1D  B = -1, Z = -2, X = -3, 1-9 = 1-9 (pressed): build menu and picks (E8, E9)
  IA_PilotFree     Bool    either Option key (the pointer out for the HUD)
  IMC_Pilot        the mappings above (added while driving, IMC_Rts removed)

Rerunnable: makes what is missing, then rebuilds the mappings of THESE
actions only. Run it in the editor (`py <path>` in the console), or headless
when the live editor does not have these assets open:
  UnrealEditor-Cmd Autocraft.uproject -run=pythonscript -script=<abs path>
"""
import unreal

DIR = "/Game/Input"
eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
Value = unreal.InputActionValueType


def asset(name, cls, factory):
    path = "%s/%s" % (DIR, name)
    if eal.does_asset_exist(path):
        return eal.load_asset(path)
    unreal.log("make_pilot_input: made %s" % path)
    return tools.create_asset(name, DIR, cls, factory)


def action(name, kind, description):
    a = asset(name, unreal.InputAction, unreal.InputAction_Factory())
    a.set_editor_property("value_type", kind)
    a.set_editor_property("action_description", description)
    eal.save_loaded_asset(a, only_if_is_dirty=False)
    return a


def key(name):
    k = unreal.Key()
    k.import_text(name)
    return k


move = action("IA_PilotMove", Value.AXIS2D, "Driving: walk, or drive and turn the hull (x right, y back)")
look = action("IA_PilotLook", Value.AXIS2D, "Driving: mouse look")
act = action("IA_PilotAct", Value.BOOLEAN, "Driving: act (press and hold)")
ability = action("IA_PilotAbility", Value.BOOLEAN, "Driving: the kind's ability (held: a Prospector mends)")
view = action("IA_PilotView", Value.BOOLEAN, "Driving: first or third person")
nxt = action("IA_PilotNext", Value.BOOLEAN, "Driving: the nearest other unit of the same kind")
leave = action("IA_PilotLeave", Value.BOOLEAN, "Driving: back to the top-down camera (after closing a menu)")
keys = action("IA_PilotKey", Value.AXIS1D, "Driving: B (-1), Z (-2), X (-3), digits (1-9)")
free = action("IA_PilotFree", Value.BOOLEAN, "Driving: Option held frees the pointer for the HUD")
mine = [move, look, act, ability, view, nxt, leave, keys, free]

imc = asset("IMC_Pilot", unreal.InputMappingContext, unreal.InputMappingContext_Factory())
imc.set_editor_property("context_description", "Driving a unit (AAcPilotPawn, chunks E2-E9)")
for a in mine:
    imc.unmap_all_keys_from_action(a)
built = []


def new(cls, **props):
    o = unreal.new_object(cls, outer=imc)
    for k, v in props.items():
        o.set_editor_property(k, v)
    return o


def bind(a, key_name, modifiers=(), triggers=()):
    m = unreal.EnhancedActionKeyMapping()
    m.set_editor_property("action", a)
    m.set_editor_property("key", key(key_name))
    m.set_editor_property("modifiers", [new(c, **p) for c, p in modifiers])
    m.set_editor_property("triggers", [new(c, **p) for c, p in triggers])
    built.append(m)


NEG = (unreal.InputModifierNegate, {})
SWZ = (unreal.InputModifierSwizzleAxis, {"order": unreal.InputAxisSwizzle.YXZ})
PRESSED = (unreal.InputTriggerPressed, {})


def scalar(s):
    return (unreal.InputModifierScalar, {"scalar": unreal.Vector(s, s, s)})


for k in ("D", "Right"):
    bind(move, k)
for k in ("A", "Left"):
    bind(move, k, [NEG])
for k in ("S", "Down"):
    bind(move, k, [SWZ])
for k in ("W", "Up"):
    bind(move, k, [SWZ, NEG])
bind(look, "Mouse2D")
for k in ("LeftMouseButton", "E", "SpaceBar"):
    bind(act, k)
bind(ability, "R")
bind(view, "V", triggers=[PRESSED])
bind(nxt, "Tab", triggers=[PRESSED])
for k in ("Escape", "F"):
    bind(leave, k, triggers=[PRESSED])
for k, s in (("B", -1.0), ("Z", -2.0), ("X", -3.0)):
    bind(keys, k, [scalar(s)], [PRESSED])
for i, k in enumerate(["One", "Two", "Three", "Four", "Five", "Six", "Seven", "Eight", "Nine"]):
    bind(keys, k, [scalar(float(i + 1))], [PRESSED])
for k in ("LeftAlt", "RightAlt"):
    bind(free, k)

dkm = imc.get_editor_property("default_key_mappings")
dkm.set_editor_property("mappings", list(dkm.get_editor_property("mappings")) + built)
imc.set_editor_property("default_key_mappings", dkm)
eal.save_loaded_asset(imc, only_if_is_dirty=False)
count = len(imc.get_editor_property("default_key_mappings").get_editor_property("mappings"))
unreal.log("make_pilot_input: IMC_Pilot has %d mappings" % count)
