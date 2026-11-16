#pragma once
#include "Scene/Component/RenderableComponent.h"
#include "Asset/AssetManager.h"
#include "Math/Matrix4.h"
#include <vector>

namespace Canape
{
    class TransformComponent;

    class SkinnedMeshRendererComponent : public RenderableComponent
    {
        RTTR_ENABLE(RenderableComponent)

    public:
        SkinnedMeshRendererComponent() = default;

        void SetMesh(MeshAssetHandle mesh);              // Skinned 형식만 허용
        const MeshAssetHandle& GetMesh() const { return m_Mesh; }

        // 메쉬의 bonePaths를 rootBone 기준으로 해석해서 m_Bones를 채움
        void ResolveBones(TransformComponent* root);
        const std::vector<TransformComponent*>& GetBones() const { return m_Bones; }

        void AddRenderItem(const AssetManager& assets, Renderer& renderer) const override;  // 지금은 빈 함수

    private:
        MeshAssetHandle m_Mesh;
        std::vector<MaterialAssetHandle> m_Materials;    // 슬롯별 머티리얼 (우선 기본 머티리얼만)
        std::vector<TransformComponent*> m_Bones;        // joint j → bone Transform
        mutable std::vector<Matrix4> m_Palette;          // AddRenderItem에서 매 프레임 채움
    };
}