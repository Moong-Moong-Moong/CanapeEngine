// Deterministic post-merge repairs on assembled-graph.json:
//   1. re-target batch edges that dangled only because of a header/source or class-location mismatch
//   2. restore tested_by edges to this project's validation programs
//   3. add inherits edges found in the C++ class declarations
// Usage: node enrich-graph.cjs [--write]
const fs = require('fs');
const { buildIndex } = require('./cpp-index.cjs');

const { INTER } = require('./paths.cjs');
const write = process.argv.includes('--write');

const graph = JSON.parse(fs.readFileSync(`${INTER}/assembled-graph.json`, 'utf8'));
const nodeById = new Map(graph.nodes.map(node => [node.id, node]));
const edgeKey = edge => `${edge.source}\u0000${edge.target}\u0000${edge.type}`;
const existing = new Set(graph.edges.map(edgeKey));

const batchEdges = [];
for (const name of fs.readdirSync(INTER)) {
  if (!/^batch-\d+(?:-part-\d+)?\.json$/.test(name)) continue;
  batchEdges.push(...JSON.parse(fs.readFileSync(`${INTER}/${name}`, 'utf8')).edges);
}

const functionsByFile = new Map();
const classesByFile = new Map();
const classesByName = new Map();
const bareName = name => String(name).split('::').pop();
for (const node of graph.nodes) {
  if (node.type !== 'function' && node.type !== 'class') continue;
  const byFile = node.type === 'function' ? functionsByFile : classesByFile;
  if (!byFile.has(node.filePath)) byFile.set(node.filePath, []);
  byFile.get(node.filePath).push(node);
  if (node.type === 'class') {
    const key = bareName(node.name);
    if (!classesByName.has(key)) classesByName.set(key, []);
    classesByName.get(key).push(node);
  }
}

const splitSymbolId = id => {
  const rest = id.slice(id.indexOf(':') + 1);
  const cut = rest.indexOf(':');
  return cut < 0 ? null : { path: rest.slice(0, cut), name: rest.slice(cut + 1) };
};
const counterpart = path => {
  if (/\.h$/.test(path)) return path.replace(/\.h$/, '.cpp');
  if (/\.cpp$/.test(path)) return path.replace(/\.cpp$/, '.h');
  return null;
};
const preferHeaders = nodes => {
  const headers = nodes.filter(node => /\.h$/.test(node.filePath));
  return headers.length ? headers : nodes;
};

function resolveEndpoint(id) {
  if (nodeById.has(id)) return { id, how: 'exact' };
  const fileLevel = id.match(/^(file|document|config):(.+)$/);
  if (fileLevel) {
    for (const prefix of ['file', 'document', 'config']) {
      if (nodeById.has(`${prefix}:${fileLevel[2]}`)) return { id: `${prefix}:${fileLevel[2]}`, how: 'file-prefix' };
    }
    return null;
  }
  if (id.startsWith('function:')) {
    const symbol = splitSymbolId(id);
    if (!symbol) return null;
    const wanted = bareName(symbol.name);
    const qualifier = symbol.name.includes('::') ? symbol.name.split('::').slice(-2)[0] : null;
    const other = counterpart(symbol.path);
    const pool = [...(functionsByFile.get(symbol.path) ?? []), ...(functionsByFile.get(other) ?? [])];
    const twins = pool.filter(node => {
      if (bareName(node.name) !== wanted) return false;
      const nodeQualifier = node.name.includes('::') ? node.name.split('::').slice(-2)[0] : null;
      return !qualifier || !nodeQualifier || qualifier === nodeQualifier;
    });
    if (twins.length === 1) return { id: twins[0].id, how: 'function-in-paired-file' };
    const header = /\.h$/.test(symbol.path) ? symbol.path : other;
    const owners = classesByFile.get(header) ?? [];
    const named = qualifier ? owners.filter(node => bareName(node.name) === qualifier) : [];
    if (named.length === 1) return { id: named[0].id, how: 'method-to-class' };
    if (owners.length === 1) return { id: owners[0].id, how: 'method-to-class' };
    return null;
  }
  if (id.startsWith('class:')) {
    const symbol = splitSymbolId(id);
    if (!symbol) return null;
    const candidates = preferHeaders(classesByName.get(bareName(symbol.name)) ?? []);
    if (candidates.length === 1) return { id: candidates[0].id, how: 'class-elsewhere' };
    const sameStem = candidates.filter(node => node.filePath === counterpart(symbol.path));
    if (sameStem.length === 1) return { id: sameStem[0].id, how: 'class-elsewhere' };
    return null;
  }
  return null;
}

