# Renderer debug quick test

Build and run **Game** with **Debug | x64** or **Release | x64**.
Give the game window keyboard focus. Controls use the existing scene and camera.

| Key | Action |
| --- | --- |
| F1 | Cycle Final -> CameraDepth -> WorldNormal -> UV -> MipLOD -> ShadowDepth -> ShadowVisibility -> Final. |
| F2 | Toggle wireframe while keeping the current view. |
| F3 | Toggle GPU timing capture independently of the view. |
| F4 | Switch to CameraDepth and cycle the raw depth minimum: 0.99, 0.999, 0, 0.95. Maximum stays at 1. |
| F10 | Reset F1-F4 / Ctrl+F1-F4 settings: Final, solid fill, depth range [0.95, 1], GPU capture off, and default cull/bias/filtering. |
| Ctrl+F1 | Cycle shadow culling: None / Front / Back. |
| Ctrl+F2 | Cycle slope bias: 0 / 0.5 / 1 / 2 / 3 / 4 / 5 / 6 / 7 / 8. |
| Ctrl+F3 | Cycle receiver bias: 0 / 0.0001 / 0.0003 / 0.0006 / 0.001. |
| Ctrl+F4 | Toggle shadow filtering. |
| Ctrl+- / Ctrl++ | Decrease / increase shadow distance by 5; default 25, minimum 5. Main keyboard and numpad supported. |
| Ctrl+Shift+P | Switch between PCSS and 3x3 PCF. |
| Ctrl+PageDown/Up | Decrease / increase final PCF samples: 8 / 16 / 25 / 32 / 64 / 128; default 64. |
| Ctrl+Alt+PageDown/Up | Decrease / increase blocker search samples: 8 / 16 / 25 / 32 / 64 / 128; default 128. |
| Ctrl+Shift+PageUp | Switch legacy / Vogel pattern when final PCF samples is 25. |
| Ctrl+Shift+PageDown | Toggle PCSS filter radius view (log2 texels). |
| Ctrl+Shift+Home/End | Decrease / increase final PCF tap spacing. |
| Ctrl+Alt+Home/End | Decrease / increase blocker search radius. |

The window title shows the view, wireframe state, draw calls and triangle count.
It refreshes every 250 ms, or after the next rendered frame when controls change.
GPU capture adds total frame, DrawScene and FinalOutput times in milliseconds,
plus the GPU frame ID. Results arrive asynchronously; pending/unavailable is
shown until a valid measurement arrives. GPU frame time excludes Present/VSync.
Draw/triangle counts include the fullscreen FinalOutput draw (one triangle).

Quick check:

1. Check the startup `RenderDebug` log: View / display, Shadow, and PCSS controls
   should appear in separate, aligned groups. Press F1 repeatedly and compare
   the seven views on the sample meshes.
2. Toggle F2 in each view, then toggle it off. In CameraDepth the wire pattern
   appears in the depth visualization rather than as a colored line overlay.
3. Use F4 when depth appears uniformly white: D3D depth is non-linear and the
   sample camera can place most geometry near 1.
4. In MipLOD, textured meshes should show LOD colors; meshes using the fallback
   white texture remain white. UV needs a mesh with UV attributes.
5. Toggle F3, wait a few frames, and check that timing values appear. Toggle it
   off and on again; old measurements should not immediately reappear.
6. Resize/minimize/restore the window, switch scenes with Space, and reset with F10.

Implementation: Renderer owns the temporary settings and title display;
Application forwards its existing InputManager once per frame, before rendering.
DX11Renderer already supplies SetDebugSettings/GetDebugStats, debug shader
variants, depth visualization and GPU timestamp queries. The quick-test controls
and title display are compiled out when CANAPE_RENDER_DEBUG is 0 (Shipping).
Existing console keys F5-F9/F11 keep their current bindings.
