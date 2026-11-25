// Generates, for every batch in batches.json, the extract-structure input file,
// a batch context file, and the full dispatch prompt for the file-analyzer.
const fs = require('fs');
const { PROJECT_ROOT, UA_DIR, findSkillDir } = require('./paths.cjs');
const { writeClassIndex } = require('./cpp-index.cjs');

const SKILL_DIR = findSkillDir();
const PLATFORM = { win32: 'Windows', darwin: 'macOS', linux: 'Linux' }[process.platform] ?? process.platform;
const SHELL_NOTE = process.platform === 'win32'
  ? 'Run the bundled scripts with the Bash tool (Git Bash, POSIX syntax); forward-slash paths such as `C:/Users/...` work there.'
  : 'Run the bundled scripts with the Bash tool.';
const ENVIRONMENT_LINE = `${PLATFORM}. ${SHELL_NOTE} \`node\` (${process.version}) is on PATH. Do any scripting with \`node\`; \`python\` may not be available.`;

const scan = JSON.parse(fs.readFileSync(`${UA_DIR}/intermediate/scan-result.json`, 'utf8'));
const batches = JSON.parse(fs.readFileSync(`${UA_DIR}/intermediate/batches.json`, 'utf8'));
const description = scan.description.replace(/\s*참고:.*$/, '').trim();

