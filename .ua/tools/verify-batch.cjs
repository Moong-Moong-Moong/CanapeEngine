// Checks file-analyzer output for the given batch indices against batches.json.
const fs = require('fs');

const { UA_DIR } = require('./paths.cjs');
const batches = JSON.parse(fs.readFileSync(`${UA_DIR}/intermediate/batches.json`, 'utf8'));
const requested = process.argv.slice(2).map(Number);
const indices = requested.length ? requested : batches.batches.map(b => b.batchIndex);

const NODE_TYPES = new Set(['file', 'function', 'class', 'config', 'document', 'service', 'table', 'endpoint', 'pipeline', 'schema', 'resource']);
const COMPLEXITY = new Set(['simple', 'moderate', 'complex']);
const PREFIX_BY_CATEGORY = { code: 'file', script: 'file', markup: 'file', docs: 'document', config: 'config' };
const HANGUL = /[\uac00-\ud7a3]/;

let failed = 0;
for (const index of indices) {
  const batch = batches.batches.find(b => b.batchIndex === index);
  if (!batch) { console.log(`batch ${index}: NOT IN batches.json`); failed++; continue; }

  const names = fs.readdirSync(`${UA_DIR}/intermediate`)
    .filter(name => new RegExp(`^batch-${index}(?:-part-\\d+)?\\.json$`).test(name));
  if (!names.length) { console.log(`batch ${index}: MISSING OUTPUT`); failed++; continue; }
  const single = names.includes(`batch-${index}.json`);
  const problems = [];
  if (single && names.length > 1) problems.push(`both single and part files present: ${names.join(', ')}`);

  const nodes = [];
  const edges = [];
  for (const name of names) {
    try {
      const doc = JSON.parse(fs.readFileSync(`${UA_DIR}/intermediate/${name}`, 'utf8'));
      if (!Array.isArray(doc.nodes) || !Array.isArray(doc.edges)) { problems.push(`${name}: nodes/edges not arrays`); continue; }
      nodes.push(...doc.nodes);
      edges.push(...doc.edges);
    } catch (error) {
      problems.push(`${name}: invalid JSON (${error.message})`);
    }
  }

  const ids = new Map();
  for (const node of nodes) ids.set(node.id, (ids.get(node.id) || 0) + 1);
  const duplicates = [...ids].filter(([, n]) => n > 1).map(([id]) => id);
  if (duplicates.length) problems.push(`duplicate ids: ${duplicates.slice(0, 5).join(', ')}${duplicates.length > 5 ? ` (+${duplicates.length - 5})` : ''}`);

  const missingFiles = batch.files.filter(f => !ids.has(`${PREFIX_BY_CATEGORY[f.fileCategory] || 'file'}:${f.path}`));
  if (missingFiles.length) problems.push(`no file-level node for: ${missingFiles.map(f => f.path).slice(0, 6).join(', ')}${missingFiles.length > 6 ? ` (+${missingFiles.length - 6})` : ''}`);

  const badNodes = nodes.filter(n => !n.id || !NODE_TYPES.has(n.type) || !n.name || !n.summary || !Array.isArray(n.tags) || !n.tags.length || !COMPLEXITY.has(n.complexity));
  if (badNodes.length) problems.push(`nodes with missing/invalid required fields: ${badNodes.length} (e.g. ${badNodes.slice(0, 3).map(n => n.id).join(', ')})`);
  const noLineRange = nodes.filter(n => (n.type === 'function' || n.type === 'class') && !(Array.isArray(n.lineRange) && n.lineRange.length === 2));
  if (noLineRange.length) problems.push(`function/class nodes without lineRange: ${noLineRange.length}`);
  const nonKorean = nodes.filter(n => typeof n.summary === 'string' && !HANGUL.test(n.summary));
  if (nonKorean.length) problems.push(`summaries without Hangul: ${nonKorean.length} (e.g. ${nonKorean.slice(0, 3).map(n => n.id).join(', ')})`);

  const badEdges = edges.filter(e => !e.source || !e.target || !e.type || e.direction !== 'forward' || typeof e.weight !== 'number');
  if (badEdges.length) problems.push(`edges with missing/invalid fields: ${badEdges.length}`);
  const selfEdges = edges.filter(e => e.source === e.target);
  if (selfEdges.length) problems.push(`self edges: ${selfEdges.length}`);
  const foreignSource = edges.filter(e => !ids.has(e.source) && e.type !== 'tested_by');
  if (foreignSource.length) problems.push(`edges whose source is not a node of this batch: ${foreignSource.length} (e.g. ${foreignSource.slice(0, 2).map(e => `${e.source} -[${e.type}]-> ${e.target}`).join(' | ')})`);

  const expectedImports = Object.values(batch.batchImportData).reduce((n, list) => n + list.length, 0);
  const importKeys = new Set(edges.filter(e => e.type === 'imports').map(e => `${e.source}\u0000${e.target}`));
  const expectedKeys = [];
  for (const [file, targets] of Object.entries(batch.batchImportData)) {
    for (const target of targets) expectedKeys.push([file, target]);
  }
  const stripPrefix = id => id.replace(/^(file|document|config):/, '');
  const emitted = new Set([...importKeys].map(key => key.split('\u0000').map(stripPrefix).join('\u0000')));
  const missingImports = expectedKeys.filter(([file, target]) => !emitted.has(`${file}\u0000${target}`));

  const typeCounts = {};
  for (const node of nodes) typeCounts[node.type] = (typeCounts[node.type] || 0) + 1;
  const edgeCounts = {};
  for (const edge of edges) edgeCounts[edge.type] = (edgeCounts[edge.type] || 0) + 1;

  const status = problems.length ? 'PROBLEMS' : 'ok';
  if (problems.length) failed++;
  console.log(`batch ${index}: ${status} | files ${names.length} (${names.join(', ')}) | batchFiles ${batch.files.length} | nodes ${nodes.length} ${JSON.stringify(typeCounts)} | edges ${edges.length} ${JSON.stringify(edgeCounts)} | imports ${importKeys.size}/${expectedImports} (missing ${missingImports.length})`);
  for (const problem of problems) console.log(`    - ${problem}`);
}
process.exit(failed ? 1 : 0);
