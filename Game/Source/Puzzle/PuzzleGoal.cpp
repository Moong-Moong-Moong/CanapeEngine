#include "Puzzle/PuzzleGoal.h"

#include <algorithm>

#include "CNDebug/DebugLog.h"

RTTR_REGISTRATION
{
    rttr::registration::class_<PuzzleGoal>("PuzzleGoal")
        .property("payload", &PuzzleGoal::payload)
        .property("idleMaterial", &PuzzleGoal::idleMaterial)
        .property("solvedMaterial", &PuzzleGoal::solvedMaterial);
}

std::vector<Canape::GameObject*> PuzzleGoal::GetPieces() const
{
    std::vector<Canape::GameObject*> pieces;
    for(const auto* child : GetTransform()->GetChildren())
    {
        pieces.push_back(child->GetOwnerObject());
    }
    return pieces;
}

bool PuzzleGoal::HoldsPayload() const
{
    const auto region = GetRegion();
    const auto* object = GetGameObject(payload);
    const auto* body = object ? object->GetComponent<Canape::RigidBodyComponent>() : nullptr;
    if(!region || body == nullptr) return false;

    const auto& physicsScene = GetOwnerObject()->GetScene()->GetPhysics();
    const Canape::RigidBodies inside = physicsScene.QueryBox(region->center, region->halfExtent);
    return std::ranges::find(inside, body) != inside.end();
}

void PuzzleGoal::FixedUpdate()
{
    const bool inside = HoldsPayload();
    if(Observe(inside))
    {
        satisfied = true;
    }
    show(inside);
}

void PuzzleGoal::show(bool inside)
{
    if(inside == shownInside) return;

    const std::string& name = inside ? solvedMaterial : idleMaterial;
    bool applied = true;
    for(const auto* piece : GetPieces())
    {
        auto* renderer = piece->GetComponent<Canape::MeshRendererComponent>();
        if(renderer && !name.empty() && !renderer->SetMaterial(0, name))
        {
            applied = false;
        }
    }

    if(applied)
    {
        shownInside = inside;
        lookLogged = false;
    }
    else if(!lookLogged)
    {
        CN_LOG_GAME("puzzle", "'", GetOwnerObject()->GetName(), "': material '", name, "' could not be set; the goal keeps its old look");
        lookLogged = true;
    }
}
