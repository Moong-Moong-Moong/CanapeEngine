# Rendering benchmark

`Game/Content/Scenes/Byeongheon/StressTest.json` contains the benchmark camera,
one directional light, and `RenderingBenchmarkController`. Run the Release game
with `Game.exe -scene Byeongheon/StressTest` at its default 1280×720 window size.
The Editor Play button also works, but resizing Scene View changes render resolution.
After building Release x64, double-click `Tools/RunStressTest.cmd` to launch this
scene directly without the Editor. The benchmark requests a visible mouse cursor
for the runtime panel and restores the previous cursor request when it ends.
The expanded standalone build is in `Binaries/x64/Release/StressTest`; the launcher
prefers that folder and falls back to the standard `Release/Game` output.

## Controls

The right-hand runtime panel uses the engine's `UICanvasComponent` / `UIButton`
UI, with no ImGui dependency. It is available in both Editor Play and the game.
All nine benchmark Inspector properties can be changed with the mouse:

| UI control | Inspector property | Values |
| --- | --- | --- |
| Light / Draw Call / Mesh tabs | `mode` | Point Lights / Draw Calls / Polygons |
| Point lights - / + | `pointLevel` | 0 / 4 / 8 / 16 / 24 / 32 / 40 / 48 / 56 / 64 lights |
| Mesh instances - / + in Draw Call mode | `drawLevel` | 100 / 500 / 1,000 / 2,000 / 5,000 / 10,000 / 20,000 / 50,000 / 100,000 |
| Mesh instances - / + in Mesh mode | `polygonInstanceLevel` | Same instance presets, independent of Draw Call mode |
| Submeshes / mesh - / + | `submeshLevel` | 1 / 2 / 4 / 8 |
| Triangles / mesh - / + | `polygonLevel` | 1,024 / 10,000 / 50,000 / 100,000 |
| Light range -1 / -0.1 / +0.1 / +1 | `pointRange` | 0.1 to 30 |
| Light intensity -1 / -0.1 / +0.1 / +1 | `pointIntensity` | 0 to 20 |
| Light layout toggle | `concentrated` | Distributed / Concentrated |

The panel shows actual preset values and their zero-based levels. Buttons stop
at the valid limits. Each mode keeps its settings; editing another mode's fields
prepares its next run. The Mesh instances row edits the active mesh mode's count;
in Light mode it prepares the Draw Call count. Inspector and keyboard changes also update the panel.
Directional shadows can additionally be toggled from the panel. Active workload
or shadow changes restart the two-second measurement warmup.

**Hide all UI** disables the entire benchmark canvas, including the stats and
buttons; press **H** to restore it. This also skips control refresh work while
hidden. The visible runtime UI contributes to the displayed draw/triangle counts.

| Key | Action |
| --- | --- |
| F1 / F2 / F3 | Point Light / Draw Call / Polygon mode |
| Left / Right | Decrease / increase the active mode's zero-based level |
| Up / Down | Increase / decrease Draw Call submesh level (1 / 2 / 4 / 8) |
| Up / Down in Mesh mode | Increase / decrease sphere instance count |
| L | Toggle distributed / concentrated Point Light layout |
| H | Hide / show all benchmark UI |

In Editor Play mode, select **Rendering Benchmark Controller** in Hierarchy to
change `mode`, `pointLevel`, `drawLevel`, `polygonLevel`, `polygonInstanceLevel`, `submeshLevel`,
`pointRange`, `pointIntensity`, and `concentrated` in Inspector. Levels outside
the valid range are clamped. The Point Light count is controlled by `pointLevel`.

| Mode | Level 0 | Level 1 | Level 2 | Level 3 | Level 4 | Level 5 | Level 6 | Level 7 | Level 8 | Level 9 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Point lights | 0 | 4 | 8 | 16 | 24 | 32 | 40 | 48 | 56 | 64 |
| Draw / Polygon mesh instances | 100 | 500 | 1,000 | 2,000 | 5,000 | 10,000 | 20,000 | 50,000 | 100,000 | |
| Polygon triangles per mesh | 1,024 | 10,000 | 50,000 | 100,000 | | | | | | |

