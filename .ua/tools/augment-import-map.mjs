#!/usr/bin/env node
// Adds the #include edges the bundled resolver cannot see: MSBuild
// AdditionalIncludeDirectories for C/C++ and shader-root lookup for HLSL.

import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import { join, posix } from 'node:path';

const USAGE =
  'Usage: node augment-import-map.mjs <projectRoot> ' +
  '[--scan-result=<path>] [--report=<path>] [--dry-run]';

const CPP_LANGUAGES = new Set(['c', 'cpp']);
const SHADER_LANGUAGES = new Set(['hlsl', 'hlsli']);
const INCLUDE_RE = /^[ \t]*#[ \t]*include[ \t]*(?:"([^"\r\n]+)"|<([^>\r\n]+)>)/gm;
const BLOCK_COMMENT_RE = /\/\*[\s\S]*?\*\//g;
const PROPERTY_GROUP_RE = /<PropertyGroup\b[^>]*>([\s\S]*?)<\/PropertyGroup>/g;
const PROPERTY_RE = /<([A-Za-z_][\w.]*)(?:\s[^>]*)?>([^<]*)<\/\1>/g;
const INCLUDE_DIRS_RE = /<AdditionalIncludeDirectories>([^<]*)<\/AdditionalIncludeDirectories>/g;
const MACRO_RE = /\$\(([^)]+)\)/g;

function parseArgs(argv) {
  const options = { projectRoot: null, scanResult: null, report: null, dryRun: false };
  for (const arg of argv) {
    if (arg === '--dry-run') options.dryRun = true;
    else if (arg.startsWith('--scan-result=')) options.scanResult = arg.slice('--scan-result='.length);
    else if (arg.startsWith('--report=')) options.report = arg.slice('--report='.length);
    else if (arg.startsWith('--')) throw new Error(`unknown option: ${arg}`);
    else if (!options.projectRoot) options.projectRoot = arg;
    else throw new Error(`unexpected argument: ${arg}`);
  }
  if (!options.projectRoot) throw new Error('projectRoot is required');
  return options;
}

function dirOf(path) {
  const index = path.lastIndexOf('/');
  return index < 0 ? '' : path.slice(0, index);
}

function compareCodeUnits(a, b) {
  return a < b ? -1 : a > b ? 1 : 0;
}

// Returns a project-relative posix path, or null when it leaves the project.
function joinInside(base, relative) {
  const joined = posix.normalize(base ? `${base}/${relative}` : relative);
  if (joined === '.' || joined.startsWith('../') || joined === '..' || joined.startsWith('/')) return null;
  return joined;
}

function toRelativeDir(value) {
  const cleaned = value.replace(/\\/g, '/').trim();
  if (!cleaned || /^[A-Za-z]:/.test(cleaned) || cleaned.startsWith('/')) return null;
  const normalized = posix.normalize(cleaned).replace(/\/+$/, '');
  if (normalized.startsWith('..')) return null;
  return normalized === '.' ? '' : normalized;
}

function expandMacros(value, properties) {
  let unresolved = false;
  let current = value;
  for (let depth = 0; depth < 8 && MACRO_RE.test(current); depth++) {
    MACRO_RE.lastIndex = 0;
    current = current.replace(MACRO_RE, (_, name) => {
      if (properties.has(name)) return properties.get(name);
      unresolved = true;
      return '';
    });
  }
  MACRO_RE.lastIndex = 0;
  return unresolved ? null : current;
}

function readProperties(text, fileDir, inherited) {
  const properties = new Map(inherited);
  properties.set('MSBuildThisFileDirectory', fileDir ? `${fileDir}/` : './');
  for (const group of text.matchAll(PROPERTY_GROUP_RE)) {
    for (const [, name, raw] of group[1].matchAll(PROPERTY_RE)) {
      const expanded = expandMacros(raw, properties);
      if (expanded !== null) properties.set(name, expanded);
    }
  }
  return properties;
}

