# Music

The game's music comes on as a radio station, **KSTR 88.7 Stardust**
(KSTR: US call signs west of the Mississippi start with K, and the rest
reads "K-STaR"). It plays a shuffled playlist: Autocraft's own tracks,
shipped in `unreal/Resources/Sounds/music/`, and any audio
file in the Audio folder's `music/`
(`~/Library/Application Support/Autocraft/Audio/music/`). After a song
that plays out, the station runs one of its ads. The music player on the
dashboard shows what is on, skips and pauses it, and takes a vote on it,
ads included. Window games record how long each track is listened to and
the votes, so the next tracks (and ads) can be written from what worked.

## The tracks

A track's id is its file name without the extension (`long_haul.mp3` is
`long_haul`); the tracking database keys on it, so renaming a file
starts its history over. The player's title is the file's own title tag,
else the id with its underscores as spaces, in title case. Its cover is
the file's embedded artwork, else an image beside it with the same name
(`long_haul.jpg` or `.png`), else one drawn from the title.

To try a track, drop it in the Audio folder's `music/`: it plays from
the next launch. To ship it, put it in `Sounds/music/` and build.

The own tracks are Suno batch 2 (below), from 2026-10-02: seven songs,
two takes of each, as `<song>_1.mp3` and `<song>_2.mp3` (`long_haul_2`),
their title tags marked with the take ("Long Haul (2)") so the player
tells them apart; the votes and skips pick the better take. They keep
Suno's covers. Silver Well Blues isn't made yet.

Before them, six stand-in tracks made with ElevenLabs Music played
(Ashfall Reach, Prospecting, Night Shift, Rising Pressure, Firefight and
Aftermath, an orchestral-electronic hybrid). They are retired to
`~/Library/Application Support/Autocraft/Audio/retired-elevenlabs/`,
outside the folders the game reads, and their prompts stay in
`Tools/AudioGen/music.manifest.json`.

## The station's ads

After a song plays out (never after a skip) comes one of the station's
ads, then the next song: a 1.5 s pause before the ad and 2 s after it,
where songs alone have 6 s between them. The ads take turns in a
shuffled order, each once before any comes again. On the player an ad
shows the station's card and "AD ·" with its sponsor in amber. Next (⏭)
skips it to the next song; previous (⏮) goes back to the start of the
song before it; the thumbs vote on the ad. Its plays and votes go in the
same tables as the songs', as source `ad`; `make music` lists them after
the songs.

The thirteen spots, in `unreal/Resources/Sounds/ads/`
(titles in `titles.json` there):

| Id | Title | The joke |
| --- | --- | --- |
| `quicksilver_cola` | Quicksilver Cola | a soda pumped from Metallic Hydrogen wells; "not a beverage" |
| `opal_glow` | Opal Glow | Stardust Ore face scrub; side effects: levitation, being mined |
| `hab_dome_deluxe` | Hab Dome Deluxe | now with walls and a roof; air sold separately |
| `dropride` | DropRide | medics heal you on the way, hurt or not |
| `citadel_timeshares` | Citadel Timeshares | ash view guaranteed; may be demolished during your stay |
| `comet_jump_academy` | Comet Jump Academy | most graduates land |
| `ash_away` | Ash-Away | removes ash, contains ash |
| `longbow_life` | Longbow Life | covers everything but artillery, which all Longbows are |
| `night_shift_singles` | Night Shift Singles | matches within three cells; drills holstered |
| `oracle_premium` | Oracle Premium | forty voices and smooth jazz while the base burns |
| `ministry_of_regrowth` | Ministry of Regrowth | a public notice: fields grow back, stop fighting (a blast in the pause) |
| `firefly_drivers_ed` | Firefly Driver's Ed | the flamethrower is not the horn |
| `kstr_ident` | KSTR 88.7 Stardust | the only station on Ashfall Reach, the others got shelled |

The ads say "Metallic Hydrogen" in full, never "MH", so players learn
what the HUD's MH stands for. Each ends on its fine print, read fast.

