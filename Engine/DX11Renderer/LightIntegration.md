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
- `Shader/Mesh.hlsl`: shadow depth shaders, comparison/PCF, selected-light shading,
  and visibility debugging; existing main/debug entry points are retained.
- `Shader/Common/Constants.hlsli`: updated usage comments; binary layouts unchanged.
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

The map is a single 2048x2048 Texture2D by default, one mip, SampleCount=1:

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
fragment is occluded when currentDepth exceeds storedDepth + bias. With PCF enabled,
the comparison sampler performs hardware 2x2 filtering; disabling PCF selects a
single point comparison. Rasterizer bias remains zero, avoiding duplicate bias.

Visibility multiplies only the owning Directional light's diffuse contribution.
Other Directional lights, Point lights and Ambient remain independent. Transparent
surfaces can receive shadows through their existing main shader/blending path.
There is currently no specular/emissive shader term to change. Fog fields/uploads
are preserved; fog shading was not implemented before this work and is still absent.

## Renderer-local settings and debug views

Change DirectionalShadowSettings / Impl::ShadowSettings in DX11Renderer.cpp, or
inspect/edit the instance in a debugger. No engine API or new UI is required.

| Parameter | Default | Meaning |
| --- | --- | --- |
| enabled | true | Shadow rendering switch |
| resolution | 2048 | Square map; 1..16384 supported |
| pcf | true | Hardware 2x2 comparison filtering; false = point comparison |
| center | (0,0,0) | Fixed world-space coverage center |
| width / height | 80 / 80 | Orthographic coverage in world units |
| lightDistance | 80 | Distance upstream from center |
| nearPlane / farPlane | 0.1 / 200 | Light-view depth interval |
| depthBias | 0.0005 | Receiver offset in normalized light depth |
| debugView | None | None / MapDepth / Visibility |

MapDepth uses the existing fullscreen depth display with [0,1] range. Visibility
shows white=lit and black=occluded. Both internal debug selections are active only
with CANAPE_RENDER_DEBUG; existing CameraDepth, WorldNormal, UV, MipLOD and wireframe
controls remain available. Existing non-Final debug modes take precedence over the
visibility override, and CameraDepth takes precedence over MapDepth.

## Reproducible verification

From the repository root in PowerShell:

```powershell
& ./Engine/DX11Renderer/Tests/ValidateShadows.ps1
```

The runner discovers Visual Studio with vswhere, builds DX11Renderer Debug, compiles
13 FXC shader variants with /WX /Zpr, and runs a hidden-window D3D11 WARP device with
the debug layer. It uses /Z7 for the installed PDB-manager C1902 issue, without
changing project settings. Visual Studio C++ tools, Windows SDK FXC, and the D3D11
debug layer (Windows Graphics Tools) are required. Outputs/readbacks are placed
under ignored Intermediate/ShadowValidation; the expected invalid-resolution test
prints one fallback log but exits successfully.

Executed successfully:

- DX11Renderer x64 Debug, Release and Shipping builds (/Z7 environment override).
  The existing Impl alignment/padding warning C4324 remains; /Z7 also emits an
  override warning. No project build settings were changed.
- All 13 shader variants, including both vertex formats, masked shadow and existing
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
