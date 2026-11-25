// Heuristic: identifiers quoted in a section's text should occur in the lines the section points at.
const fs = require('fs');
const { PROJECT_ROOT: ROOT } = require('./paths.cjs');
const numbers = process.argv.slice(2);
for (const number of numbers) {
  const doc = JSON.parse(fs.readFileSync(`${ROOT}/.ua/intermediate/code-lessons.detailed.${number}.json`, 'utf8'));
  let checked = 0, aligned = 0; const suspects = [];
  for (const [path, entry] of Object.entries(doc.files)) {
    const lines = fs.readFileSync(`${ROOT}/${path}`, 'utf8').split('\n');
    for (const section of entry.sections) {
      const text = `${section.title} ${section.explanation}`;
      const tokens = [...text.matchAll(/`([^`]+)`/g)].flatMap(m => m[1].split(/[^A-Za-z0-9_]+/)).filter(t => t.length >= 4 && /[A-Za-z]/.test(t));
      if (!tokens.length) continue;
      checked++;
      const body = lines.slice(section.lineRange[0] - 1, section.lineRange[1]).join('\n');
      const hits = tokens.filter(t => body.includes(t)).length;
      if (hits / tokens.length >= 0.4) aligned++;
      else suspects.push(`${path} ${JSON.stringify(section.lineRange)} "${section.title}" (${hits}/${tokens.length} tokens found)`);
    }
  }
  console.log(`batch ${number}: sections with quoted identifiers ${checked} | aligned ${aligned} | suspects ${suspects.length}`);
  suspects.slice(0, 8).forEach(s => console.log(`    ? ${s}`));
}
