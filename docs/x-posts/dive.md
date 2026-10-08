---
status: posted
date: 2026-10-08
angle: play
video: video/renders/dive-giant-x.mp4
posted: https://x.com/escu__valentin/status/2108163349594701937
---

## Post

Ringshadow is an RTS where an AI commander runs your army, and you can take over any of your units and play it like a shooter.

In the clip I click a Ranger on the map. The camera dives from the top-down view to the Ranger's own eyes in 1.2 seconds, and it opens fire on the enemy Ranger in front of it.

From there it plays like a shooter: WASD walks, the mouse aims, a click fires. Esc hands the Ranger back to the AI commander.

It works the same on all twelve kinds of unit, each with its own cockpit and weapon.

## Reply

The whole game is public domain (CC0): code, art, models, sounds, music, and the Claude Code skills the agents build it from.
github.com/ValiDraganescu/autocraft

## Notes

Round 2 of the skill benchmark (skill), voted up.

Video: the ranger-dive-full sim (10 s, with the game's sound): the RTS view, the dive, the duel (aimed at the enemy's chest), a walk off, Esc back to the RTS view. 10.1 MB, so re-encode under 10 MB for the browser upload.

Video, test of the gas giant signature (2026-10-07): video/renders/dive-giant.mp4, 18 s, 15.7 MB (dive-giant-x.mp4: 9.4 MB, for the upload). Opens on the gas giant over the Citadel with the title, comes down into the scene (the clip's own pull-out, reversed), the RTS view, the dive, the duel, then the pull-out back to the gas giant under the end card. Cut by video/projects/dive-giant/cut.py from the ranger-dive-giant sim.

Re-cut 2026-10-07 with title card D and the eject (17.1 s, the X file 8.9 MB):

    uv run .claude/skills/ringshadow-video/scripts/giantcut.py video/clips/ranger-dive-giant.mp4 --down 5.1,9.5 --end 11.95 --title "Take over any unit." --music ashfall_reach_1 --music-at 18 --out video/renders/dive-giant.mp4 --x
