"""The RTS camera's input: actions and IMC_Rts (chunk A3, GAME-LAYER.md §3.5).

Outputs, under /Game/Input:
  IA_Pan       Axis2D  W A S D and the arrows; x right, y down the view
  IA_DragPan   Bool    left mouse button (AAcRtsPawn turns it into a drag after 4 points)
  IA_Zoom      Axis1D  mouse wheel (the trackpad's scroll and pinch go through
                       AAcRtsPawn's Slate pre-processor instead)
  IA_ZoomStep  Axis1D  + (=, numpad +) is +1, - (numpad -) is -1
  IA_Fit       Bool    0, numpad 0
  IA_Command   Bool    either Command key (the chord for IA_GoToBase)
  IA_GoToBase  Axis1D  Command-1 to Command-8: the value is the player + 1
  IMC_Rts      the mappings above

Rerunnable: makes what is missing, then rebuilds the mappings of THESE
actions only, so other chunks can add their own actions to IMC_Rts (card
hotkeys, the command map, F to drive) with their own scripts.

Run it in the editor (`py <path>` in the console), or headless when the
live editor does not have these assets open:
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
    unreal.log("make_input: made %s" % path)
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


pan = action("IA_Pan", Value.AXIS2D, "Pan the RTS camera (x right, y down the view)")
drag = action("IA_DragPan", Value.BOOLEAN, "Drag the ground (left button, after 4 points)")
zoom = action("IA_Zoom", Value.AXIS1D, "Mouse wheel zoom about the cursor")
step = action("IA_ZoomStep", Value.AXIS1D, "Zoom by 1.25 about the centre (+1 in, -1 out)")
fit = action("IA_Fit", Value.BOOLEAN, "Show the whole map")
command = action("IA_Command", Value.BOOLEAN, "The Command key held (chord)")
base = action("IA_GoToBase", Value.AXIS1D, "Centre on a player's start base (value = player + 1)")
mine = [pan, drag, zoom, step, fit, command, base]

imc = asset("IMC_Rts", unreal.InputMappingContext, unreal.InputMappingContext_Factory())
imc.set_editor_property("context_description", "Top-down: camera, clicks, the command card (AAcRtsPawn and later chunks)")
for a in mine:
    imc.unmap_all_keys_from_action(a)
# Built here, set on the context in one go at the end (`map_key` hands back
# a copy, so modifiers set on it are lost).
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

# Pan: the raw key value is (1, 0, 0); y is down the view, as in Swift's `moveDirection`.
for k in ("D", "Right"):
    bind(pan, k)
for k in ("A", "Left"):
    bind(pan, k, [NEG])
for k in ("S", "Down"):
    bind(pan, k, [SWZ])
for k in ("W", "Up"):
    bind(pan, k, [SWZ, NEG])

bind(drag, "LeftMouseButton")
bind(zoom, "MouseWheelAxis")
for k in ("Equals", "Add"):
    bind(step, k, triggers=[PRESSED])
for k in ("Hyphen", "Subtract"):
    bind(step, k, [NEG], [PRESSED])
for k in ("Zero", "NumPadZero"):
    bind(fit, k, triggers=[PRESSED])
for k in ("LeftCommand", "RightCommand"):
    bind(command, k)
digits = ["One", "Two", "Three", "Four", "Five", "Six", "Seven", "Eight"]
for i, k in enumerate(digits):
    s = float(i + 1)
    bind(base, k, [(unreal.InputModifierScalar, {"scalar": unreal.Vector(s, s, s)})],
         [PRESSED, (unreal.InputTriggerChordAction, {"chord_action": command})])

dkm = imc.get_editor_property("default_key_mappings")
dkm.set_editor_property("mappings", list(dkm.get_editor_property("mappings")) + built)
imc.set_editor_property("default_key_mappings", dkm)
eal.save_loaded_asset(imc, only_if_is_dirty=False)
count = len(imc.get_editor_property("default_key_mappings").get_editor_property("mappings"))
unreal.log("make_input: IMC_Rts has %d mappings" % count)
