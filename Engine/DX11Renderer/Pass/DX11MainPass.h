#pragma once
#include "DX11Renderer/Pipeline/DX11MeshDraw.h"

namespace Canape::MainPass
{
    void DrawOpaqueAndMasked(const PreparedScene& scene, MeshDrawContext& context);
    void DrawTransparent(const PreparedScene& scene, MeshDrawContext& context);
}
