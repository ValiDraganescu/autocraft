---
status: approved
date: 2026-10-09
angle: mechanic
video: video/renders/leveling-x.mp4
posted: 
---

## Post

Ringshadow is an RTS where an AI commander runs your army, and you can take over any unit and play it like a shooter. Now the more you play a kind of unit yourself, the better every unit of that kind gets.

Each level puts two upgrade cards over the cockpit and you keep one. This one is the Prospector's: every Prospector pumps Metallic Hydrogen 20% faster. There are 216 of them across the twelve kinds.

The Prospector is the worker that mines and builds, the dullest job on Ashfall Reach. Now there's a reason to climb into the cab and pump for a while, and every worker on your side speeds up.

## Reply

How leveling works, with all 216 upgrades for the twelve kinds of unit: github.com/ValiDraganescu/autocraft/blob/main/docs/leveling.md

## Notes

Round 2 of the skill benchmark (skill), voted up.

Video: the prospector-level-up sim (15 s, night: -AcLevel=prospector:2 -AcPicks=1 -AcEarn=100@2 -AcPickKey=z@3, the cards up 2.7 s, Rig master kept), zoomed 2× on the cards, 19.4 s, the X file 9.8 MB:

    uv run .claude/skills/ringshadow-video/scripts/giantcut.py video/clips/prospector-level-up.mp4 --down 9.3,12.5 --end 14.9 --title "Play it. It levels up." --music night_shift_1 --music-at 20 --zoom 0.8,3.6,952,715,2 --caption "Level 3: keep one card. Every Prospector pumps 20% faster." --caption-at 4,8.5 --out video/renders/leveling.mp4 --x
