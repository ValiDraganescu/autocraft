import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

export const SKILL_DIR = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..');
export const KIT_DIR = join(SKILL_DIR, 'kit');
export const REPO_ROOT = resolve(SKILL_DIR, '..', '..', '..');
export const WORKSPACE = join(REPO_ROOT, 'video');
export const PROJECTS = join(WORKSPACE, 'projects');

export const projectDir = (feature) => join(PROJECTS, feature);
