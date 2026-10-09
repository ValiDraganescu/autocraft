---
status: approved
date: 2026-10-12
angle: world
video: video/renders/kstr-sponsors-x.mp4
posted: 
---

## Post

The music in Ringshadow plays as a radio station: KSTR 88.7 Stardust, original songs with in-world ads between them.

Tonight's sponsors:
Quicksilver Cola, pumped from Metallic Hydrogen wells. "Not a beverage."
Opal Glow face scrub. Side effects include levitation and being mined.
Citadel Timeshares. Ash view guaranteed.

Sound on 🔊

## Reply

The soundtrack, the station and the prompts behind the songs: github.com/ValiDraganescu/ringshadow/blob/main/docs/music.md

## Notes

Round 1 of the skill benchmark (baseline), voted up. The developer's note: -

Video: its own scene, asked for by the developer: the kstr-scorpion-ridge sim (19 s, dawn, third person: -AcPilotAt=ridge puts a Scorpion out on the base's level 20 cells from the Citadel, in line with the gas giant; the maps have no ground above a base, so it is the base's own level; -AcPilotMiners=6, -AcMusicAd=citadel_timeshares), 22.8 s, the X file 9.6 MB:

    uv run .claude/skills/ringshadow-video/scripts/giantcut.py video/clips/kstr-scorpion-ridge.mp4 --start 1.0 --down 13.1,17.0 --end 18.9 --title "Sound on. Ads included." --music prospecting_1 --music-at 20 --music-ends --caption "On the radio: Citadel Timeshares. Ash view guaranteed." --caption-at 0.8,8 --out video/renders/kstr-sponsors.mp4 --x
