export const escapeHtml = (text) =>
  String(text).replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;').replaceAll('"', '&quot;');

export const rich = (text) => escapeHtml(text).replace(/\*([^*]+)\*/g, '<em>$1</em>');

export const plain = (text) => String(text).replace(/\*([^*]+)\*/g, '$1');

export const enter = (step, motion = 'up') => `data-in="${motion}" data-step="${step}"`;

export const head = (visual, { kickerStep = 0, headingStep = 0 } = {}) => {
  const kicker = visual.kicker ? `<span class="kicker" ${enter(kickerStep)}>${escapeHtml(visual.kicker)}</span>` : '';
  const heading = visual.title ? `<h1 class="heading" ${enter(headingStep)}>${rich(visual.title)}</h1>` : '';
  return kicker || heading ? `<div class="head">${kicker}${heading}</div>` : '';
};

export const check = (errors, condition, message) => {
  if (!condition) errors.push(message);
};

export const isText = (value) => typeof value === 'string' && value.trim().length > 0;

export const textWithin = (errors, value, name, max) => {
  check(errors, isText(value), `${name} is required`);
  if (isText(value)) check(errors, plain(value).length <= max, `${name} has ${plain(value).length} characters; the most is ${max}`);
};

export const optionalTextWithin = (errors, value, name, max) => {
  if (value !== undefined) textWithin(errors, value, name, max);
};

export const listWithin = (errors, value, name, min, max) => {
  const ok = Array.isArray(value) && value.length >= min && value.length <= max;
  check(errors, ok, `${name} needs ${min} to ${max} entries`);
  return ok ? value : [];
};
