# DX11 light and directional-shadow integration

## Scope and changed files

All changes are inside `Engine/DX11Renderer`. The public DX11Renderer interface,
RenderScene / RenderLight / RenderItem contracts, GPU structure layouts and existing
constant-buffer slots are unchanged. No engine-side light collection is added.

- `DX11Renderer.cpp`: test light source, GPU packing, shadow settings/resources,
  light-view construction, ShadowPass, and explicit restoration of camera targets.
  Object/material/geometry submission is shared by the shadow and main draws.
- `Pipeline/DX11PipelineState.h/.cpp`: optional shadow shaders/input layouts,
  depth-only/alpha-masked selections, and an internal visibility-debug selection.
- `Shader/Mesh.hlsl`: main mesh vertex/pixel entry points.
- `Shader/Common/`: shared constants, mesh/fullscreen types, surface normals,
  material resources/alpha handling, and direct lighting.
- `Shader/Shadow/`: shadow constants/resources, comparison/PCF, PCSS, and
  shadow-depth entry points.
- `Shader/Debug/`: mesh debug entry points and camera/shadow depth visualization.
- `Shader/FinalOutput.hlsl` and `Shader/Skybox.hlsl`: final color output and skybox.
  Existing entry-point names, register slots and binary layouts are retained.
- `Pipeline/DX11ShaderCompiler.cpp`: resolves nested includes relative to their
  parent files while preserving Unicode paths.
- `Tests/ShadowValidation.cpp` and `Tests/ValidateShadows.ps1`: standalone WARP
  integration tests and a runner; neither is compiled into the renderer library.
- `LightIntegration.md`: implementation, settings, limitations and verification.

## Existing architecture confirmed

There is no separate RenderFrame, MainPass class, DepthTarget wrapper or general
RenderPass system in this checkout. DrawScene consumes RenderScene synchronously,
sets camera/scene constants, and draws opaque, masked and sorted transparent items.
DrawMesh resolves cached GPU mesh/material resources and submits a SubMesh using
DrawIndexed(indexCount, startIndex, baseVertex). Position-only and Static vertices
are both supported. RenderItem already carries world, submeshIndex and castShadow.

The renderer owns its camera DepthTexture / DepthStencilView and SceneColor
texture directly. Camera depth uses D24/S8 (typeless with a debug SRV in Debug /
Release). SceneColor goes through the existing fullscreen FinalOutput into the
sRGB back buffer. These resources and debug modes remain distinct from shadow depth.

Matrix4 and shader compilation use row-major storage, left-handed transforms,
row-vector multiplication, and ordinary D3D z in [0,1] with LESS depth testing.
CPU matrices are uploaded without transpose. The object normal matrix remains a
mathematical inverse-transpose; that is unrelated to upload packing.

## Frame / scene flow

1. BeginFrame clears camera targets, uploads fresh light data, and invalidates all
   shadow views. Legacy direct DrawMesh calls retain lighting without inheriting
   a map from the previous frame.
2. DrawScene obtains one SelectLights(scene) result and passes it to UploadLights.
3. BuildLightConstants validates/packs the same lights used for MainPass. The first
   accepted Directional light (packed index 0) is the single shadow owner. Its
   existing RenderLight.castShadow flag must be true; if false, no other light is
   silently substituted. The test Directional light explicitly enables this flag.
4. If enabled, the optional shadow pipeline/resources and frustum are validated.
   The packed light direction supplies the light view; no second light direction
   or hardcoded shadow light is maintained.
5. Fresh LightBuffer and ShadowBuffer data are uploaded in full. Exactly that
   Directional light gets ShadowViewOffset=0 / ShadowViewCount=1. Every other light
   keeps UINT32_MAX / 0. Disabled/failing/empty inputs leave all metadata inactive.
6. ShadowPass unbinds PS t1, binds the shadow DSV without a color RTV, clears depth
   to 1, selects the shadow viewport, and draws the scene's shadow-casting items.
   Opaque uses no PS. Masked uses the same texture alpha, BaseColor.a and AlphaCutoff
   as MainPass. Transparent is excluded from casting. Item.castShadow is honored.
7. ShadowPass restores the camera RTV/DSV, viewport, topology and default pipeline.
   Camera view constants were never overwritten. Main draws rebind all their own
   object/material/lighting/shadow resources and shader variants.
8. MainPass retains opaque/masked/transparent ordering and alpha handling. Existing
   FinalOutput and CameraDepth processing follow as before.

