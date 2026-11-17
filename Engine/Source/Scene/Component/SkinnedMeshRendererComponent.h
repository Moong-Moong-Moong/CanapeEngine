#pragma once
#include "Scene/Component/MeshRendererComponent.h"
#include "Math/Matrix4.h"
#include <vector>

namespace Canape
{
    class TransformComponent;

    class SkinnedMeshRendererComponent : public MeshRendererComponent
    {
        RTTR_ENABLE(MeshRendererComponent)

    public:
        SkinnedMeshRendererComponent() = default;

        TransformComponent* GetSkeleton() const { return m_Skeleton; }
        void SetSkeleton(TransformComponent* root);

        // 메쉬의 bonePaths를 rootBone 기준으로 해석해서 m_Bones를 채움
        void ResolveBones(TransformComponent* root);
        const std::vector<TransformComponent*>& GetBones() const { return m_Bones; }

        void AddRenderItem(const AssetManager& assets, Renderer& renderer) const override;  // 지금은 빈 함수

    private:
        TransformComponent* m_Skeleton = nullptr;
        std::vector<TransformComponent*> m_Bones;        // joint j → bone Transform
        mutable std::vector<Matrix4> m_Palette;          // AddRenderItem에서 매 프레임 채움
    };
}
