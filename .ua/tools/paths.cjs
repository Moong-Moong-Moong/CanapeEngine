// Locations shared by the helper scripts, derived from this file's place in <project>/.ua/tools.
const fs = require('fs');
const os = require('os');
const path = require('path');

const toPosix = value => value.split(path.sep).join('/');

const PROJECT_ROOT = toPosix(path.resolve(__dirname, '..', '..'));
const UA_DIR = `${PROJECT_ROOT}/.ua`;
const INTER = `${UA_DIR}/intermediate`;
const TMP = `${UA_DIR}/tmp`;

// UA_SKILL_DIR wins; otherwise the newest understand-anything version in the Claude plugin cache.
function findSkillDir() {
  if (process.env.UA_SKILL_DIR) return toPosix(path.resolve(process.env.UA_SKILL_DIR));
  const cache = path.join(os.homedir(), '.claude', 'plugins', 'cache', 'understand-anything', 'understand-anything');
  const versions = fs.existsSync(cache)
    ? fs.readdirSync(cache).filter(name => fs.existsSync(path.join(cache, name, 'skills', 'understand')))
    : [];
  if (!versions.length) {
    throw new Error('understand-anything plugin not found; set UA_SKILL_DIR to its skills/understand directory');
  }
  versions.sort((a, b) => a.localeCompare(b, undefined, { numeric: true }));
  return toPosix(path.join(cache, versions.at(-1), 'skills', 'understand'));
}

module.exports = { PROJECT_ROOT, UA_DIR, INTER, TMP, findSkillDir };
