// Orchestration helpers for the /understand run. Usage: node graph-tools.cjs <command>
const fs = require('fs');

const path = require('path');
const { INTER, TMP } = require('./paths.cjs');
const FILE_LEVEL_TYPES = new Set(['file', 'config', 'document', 'service', 'pipeline', 'table', 'schema', 'resource', 'endpoint']);
const KNOWN_PREFIX = /^(file|config|document|service|pipeline|table|schema|resource|endpoint):/;

const readJson = path => JSON.parse(fs.readFileSync(path, 'utf8'));
const writeJson = (file, value) => {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, JSON.stringify(value, null, 2));
};
const kebab = text => String(text).trim().toLowerCase().replace(/[^\p{L}\p{N}]+/gu, '-').replace(/^-+|-+$/g, '');

function loadGraph() {
  const graph = readJson(`${INTER}/assembled-graph.json`);
  const fileNodes = graph.nodes.filter(node => FILE_LEVEL_TYPES.has(node.type));
  const fileIds = new Set(fileNodes.map(node => node.id));
  const fileEdges = graph.edges.filter(edge => fileIds.has(edge.source) && fileIds.has(edge.target));
  return { graph, fileNodes, fileIds, fileEdges };
}

function loadLayers() {
  return readJson(`${INTER}/layers.normalized.json`);
}