Each DrawScene, including an empty scene or a second scene in one frame, rebuilds
its own shadow map. Statistics include shadow draws/triangles; DrawScene GPU time
now includes both ShadowPass and MainPass. No separate timing/UI system was added.

## Shadow map and binding slots

The map is a single 4096x4096 Texture2D by default, one mip, SampleCount=1:

| Resource | Format / slot |
| --- | --- |
| Texture storage | DXGI_FORMAT_R32_TYPELESS, DEPTH_STENCIL + SHADER_RESOURCE |
| Writable view | DXGI_FORMAT_D32_FLOAT DSV, no color target |
| Readable view | DXGI_FORMAT_R32_FLOAT SRV, PS t1 |
| Comparison sampler | PS s1, LESS_EQUAL, white border |
| Shadow constants | Existing 5136-byte ShadowBuffer, VS/PS b5 |
| Light constants | Existing 4240-byte LightBuffer, PS b3 |
| Object / Material / View / Scene | Existing b0 / b1 / b2 / b4 |
| BaseColor texture / sampler | Existing t0 / s0 |

The existing ShadowViews[0] stores LightViewProjection, DepthBias, MapIndex=0 and
near/far planes. Per-light view metadata identifies the selected Directional light,
so no duplicate shadow-light index buffer or new GPU contract is needed.

A complete resource set is published only after all creation calls succeed.
Resolution/PCF changes recreate it; camera resizing leaves its resolution alone.
ComPtr ownership and context ClearState handle release. A failed request is logged
once and retried after resolution/PCF changes or renderer reinitialization. The old
set is retained safely but is not used for the failed configuration. MainPass
continues with visibility 1. A fallback comparison sampler keeps s1 valid even
before the first map allocation or while shadows are disabled.

## Light view and shader comparison

BuildDirectionalShadowView is separate from light generation and draw submission.
It places the eye at `center - normalizedDirection * lightDistance`, looks along
the light's world travel direction using XMMatrixLookToLH, and chooses Z-up when
that direction is nearly parallel to Y-up. It uses XMMatrixOrthographicLH and
stores `LightView * LightProjection` without transpose.

VSShadow computes `mul(mul(position, World), LightViewProjection)`. MainPass already
interpolates world position. Its visibility helper computes light clip coordinates,
divides xyz by w, maps XY to UV using `(0.5, -0.5) + 0.5`, and uses NDC z directly.
Coordinates outside XY [-1,1], z [0,1], or with invalid forward w return visibility 1.

SampleCmpLevelZero compares `currentDepth - DepthBias <= storedDepth`. Thus a
fragment is occluded when currentDepth exceeds storedDepth + bias. PCSS uses the
existing comparison sampler for its variable-radius final PCF; the fixed 3x3 PCF
remains selectable. Disabling PCF selects a single point comparison. The ShadowPass
also uses slope-scaled rasterizer bias.

Visibility multiplies only the owning Directional light's diffuse contribution.
Other Directional lights, Point lights and Ambient remain independent. Transparent
surfaces can receive shadows through their existing main shader/blending path.
There is currently no specular/emissive shader term to change. Fog fields/uploads
are preserved; fog shading was not implemented before this work and is still absent.

## Renderer-local settings and debug views

The renderer-local defaults live in DirectionalShadowSettings. In debug builds,
RendererDebugSettings also exposes shadow controls through the existing input path.

| Parameter | Default | Meaning |
| --- | --- | --- |
| enabled | true | Shadow rendering switch |
| resolution | 8192 | Square map; 1..16384 supported |
| pcf | true | PCSS by default; false = point comparison |
| shadowDistance | 25 | Camera-frustum distance fitted into the shadow projection |
| center | (0,0,0) | Legacy field; fitted projection does not use it |
| width / height | 40 / 40 | Legacy fields; fitted projection does not use them |
| lightDistance | 80 | Offset upstream from the fitted sphere |
| nearPlane / farPlane | 0.1 / 200 | Near plane and base far value; actual far adds twice the fitted radius |
| lightAngularRadius | 0.02 rad | Virtual directional-light angular radius |
| blockerSearchRadiusWorld | 0.125 | World-space blocker search radius; diagnostic options 0.125 / 0.25 / 0.4 / 0.5 / 0.75 / 1 / 1.5 |
| maxFilterRadiusWorld | 0.4 / 1.5 (about 0.267) | Base world-space penumbra cap; final taps are also clamped to the current blocker search radius |
| pcfTapSpacingScale | 1 | Diagnostic multiplier on final PCF offsets only; options 0.25 / 0.5 / 1 / 1.2 / 1.4 / 1.5 / 1.6 / 1.8 / 2 / 3 / 4 |
| pcfSampleCount | 64 | Final PCSS comparison taps |
| blockerSampleCount | 128 | Raw-depth blocker search taps; options 8 / 16 / 25 / 32 / 64 / 128 |
| shadowCullMode | Back | Cull back faces in the shadow pass |
| shadowSlopeBias | 2.0 | Slope-scaled rasterizer depth bias |
| depthBias | 0.0003 | Receiver offset in normalized light depth |

