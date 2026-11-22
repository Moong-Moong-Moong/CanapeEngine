#pragma once

#include <Engine.h>

// Editor-only stand-ins for objects with nothing to draw (lights, cameras), built from the builtin meshes.
// Each model points down local +Z, so the object's forward direction can be read at a glance. They are submitted
// through Renderer::SetEditorSubmit, so they never exist in the game.
class EditorProxyMeshes
{
public:
    void Submit(Canape::Scene& scene, Canape::Renderer& renderer);
    void Release();

private:
    struct Part
    {
        const Canape::MeshAssetHandle* mesh;
        Canape::Matrix4 local;
    };

    bool EnsureAssets();
    void SubmitParts(Canape::Renderer& renderer, std::initializer_list<Part> parts, const Canape::MaterialAssetHandle& material,
        const Canape::Matrix4& objectWorld) const;

    Canape::MeshAssetHandle m_Cube;
    Canape::MeshAssetHandle m_Sphere;
    Canape::MeshAssetHandle m_Cylinder;
    Canape::MaterialAssetHandle m_LightMaterial;
    Canape::MaterialAssetHandle m_CameraMaterial;
    bool m_Attempted = false;
};
