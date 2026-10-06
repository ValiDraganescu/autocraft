#!/bin/zsh
# Prompt, split and decal steps of the model pipeline, one unit at a time. A
# unit's files live in art/models/<unit>/ (look.txt is written by hand; the
# steps write the rest). The stencil (stencil.png, stencil.json,
# stencil.parts.png) comes from `model.sh stencil`; the paint goes through mcp__orchestrator__grok_image, not this script.
#
#   model.sh prompt <unit>                print the prompt for the image AI (Grok): the sheet rules
#                                         plus <unit>/look.txt. Saves it as <unit>/prompt.txt and its
#                                         version as prompt.version; with a <unit>/style.* image it asks
#                                         to match that image's look. Reference images for the call:
#                                         <unit>/stencil.png and the style image
#   model.sh stencil <unit>               step 2: twelve hidden shots of <unit>_blue from the model row
#                                         (six views, clay and part colours, 512 px each) and stencil.py
#                                         composes <unit>/stencil.png, stencil.json, stencil.parts.png
#   model.sh split <unit> [painting]      split the painted sheet (default <unit>/painted.*) into
#                                         views fitted to the stencil (<unit>/views/), compare them
#                                         with it (<unit>/compare.png) and with each other, and build
#                                         the texture sheets (skin.jpg, skin_glow.jpg); the first split
#                                         of a painting freezes its pose (paint-pose.json) when the
#                                         stencil writer made a stencil.skin.json
#   model.sh decal <unit>                 unwarp the flat designed faces listed in <unit>/decals.json
#                                         into their own textures (<unit>/decals/); split runs it too
#
# Nets tools (tint.py, legend.py, recolour.py, relayout.py, netscheck.py, install_nets.py) are
# run by hand with uv; see NETS.md.
#
# <unit> is a folder name under art/models/ (ranger, prospector, kestrel, ...).
set -e
self="${0:A}"
here="${self:h}"
root="${here:h:h:h}"

usage() { sed -n '2,/^[^#]/p' "$self" | grep '^#' | sed 's/^# \{0,1\}//'; exit 1; }
[[ $# -ge 2 ]] || usage
cmd="$1"; unit="$2"; shift 2
dir="$root/art/models/$unit"

case "$cmd" in
  prompt)
    [[ -f "$dir/look.txt" ]] || { echo "write $dir/look.txt first: how the unit should look" >&2; exit 1; }
    # "#" lines in sheet-rules.txt are its version header, not prompt text.
    # The prompt is saved next to the painting so PROMPTS.md can say which
    # version made it.
    # A style reference (<unit>/style.*, a second reference image) fills
    # in {STYLE}; without one the line goes.
    style=( "$dir"/style.(jpg|jpeg|png|webp)(N) )
    styletext=""
    # The images are named by their file names, as attached.
    [[ ${#style} -gt 0 ]] && styletext="The attached image ${style[1]:t} is the style reference: match its rendering, level of detail, materials, wear and colours as closely as you can. Take every shape, pose and proportion from stencil.png only."
    { grep -v '^#' "$here/sheet-rules.txt" | sed 's/{STENCIL}/stencil.png/g' | STYLE="$styletext" awk '$0 == "{STYLE}" { if (ENVIRON["STYLE"] != "") print ENVIRON["STYLE"] "\n"; else print ""; next } { print }'
      echo; cat "$dir/look.txt"; } > "$dir/prompt.txt"
    head -1 "$here/sheet-rules.txt" > "$dir/prompt.version"
    cat "$dir/prompt.txt"
    echo "(saved as $dir/prompt.txt, $(cat "$dir/prompt.version" | sed 's/^# //'))" >&2
    echo "reference_images: $dir/stencil.png${style:+ and ${style[1]} (style reference)}" >&2
    ;;
  stencil)
    # Hidden runs only (SKILL.md): the Dock-less editor copy, -nosound, no -log. Shots go to a scratch folder.
    BG="/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditorBG.app/Contents/MacOS/UnrealEditor"
    shots="${TMPDIR:-/tmp}/ac-stencil-$unit"; mkdir -p "$shots"
    for v in front right back left top 3q; do for p in clay parts; do
      "$BG" "$root/unreal/Autocraft.uproject" /Game/Maps/ModelRow -game -RenderOffscreen -ResX=512 -ResY=512 \
        -unattended -nosplash -nosound -AcNoSave -AcNoAI -AcPaused -AcModelFocus=${unit}_blue -AcModelView=$v -AcModelPaint=$p \
        -AcShot="$shots/$v-$p.png" -abslog="$shots/run.log" "-ini:Input:[/Script/Engine.InputSettings]:bCaptureMouseOnLaunch=False" >/dev/null 2>&1 \
        || { echo "shot $v $p failed, see $shots/run.log" >&2; exit 1; }
    done; done
    uv run "$here/stencil.py" "$shots" "$dir" "$unit"
    ;;
  split)
    uv run "$here/split.py" "$dir" "$@"
    [[ -f "$dir/prompt.version" ]] && echo "prompt: $(sed 's/^# //' "$dir/prompt.version") — log this painting in PROMPTS.md"
    # The painting was made on this stencil: freeze the pose it shows, so
    # later stencils plan the paint for where each part was when painted.
    if [[ ! -f "$dir/paint-pose.json" && -f "$dir/stencil.skin.json" ]]; then
      cp "$dir/stencil.skin.json" "$dir/paint-pose.json"
      echo "froze the painted pose: $dir/paint-pose.json"
    fi
    [[ -f "$dir/decals.json" ]] && uv run "$here/decal.py" "$dir"
    ;;
  decal)
    [[ -f "$dir/decals.json" ]] || { echo "no $dir/decals.json: nothing to unwarp" >&2; exit 1; }
    [[ -f "$dir/paint-pose.json" ]] || { echo "missing $dir/paint-pose.json: run split first" >&2; exit 1; }
    uv run "$here/decal.py" "$dir"
    ;;
  *) usage ;;
esac