function readIncludeRoots(projectRoot, projectFiles) {
  const rootPropsPath = join(projectRoot, 'Directory.Build.props');
  const shared = existsSync(rootPropsPath)
    ? readProperties(readFileSync(rootPropsPath, 'utf-8'), '', new Map())
    : new Map();
  shared.delete('MSBuildThisFileDirectory');

  const projects = [];
  for (const projectFile of projectFiles) {
    const projectDir = dirOf(projectFile);
    const text = readFileSync(join(projectRoot, projectFile), 'utf-8');
    const builtins = new Map(shared);
    builtins.set('ProjectDir', projectDir ? `${projectDir}/` : './');
    builtins.set('MSBuildProjectDirectory', projectDir || '.');
    builtins.set('SolutionDir', './');
    const properties = readProperties(text, projectDir, builtins);

    const roots = [];
    for (const [, list] of text.matchAll(INCLUDE_DIRS_RE)) {
      for (const entry of list.split(';')) {
        if (!entry.trim() || entry.includes('%(')) continue;
        const expanded = expandMacros(entry, properties);
        const root = expanded === null ? null : toRelativeDir(expanded);
        if (root !== null && !roots.includes(root)) roots.push(root);
      }
    }
    projects.push({ file: projectFile, dir: projectDir, roots });
  }
  projects.sort((a, b) => b.dir.length - a.dir.length || compareCodeUnits(a.dir, b.dir));
  return projects;
}

function parseIncludes(source) {
  const includes = [];
  const text = source.charCodeAt(0) === 0xfeff ? source.slice(1) : source;
  for (const match of text.replace(BLOCK_COMMENT_RE, '').matchAll(INCLUDE_RE)) {
    const quoted = match[1] !== undefined;
    includes.push({ target: (quoted ? match[1] : match[2]).replace(/\\/g, '/').trim(), quoted });
  }
  return includes;
}

function commonPrefixLength(a, b) {
  const left = a.split('/');
  const right = b.split('/');
  let length = 0;
  while (length < left.length && length < right.length && left[length] === right[length]) length++;
  return length;
}

