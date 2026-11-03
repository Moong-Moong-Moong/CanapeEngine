#pragma once
#include "DX11Renderer/Data/ShaderConstants.h"
#include <d3d11.h>

namespace Canape
{
    struct PreparedLighting;
    // Borrowed handles. Buffer ownership stays with the renderer and shadow resources.
    struct SceneConstantBuffers
    {
        ID3D11Buffer* View = nullptr;
        ID3D11Buffer* Scene = nullptr;
        ID3D11Buffer* Lights = nullptr;
        ID3D11Buffer* Shadows = nullptr;
        ID3D11Buffer* Pcss = nullptr;
    };
    namespace ConstantBuffers
    {
        template<class T>
        void Upload(ID3D11DeviceContext& context, ID3D11Buffer* buffer, const T& data)
        {
            static_assert(sizeof(T) % 16 == 0);
            context.UpdateSubresource(buffer, 0, nullptr, &data, 0, 0);
        }
        void UploadLighting(ID3D11DeviceContext& context, const SceneConstantBuffers& buffers,
            const PreparedLighting& data);
        void UploadPending(ID3D11DeviceContext& context, const SceneConstantBuffers& buffers,
            const ShaderConstants::ViewConstants& view, const ShaderConstants::SceneConstants& scene,
            bool& viewDirty, bool& sceneDirty);
    }
}