The voice is "Autocraft KSTR Announcer" (ElevenLabs Voice Design,
`eleven_ttv_v3`): a bright, glossy radio-ad announcer, a woman in her
early thirties, smiling and fast; the brightest of three previews
(spectral centroid 2.3 kHz, pitch 276 Hz). `Tools/AudioGen/kstr.manifest.json`
holds the lines with their delivery directions; `elevenlabs.sh manifest
Tools/AudioGen/kstr.manifest.json unreal/Resources/Sounds/ads`
makes any that are missing into `Tools/AudioGen/raw/`, then puts each
through `radio.py`: trimmed, the broadcast band (110 Hz - 7.5 kHz) with
a presence lift, a station compressor, the songs' level (-16.5 dBFS over
the louder half), peaks under -1.7 dBFS; `--boom` lays a distant
explosion into the Ministry's pause. The player then plays the ads 3 dB
over the songs (`MusicPlayer.adGain`), so they stand out a little. A new ad is a manifest entry, a
line in `titles.json`, and a run of that command. The spots were checked
by transcribing them (Whisper, locally): each says its script and no
direction aloud.

## The player

Status, 2026-10-02: built (`MusicPlayer.swift`, `MusicDeck.swift`,
`GameCore/MusicQueue.swift`, `GameCore/MusicStore.swift`). Still to see
in the running game: that macOS hands the media keys to the game, which
runs as a plain binary rather than a `.app`.

The music player sits in the left instrument block of the dashboard, in
every view, "KSTR 88.7 STARDUST" lit on its faceplate: the cover, the
title, how far along the track is, and five buttons. In the top-down view click them; in every view, the keys work:

| Key | Does |
| --- | --- |
| ⏮ (F7) | previous track (back to the start of this one after its first 3 s) |
| ⏯ (F8) | pause / play |
| ⏭ (F9) | next track |
| `]` | thumbs up (again: take the vote back) |
| `[` | thumbs down (again: take the vote back) |

The media keys work as they do for the Music app, without fn (the Mac's
default): the game registers as the Now Playing app (`MPRemoteCommandCenter`)
and tells macOS the track, its cover and how far along it is
(`MPNowPlayingInfoCenter`), so Control Center shows it and headphone
buttons work too. macOS gives the keys to whichever app started playing
last. With fn held (or with "Use F1, F2, etc. keys as standard function
keys" on) F7, F8 and F9 reach the game as plain keys and do the same.
Window mode only: the wallpaper never takes the media keys.

If macOS doesn't hand the media keys to the game, F7–F9 with fn and the
player's buttons still work. The driving tips list the keys too.

Renders (`windowshot`) have no sound, so they show the first own track
1:23 in and voted up; `AUTOCRAFT_MUSIC=paused`, `down`, `ad` or `off`
shows it paused, voted down, the station's first ad, or no music.

## Tracking

Window games write to the tracking database
(`~/Library/Application Support/Autocraft/tracking.sqlite`), beside the
leveling and the performance tables, through the same `Database`
(`MusicStore`, versioned as `version.music` in `meta`). The wallpaper,
the headless renders and the tests never write.

- `music_plays`: one row per time a track starts. It holds the track,
  its title and source (`own` or `folder`), its length, when it started,
  the seconds it played (not paused) and the seconds it was heard
  (playing, the game not muted and not ducked under another app), and
  how it ended: `end` (played out), `next` or `previous` (skipped),
  `restart` (the sound output changed) or `quit`. The row is written when
  the track starts and again when it pauses and when it ends.
- `music_votes`: each vote as it is cast (`1` up, `-1` down, `0` taken
  back). A track's vote is its latest.
- `music_kpis` (a view): per track, plays, hours heard, the average share
  of the track heard per play, the skip rate (skipped before 90%), and
  the vote.

`make music` prints `music_kpis`, best first.

## Writing new tracks

Read `make music`: tracks heard through and voted up show what to make
more of; tracks skipped early or voted down, what to drop. Write the
next prompts from those, add them below with the date, and ship the new
tracks under the names the prompts give them.

Suno takes no artist names in its style prompts, so a prompt describes
the sound instead. Each prompt goes in Custom mode with Instrumental on;
the exclude line goes in "Exclude styles". A prompt is about 500
characters: Suno v4.5 or newer takes up to 1,000.

### Suno, batch 1 (2026-10-02): rejected

Symphonic rock opera between the 1978 War of the Worlds album and heavy
metal ballads (clean arpeggios building to distorted power chords, twin
harmony leads). The user found it "too much metal ballad". Not made.

### Suno, batch 2 (2026-10-02)

Made the same day, two takes each, except Silver Well Blues. The takes
sit within 3.4 dB of each other in loudness (about -14.5 to -18 dBFS
over their louder half), so they play at one level.

The War of the Worlds album's ballad side (lush 70s string orchestra,
warm Moog synths, a mellow phaser guitar, a melodic fingered bass, a
distant two-note alien wail) mixed with dark country, dark blues and a
feeling of travel between the stars, in a different mix per track.
Instrumental, no lyrics.

