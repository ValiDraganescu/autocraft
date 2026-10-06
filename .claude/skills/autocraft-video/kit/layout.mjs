// The canvases: `format` in scenes.json. Landscape is the 16:9 explainer;
// portrait (4:5, the X feed) and story (9:16) are laid out natively: the
// panels stack, the clips fill the width (cropped to the box, `fit`/`focus`).
export const FORMATS = {
  landscape: { width: 1920, height: 1080, pad: 240, media: { left: 240, top: 190, width: 1440, height: 810 }, fit: 'contain' },
  portrait: { width: 1080, height: 1350, pad: 64, media: { left: 0, top: 330, width: 1080, height: 810 }, fit: 'cover' },
  story: { width: 1080, height: 1920, pad: 64, media: { left: 0, top: 520, width: 1080, height: 1080 }, fit: 'cover' },
};
export const CANVAS = { width: 1920, height: 1080 };
export const MEDIA = { left: 240, top: 190, width: 1440, height: 810 };
export const FORMAT = { name: 'landscape', vertical: false, pad: 240, fit: 'contain' };
export const TIMING = { lead: 0.4, tail: 0.7, fade: 0.3, stepGap: 0.2, firstStep: 0.2 };
export const CLIP_RATE = { min: 0.5, max: 3 };
// A clip's game sound: `full` without the voice, `duck` under it, `ramp` seconds between.
export const SOUND = { full: 1, duck: 0.35, ramp: 0.25 };
// The music bed: `volume` between the voices, `duck` × volume under them,
// faded in over `fadeIn` and out over the last `fadeOut` seconds.
export const MUSIC = { volume: 0.25, duck: 0.4, ramp: 0.4, fadeIn: 1, fadeOut: 2 };
export const BRAND = 'Autocraft';

// Called once by the build, before anything is laid out.
export const useFormat = (name = 'landscape') => {
  const f = FORMATS[name];
  if (!f) return false;
  Object.assign(CANVAS, { width: f.width, height: f.height });
  Object.assign(MEDIA, f.media);
  Object.assign(FORMAT, { name, vertical: name !== 'landscape', pad: f.pad, fit: f.fit });
  return true;
};

// `fit`: contain (the whole frame, letterboxed) or cover (fills the box,
// cropped); `focus` 0..1 picks the part of a cropped frame kept, left to right.
export const mediaStyle = ({ fit, focus } = {}) =>
  `position:absolute; left:${MEDIA.left}px; top:${MEDIA.top}px; width:${MEDIA.width}px; height:${MEDIA.height}px; object-fit:${fit ?? FORMAT.fit}; object-position:${Math.round((focus ?? 0.5) * 100)}% 50%;${FORMAT.vertical ? '' : ' border-radius:20px;'}`;

export const layoutVariables = () =>
  `:root { --pad: ${FORMAT.pad}px; --media-left: ${MEDIA.left}px; --media-top: ${MEDIA.top}px; --media-width: ${MEDIA.width}px; --media-height: ${MEDIA.height}px; }`;
