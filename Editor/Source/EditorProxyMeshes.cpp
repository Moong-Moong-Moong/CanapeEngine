#include "EditorProxyMeshes.h"

#include "Renderer/Renderer.h"
#include "Scene/Component/LightComponent.h"

#include <cmath>

using namespace Canape;

namespace
{
    constexpr float kHalfPi = 1.5707963f;

    // Builtin cylinder runs along +Y; this turns it to run along +Z (row vectors: (0,1,0) * RotX(90deg) = (0,0,1)).
    Matrix4 AlongZ(const Vector3& scale, float centerZ)
    {
        return Matrix4::Scale(scale)
            * Matrix4::FromXM(DirectX::XMMatrixRotationX(kHalfPi))
            * Matrix4::Translation({ 0.0f, 0.0f, centerZ });
    }

    Matrix4 Box(const Vector3& scale, const Vector3& center)
    {
        return Matrix4::Scale(scale) * Matrix4::Translation(center);
    }

    AssetOwnerInfo ProxyOwner()
    {
        return { "Editor", "ProxyMeshes", "EditorProxyMeshes" };
    }

    MaterialAssetHandle MakeMaterial(AssetManager& assets, const char* name, const Vector4& color)
    {
        MaterialData data;
        data.name = name;
        data.desc.baseColor = color;
        data.desc.roughness = 0.9f;
        return assets.CreateMaterial(name, data, ProxyOwner());
    }
}

bool EditorProxyMeshes::EnsureAssets()
{
    if (!m_Attempted)
    {
        m_Attempted = true;
        AssetManager& assets = Application::Get().GetAssetManager();
        m_Cube = assets.GetMeshHandle("builtin/cube", ProxyOwner());
        m_Sphere = assets.GetMeshHandle("builtin/sphere", ProxyOwner());
        m_Cylinder = assets.GetMeshHandle("builtin/cylinder", ProxyOwner());
        m_LightMaterial = MakeMaterial(assets, "Editor/LightProxy", { 1.0f, 0.85f, 0.25f, 1.0f });
        m_CameraMaterial = MakeMaterial(assets, "Editor/CameraProxy", { 0.35f, 0.6f, 1.0f, 1.0f });
    }
    return m_Cube.IsValid() && m_Sphere.IsValid() && m_Cylinder.IsValid();
}

void EditorProxyMeshes::Release()
{
    m_Cube.Reset();
    m_Sphere.Reset();
    m_Cylinder.Reset();
    m_LightMaterial.Reset();
    m_CameraMaterial.Reset();
    m_Attempted = false;
}

void EditorProxyMeshes::SubmitParts(Renderer& renderer, std::initializer_list<Part> parts, const MaterialAssetHandle& material,
    const Matrix4& objectWorld) const
{
    AssetManager& assets = Application::Get().GetAssetManager();
    const MaterialAsset* materialAsset = assets.GetMaterial(material);
    const MaterialAssetId materialId = materialAsset ? materialAsset->GetAssetId() : MaterialAssetId{};
    for (const Part& part : parts)
    {
        if (const MeshAsset* mesh = assets.GetMesh(*part.mesh))
        {
            renderer.AddRenderItem(mesh->GetAssetId(), 0, materialId, part.local * objectWorld, false);
        }
    }
}

void EditorProxyMeshes::Submit(Scene& scene, Renderer& renderer)
{
    if (!EnsureAssets())
    {
        return;
    }

    // Object position and rotation only: the icon keeps its size whatever the object's scale.
    const auto objectWorld = [](const Component& component)
    {
        const TransformComponent* transform = component.GetTransform();
        return Matrix4::TRS(transform->GetWorldPosition(), transform->GetWorldRotation(), Vector3::One);
    };
    for (const LightComponent* light : scene.GetLights())
    {
        if (!light || !light->IsProxyVisible() || !Scene::IsRunnable(light)) continue;
        const Matrix4 world = objectWorld(*light);
        if (light->GetType() == LightType::Point)
        {
            SubmitParts(renderer, { { &m_Sphere, Box({ 0.5f, 0.5f, 0.5f }, {}) } }, m_LightMaterial, world);
        }
        else
        {
            // Directional / spot: a ball with an arrow showing where the light travels (+Z).
            // No cone mesh among the builtins, so the head is four stacked discs narrowing toward the tip.
            SubmitParts(renderer, {
                { &m_Sphere, Box({ 0.5f, 0.5f, 0.5f }, {}) },
                { &m_Cylinder, AlongZ({ 0.12f, 1.0f, 0.12f }, 0.7f) },   // shaft z 0.2 .. 1.2
                { &m_Cylinder, AlongZ({ 0.48f, 0.1f, 0.48f }, 1.25f) },  // head
                { &m_Cylinder, AlongZ({ 0.36f, 0.1f, 0.36f }, 1.35f) },
                { &m_Cylinder, AlongZ({ 0.24f, 0.1f, 0.24f }, 1.45f) },
                { &m_Cylinder, AlongZ({ 0.12f, 0.1f, 0.12f }, 1.55f) },  // tip
            }, m_LightMaterial, world);
        }
    }

    for (const CameraComponent* camera : scene.GetCameras())
    {
        if (!camera || !camera->IsProxyVisible() || !Scene::IsRunnable(camera)) continue;
        // Body, a lens toward +Z (where it looks) and a fin on top (its up).
        SubmitParts(renderer, {
            { &m_Cube, Box({ 0.3f, 0.3f, 0.45f }, {}) },
            { &m_Cylinder, AlongZ({ 0.22f, 0.16f, 0.22f }, 0.3f) },
            { &m_Cube, Box({ 0.06f, 0.12f, 0.2f }, { 0.0f, 0.21f, -0.05f }) },
        }, m_CameraMaterial, objectWorld(*camera));
    }
}