Point Light geometry is fixed while the light count, range, intensity, or layout
changes. Light positions depend only on the light index and layout, so existing
lights keep their positions when the count grows. Draw Call mode retains its
original camera-facing 100×50 grid through 5,000 instances. Larger populations
subdivide that grid and shrink each cube to keep all objects within the same
screen area, without reusing grid cells. A fixed permutation spreads each
population across the available cells. Positions remain fixed within a grid
resolution; crossing a subdivision threshold repacks the grid.

Polygon mode starts at the original 100 spheres in a 10×10 camera-facing grid.
It now supports the same instance counts up to 100,000, independently of the
triangle density. Larger counts subdivide and shrink the spheres across the
same area. Changing triangle density alone preserves positions and size. These
are individual GameObjects and render submissions sharing a mesh asset, with
no hardware instancing or batching. At the highest count, Draw Call mode can
submit 800,000 scene submesh draws; Mesh mode can submit 10 billion scene
triangles at 100,000 triangles per sphere. The HUD uses a 64-bit total for mesh
triangles. High-count changes rebuild the objects synchronously; wait for the
rebuild and the following warmup before recording measurements.

The controller disables the scene directional shadow and sets a dim directional
light and skybox ambient/IBL at startup. The directional light's **castShadow**
setting can still be changed in Inspector during Play or with the runtime panel
to make a separate shadow comparison. Point lights do not cast shadows in this renderer. Mode transitions
mark old objects inactive before deferred destruction and create replacement
objects only when settings change. Ignore the two-second warmup shown in the HUD;
large asset loads may warrant waiting longer for stable values.

## Assets

All test models live under `Game/Content/Models/RenderingBenchmark`, following
the existing model directory and `.gitignore` policy. This directory is ignored
by Git, like other game models. Run `python Tools/GenerateBenchmarkMeshes.py`
after a fresh checkout to recreate the seven local glTF/bin pairs. The ordinary
material files are `Game/Content/Materials/M_BenchmarkDark.mat` and
`M_BenchmarkObject.mat`.

The Draw Call test uses `Builtin/Cube` at submesh level 0. The generated
CubeSplit2/4/8 assets have the same 12 triangles and outline, split into 2/4/8
glTF primitives. The current importer copies the vertex array for each primitive,
so the split variants also use more vertex storage; this is a known secondary
difference when comparing submesh costs. The sphere assets each have one
submesh and exactly 1,024/10,000/50,000/100,000 indexed triangles. The HUD
shows the actual index-derived count and its product with placed instances;
with the default 100 instances these correspond to
102,400/1,000,000/5,000,000/10,000,000 scene triangles before other passes.

## Reading the numbers

The HUD refreshes once per second after warmup. FPS is the number of game-loop
frames divided by unscaled elapsed wall time; Frame is its reciprocal in ms.
It includes VSync waiting and is **not** CPU execution time. Present uses
`Present(1, 0)`, so FPS may stay near the display refresh rate until the workload
exceeds the budget. GPU Frame excludes Present/VSync. Its latest available GPU
query can lag behind the CPU frame.

`RenderItems` is the number of valid submesh draws prepared for the scene, before
main/shadow/output passes. It is **not** a count of GameObjects or unique meshes.
`Meshes placed` is the controller's count of live benchmark objects with mesh
renderers. Point Lights requested is the configured count; renderer accepted is
the actual `LocalCount` after validation and the 64-light limit. At zero
intensity the renderer rejects those lights, so the two numbers can differ.

Draw Calls and Triangles count actual pass submissions. They include the final
output triangle, UI, and, when enabled, shadow draws; they should not be read as
the number of mesh instances or unique geometry triangles. Scene GPU time
includes shadow and main scene drawing; Output GPU time covers resolve, final
output, and UI. Neither is a standalone FPS measurement. The displayed MSAA
sample count is the actual scene target count after the renderer's device
fallback (normally 4×, otherwise 1×). GPU timing and these debug stats are
available in Debug and Release builds, not Shipping.

For comparisons, keep the camera/window size, material, lighting, MSAA, and
shadow state fixed within each test. Record the GPU Frame and Scene times along
with FPS, Draw Calls, RenderItems, and Triangles after the warmup. The generated
meshes share each mesh asset across all instances; no Instancing, Batching,
Frustum Culling, VSync toggle, MSAA toggle, CPU profiler, or separate main/shadow
GPU timers are added by this benchmark.
