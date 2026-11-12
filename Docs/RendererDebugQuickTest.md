# Renderer debug quick test

Build and run **Editor** with **Debug | x64** or **Release | x64**.
Open the **Render Debug** dropdown at the right end of the Scene view toolbar.

| Section | Options |
| --- | --- |
| View | Debug View on/off, View Mode (Final, CameraDepth, WorldNormal, UV, MipLOD, ShadowDepth, ShadowVisibility, PcssFilterRadius), Wireframe, Depth Range (CameraDepth only) |
| Post Processing | Tone Mapping on/off (independent of Debug View) |
| Shadow | Cull Mode, Slope Bias, Receiver Bias, PCF |
| PCSS | PCSS on/off, Shadow Distance, PCF Samples, Legacy 25-tap pattern, Tap Spacing, Blocker Samples, Blocker Search Radius |
| Stats | GPU timing capture, draw calls, triangles, GPU frame/DrawScene/FinalOutput times |

GPU timing results arrive asynchronously; pending/unavailable is shown until a
valid measurement arrives. GPU frame time excludes Present/VSync. Draw/triangle
counts include the fullscreen FinalOutput draw (one triangle).

Quick check:

1. Cycle View Mode and compare the views on the sample meshes.
2. Toggle Wireframe in each view, then toggle it off. In CameraDepth the wire
   pattern appears in the depth visualization rather than as a colored line overlay.
3. Narrow Depth Range when depth appears uniformly white: D3D depth is non-linear
   and the sample camera can place most geometry near 1.
4. In MipLOD, textured meshes should show LOD colors; meshes using the fallback
   white texture remain white. UV needs a mesh with UV attributes.
5. Enable GPU Timings, wait a few frames, and check that timing values appear.
6. Change PCSS options in ShadowVisibility or PcssFilterRadius to compare filters.
7. In Final mode (or with Debug View off), toggle Post Processing > Tone Mapping.
   Changes apply to the next rendered frame in both edit and Play modes. Compare a
   brightly lit/textured object: off clamps HDR highlights, on applies Reinhard.
   Other debug views bypass tone mapping regardless of the checkbox state.
8. Use Reset All to restore defaults, including Tone Mapping on.
9. Open `Content/Scenes/MaterialMaps.json` for per-material map checks. Left to
   right: Player with its own normal/ORM, Enemy with a different normal/ORM,
   Player without maps, Player with MR only, and Player with AO only.
   In WorldNormal, only the first two objects show mapped normals; the remaining
   three use mesh normals. Switching the view does not change map ownership.
   In Final, MR-only must ignore the fixture's red channel; AO-only darkens ambient
   on half the UV range. Disable the directional light to isolate the ambient AO
   comparison; set the Ambient AO check object's ambientIntensity to zero to
   confirm that AO does not darken direct lighting. Restore both afterwards.

Material map inputs use `normalTexture`, `metallicRoughnessTexture`, and
`occlusionTexture` in `.mat` files. The legacy `ormTexture` key is not read by the
material loader. A packed ORM file can be assigned to both metallicRoughness and
occlusion; each slot is optional and loaded as linear UNORM. Roughness/metallic
parameters multiply G/B; set both factors to 1 to use the texture values directly.
Missing normal maps or invalid tangents keep the mesh normal, and missing AO is 1.
AO currently affects ambient diffuse only, not direct lighting or specular IBL.

`MaterialMaps_ORM.png` is a deterministic 32x32 RGBA8 fixture, not an art asset:
R is 64/255 across the left/right halves, G is 64/224 across the top/bottom halves,
and B is 0/255 across the left/right halves. The ordinary Player/Enemy ORM assets
have white AO, so this separate fixture makes AO application visible.

The standalone `ShadowValidation.cpp` suite includes `MaterialMapChecks` for
normal selection, tangent fallback, null SRV bindings, AO ambient/direct-light
separation, MR factor multiplication, and texture-release invalidation. These
checks need a subsequent build/run; adding them does not execute the suite.

PostProcessSettings are read directly from DX11Renderer; the editor and Renderer
keep no separate settings copy. The Tone Mapping control remains available when
Debug View is off and in Shipping editor builds.

Implementation: Renderer exposes one setter per option for RendererDebugSettings
and ShadowFilterSettings and forwards them to DX11Renderer, which validates and
applies them. Debug views and GPU queries are compiled out when CANAPE_RENDER_DEBUG
is 0 (Shipping); ShadowFilterSettings remain available in every configuration.