const commands = {
  // Input file for the architecture-analyzer's structural script.
  'prep-arch'() {
    const { fileNodes, fileEdges } = loadGraph();
    const pick = ({ source, target, type }) => ({ source, target, type });
    writeJson(`${TMP}/ua-arch-input.json`, {
      fileNodes: fileNodes.map(({ id, type, name, filePath, summary, tags }) => ({ id, type, name, filePath, summary, tags })),
      importEdges: fileEdges.filter(edge => edge.type === 'imports').map(pick),
      allEdges: fileEdges.map(pick),
    });
    const byType = {};
    for (const edge of fileEdges) byType[edge.type] = (byType[edge.type] || 0) + 1;
    console.log(`fileNodes ${fileNodes.length} | file-level edges ${fileEdges.length} ${JSON.stringify(byType)}`);
  },

  // The five layer normalization steps from the skill, plus coverage checks.
  'normalize-layers'() {
    const { graph, fileNodes } = loadGraph();
    const nodeIds = new Set(graph.nodes.map(node => node.id));
    const idByPath = new Map(fileNodes.map(node => [node.filePath, node.id]));
    let raw = readJson(`${INTER}/layers.json`);
    if (!Array.isArray(raw) && Array.isArray(raw.layers)) raw = raw.layers;
    if (!Array.isArray(raw)) throw new Error('layers.json is neither an array nor a { layers } envelope');

    const dropped = [];
    const layers = raw.map(layer => {
      let ids = layer.nodeIds ?? layer.nodes ?? [];
      ids = ids.map(entry => (entry && typeof entry === 'object' ? entry.id : entry)).filter(Boolean);
      ids = ids.map(id => (KNOWN_PREFIX.test(id) ? id : idByPath.get(id) ?? `file:${id}`));
      const kept = [];
      for (const id of ids) (nodeIds.has(id) ? kept : dropped).push(id);
      return {
        id: layer.id || `layer:${kebab(layer.name)}`,
        name: layer.name,
        description: layer.description,
        nodeIds: [...new Set(kept)],
      };
    });

    const owner = new Map();
    const multi = [];
    for (const layer of layers) {
      for (const id of layer.nodeIds) {
        if (owner.has(id)) multi.push(`${id} (${owner.get(id)} + ${layer.id})`);
        else owner.set(id, layer.id);
      }
    }
    const unassigned = fileNodes.filter(node => !owner.has(node.id)).map(node => node.id);
    const incomplete = layers.filter(layer => !layer.id || !layer.name || !layer.description || !Array.isArray(layer.nodeIds));
    writeJson(`${INTER}/layers.normalized.json`, layers);
    for (const layer of layers) console.log(`${String(layer.nodeIds.length).padStart(4)}  ${layer.id}  —  ${layer.name}`);
    console.log(`layers ${layers.length} | assigned ${owner.size}/${fileNodes.length} | unassigned ${unassigned.length} | multi-assigned ${multi.length} | dangling dropped ${dropped.length} | incomplete layers ${incomplete.length}`);
    if (unassigned.length) console.log('unassigned:', unassigned.join(', '));
    if (multi.length) console.log('multi-assigned:', multi.join(', '));
    if (dropped.length) console.log('dropped:', dropped.join(', '));
  },

  // Input for the container-namer: layers with at least 8 file-level nodes.
  'prep-containers'() {
    const { fileNodes, fileEdges } = loadGraph();
    const nodeById = new Map(fileNodes.map(node => [node.id, node]));
    const layers = loadLayers();
    const big = layers.filter(layer => layer.nodeIds.filter(id => nodeById.has(id)).length >= 8);
    const layerOf = new Map();
    for (const layer of big) for (const id of layer.nodeIds) layerOf.set(id, layer.id);
    writeJson(`${TMP}/ua-container-input.json`, {
      layers: big.map(layer => ({
        id: layer.id,
        name: layer.name,
        description: layer.description,
        nodes: layer.nodeIds.filter(id => nodeById.has(id)).map(id => {
          const { name, filePath, summary, tags } = nodeById.get(id);
          return { id, name, filePath, summary, tags };
        }),
      })),
      edges: fileEdges
        .filter(edge => layerOf.has(edge.source) && layerOf.get(edge.source) === layerOf.get(edge.target))
        .map(({ source, target, type }) => ({ source, target, type })),
    });
    console.log(`layers with >= 8 file nodes: ${big.length}/${layers.length} — ${big.map(layer => `${layer.id}(${layer.nodeIds.length})`).join(', ')}`);
  },

  // Attach validated container labels to the normalized layers.
  'apply-containers'() {
    const { graph } = loadGraph();
    const nodeIds = new Set(graph.nodes.map(node => node.id));
    const layers = loadLayers();
    let labelsDoc;
    try {
      labelsDoc = readJson(`${INTER}/container-labels.json`);
    } catch (error) {
      console.log(`container labels unavailable (${error.message}) — keeping default cluster names`);
      return;
    }
    if (!Array.isArray(labelsDoc)) { console.log('container labels file is not an array — skipped'); return; }
    let applied = 0;
    for (const entry of labelsDoc) {
      const layer = layers.find(candidate => candidate.id === entry.layerId);
      if (!layer || !Array.isArray(entry.labels)) continue;
      const members = new Set(layer.nodeIds);
      const labels = entry.labels
        .map(label => ({ name: String(label.name ?? '').trim(), nodeIds: (label.nodeIds ?? []).filter(id => nodeIds.has(id) && members.has(id)) }))
        .filter(label => label.name && label.nodeIds.length >= 2);
      if (labels.length) { layer.containerLabels = labels; applied += labels.length; }
      console.log(`${layer.id}: ${labels.map(label => `${label.name}(${label.nodeIds.length})`).join(', ') || '(no labels)'}`);
    }
    writeJson(`${INTER}/layers.normalized.json`, layers);
    console.log(`container labels applied: ${applied}`);
  },

  // Input file for the tour-builder's topology script.
  'prep-tour'() {
    const { fileNodes, fileEdges } = loadGraph();
    const layers = loadLayers();
    writeJson(`${TMP}/ua-tour-input.json`, {
      nodes: fileNodes.map(({ id, type, name, filePath, summary }) => ({ id, type, name, filePath, summary })),
      edges: fileEdges.map(({ source, target, type }) => ({ source, target, type })),
      layers: layers.map(({ id, name, description }) => ({ id, name, description })),
    });
    console.log(`nodes ${fileNodes.length} | edges ${fileEdges.length} | layers ${layers.length}`);
  },

  // Tour-covered files split into lesson-writer batches of at most 12.
  'prep-lessons'() {
    const { graph } = loadGraph();
    const nodeById = new Map(graph.nodes.map(node => [node.id, node]));
    const tour = readJson(`${INTER}/tour.normalized.json`);
    const anchor = new Map();
    for (const step of tour) {
      for (const id of step.nodeIds) {
        const node = nodeById.get(id);
        const filePath = node && FILE_LEVEL_TYPES.has(node.type) ? node.filePath : null;
        if (!filePath) continue;
        if (!anchor.has(filePath) || step.order < anchor.get(filePath)) anchor.set(filePath, step.order);
      }
    }
    const detailed = [...anchor].map(([filePath, tourOrder]) => ({ filePath, tourOrder }))
      .sort((a, b) => a.tourOrder - b.tourOrder || (a.filePath < b.filePath ? -1 : 1));
    const batchList = [];
    for (let i = 0; i < detailed.length; i += 12) batchList.push(detailed.slice(i, i + 12));
    for (const name of fs.readdirSync(INTER)) {
      if (/^code-lessons(\.detailed\.\d+|\.normalized)?\.json$/.test(name)) fs.rmSync(`${INTER}/${name}`);
    }
    writeJson(`${TMP}/ua-lesson-batches.json`, { tour, batches: batchList });
    console.log(`tour steps ${tour.length} | detailed files ${detailed.length} | lesson batches ${batchList.length}`);
    batchList.forEach((list, index) => console.log(`  batch ${index + 1}: ${list.map(file => `${file.filePath}@${file.tourOrder}`).join(', ')}`));
  },

  // The eight lesson normalization steps from the skill; stages the result.
  'normalize-lessons'() {
    const { fileNodes } = loadGraph();
    const nodeByPath = new Map(fileNodes.map(node => [node.filePath, node]));
    const { tour, batches: batchList } = readJson(`${TMP}/ua-lesson-batches.json`);
    const detailedPaths = new Set(batchList.flat().map(file => file.filePath));
    const tourOrders = new Set(tour.map(step => step.order));
    const sources = batchList.length === 1
      ? ['code-lessons.json']
      : batchList.map((_, index) => `code-lessons.detailed.${index + 1}.json`);

    const warnings = [];
    const files = {};
    for (const name of sources) {
      const doc = readJson(`${INTER}/${name}`);
      const entries = doc && typeof doc.files === 'object' && doc.files !== null ? doc.files : doc;
      for (const [key, value] of Object.entries(entries)) {
        if (key === 'version' || key === 'generatedAt') continue;
        if (!nodeByPath.has(key)) { warnings.push(`${name}: unknown file dropped: ${key}`); continue; }
        if (key in files) { warnings.push(`${name}: duplicate entry ignored: ${key}`); continue; }
        files[key] = value;
      }
    }
    for (const node of fileNodes) {
      if (!detailedPaths.has(node.filePath) && !(node.filePath in files)) {
        files[node.filePath] = { overview: node.summary, sections: [] };
      }
    }
    let detailed = 0;
    let droppedSections = 0;
    for (const [filePath, entry] of Object.entries(files)) {
      const sections = Array.isArray(entry.sections) ? entry.sections : [];
      const valid = sections.filter(section => {
        const range = section && section.lineRange;
        return Array.isArray(range) && range.length === 2 && Number.isInteger(range[0]) && Number.isInteger(range[1]) && range[0] >= 1 && range[0] <= range[1];
      });
      droppedSections += sections.length - valid.length;
      entry.sections = valid.slice(0, 10);
      for (const section of entry.sections) {
        if ('tourOrder' in section && !tourOrders.has(section.tourOrder)) delete section.tourOrder;
      }
      if ('tourOrder' in entry && !tourOrders.has(entry.tourOrder)) delete entry.tourOrder;
      if (typeof entry.overview !== 'string' || !entry.overview.trim()) entry.overview = nodeByPath.get(filePath).summary;
      if (entry.sections.length) detailed++;
    }
    const missingDetailed = [...detailedPaths].filter(path => !(path in files) || !files[path].sections.length);
    writeJson(`${INTER}/code-lessons.normalized.json`, { version: 1, generatedAt: new Date().toISOString(), files });
    console.log(`lesson entries ${Object.keys(files).length} | detailed ${detailed} | overview-only ${Object.keys(files).length - detailed} | invalid sections dropped ${droppedSections}`);
    if (missingDetailed.length) console.log('detailed files without sections:', missingDetailed.join(', '));
    warnings.forEach(warning => console.log('warning:', warning));
  },

  // Full KnowledgeGraph object for validation and saving.
  'assemble-final'() {
    const { graph } = loadGraph();
    const scan = readJson(`${INTER}/scan-result.json`);
    const final = {
      version: '1.0.0',
      project: {
        name: scan.name,
        languages: scan.languages,
        frameworks: scan.frameworks,
        description: scan.description,
        analyzedAt: new Date().toISOString(),
        gitCommitHash: process.argv[3],
      },
      nodes: graph.nodes,
      edges: graph.edges,
      layers: loadLayers(),
      tour: readJson(`${INTER}/tour.normalized.json`),
    };
    if (!/^[0-9a-f]{40}$/.test(final.project.gitCommitHash || '')) throw new Error('pass the commit hash as the second argument');
    writeJson(`${INTER}/assembled-graph.json`, final);
    console.log(`nodes ${final.nodes.length} | edges ${final.edges.length} | layers ${final.layers.length} | tour ${final.tour.length}`);
  },
};

const command = commands[process.argv[2]];
if (!command) {
  console.error(`Usage: node graph-tools.cjs <${Object.keys(commands).join('|')}>`);
  process.exit(1);
}
command();
