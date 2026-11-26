#include "EditorContext.h"

#include <algorithm>

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
    m_SelectedIds.clear();
    if (gameObject)
    {
        m_SelectedIds.push_back(m_SelectedId);
        m_SelectedAsset.clear();
    }
}

void EditorContext::ClearSelection()
{
    m_SelectedId = 0;
    m_SelectedIds.clear();
}

bool EditorContext::IsSelected(uint64_t gameObjectId) const
{
    return gameObjectId != 0 && std::find(m_SelectedIds.begin(), m_SelectedIds.end(), gameObjectId) != m_SelectedIds.end();
}

void EditorContext::ToggleSelect(const GameObject* gameObject)
{
    if (!gameObject)
    {
        return;
    }
    const uint64_t id = gameObject->GetId();
    const auto found = std::find(m_SelectedIds.begin(), m_SelectedIds.end(), id);
    if (found != m_SelectedIds.end())
    {
        m_SelectedIds.erase(found);
        if (m_SelectedId == id)
        {
            m_SelectedId = m_SelectedIds.empty() ? 0 : m_SelectedIds.back();
        }
        return;
    }
    m_SelectedIds.push_back(id);
    m_SelectedId = id;
    m_SelectedAsset.clear();
}

void EditorContext::SetSelection(std::vector<uint64_t> ids, uint64_t primaryId)
{
    std::vector<uint64_t> unique;
    unique.reserve(ids.size());
    for (const uint64_t id : ids)
    {
        if (id != 0 && std::find(unique.begin(), unique.end(), id) == unique.end())
        {
            unique.push_back(id);
        }
    }
    m_SelectedIds = std::move(unique);
    m_SelectedId = std::find(m_SelectedIds.begin(), m_SelectedIds.end(), primaryId) != m_SelectedIds.end()
        ? primaryId
        : (m_SelectedIds.empty() ? 0 : m_SelectedIds.back());
    if (m_SelectedId != 0)
    {
        m_SelectedAsset.clear();
    }
}

std::vector<GameObject*> EditorContext::GetSelectedObjects(Scene* scene) const
{
    std::vector<GameObject*> result;
    if (!scene)
    {
        return result;
    }
    for (const uint64_t id : m_SelectedIds)
    {
        GameObject* gameObject = scene->GetGameObject(scene->FindGameObjectById(id));
        if (gameObject && !gameObject->IsPendingDestroy())
        {
            result.push_back(gameObject);
        }
    }
    return result;
}

std::vector<GameObject*> EditorContext::GetSelectedRoots(Scene* scene) const
{
    const std::vector<GameObject*> selected = GetSelectedObjects(scene);
    std::vector<GameObject*> roots;
    for (GameObject* gameObject : selected)
    {
        bool hasSelectedAncestor = false;
        for (const TransformComponent* parent = gameObject->GetTransform()->GetParent(); parent && !hasSelectedAncestor;
             parent = parent->GetParent())
        {
            const GameObject* owner = parent->GetOwnerObject();
            hasSelectedAncestor = owner && IsSelected(owner->GetId());
        }
        if (!hasSelectedAncestor)
        {
            roots.push_back(gameObject);
        }
    }
    return roots;
}

void EditorContext::SelectAsset(std::filesystem::path path)
{
    m_SelectedAsset = std::move(path);
    if (!m_SelectedAsset.empty())
    {
        ClearSelection();
    }
}
