# Rendering benchmark

`Game/Content/Scenes/Byeongheon/StressTest.json` contains the benchmark camera,
one directional light, and `RenderingBenchmarkController`. Run the Release game
with `Game.exe -scene Byeongheon/StressTest` at its default 1280×720 window size.
The Editor Play button also works, but resizing Scene View changes render resolution.

## Controls

| Key | Action |
| --- | --- |
| F1 / F2 / F3 | Point Light / Draw Call / Polygon mode |
| Left / Right | Decrease / increase the active mode's zero-based level |
| Up / Down | Decrease / increase Draw Call submesh level (1 / 2 / 4 / 8) |
| L | Toggle distributed / concentrated Point Light layout |
| H | Hide / show the small benchmark HUD |

In Editor Play mode, select **Rendering Benchmark Controller** in Hierarchy to
change `mode`, `pointLevel`, `drawLevel`, `polygonLevel`, `submeshLevel`,
`pointRange`, `pointIntensity`, and `concentrated` in Inspector. Levels outside
the valid range are clamped. The Point Light count is controlled by `pointLevel`.

| Mode | Level 0 | Level 1 | Level 2 | Level 3 | Level 4 | Level 5 | Level 6 | Level 7 | Level 8 | Level 9 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Point lights | 0 | 4 | 8 | 16 | 24 | 32 | 40 | 48 | 56 | 64 |
| Draw mesh instances | 100 | 500 | 1,000 | 2,000 | 5,000 | | | | | |
| Polygon triangles per mesh | 1,024 | 10,000 | 50,000 | 100,000 | | | | | | |

Point Light geometry is fixed while the light count, range, intensity, or layout
changes. Light positions depend only on the light index and layout, so existing
lights keep their positions when the count grows. Draw Call objects occupy a
fixed camera-facing 100×50 grid. A fixed permutation spreads every count stage
across that grid; each existing instance keeps its position. Polygon mode uses
100 spheres in the same fixed 10×10 camera-facing grid at every density.

The controller disables the scene directional shadow and sets a dim directional
light and skybox ambient/IBL at startup. The directional light's **castShadow**
setting can still be changed in Inspector during Play to make a separate shadow
comparison. Point lights do not cast shadows in this renderer. Mode transitions
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
shows the actual index-derived count; with 100 instances these correspond to
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
