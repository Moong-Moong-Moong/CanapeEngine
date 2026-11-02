#include "EditorContext.h"

using namespace Canape;

GameObject* EditorContext::GetSelected(Scene* scene) const
{
    if (!scene || m_SelectedId == 0)
    {
        return nullptr;
    }
    return scene->GetGameObject(scene->FindGameObjectById(m_SelectedId));
}

void EditorContext::Select(const GameObject* gameObject)
{
    m_SelectedId = gameObject ? gameObject->GetId() : 0;
    if (gameObject)
    {
        m_SelectedAsset.clear();
    }
}

void EditorContext::SelectAsset(std::filesystem::path path)
{
    m_SelectedAsset = std::move(path);
    if (!m_SelectedAsset.empty())
    {
        m_SelectedId = 0;
    }
}
