// Checks code-lesson output against the real source files.
// Usage: node verify-lessons.cjs [batchNumber...]   (no arguments: the staged normalized document)
const fs = require('fs');

const { PROJECT_ROOT, INTER } = require('./paths.cjs');
const { batches } = JSON.parse(fs.readFileSync(`${PROJECT_ROOT}/.ua/tmp/ua-lesson-batches.json`, 'utf8'));
const HANGUL = /[\uac00-\ud7a3]/;
const lineCount = path => fs.readFileSync(`${PROJECT_ROOT}/${path}`, 'utf8').split('\n').length;

function check(label, files, expectedPaths) {
  const problems = [];
  let sections = 0;
  let covered = 0;
  let total = 0;
  for (const path of expectedPaths) {
    if (!(path in files)) problems.push(`missing entry: ${path}`);
  }
  for (const [path, entry] of Object.entries(files)) {
    if (expectedPaths.length && !expectedPaths.includes(path)) { problems.push(`unexpected entry: ${path}`); continue; }
    if (!fs.existsSync(`${PROJECT_ROOT}/${path}`)) { problems.push(`no such file: ${path}`); continue; }
    if (typeof entry.overview !== 'string' || !HANGUL.test(entry.overview)) problems.push(`${path}: overview missing or not Korean`);
    const list = Array.isArray(entry.sections) ? entry.sections : [];
    if (expectedPaths.length && (list.length < 1 || list.length > 10)) problems.push(`${path}: ${list.length} sections`);
    const max = lineCount(path);
    let previousEnd = 0;
    let fileCovered = 0;
    for (const section of list) {
      sections++;
      const range = section.lineRange;
      const valid = Array.isArray(range) && range.length === 2 && Number.isInteger(range[0]) && Number.isInteger(range[1]) && range[0] >= 1 && range[0] <= range[1];
      if (!valid) { problems.push(`${path}: bad lineRange ${JSON.stringify(range)}`); continue; }
      if (range[1] > max) problems.push(`${path}: lineRange ${JSON.stringify(range)} exceeds ${max} lines`);
      if (range[0] <= previousEnd) problems.push(`${path}: section ${JSON.stringify(range)} overlaps or is out of order (previous end ${previousEnd})`);
      previousEnd = Math.max(previousEnd, range[1]);
      fileCovered += range[1] - range[0] + 1;
      if (!section.title || !section.explanation || !HANGUL.test(section.explanation)) problems.push(`${path}: section ${JSON.stringify(range)} lacks a title or a Korean explanation`);
    }
    if (list.length) { covered += Math.min(fileCovered, max); total += max; }
  }
  const coverage = total ? Math.round((covered / total) * 100) : 0;
  console.log(`${label}: ${problems.length ? 'PROBLEMS' : 'ok'} | entries ${Object.keys(files).length} | sections ${sections} | line coverage of detailed files ${coverage}%`);
  problems.slice(0, 15).forEach(problem => console.log(`    - ${problem}`));
  if (problems.length > 15) console.log(`    ... and ${problems.length - 15} more`);
  return problems.length;
}

const requested = process.argv.slice(2).map(Number);
let failed = 0;
if (!requested.length) {
  const doc = JSON.parse(fs.readFileSync(`${INTER}/code-lessons.normalized.json`, 'utf8'));
  failed += check('normalized', doc.files, []);
} else {
  for (const number of requested) {
    const name = batches.length === 1 ? 'code-lessons.json' : `code-lessons.detailed.${number}.json`;
    if (!fs.existsSync(`${INTER}/${name}`)) { console.log(`batch ${number}: MISSING ${name}`); failed++; continue; }
    const doc = JSON.parse(fs.readFileSync(`${INTER}/${name}`, 'utf8'));
    const files = doc.files && typeof doc.files === 'object' ? doc.files : doc;
    failed += check(`batch ${number}`, files, batches[number - 1].map(file => file.filePath));
  }
}
process.exit(failed ? 1 : 0);