Ctrl+F1 cycles shadow culling None / Front / Back (the names indicate which face is
culled). Ctrl+F2 applies slope bias 0 / 0.5 / 1 / 2 / 3 / 4 / 5 / 6 / 7 / 8. Ctrl+F3 cycles receiver bias
0 / 0.0001 / 0.0003 / 0.0006 / 0.001. Ctrl+F4 toggles PCF. F10 resets all renderer
debug settings. Because the Engine/Source control still stores only the original
0 / 0.5 / 1 / 2 cycle, its window title shows that stored value for slope bias;
the `Shadow` debug log reports the actual DX11 rasterizer bias through 8. F10
resets the backend override. Resolution stays
at the configured value; PCF toggling switches samplers without rebuilding the map.

MapDepth uses the existing fullscreen depth display with [0,1] range. Visibility
shows white=lit and black=occluded. The shadow visibility and PCSS radius shader
views are active only with CANAPE_RENDER_DEBUG; existing CameraDepth, WorldNormal,
UV, MipLOD and wireframe controls remain available.

### PCSS sample-density experiment

The final PCF tap count is 64 by default and can be set to 8, 16, 25, 32, 64,
or 128 without recompiling shaders. `DX11Renderer::SetPcssPcfSampleCount` also
accepts those values at runtime. Blocker search starts at 128 raw-depth taps. The
default 64-tap mode uses the Vogel pattern. The original 25-tap pattern remains
available when selecting 25 taps. The Vogel pattern fills the same filter disk
at every supported count. Neither pattern changes the
penumbra radius or blocker search.

With the game window focused, Ctrl+Alt+PageDown/Up decreases/increases the
blocker search count through 8 / 16 / 25 / 32 / 64 / 128. The 25-tap
search keeps its original disk offsets; other counts use a fixed Vogel disk
with a center tap. This control does not change the final PCF count, search
radius, or receiver bias. The `Shadow` debug log reports the active count.

With the game window focused, Ctrl+PageDown/Up decreases/increases the final
PCF count one step per press. At 25 taps, Ctrl+Shift+PageUp switches between the
original and Vogel patterns. Ctrl+Shift+PageDown toggles the filter-radius debug
view; pressing it again returns to the engine-selected view. The renderer debug
log reports the active count and pattern on change. These Page keys avoid the
Application's unmodified F5-F8 debug-log shortcuts. Ctrl+Shift+P still switches
PCSS and the fixed 3x3 PCF; `shadowPcf=false` still selects one comparison.

### PCSS bias and footprint diagnostics

Ctrl+Shift+Home/End decreases/increases the final PCF tap spacing multiplier.
This multiplies the calculated filter radius only at the final comparison taps;
the blocker search, estimated depth gap, angular radius, sample count, and base
penumbra radius remain fixed. Ctrl+Alt+Home/End independently changes the
world-space blocker search radius. Each key chord advances once per press and
reports its actual value in the `Shadow` debug log. The existing radius debug
view (Ctrl+Shift+PageDown) reports the effective final tap footprint in texels,
including the spacing multiplier. Final tap offsets are clamped to the current
blocker search radius, including when either debug control changes. The configured
defaults are 1x spacing and a 0.125-world blocker search radius, which also
limits the final tap radius to 0.125 world units, subject to the shader
minimum search radius of two shadow texels.
On startup and whenever shadow distance, tap
spacing, or search radius changes, the log also reports world units per shadow
texel, blocker search radius in texels, and the maximum final tap radius in
texels for the fitted projection.