Exclude styles, for all eight:

```
vocals, lyrics, choir, heavy metal, distorted power chords, double kick, EDM, dubstep, trap, pop
```

**1. Ashfall Reach** (main theme: War of the Worlds ballad, a touch of space travel)

```
Instrumental 1970s orchestral rock ballad meets cosmic space western, D minor, 84 BPM. Lush sweeping string orchestra, warm Moog synth lead and vintage string machine, mellow phaser electric guitar, gentle strummed acoustic guitar, melodic fingered bass. A wistful, memorable melody grows from a lone guitar into a vast orchestral swell, shimmering synth arpeggios like passing stars. Nostalgic, wide, hopeful wonder, warm analog tape production.
```

**2. Prospecting** (dark country with a space backdrop)

```
Instrumental dark country space western, 76 BPM, A minor. Twangy baritone electric guitar with tremolo and deep spring reverb, weeping pedal steel, slow boom-chick rhythm on brushed snare and upright bass, soft analog synth pads drifting underneath like a night sky. Unhurried trail-riding pace, a lonesome melody passed between guitar and steel, gently joined by strings. Dusty, vast, quietly hopeful.
```

**3. Night Shift** (dark blues turning into ambient space)

```
Instrumental dark ambient blues, slow and free, E minor, no drum kit. A slide resonator guitar moaning over a low droning bass note, distant harmonica, faint Hammond organ swells, deep analog synth drones and shimmering cosmic pads, a two-note alien synth call far in the distance. Smoky, lonely, nocturnal, lots of space and silence.
```

**4. Rising Pressure** (War of the Worlds tension meets hill-country blues)

```
Instrumental 1970s rock opera suspense meets hill-country blues, 104 BPM, G minor. A hypnotic repeating slide guitar riff over a stomping kick drum, a phaser-washed fingered bass ostinato, a stabbing string-orchestra figure that climbs and layers, pulsing Moog synth, eerie alien synth wails, rolling timpani. Builds in waves, ominous and dusty, never fully releases.
```

**5. Firefight** (gothic western rock with the orchestra's stabs)

```
Instrumental gothic western rock with orchestra, driving train beat at 132 BPM, E minor. Galloping tom-heavy drums, a fierce twangy baritone guitar riff with tremolo, gritty overdriven slide guitar, stabbing strings and brass hits, roaring Moog bass synth, sci-fi heat-ray synth sweeps, wailing harmonica. Urgent and relentless, a desperate frontier showdown.
```

**6. Aftermath** (War of the Worlds ballad with dark country)

```
Instrumental melancholic 1970s orchestral ballad with dark country, 66 BPM, F minor. Fingerpicked acoustic guitar and warm string orchestra, weeping pedal steel, a soft Moog pad, a mellow phaser electric melody, solo cello. Slow, tender and sorrowful, swelling once into a wide, bittersweet orchestral lift before fading back to a lone guitar. Weary, ending on fragile hope.
```

**7. Long Haul** (mostly space travel)

```
Instrumental cosmic space western, 92 BPM, A minor lifting to C major. Shimmering analog synth arpeggios in vast reverb, a twangy clean electric guitar melody with tremolo, rolling acoustic guitar over a steady train-like rhythm, warm strings and a soaring Moog lead. The feeling of travelling between stars: wide open, wondrous, a little lonely, building to a sweeping, uplifting climax.
```

**8. Silver Well Blues** (mostly dark blues)

```
Instrumental slow dark blues in 12/8, 58 BPM, C minor. Smoky Hammond organ, a moaning slide guitar, lazy brushed drums and walking upright bass, a mellow phaser electric guitar answering each phrase, a 1970s string section swelling in the background, faint cosmic synth shimmer. Late night, weary and soulful, heavy with dust and longing.
```
