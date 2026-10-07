---
status: proposed
date:
angle: unit
video: video/renders/longbow-giant-x.mp4
posted: 
---

## Post

Ringshadow is an RTS in Unreal Engine 5 where you can take over any unit and play it like a shooter. This is the Longbow, a tank that anchors into artillery.

Roll down the ramp, anchor, shell an enemy base, walk it on its siege legs, pack up and fire on the move. Top-down, first and third person, day into night.

Then look up.

## Reply

Every shot is the real game, staged and recorded hidden by an agent skill at a fixed 30 fps, with the game's own sound. The skill: github.com/ValiDraganescu/autocraft/tree/main/.claude/skills/ringshadow-video

## Notes

The Longbow video re-cut in the gas giant shape, asked for by the developer on 2026-10-07 (the first one went out as `longbow`). The 46 s cut's body (0 to 36 s, its scene titles kept) joined to a fresh longbow-finale with the eject, the old finale and AUTOCRAFT end card dropped; 51.8 s, the X file 9.6 MB:

    video/projects/longbow-giant/source.mp4: video/renders/longbow.mp4 0-36 s + video/clips/longbow-finale.mp4 (ffmpeg concat)
    uv run .claude/skills/ringshadow-video/scripts/giantcut.py video/projects/longbow-giant/source.mp4 --down 38.6,42.6 --end 46.9 --title "The Longbow: drive the artillery." --music firefight_1 --music-at 20 --music-ends --out video/renders/longbow-giant.mp4 --x
