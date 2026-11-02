# Renderer debug quick test

Build and run **Editor** with **Debug | x64** or **Release | x64**.
Open the **Render Debug** dropdown at the right end of the Scene view toolbar.

| Section | Options |
| --- | --- |
| View | Debug View on/off, View Mode (Final, CameraDepth, WorldNormal, UV, MipLOD, ShadowDepth, ShadowVisibility, PcssFilterRadius), Wireframe, Depth Range (CameraDepth only) |
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
7. Use Reset All to restore defaults.

Implementation: Renderer exposes one setter per option for RendererDebugSettings
and ShadowFilterSettings and forwards them to DX11Renderer, which validates and
applies them. Debug views and GPU queries are compiled out when CANAPE_RENDER_DEBUG
is 0 (Shipping); ShadowFilterSettings remain available in every configuration.
