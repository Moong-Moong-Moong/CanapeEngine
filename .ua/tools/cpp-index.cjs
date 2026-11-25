// Indexes class/struct declarations (with their base classes) across the scanned C++ files.
const fs = require('fs');

const { PROJECT_ROOT, UA_DIR, TMP } = require('./paths.cjs');
const DECLARATION_RE = /^[ \t]*(?:template\s*<[^{};]*>\s*)?(?:class|struct)\s+(?:[A-Z][A-Z0-9_]*\s+)?([A-Za-z_]\w*)\s*(?:final\s*)?(?::\s*([^{;]+?))?\s*\{/gm;

function splitTopLevel(list) {
  const parts = [];
  let depth = 0;
  let current = '';
  for (const char of list) {
    if (char === '<') depth++;
    else if (char === '>') depth--;
    if (char === ',' && depth === 0) { parts.push(current); current = ''; } else current += char;
  }
  if (current.trim()) parts.push(current);
  return parts;
}

function baseName(specifier) {
  const cleaned = specifier.replace(/\b(public|protected|private|virtual)\b/g, '').trim();
  const withoutTemplate = cleaned.replace(/<[\s\S]*>/, '').trim();
  return withoutTemplate.split('::').pop().trim();
}

function buildIndex() {
  const scan = JSON.parse(fs.readFileSync(`${UA_DIR}/intermediate/scan-result.json`, 'utf8'));
  const declarations = [];
  for (const file of scan.files) {
    if (file.language !== 'c' && file.language !== 'cpp') continue;
    const raw = fs.readFileSync(`${PROJECT_ROOT}/${file.path}`, 'utf8');
    const text = (raw.charCodeAt(0) === 0xfeff ? raw.slice(1) : raw)
      .replace(/\/\*[\s\S]*?\*\//g, block => block.replace(/[^\n]/g, ' '))
      .replace(/\/\/[^\n]*/g, line => ' '.repeat(line.length));
    for (const match of text.matchAll(DECLARATION_RE)) {
      const headerOffset = match.index + match[0].search(/class|struct/);
      declarations.push({
        name: match[1],
        file: file.path,
        line: text.slice(0, headerOffset).split('\n').length,
        bases: match[2] ? splitTopLevel(match[2]).map(baseName).filter(Boolean) : [],
      });
    }
  }
  return declarations;
}

function writeClassIndex() {
  const declarations = buildIndex();
  const byName = {};
  for (const { name, file } of declarations) {
    (byName[name] ??= []);
    if (!byName[name].includes(file)) byName[name].push(file);
  }
  const sorted = Object.fromEntries(Object.entries(byName).sort(([a], [b]) => (a < b ? -1 : a > b ? 1 : 0)));
  fs.mkdirSync(TMP, { recursive: true });
  fs.writeFileSync(`${TMP}/ua-class-index.json`, JSON.stringify(sorted, null, 1));
  const withBases = declarations.filter(d => d.bases.length);
  const multi = Object.entries(sorted).filter(([, files]) => files.length > 1);
  console.log(`declarations ${declarations.length} | distinct names ${Object.keys(sorted).length} | with base classes ${withBases.length} | names declared in more than one file ${multi.length}`);
  const baseCounts = {};
  for (const d of withBases) for (const base of d.bases) baseCounts[base] = (baseCounts[base] || 0) + 1;
  console.log('most common bases:', Object.entries(baseCounts).sort((a, b) => b[1] - a[1]).slice(0, 14).map(([n, c]) => `${n}(${c})`).join(', '));
  console.log('multi-file names:', multi.slice(0, 12).map(([n, f]) => `${n}[${f.length}]`).join(', '));
}

module.exports = { buildIndex, writeClassIndex };

if (require.main === module) writeClassIndex();