function main() {
  const options = parseArgs(process.argv.slice(2));
  const scanPath = options.scanResult ?? join(options.projectRoot, '.ua', 'intermediate', 'scan-result.json');
  const reportPath = options.report ?? join(options.projectRoot, '.ua', 'tmp', 'import-augment-report.json');
  const scan = JSON.parse(readFileSync(scanPath, 'utf-8'));

  const byLowerPath = new Map(scan.files.map(file => [file.path.toLowerCase(), file.path]));
  const lookup = candidate => (candidate === null ? null : byLowerPath.get(candidate.toLowerCase()) ?? null);

  const projects = readIncludeRoots(
    options.projectRoot,
    scan.files.filter(file => file.path.endsWith('.vcxproj')).map(file => file.path),
  );
  const fallbackRoots = [];
  for (const project of [...projects].sort((a, b) => compareCodeUnits(a.file, b.file))) {
    for (const root of project.roots) if (!fallbackRoots.includes(root)) fallbackRoots.push(root);
  }
  const projectOf = path =>
    projects.find(project => project.dir === '' || path.startsWith(`${project.dir}/`)) ?? null;

  const shaderFiles = scan.files.filter(file => SHADER_LANGUAGES.has(file.language)).map(file => file.path);
  const shaderDirs = [...new Set(shaderFiles.map(dirOf))];
  const shaderRoots = shaderDirs
    .filter(dir => !shaderDirs.some(other => other !== dir && dir.startsWith(`${other}/`)))
    .sort(compareCodeUnits);

  const report = {
    scanResult: scanPath,
    projects: projects.map(({ file, roots }) => ({ file, roots })),
    fallbackRoots,
    shaderRoots,
    resolvedByKind: {},
    fallbackResolutions: [],
    unresolvedQuotedWithSuffixMatch: [],
    ambiguous: [],
    bundledOnly: [],
    before: {},
    after: {},
  };
  const count = kind => { report.resolvedByKind[kind] = (report.resolvedByKind[kind] ?? 0) + 1; };

  function resolveCpp(importer, include) {
    if (include.quoted) {
      const hit = lookup(joinInside(dirOf(importer), include.target));
      if (hit) return { path: hit, kind: 'cpp-relative' };
    }
    const project = projectOf(importer);
    for (const root of project?.roots ?? []) {
      const hit = lookup(joinInside(root, include.target));
      if (hit) return { path: hit, kind: 'cpp-project-root' };
    }
    const hits = [];
    for (const root of fallbackRoots) {
      const hit = lookup(joinInside(root, include.target));
      if (hit && !hits.includes(hit)) hits.push(hit);
    }
    if (hits.length > 1) report.ambiguous.push({ importer, include: include.target, candidates: hits });
    return hits.length ? { path: hits[0], kind: 'cpp-fallback-root' } : null;
  }

  function resolveShader(importer, include) {
    const relative = lookup(joinInside(dirOf(importer), include.target));
    if (relative) return { path: relative, kind: 'shader-relative' };
    for (const root of shaderRoots) {
      const hit = lookup(joinInside(root, include.target));
      if (hit) return { path: hit, kind: 'shader-root' };
    }
    const suffix = `/${include.target.toLowerCase()}`;
    const candidates = shaderFiles.filter(path => `/${path.toLowerCase()}`.endsWith(suffix));
    if (candidates.length > 1) {
      candidates.sort((a, b) =>
        commonPrefixLength(b, importer) - commonPrefixLength(a, importer) || compareCodeUnits(a, b));
      report.ambiguous.push({ importer, include: include.target, candidates });
    }
    return candidates.length ? { path: candidates[0], kind: 'shader-suffix' } : null;
  }

  const summarize = importMap => {
    const lists = Object.values(importMap);
    return {
      filesWithImports: lists.filter(list => list.length > 0).length,
      totalEdges: lists.reduce((sum, list) => sum + list.length, 0),
    };
  };
  report.before = summarize(scan.importMap);

  for (const file of scan.files) {
    const isCpp = CPP_LANGUAGES.has(file.language);
    if (!isCpp && !SHADER_LANGUAGES.has(file.language)) continue;

    const text = readFileSync(join(options.projectRoot, file.path), 'utf-8');
    const resolved = new Set();
    for (const include of parseIncludes(text)) {
      const result = isCpp ? resolveCpp(file.path, include) : resolveShader(file.path, include);
      if (result && result.path !== file.path) {
        if (!resolved.has(result.path)) {
          count(result.kind);
          if (result.kind === 'cpp-fallback-root' || result.kind === 'shader-suffix') {
            report.fallbackResolutions.push({ importer: file.path, include: include.target, target: result.path });
          }
        }
        resolved.add(result.path);
      } else if (!result && include.quoted) {
        const suffix = `/${include.target.toLowerCase()}`;
        const candidates = scan.files
          .map(entry => entry.path)
          .filter(path => `/${path.toLowerCase()}`.endsWith(suffix));
        if (candidates.length) {
          report.unresolvedQuotedWithSuffixMatch.push({ importer: file.path, include: include.target, candidates });
        }
      }
    }

    const existing = scan.importMap[file.path] ?? [];
    for (const path of existing) {
      if (!resolved.has(path)) report.bundledOnly.push({ importer: file.path, target: path });
    }
    scan.importMap[file.path] = [...new Set([...existing, ...resolved])].sort(compareCodeUnits);
  }
  report.after = summarize(scan.importMap);

  if (!options.dryRun) {
    writeFileSync(scanPath, JSON.stringify(scan, null, 2), 'utf-8');
    writeFileSync(reportPath, JSON.stringify(report, null, 2), 'utf-8');
  }
  process.stdout.write(`${JSON.stringify(report, null, 2)}\n`);
}

try {
  main();
} catch (error) {
  process.stderr.write(`augment-import-map failed: ${error.message}\n${USAGE}\n`);
  process.exit(1);
}
