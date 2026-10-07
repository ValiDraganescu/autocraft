import { copyFileSync, existsSync } from 'node:fs';
import { extname, join } from 'node:path';
import { CLIP_RATE, SOUND, TIMING, mediaStyle } from '../../kit/layout.mjs';
import { fail } from './fail.mjs';
import { duckLane } from './envelope.mjs';
import { blankLeadEnd, extractFrame, hasAudio, probeDuration } from './ffmpeg.mjs';

const round = (value) => Math.round(value * 1000) / 1000;
const stillTrack = (index) => 20 + index;

const fadeIn = (id, at) =>
  `tl.fromTo("#${id}", { opacity: 0, scale: 0.985 }, { opacity: 1, scale: 1, duration: 0.5, ease: "power2.out" }, ${round(at)});`;
const fadeOut = (id, end) => `tl.to("#${id}", { opacity: 0, duration: ${TIMING.fade}, ease: "power1.in" }, ${round(end - TIMING.fade)});`;

export const placeImage = ({ scene, index, src, start, duration, dirs }) => {
  if (!existsSync(src)) fail(`scene "${scene.id}": image ${scene.visual.src} does not exist`);
  const name = `${scene.id}${extname(src)}`;
  copyFileSync(src, join(dirs.images, name));
  const id = `${scene.id}-image`;
  return {
    elements: [`<img id="${id}" class="clip" src="assets/images/${name}" alt="" data-start="${start}" data-duration="${duration}" data-track-index="${stillTrack(index)}" style="${mediaStyle(scene.visual)}" />`],
    motion: [fadeIn(id, start + 0.15), fadeOut(id, start + duration)],
    entry: {},
  };
};

export const placeClip = ({ scene, index, src, start, duration, voice, dirs }) => {
  const visual = scene.visual;
  if (!existsSync(src)) fail(`scene "${scene.id}": clip ${visual.src} does not exist`);
  const name = `${scene.id}${extname(src)}`;
  copyFileSync(src, join(dirs.clips, name));
  const sourceLength = probeDuration(src);
  const mediaStart = visual.mediaStart ?? blankLeadEnd(src, sourceLength);
  const mediaEnd = Math.min(visual.mediaEnd ?? sourceLength, sourceLength);
  if (mediaEnd <= mediaStart) fail(`scene "${scene.id}": mediaEnd must be after mediaStart`);
  // The game's sound plays with a clip that has it, unless `sound: false`; a
  // clip with sound keeps its speed (1×, so the sound keeps its pitch) unless
  // `rate` says other.
  const sound = visual.sound !== false && hasAudio(src);
  const fitted = Math.min(CLIP_RATE.max, Math.max(CLIP_RATE.min, (mediaEnd - mediaStart) / duration));
  const rate = round(visual.rate ?? (sound ? 1 : fitted));
  const play = round(Math.min(duration, (mediaEnd - mediaStart) / rate));
  const clipId = `${scene.id}-clip`;
  const elements = [
    `<video id="${clipId}" class="clip" src="assets/clips/${name}" ${sound ? `data-has-audio="true" data-volume="${SOUND.full}" data-automation='${duckLane({ length: play, level: SOUND.full, low: visual.soundVolume ?? SOUND.duck, ramp: SOUND.ramp, fadeIn: SOUND.ramp, fadeOut: TIMING.fade, voices: [[TIMING.lead, TIMING.lead + voice]] })}'` : 'muted'} playsinline data-start="${start}" data-duration="${play}" data-media-start="${round(mediaStart)}" data-playback-rate="${rate}" data-track-index="2" style="${mediaStyle(visual)}"></video>`,
  ];
  const motion = [fadeIn(clipId, start + 0.15)];
  if (duration - play > 0.05) {
    const holdId = `${scene.id}-hold`;
    extractFrame(src, Math.min(mediaStart + play * rate, sourceLength) - 0.08, join(dirs.holds, `${scene.id}.png`));
    elements.push(
      `<img id="${holdId}" class="clip" src="assets/holds/${scene.id}.png" alt="" data-start="${round(start + play)}" data-duration="${round(duration - play)}" data-track-index="${stillTrack(index)}" style="${mediaStyle(visual)}" />`,
    );
    motion.push(fadeOut(holdId, start + duration));
  } else {
    motion.push(fadeOut(clipId, start + duration));
  }
  return { elements, motion, entry: { rate, play, mediaStart: round(mediaStart), sound } };
};
