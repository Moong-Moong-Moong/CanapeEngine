#include "Scene/Component/SkinnedMeshRendererComponent.h"
#include "Core/Application.h"
#include "Renderer/Renderer.h"
#include "Scene/Component/TransformComponent.h"
#include "Scene/GameObject/GameObject.h"

namespace Canape
{
    void SkinnedMeshRendererComponent::SetSkeleton(TransformComponent* root)
    {
        m_Skeleton = root;
        m_Bones.clear();
    }

    void SkinnedMeshRendererComponent::ResolveBones(TransformComponent* root)
    {
        m_Bones.clear();

        const MeshAsset* mesh = Application::Get().GetAssetManager().GetMesh(GetMesh());
        if (!mesh || !root)
        {
            return;
        }

        for (const std::string& bonePath : mesh->GetBonePaths())
        {
            TransformComponent* current = root;
            size_t begin = 0;
            while (current && begin < bonePath.size())
            {
                size_t end = bonePath.find('/', begin);
                if (end == std::string::npos)
                {
                    end = bonePath.size();
                }
                const std::string_view name(bonePath.data() + begin, end - begin);

                TransformComponent* next = nullptr;
                for (TransformComponent* child : current->GetChildren())
                {
                    const GameObject* object = child ? child->GetOwnerObject() : nullptr;
                    if (object && object->GetName() == name)
                    {
                        next = child;
                        break;
                    }
                }
                current = next;
                begin = end + 1;
            }
            m_Bones.push_back(current);
        }
    }

    void SkinnedMeshRendererComponent::AddRenderItem(const AssetManager& assets, Renderer& renderer) const
    {
        const MeshAsset* mesh = assets.GetMesh(GetMesh());
        if (!mesh || mesh->GetBoneCount() == 0 || !m_Skeleton)
        {
            return;
        }

        if (m_Bones.size() != mesh->GetBoneCount())
        {
            const_cast<SkinnedMeshRendererComponent*>(this)->ResolveBones(m_Skeleton);
            if (m_Bones.size() != mesh->GetBoneCount())
            {
                return;
            }
        }

        const Matrix4 rendererInverse = GetTransform()->GetWorldToLocalMatrix();
        const std::vector<Matrix4>& inverseBindMatrices = mesh->GetInverseBindMatrices();
        m_Palette.resize(m_Bones.size());
        for (size_t j = 0; j < m_Bones.size(); ++j)
        {
            m_Palette[j] = m_Bones[j]
                ? inverseBindMatrices[j] * m_Bones[j]->GetLocalToWorldMatrix() * rendererInverse
                : Matrix4::Identity;
        }

        const uint32_t paletteOffset = renderer.AddBonePalette(m_Palette);
        const uint32_t paletteCount = static_cast<uint32_t>(m_Palette.size());
        const Matrix4 world = GetTransform()->GetLocalToWorldMatrix();
        const std::vector<SubMesh>& subMeshes = mesh->GetSubMeshes();
        for (uint32_t i = 0; i < static_cast<uint32_t>(subMeshes.size()); ++i)
        {
            MaterialAssetId materialId;
            if (const MaterialAsset* material = assets.GetMaterial(GetMaterial(subMeshes[i].materialSlot)))
            {
                materialId = material->GetAssetId();
            }
            renderer.AddSkinnedRenderItem(mesh->GetAssetId(), i, materialId, world, paletteOffset, paletteCount);
        }
    }
}
