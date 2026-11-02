#include "DX11Renderer/Pipeline/DX11ConstantBuffers.h"
#include "DX11Renderer/Frame/DX11ScenePreparation.h"

namespace Canape::ConstantBuffers
{
    void UploadLighting(ID3D11DeviceContext& context, const SceneConstantBuffers& buffers,
        const PreparedLighting& data)
    {
        Upload(context, buffers.Lights, data.Lights);
        if (buffers.Shadows) Upload(context, buffers.Shadows, data.Shadows);
        if (buffers.Pcss) Upload(context, buffers.Pcss, data.Pcss);
    }

    void UploadPending(ID3D11DeviceContext& context, const SceneConstantBuffers& buffers,
        const ShaderConstants::ViewConstants& view, const ShaderConstants::SceneConstants& scene,
        bool& viewDirty, bool& sceneDirty)
    {
        if (viewDirty)
        {
            Upload(context, buffers.View, view);
            viewDirty = false;
        }
        if (sceneDirty)
        {
            Upload(context, buffers.Scene, scene);
            sceneDirty = false;
        }
    }
}