const template = ({ batch, total, fileLines, expectedImportEdgeCount }) => `Analyze these files and produce GraphNode and GraphEdge objects.
Project root: \`${PROJECT_ROOT}\`
Project: \`${scan.name}\`
Languages: \`${scan.languages.join(', ')}\`
Batch: \`${batch.batchIndex}/${total}\`
Skill directory (for bundled scripts): \`${SKILL_DIR}\`
Output: write to \`${UA_DIR}/intermediate/batch-${batch.batchIndex}.json\` (single-file mode) OR \`batch-${batch.batchIndex}-part-<k>.json\` (split mode, per Step B of your output protocol).

## Batch data (generated verbatim from batches.json by the main session — read it, never retype it)

The pre-resolved import data and the neighbor map are too large to inline, so they are on disk:

- \`${UA_DIR}/tmp/ua-batch-context-${batch.batchIndex}.json\` — \`{ batchIndex, totalBatches, projectRoot, files, batchImportData, neighborMap, expectedImportEdgeCount }\`
  - \`files\` — the ${batch.files.length} files of this batch, each with \`path\`, \`language\`, \`sizeLines\`, \`fileCategory\`.
  - \`batchImportData\` — pre-resolved import data for this batch (use directly — do NOT re-resolve imports from source).
  - \`neighborMap\` — cross-batch neighbors with their exported symbols (confidence boost for cross-batch edges).
  - \`expectedImportEdgeCount\` — ${expectedImportEdgeCount}; the sum of \`batchImportData[file].length\`. Your output must contain exactly this many \`imports\` edges.
- \`${UA_DIR}/tmp/ua-file-analyzer-input-${batch.batchIndex}.json\` — the Step 1 input file for \`extract-structure.mjs\`, already written with \`projectRoot\`, \`batchFiles\` (all four fields per entry, verbatim) and \`batchImportData\`. Do NOT recreate it: start at Step 2 and pass it to the script, writing results to \`${UA_DIR}/tmp/ua-file-extract-results-${batch.batchIndex}.json\`.

Files to analyze in this batch (every entry MUST be passed through to \`batchFiles\` with all four fields — \`path\`, \`language\`, \`sizeLines\`, \`fileCategory\`; the pre-written input file already does this):
${fileLines}

## Environment facts (verified by the main session)

- ${ENVIRONMENT_LINE}
- Quote every path in shell commands; one project file name contains a space and Hangul (\`Tools/NewScript 사용법.md\`).
- Write scratch files only under \`${UA_DIR}/tmp/\` with the batch index in the file name, and outputs only under \`${UA_DIR}/intermediate/\`. Never create, modify or delete anything else in the project. Other file-analyzer agents are running concurrently on other batches — touch only files whose names carry your batch index ${batch.batchIndex}.
- Build the output JSON with \`node\` (\`JSON.stringify\`) instead of hand-writing large JSON, then re-read and validate it per Step E.
- Work efficiently: hand-author only what needs judgment — each node's \`summary\`, \`tags\`, \`complexity\`, optional \`languageNotes\`, and the semantic edges (\`calls\`, \`inherits\`, \`implements\`, \`exports\`, \`depends_on\`, \`tested_by\`, \`documents\`, \`configures\`, \`related\`). Generate the mechanical edges in your script: exactly one \`imports\` edge (weight 0.7) per \`batchImportData\` entry, and one \`contains\` edge (weight 1.0) from each file node to every \`function:\` / \`class:\` node you create for it. Keep the authored data in several moderate-size data files (roughly 8-10 source files' worth each) rather than one huge script, so that no single write is enormous; a short assembler script then merges them, splits parts per Steps B-C, validates per Step E and writes the output.

## Project-specific notes

- This is a C++20/23 game engine (Visual Studio/MSBuild + vcpkg). Every \`.h\` file is a C++ header even though the scanner labels it \`c\`; the bundled extractor parses these headers correctly (classes and methods are extracted).
- Known extractor gap (confirmed on this project): \`extract-structure.mjs\` does not report C++ function definitions whose return type is a pointer or a reference — for example \`UIElement* UIElement::FindByName(...)\` or \`Foo& Bar::Get()\`, including such templates defined in headers. It also drops every definition inside a namespace-level \`#if ... #endif\` block (for example \`#if CANAPE_RENDER_DEBUG\`) and variadic function templates. For every \`.cpp\` / \`.h\` in the batch, scan the source for these definitions and add the significant ones (10+ lines, or part of the public interface) as \`function:\` / \`class:\` nodes with \`lineRange\` read from the source, plus their \`contains\` edges.
- Where to hang member functions: create the \`function:\` node under the file that holds the definition (usually the \`.cpp\`). When you emit a cross-batch \`calls\` edge to a method that \`neighborMap\` lists under a header, target the class node instead (\`class:<header path>:<ClassName>\`) unless the method is clearly defined inline in that header — a \`function:<header>:<method>\` node normally does not exist and the edge would be dropped.
- \`.hlsl\` / \`.hlsli\` shader files have no parser: the script reports them under \`filesSkipped\`. Read their source, write the file node, and add \`function:\` nodes for the significant functions (shader entry points and helper functions of 10+ lines) with \`lineRange\` taken from the source.
- \`.ps1\` scripts have no parser either — supplement their functions from the source as your instructions describe for PowerShell.
- \`.vcxproj\`, \`.props\` and \`.slnx\` files are MSBuild / solution XML. Their \`fileCategory\` is \`code\`, so their node type stays \`file\`; describe them as build configuration (tags such as \`build-system\`, \`configuration\`). They may carry \`configures\` / \`depends_on\` edges, but only to nodes that really exist (see the ID rule below).
- \`batchImportData\` was resolved by the main session against the MSBuild include directories (\`Common\`, \`Engine\`, \`Engine/Source\`, \`<Project>/Source\`) and the shader include roots, so it is complete for C++ and HLSL alike. Emit exactly one \`imports\` edge per entry for every file that has entries, shaders included.
- \`*Validation.cpp\` files under \`Tests/\` directories are standalone validation programs driven by the \`Validate*.ps1\` script beside them. Tag them \`test\` and emit \`tested_by\` edges (production -> test) for the production files they exercise.
- File-level node ID prefix for cross-batch targets in this project: \`.md\` files are \`document:<path>\`; \`vcpkg.json\` and \`vcpkg-configuration.json\` are \`config:<path>\`; every other file is \`file:<path>\`. Only target files that appear in this batch, in \`batchImportData\`, or in \`neighborMap\` — with one extension for documentation, script, config and build files, which have no import data: their \`documents\` / \`configures\` / \`triggers\` / \`depends_on\` / \`related\` edges may target any scanned project file. \`${UA_DIR}/tmp/ua-scan-paths.txt\` lists every scanned path (one per line); confirm the path is listed before you emit the edge, and use \`${UA_DIR}/tmp/ua-class-index.json\` to find the file that declares a class a document talks about.
- Node IDs must be unique. When two functions in one file share a name (the same method name on different classes), disambiguate the ID and \`name\` as \`ClassName::method\`; for plain overloads emit a single node.
- Summaries must be grounded in the actual source. The extraction output gives you structure; when it is not enough to write a specific, accurate summary, read the file (for a \`.h\`/\`.cpp\` pair the header is usually the most informative). Do not infer behavior from names alone.

## Quality bar — the main session checks these after you finish

1. **Read before you write.** Open every source file of this batch before writing its nodes: files of up to 400 lines in full; for longer files, the declarations plus the body of every function you create a node for. A summary derived only from a name, a signature or the extractor output is a defect. Your final report must list which files you read in full, which partially, and which you did not read (expected: none).
2. **Every function/class summary states what the code does**, as seen in its body — what it takes, the effect it has, the notable rule or algorithm — not a paraphrase of its name.
3. **Semantic edges are required, not optional.** In addition to the mechanical \`imports\` / \`contains\` edges:
   - \`inherits\`: one per class/struct in the batch that derives from a class declared in this project.
   - \`exports\`: from a file to each of its class/function nodes that is part of its public interface (declared in a header, or defined in a \`.cpp\` and declared in its header).
   - \`calls\`: from a function node in this batch to the project function it calls in another file, for the collaborations that matter to understanding the file (not every call). Target \`function:<defining file>:<name>\` when that node is in your batch or the symbol is listed in \`neighborMap\`; otherwise target the owning class node \`class:<header>:<ClassName>\`.
   - \`depends_on\`: a file that uses a project type without including its header directly.
   - \`tested_by\`: see the validation-program note above.
4. **Resolving targets outside the batch.** \`${UA_DIR}/tmp/ua-class-index.json\` maps every class/struct name declared in the project to the file(s) that declare it. Use it to find the header of a base class or a used type and target \`class:<that file>:<Name>\`. Many game and editor files include only the umbrella header \`Engine/Source/Engine.h\`, which re-exports the engine's public headers; \`neighborMap\` lists no symbols for it, so resolve engine types through the class index instead of skipping the edge.
5. **Verify the written files.** After writing, re-read each output file from disk and run the Step E checks on what is actually there.

**Additional context from main session:**

Project: \`${scan.name}\` — \`${description}\`
Languages: \`${scan.languages.join(', ')}\`

> **Language directive**: Generate all textual content (summaries, descriptions, tags, titles, languageNotes, languageLesson) in **Korean**. Maintain technical accuracy while using natural, native-level phrasing in the target language. Keep technical terms in English when no standard translation exists (e.g., "middleware", "hook", "barrel").

Korean output guidelines: write each \`summary\` as 1-2 Korean sentences describing the file's purpose and role in active voice, without repeating the file name. Tags may mix Korean descriptive tags with English technical terms (lowercase, hyphenated). Keep identifiers, API names and established technical terms in English.

When you finish, respond with ONLY the brief text summary your instructions call for (parts written, total nodes/edges, the number of \`imports\` edges versus the expected ${expectedImportEdgeCount}, and any files skipped or problems hit).
`;

