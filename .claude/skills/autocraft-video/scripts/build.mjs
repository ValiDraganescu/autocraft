#!/usr/bin/env node
import { copyFileSync, existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { isAbsolute, join, resolve } from 'node:path';
import { COMPONENTS } from '../kit/components/index.mjs';
import { escapeHtml } from '../kit/components/shared.mjs';
import { CANVAS, FORMAT, FORMATS, MUSIC, TIMING, layoutVariables, useFormat } from '../kit/layout.mjs';
import { sceneFile } from '../kit/scene.mjs';
import { fail } from './lib/fail.mjs';
import { duckLane } from './lib/envelope.mjs';
import { probeDuration } from './lib/ffmpeg.mjs';
import { placeClip, placeImage } from './lib/media.mjs';
import { KIT_DIR, REPO_ROOT, WORKSPACE, projectDir as projectOf } from './lib/paths.mjs';
import { voiceCache } from './lib/voice.mjs';

// The game's HUD font (unreal/Content-src/fonts, OFL), by CSS weight.
const FONTS = { 500: 'BarlowCondensed-Medium.ttf', 600: 'BarlowCondensed-SemiBold.ttf', 700: 'BarlowCondensed-Bold.ttf' };
const round = (value) => Math.round(value * 1000) / 1000;

const feature = process.argv[2];
if (!/^[a-z0-9-]+$/.test(feature ?? '')) fail('usage: build.mjs <video>, where <video> is kebab-case');
const projectDir = projectOf(feature);
const scenesPath = join(projectDir, 'scenes.json');
if (!existsSync(scenesPath)) fail(`no scenes.json in ${projectDir}`);

const spec = JSON.parse(readFileSync(scenesPath, 'utf8'));
const voice = spec.voice ?? 'af_heart';
const speed = String(spec.speed ?? 1.0);
if (typeof spec.title !== 'string' || !spec.title.trim()) fail('scenes.json needs a title');
if (!Array.isArray(spec.scenes) || spec.scenes.length === 0) fail('scenes.json has no scenes');
if (!useFormat(spec.format)) fail(`format must be one of ${Object.keys(FORMATS).join(', ')}`);
// `music`: a track of the game's soundtrack by name ("long_haul_1"), or
// { track, start, volume }; a track is a name in unreal/Resources/Sounds/music
// or a path from the project.
const music = typeof spec.music === 'string' ? { track: spec.music } : spec.music;
if (music !== undefined) {
  if (typeof music?.track !== 'string' || !music.track.trim()) fail('music needs a track');
  if (music.start !== undefined && !(typeof music.start === 'number' && music.start >= 0)) fail('music.start must be seconds, 0 or more');
  if (music.volume !== undefined && !(typeof music.volume === 'number' && music.volume > 0 && music.volume <= 1)) fail('music.volume must be above 0, at most 1');
}

const problems = [];
const ids = new Set();
for (const scene of spec.scenes) {
  const label = `scene "${scene.id}"`;
  if (!/^[a-z0-9-]+$/.test(scene.id ?? '')) problems.push(`${label}: id must be kebab-case`);
  if (ids.has(scene.id)) problems.push(`${label}: id is used twice`);
  ids.add(scene.id);
  if (!scene.narration?.trim()) problems.push(`${label}: no narration`);
  const component = COMPONENTS[scene.visual?.type];
  if (!component) {
    problems.push(`${label}: visual.type must be one of ${Object.keys(COMPONENTS).join(', ')}`);
    continue;
  }
  for (const error of component.validate(scene.visual)) problems.push(`${label} (${scene.visual.type}): ${error}`);
}
if (problems.length > 0) fail(`scenes.json is not valid:\n  ${problems.join('\n  ')}`);

const dirs = {
  voice: join(projectDir, 'assets', 'voice'),
  clips: join(projectDir, 'assets', 'clips'),
  images: join(projectDir, 'assets', 'images'),
  music: join(projectDir, 'assets', 'music'),
  holds: join(projectDir, 'assets', 'holds'),
  vendor: join(projectDir, 'assets', 'vendor'),
  fonts: join(projectDir, 'assets', 'vendor', 'fonts'),
  compositions: join(projectDir, 'compositions'),
  build: join(projectDir, '.build'),
};
for (const dir of Object.values(dirs)) mkdirSync(dir, { recursive: true });

const vendor = (from, to) => {
  if (!existsSync(from)) fail(`${from} is missing; run the setup script`);
  copyFileSync(from, to);
};
vendor(join(WORKSPACE, 'node_modules', 'gsap', 'dist', 'gsap.min.js'), join(dirs.vendor, 'gsap.min.js'));
for (const [weight, file] of Object.entries(FONTS)) {
  vendor(join(REPO_ROOT, 'unreal', 'Content-src', 'fonts', file), join(dirs.fonts, `barlow-condensed-${weight}.ttf`));
}
writeFileSync(
  join(dirs.vendor, 'kit.css'),
  [readFileSync(join(KIT_DIR, 'tokens.css'), 'utf8'), layoutVariables(), readFileSync(join(KIT_DIR, 'kit.css'), 'utf8')].join('\n'),
);

const scaffold = {
  'hyperframes.json': `${JSON.stringify({ paths: { blocks: 'compositions', components: 'compositions/components', assets: 'assets' }, media: { autoProxy: true } }, null, 2)}\n`,
  'meta.json': `${JSON.stringify({ id: feature, name: feature }, null, 2)}\n`,
};
for (const [name, content] of Object.entries(scaffold)) writeFileSync(join(projectDir, name), content);

const fromProject = (src) => (isAbsolute(src) ? src : resolve(projectDir, src));
const voices = voiceCache(dirs.build);
const total = spec.scenes.length;
const hosts = [];
const media = [];
const rootMotion = [];
const timeline = [];
let cursor = 0;

spec.scenes.forEach((scene, index) => {
  const audio = voices.narrate(scene, { voice, speed, out: join(dirs.voice, `${scene.id}.wav`) });
  const duration = round(Math.max(scene.minDuration ?? 0, TIMING.lead + audio + TIMING.tail));
  const start = round(cursor);
  const component = COMPONENTS[scene.visual.type];
  const entry = { id: scene.id, start, duration, audio: round(audio), type: scene.visual.type };

  if (component.media) {
    const place = scene.visual.type === 'clip' ? placeClip : placeImage;
    const placed = place({ scene, index, src: fromProject(scene.visual.src), start, duration, voice: audio, dirs });
    media.push(...placed.elements);
    rootMotion.push(...placed.motion);
    Object.assign(entry, placed.entry);
  }

  writeFileSync(
    join(dirs.compositions, `${scene.id}.html`),
    sceneFile({ id: scene.id, index, total, title: spec.title, duration, body: component.html(scene.visual) }),
  );
  hosts.push(
    `<div id="${scene.id}" data-composition-id="scene-${scene.id}" data-composition-src="compositions/${scene.id}.html" data-start="${start}" data-duration="${duration}" data-track-index="1" data-width="${CANVAS.width}" data-height="${CANVAS.height}"></div>`,
  );
  media.push(
    `<audio id="${scene.id}-voice" src="assets/voice/${scene.id}.wav" data-start="${round(start + TIMING.lead)}" data-duration="${round(audio)}" data-track-index="10" data-volume="1"></audio>`,
  );
  timeline.push(entry);
  cursor += duration;
});

const length = round(cursor);

if (music) {
  const named = join(REPO_ROOT, 'unreal', 'Resources', 'Sounds', 'music', `${music.track}.mp3`);
  const src = existsSync(named) ? named : fromProject(music.track);
  if (!existsSync(src)) fail(`music track ${music.track} is neither in unreal/Resources/Sounds/music nor a file`);
  const from = music.start ?? 0;
  if (probeDuration(src) - from < length) fail(`music track ${music.track} is shorter than the video (${length}s) from ${from}s`);
  const name = `bed${src.slice(src.lastIndexOf('.'))}`;
  copyFileSync(src, join(dirs.music, name));
  // Up between the voices, down under them; the timeline's seconds.
  const level = music.volume ?? MUSIC.volume;
  const lane = duckLane({ length, level, low: level * MUSIC.duck, ramp: MUSIC.ramp, fadeIn: MUSIC.fadeIn, fadeOut: MUSIC.fadeOut,
    voices: timeline.map((entry) => [entry.start + TIMING.lead, entry.start + TIMING.lead + entry.audio]) });
  media.push(
    `<audio id="music-bed" src="assets/music/${name}" data-start="0" data-duration="${length}" data-media-start="${round(from)}" data-track-index="11" data-volume="${round(level)}" data-automation='${lane}'></audio>`,
  );
}
const html = `<!doctype html>
<html lang="en" data-resolution="${FORMAT.vertical ? 'portrait' : 'landscape'}">
  <head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=${CANVAS.width}, height=${CANVAS.height}" />
    <title>${escapeHtml(spec.title)}</title>
    <script src="assets/vendor/gsap.min.js"></script>
    <link rel="stylesheet" href="assets/vendor/kit.css" />
    <style>
      html, body { width: ${CANVAS.width}px; height: ${CANVAS.height}px; margin: 0; }
      #root { position: relative; width: 100%; height: 100%; overflow: hidden; background: var(--bg); }
    </style>
  </head>
  <body>
    <div id="root" data-composition-id="main" data-start="0" data-duration="${length}" data-width="${CANVAS.width}" data-height="${CANVAS.height}">
      ${hosts.join('\n      ')}
      ${media.join('\n      ')}
    </div>
    <script>
      const tl = gsap.timeline({ paused: true });
      ${rootMotion.join('\n      ')}
      window.__timelines["main"] = tl;
    </script>
  </body>
</html>
`;

writeFileSync(join(projectDir, 'index.html'), html);
voices.save();
writeFileSync(join(dirs.build, 'timeline.json'), `${JSON.stringify({ total: length, scenes: timeline }, null, 2)}\n`);
console.log(`build: ${timeline.length} scenes, ${length}s, ${FORMAT.name} ${CANVAS.width}×${CANVAS.height} → ${join(projectDir, 'index.html')}`);
for (const entry of timeline) {
  const extra = entry.type === 'clip' ? ` rate ${entry.rate} play ${entry.play}s${entry.sound ? ', game sound' : ''}` : '';
  console.log(`  ${String(entry.start).padStart(7)}s  ${entry.id}  ${entry.duration}s (${entry.type}, voice ${entry.audio}s${extra})`);
}
