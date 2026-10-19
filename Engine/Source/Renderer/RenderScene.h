#pragma once

#include "Renderer/RenderItem.h"
#include "Math/Matrix4.h"
#include "Math/Vector3.h"

#include <vector>

namespace Canape
{
    struct RenderView
    {
        Matrix4 view{};
        Matrix4 projection{};
        Vector3 cameraPosition{};
    };

    struct RenderScene
    {
        RenderView camera;
        std::vector<RenderItem> items;
    };
}
