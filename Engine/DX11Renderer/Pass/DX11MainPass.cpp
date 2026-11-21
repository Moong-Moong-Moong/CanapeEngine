#include "DX11Renderer/Pass/DX11MainPass.h"

namespace Canape::MainPass
{
    void DrawOpaqueAndMasked(const PreparedScene& scene, MeshDrawContext& context)
    {
        for (size_t index : scene.Opaque) MeshDraw::SubmitMain(scene.Draws[index], context);
        for (size_t index : scene.Masked) MeshDraw::SubmitMain(scene.Draws[index], context);
    }
    void DrawTransparent(const PreparedScene& scene, MeshDrawContext& context)
    {
        for (size_t index : scene.Transparent)
        {
#if CANAPE_RENDER_DEBUG
            if (context.DebugSettings && context.DebugSettings->enabled && context.DebugSettings->wireframe)
            {
                MeshDraw::SubmitMain(scene.Draws[index], context);
                continue;
            }
#endif
            // Depth writes are disabled for blending. Draw the far shell first so
            // triangle index order cannot place back faces over the near shell.
            MeshDraw::SubmitMain(scene.Draws[index], context, TransparentFace::Back);
            MeshDraw::SubmitMain(scene.Draws[index], context, TransparentFace::Front);
        }
    }
}
