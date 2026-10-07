# Hiccups: image generation and art

What Grok did on each unit's concept and painting rounds, as `mcp__orchestrator__grok_image` calls or, before 2026-10-05, as chats on grok.com. Add a line per fact as it happens: date, unit, what happened, what to do instead. Facts about the web UI are gone (the tool replaced it); facts about what Grok draws stay.

## Concept rounds (step 0)

Prompts: `art/models/<unit>/concepts/prompts.md` (shared text, `{DIRECTION}` slot) and one `prompt.<slug>.txt` per direction. `prompts.md` vanished from disk mid-round once (cause not found) and was rebuilt from the per-direction files, so keep both.

What Grok does with a direction:
- **It softens unusual directions toward a generic walker.** "Reverse-jointed, bird-like legs" came back with ordinary knees (Prospector).
- **It puts parts on whichever side it likes.** The drill came out on the unit's left in 3 of 6; the model mirrors it anyway.
- **Palette barely varies.** Six Juggernaut directions all came back blue plates on gunmetal with orange eyes; six Firefly directions deep blue on gunmetal with amber canopies; every Kestrel kept the Ranger's silver and blue. Name a palette in the direction when it matters.
- **Back details appear only where the direction names them** (Juggernaut exhaust stacks and packs).
- **A named count is approximate.** "A drum of six rocket tubes" came back as six or seven; "two parallel rails" read as two barrels packed, so name a single bore when one gun matters (Longbow).
- **Body bulk follows the reference, not the adjective.** All six Comet directions came back as heavy as a Ranger though the prompt said "fast, lightly armoured". Describe the body itself, part by part ("long thin legs, a narrow waist … about half the bulk of the space Ranger in painted.jpg") and every figure came back slim. The same works for bulk up: "about twice the bulk of the space Ranger in painted.jpg … no hands: each forearm is a … launcher" came through in all six Juggernaut answers.
- **A figure comes back portrait when it stands** (Comet, 784×1168 in the old UI; a tall `radar` building too).
- **Ask for two poses in one image** to get an animation reference with the model: the Comet at rest and mid-leap, the Longbow in travel mode left and anchored mode right, the Hailstorm at rest and firing at the sky. All of them did it.

What a second reference image does:
- **Attaching the chosen concept makes Grok copy its pose and framing** almost exactly, whatever the direction asks: "very long arms that reach the ground" and "legs set far apart like a crab" moved the Prospector's silhouette very little, and all four Longbow round-2 answers copied c1's walking legs. Material changes do come through (brass to gunmetal, a glass dome). To explore shapes, leave the concept out and describe it in words.
- **That copying helps for a single edit.** Attach only the chosen concept and ask "Repaint the Prospector in … with one change to its front viewport", the new part described in positive words (the pilot sealed inside, out of sight, behind dark smoked glass): both answers kept pose, colours and details and changed one thing, and neither showed a face.
- **To remove a copied feature**, attach the last round's image and say "keep the left tank exactly as it is; repaint only the right tank with one change: it has no legs", then describe the wanted pose in positive words (pods slid straight out sideways on chrome rams, tracks flat on the ground, an anchor spade at each pod). Both answers did it (Longbow round 3).

Buildings (Sentinel, 2026-10-04): a building's reference is its neighbours, not a unit's turnaround. `base.jpg` was three renders side by side (Bastion, Hab Dome, Garrison) and the prompt named them by job ("the same small footprint as the Hab Dome in base.jpg"). All six came back with the base's look: silver-grey panels, blue trim, hazard-striped plinth, yellow lamp bands, a red beacon. "The two missile pods the first thing you see" made every answer put the launchers big and up top. For a building the concept is what pays; paint and nets mostly do not, because every building shares the panelled hull materials and its lamps must stay separate materials (see NETS.md).

Process:
- Agents launched from one session share its scratchpad directory: a `prompt.json` written there by the Hailstorm's agent was read back as the Kestrel's a minute later. Give scratch files the unit's name (a `scratchpad/<unit>/` folder).
- Name saved images with `name` = `<unit>-c<round>-<slug>`: the old web downloads got random names (`UxZKv.jpg`, `2gape.jpg`) and one old v1 file was once sent back as a v3 result. `cmp` a file against the kept versions before logging it as new.

## Painting rounds (step 3)

Results per painting are in the table in [PROMPTS.md](PROMPTS.md); the lessons:
- **Two prompts at once cost little.** The Hailstorm's same prompt in two chats gave one big 3/4 illustration with no panels and one true six-panel sheet.
- **Wide, low vehicles come back as six 3/4 views** (Longbow, first three answers, with and without `style.jpg`). A paragraph ending `look.txt` that names what each panel shows fixed it.
- **`style.jpg` = a crop of the concept** (the vehicle at rest) works as the style reference, as the Firefly's and Hailstorm's did.
- **A painting can redesign a shape** (the Ranger's boxy front pauldrons; a Kestrel answer that drew a new jet across the panel borders over the stencil). Fitted IoU below 0.8 is the signal; re-roll or ask the user.
- **Baked light** (highlights, reflections, shadows, ground shadow under wheels) gets lit twice in the game: note it in the PROMPTS.md row.
- **An answer can arrive as text only, or empty.** Call again with a sharper prompt.
- **Image size differs between calls and references**: the old UI gave 1168×784 for a bare prompt and 1712×1152 with a second image attached; `grok_image` gives 1024×1024 for 1:1 and 1280×720 for 16:9. A 3:2 sheet's pixel size from the tool is not yet measured: note it here after the first sheet (`split.py` takes any size and fits it).
- **"A kept painting without style.jpg"**: the Kestrel's best of five answers had no style image; the ones with it redesigned the shape after the 3/4 concept.

## UE-era facts

(Add the first ones here: grok_image on the stencil, sheet sizes, how a 3:2 edit keeps its ratio.)

- 2026-10-05, concept round 1 (Peregrine, Atlas, Scorpion): `grok_image` with one reference and `aspect_ratio` 3:2 returns 1248×832 JPEG, 12 to 24 s each. Four calls sent at once all succeeded; one logged a 429 "at capacity" that the tool retried itself.
- 2026-10-05, Peregrine: "forward-swept wings" came back swept back in all four directions. Grok softens an unusual wing the way it softened the Prospector's reverse knees. To get it, describe the shape itself (wing tips ahead of the wing roots, the leading edge slanting forward toward the nose) or drop it.
- 2026-10-05, Peregrine dart: "one big engine bell" came back as two engines in one of four.
- 2026-10-05, Atlas: "reverse-jointed legs, knees bending backward like a bird's" came back with ordinary forward knees in all four (the Prospector's lesson again). The two-pose request (rest and stomp) worked in all four. Titan added arms with hands that no prompt asked for.
- 2026-10-05, Scorpion: "half buried in a mound of loose soil, only its back plates showing" came back as the whole robot standing on a soil heap in all four; the buried look has to be modelled (sink the body), not read from the concept.
- 2026-10-05, painting round (Atlas, Scorpion, Peregrine): `grok_image` with stencil.png + style.jpg at 3:2 returns 1248×832; all six first answers were six-panel sheets. The background came back light grey (223–226) or with a soft drop shadow, not white: split.py now measures each panel's background from its edge and drops colourless near-background grey.
- 2026-10-05, Atlas: editing the last painting with "repaint only TOP" changed nothing (both answers copied it). A fresh paint with a sentence on TOP's outline worked.
- 2026-10-05, Peregrine: a thin, wide flyer's FRONT and BACK came back about 40% larger than TOP in three rounds, and first from above. Naming the wingspan against TOP did not hold it.
