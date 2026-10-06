import { check, escapeHtml, isText, optionalTextWithin, rich, textWithin, enter } from './shared.mjs';

const IMAGE = /\.(png|jpe?g|webp|svg)$/i;
const VIDEO = /\.(webm|mp4|mov)$/i;

const number = (errors, value, name) => {
  if (value !== undefined) check(errors, typeof value === 'number' && value >= 0, `${name} must be a number of seconds, 0 or more`);
};

const mediaComponent = (type, pattern) => ({
  type,
  media: true,
  validate(visual) {
    const errors = [];
    optionalTextWithin(errors, visual.kicker, 'kicker', 32);
    textWithin(errors, visual.title, 'title', 44);
    check(errors, isText(visual.src) && pattern.test(visual.src), `src must name a ${type === 'clip' ? '.webm, .mp4 or .mov' : '.png, .jpg, .webp or .svg'} file`);
    if (visual.fit !== undefined) check(errors, visual.fit === 'contain' || visual.fit === 'cover', 'fit must be contain or cover');
    if (visual.focus !== undefined) check(errors, typeof visual.focus === 'number' && visual.focus >= 0 && visual.focus <= 1, 'focus must be a number from 0 (left) to 1 (right)');
    if (type === 'clip') {
      number(errors, visual.mediaStart, 'mediaStart');
      number(errors, visual.mediaEnd, 'mediaEnd');
      if (visual.sound !== undefined) check(errors, typeof visual.sound === 'boolean', 'sound must be true or false');
      if (visual.soundVolume !== undefined) check(errors, typeof visual.soundVolume === 'number' && visual.soundVolume >= 0 && visual.soundVolume <= 1, 'soundVolume must be 0 to 1');
      if (visual.rate !== undefined) check(errors, typeof visual.rate === 'number' && visual.rate > 0, 'rate must be a number above 0');
    }
    return errors;
  },
  html(visual) {
    const kicker = visual.kicker ? `<span class="kicker" ${enter(0)}>${escapeHtml(visual.kicker)}</span>` : '';
    return `<header class="media-head">${kicker}<h1 class="heading" ${enter(0)}>${rich(visual.title)}</h1></header>
<div class="media-frame"></div>`;
  },
});

export const clip = mediaComponent('clip', VIDEO);
export const image = mediaComponent('image', IMAGE);
