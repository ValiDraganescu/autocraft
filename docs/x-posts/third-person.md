---
status: approved
date: 2026-10-09
angle: unit
video: video/renders/third-person-x.mp4
posted: 
---

## Post

Ringshadow is an RTS in Unreal Engine 5 where an AI commander runs your army, and you can take over any unit and play it like a shooter. Press V and the view switches between over the shoulder and the cockpit, on all twelve kinds of unit.

Here a Ranger with both of its upgrades, the Mini gun and the Aegis Shield, guns down an enemy Ranger over the shoulder. Then V, the cockpit, and a Kestrel shot out of the sky. Rangers hit ground and air.

The game staged and recorded this clip itself, hidden, from a few command-line flags.

## Reply

The video skill that staged and cut this clip: github.com/ValiDraganescu/ringshadow/tree/main/.claude/skills/ringshadow-video

## Notes

Round 1 of the skill benchmark (skill), voted up. The developer's note: with accompanying video would make sense, transition from TPS to FPS, use the Ranger with shield and minigun upgrades, show the ranger shooting at ground and air targets

Video: the ranger-third-to-first sim (13 s: -AcPilotThird, -AcPilotVariant=minigun+shield+flyer, -AcPilotFoeHp=20, the pull-out at 6.8 s), cut in the gas giant shape with title card D and the eject (17.9 s), the X file 9.3 MB:

    uv run .claude/skills/ringshadow-video/scripts/giantcut.py video/clips/ranger-third-to-first.mp4 --down 5.85,10.0 --end 12.9 --title "Press V to switch views." --music firefight_1 --music-at 18 --out video/renders/third-person.mp4 --x