For the overlapping-silhouette experiment, hold camera, light, receiver bias,
and tap count fixed. First compare 1.2x / 1.5x / 1.8x tap spacing in a penumbra
below the search-radius clamp. If ghost positions move with spacing while the base
radius remains fixed, sparse final comparisons are visible. Then restore 1x
and compare 0.125 / 0.25 / 0.4 blocker search radius.
If contact softening or isolated dark patches change with search radius, nearby
geometry or receiver self-depth enters the blocker average. Ctrl+F2 changes the
depth stored in the shadow map, so it affects both blocker classification and
final comparison; Ctrl+F3 changes the receiver comparison threshold. Neither
is a blocker-only bias control.

The radius debug view is grayscale: shader gray is `log2(1 + radiusTexels) / 10`,
with black for no blocker and white at 1023 texels or more. For a non-square map,
it displays the larger of the X/Y radii. F3 enables the existing GPU timings in
the window title; compare Scene milliseconds after queries settle in the same
scene and camera. Scene timing includes both the shadow pass and mesh lighting.

## Reproducible verification

From the repository root in PowerShell:

```powershell
& ./Engine/DX11Renderer/Tests/ValidateShadows.ps1
```

The runner discovers Visual Studio with vswhere, builds DX11Renderer Debug, compiles
14 FXC shader variants with /WX /Zpr, and runs a hidden-window D3D11 WARP device with
the debug layer. It uses /Z7 for the installed PDB-manager C1902 issue, without
changing project settings. Visual Studio C++ tools, Windows SDK FXC, and the D3D11
debug layer (Windows Graphics Tools) are required. Outputs/readbacks are placed
under ignored Intermediate/ShadowValidation; the expected invalid-resolution test
prints one fallback log but exits successfully.

Executed successfully:

- DX11Renderer x64 Debug, Release and Shipping builds (/Z7 environment override).
  The existing Impl alignment/padding warning C4324 remains; /Z7 also emits an
  override warning. No project build settings were changed.
- All 14 shader variants, including both vertex formats, masked shadow and existing
  debug/final-output entry points, compiled with warnings treated as errors.
- GPU depth readback confirmed real caster geometry (minimum depth 0.36736).
  The same receiver changed from RGB (255,239,233) without shadows to
  (172,135,110) with the actual test Directional light. Unoccluded pixels stayed lit.
- Static receiver with nonzero SubMesh startIndex/baseVertex, position-only caster,
  translated world transform, outside-frustum visibility, and Y-parallel direction.
- Masked transparent/opaque halves produced depths 0.404561 / 0.390958; transparent
  materials and castShadow=false items were excluded from casting.
- Only the selected Directional light darkened: a second Directional, Point and
  Ambient contribution remained unchanged. Zero lights, Point-only input, disabled
  shadows, invalid frustum/resolution, and resource recreation retained MainPass.
- Hardware PCF produced intermediate visibility; point comparison remained binary.
- Camera depth bytes were identical with/without shadows. Existing debug modes,
  wireframe, empty-scene reset, and camera resize/restore executed successfully.
- The D3D11 debug info queue contained zero warnings/errors, including SRV/DSV hazards.
- File scope, original UTF-8/BOM state, CRLF and git diff --check were checked.

Not established by these tests: hardware-GPU/driver coverage, manual inspection of
all imported Game scenes, real device-loss/out-of-memory injection, or final tuning
of acne/peter-panning across arbitrary scene scales. Full Game build/link validation
was attempted separately but did not complete in this environment; it is not reported
as passing. Renderer builds and the standalone GPU integration test did complete.

## Engine connection and current limits

CreateTestLights remains the only temporary light-value source. To switch to engine
lights later, replace the SelectLights body with `return scene.lights;`. The same
accepted Directional light will then drive both shadow and lighting. Set its existing
castShadow flag and submit caster RenderItems with castShadow=true. Empty engine
arrays are valid; do not add a fallback to test lights. No new engine data is needed
for this single-light implementation.

Only DrawScene supplies a complete caster list. Standalone DrawMesh calls do not
collect/replay geometry into a new map; BeginFrame disables stale shadow state for
that legacy path. At most the first accepted Directional light casts shadows. There
are no cascades, Point/Spot shadows or transparent shadow casting. Spot lighting itself
also remains deferred. Fixed coverage can clip distant/moving content; outside pixels
are deliberately fully visible. Bias is configurable and scale-dependent.

Useful next work: fit BuildDirectionalShadowView to scene/camera bounds, snap the
projection to shadow texels, tune slope/normal-aware bias if needed, then consider a
larger PCF kernel or cascades. Basic hardware PCF is already implemented here.
