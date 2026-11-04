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
        for (size_t index : scene.Transparent) MeshDraw::SubmitMain(scene.Draws[index], context);
    }
}
