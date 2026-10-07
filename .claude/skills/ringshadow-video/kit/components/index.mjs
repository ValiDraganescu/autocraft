import compare from './compare.mjs';
import cover from './cover.mjs';
import flow from './flow.mjs';
import { bullets, checks } from './list.mjs';
import { clip, image } from './media.mjs';
import rules from './rules.mjs';
import stats from './stats.mjs';

export const COMPONENTS = Object.fromEntries([cover, stats, flow, rules, bullets, checks, compare, clip, image].map((component) => [component.type, component]));