fs.mkdirSync(`${UA_DIR}/tmp`, { recursive: true });
fs.writeFileSync(`${UA_DIR}/tmp/ua-scan-paths.txt`, `${scan.files.map(file => file.path).join('\n')}\n`);
writeClassIndex();
const summary = [];
for (const batch of batches.batches) {
  const batchFiles = batch.files.map(({ path: p, language, sizeLines, fileCategory }) => ({
    path: p, language, sizeLines, fileCategory,
  }));
  const expectedImportEdgeCount = Object.values(batch.batchImportData).reduce((n, list) => n + list.length, 0);

  fs.writeFileSync(
    `${UA_DIR}/tmp/ua-file-analyzer-input-${batch.batchIndex}.json`,
    JSON.stringify({ projectRoot: PROJECT_ROOT, batchFiles, batchImportData: batch.batchImportData }, null, 2),
  );
  fs.writeFileSync(
    `${UA_DIR}/tmp/ua-batch-context-${batch.batchIndex}.json`,
    JSON.stringify({
      batchIndex: batch.batchIndex,
      totalBatches: batches.totalBatches,
      projectRoot: PROJECT_ROOT,
      files: batchFiles,
      batchImportData: batch.batchImportData,
      neighborMap: batch.neighborMap,
      expectedImportEdgeCount,
    }, null, 2),
  );

  const fileLines = batchFiles
    .map((f, i) => `${i + 1}. \`${f.path}\` (${f.sizeLines} lines, language: \`${f.language}\`, fileCategory: \`${f.fileCategory}\`)`)
    .join('\n');
  const prompt = template({ batch, total: batches.totalBatches, fileLines, expectedImportEdgeCount });
  fs.writeFileSync(`${UA_DIR}/tmp/ua-dispatch-${batch.batchIndex}.md`, prompt);
  summary.push(`${batch.batchIndex}: files=${batchFiles.length} imports=${expectedImportEdgeCount} promptBytes=${Buffer.byteLength(prompt)}`);
}
console.log(summary.join('\n'));
console.log('missing fields:', batches.batches.flatMap(b => b.files).filter(f => !f.path || !f.language || typeof f.sizeLines !== 'number' || !f.fileCategory).length);