const added = [];
const stats = { retargeted: {}, testedBy: 0, testedTags: 0, inherits: 0, unresolved: {}, examples: [] };
const addEdge = (edge, reason) => {
  if (edge.source === edge.target) return false;
  const key = edgeKey(edge);
  if (existing.has(key)) return false;
  existing.add(key);
  added.push(edge);
  if (stats.examples.length < 400) stats.examples.push(`${reason}: ${edge.source} -[${edge.type}]-> ${edge.target}`);
  return true;
};

// 1. Dangling batch edges.
for (const edge of batchEdges) {
  if (edge.type === 'tested_by') continue;
  if (nodeById.has(edge.source) && nodeById.has(edge.target)) continue;
  const source = resolveEndpoint(edge.source);
  const target = resolveEndpoint(edge.target);
  if (!source || !target) {
    const missing = !source ? edge.source : edge.target;
    const kind = `${edge.type}:${missing.split(':')[0]}`;
    stats.unresolved[kind] = (stats.unresolved[kind] || 0) + 1;
    continue;
  }
  if (edge.type === 'contains' || edge.type === 'exports' || edge.type === 'imports') continue;
  const how = source.how !== 'exact' ? source.how : target.how;
  if (addEdge({ source: source.id, target: target.id, type: edge.type, direction: 'forward', weight: edge.weight }, how)) {
    stats.retargeted[how] = (stats.retargeted[how] || 0) + 1;
  }
}

// 2. tested_by edges to validation programs.
const isTestPath = path => /(^|\/)Tests\//.test(path) || path.startsWith('TestBed/');
for (const edge of batchEdges) {
  if (edge.type !== 'tested_by') continue;
  const a = resolveEndpoint(edge.source);
  const b = resolveEndpoint(edge.target);
  if (!a || !b) continue;
  const nodeA = nodeById.get(a.id);
  const nodeB = nodeById.get(b.id);
  if (!nodeA.filePath || !nodeB.filePath || isTestPath(nodeA.filePath) === isTestPath(nodeB.filePath)) continue;
  const [production, test] = isTestPath(nodeA.filePath) ? [nodeB, nodeA] : [nodeA, nodeB];
  if (addEdge({ source: production.id, target: test.id, type: 'tested_by', direction: 'forward', weight: 0.5 }, 'tested_by')) stats.testedBy++;
  if (Array.isArray(production.tags) && !production.tags.includes('tested')) {
    production.tags.push('tested');
    stats.testedTags++;
  }
}

// 3. inherits edges from the class declarations.
for (const declaration of buildIndex()) {
  const derived = (classesByFile.get(declaration.file) ?? []).filter(node => bareName(node.name) === declaration.name);
  if (derived.length !== 1) continue;
  for (const base of declaration.bases) {
    if (base === declaration.name) continue;
    const candidates = preferHeaders(classesByName.get(base) ?? []);
    if (candidates.length !== 1) continue;
    if (addEdge({ source: derived[0].id, target: candidates[0].id, type: 'inherits', direction: 'forward', weight: 0.9 }, 'inherits')) stats.inherits++;
  }
}

const byType = {};
for (const edge of added) byType[edge.type] = (byType[edge.type] || 0) + 1;
console.log(`batch edges ${batchEdges.length} | merged edges ${graph.edges.length} | added ${added.length} ${JSON.stringify(byType)}`);
console.log(`retargeted ${JSON.stringify(stats.retargeted)} | tested_by restored ${stats.testedBy} (tagged ${stats.testedTags}) | inherits added ${stats.inherits}`);
console.log(`still unresolved (edge type:missing endpoint kind) ${JSON.stringify(stats.unresolved)}`);
const shown = {};
for (const example of stats.examples) {
  const reason = example.slice(0, example.indexOf(':'));
  shown[reason] = (shown[reason] || 0) + 1;
  if (shown[reason] <= 3) console.log(`  ${example}`);
}
if (write) {
  graph.edges.push(...added);
  fs.writeFileSync(`${INTER}/assembled-graph.json`, JSON.stringify(graph, null, 2));
  console.log(`written: edges now ${graph.edges.length}`);
}
