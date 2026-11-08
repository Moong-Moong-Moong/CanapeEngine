#include "Puzzle/GoalCup.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "CNDebug/DebugLog.h"

RTTR_REGISTRATION
{
    rttr::registration::class_<GoalCup>("GoalCup")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr)
        .property("payload", &GoalCup::payload)
        .property("idleMaterial", &GoalCup::idleMaterial)
        .property("solvedMaterial", &GoalCup::solvedMaterial);
}

namespace
{
    struct WorldBox {
        Canape::Vector3 min{};
        Canape::Vector3 max{};
    };

    bool isRotated(const Canape::TransformComponent& transform)
    {
        constexpr float IdentityTolerance = 1.0e-4f;
        return std::fabs(transform.GetRotation().Normalized().W) < 1.0f - IdentityTolerance;
    }

    std::optional<WorldBox> worldBoxOf(const Canape::GameObject& piece)
    {
        const auto* box = piece.GetComponent<Canape::BoxColliderComponent>();
        if(box == nullptr) return std::nullopt;

        const auto& transform = *piece.GetTransform();
        // The box collider is measured with the lossy scale, like the body is.
        const Canape::Vector3 half = Canape::Abs(box->GetSize() * transform.GetLossyScale()) * 0.5f;
        const Canape::Vector3 center = transform.GetPosition();
        return WorldBox{ center - half, center + half };
    }

    // -x, +x, -z, +z: the side of the floor a wall stands on.
    std::size_t sideOf(const Canape::Vector3& offset)
    {
        if(std::fabs(offset.X) >= std::fabs(offset.Z))
        {
            return offset.X < 0.0f ? 0 : 1;
        }
        return offset.Z < 0.0f ? 2 : 3;
    }
}

std::vector<Canape::GameObject*> GoalCup::GetPieces() const
{
    std::vector<Canape::GameObject*> pieces;
    for(const auto* child : GetTransform()->GetChildren())
    {
        pieces.push_back(child->GetOwnerObject());
    }
    return pieces;
}

std::optional<CupRegion> GoalCup::GetRegion() const
{
    std::vector<WorldBox> boxes;
    bool rotated = isRotated(*GetTransform());
    for(const auto* piece : GetPieces())
    {
        if(const auto box = worldBoxOf(*piece))
        {
            boxes.push_back(*box);
            rotated = rotated || isRotated(*piece->GetTransform());
        }
    }
    if(rotated && !rotationLogged)
    {
        CN_LOG_GAME("puzzle", "'", GetOwnerObject()->GetName(), "': rotated pieces are measured as axis-aligned boxes");
        rotationLogged = true;
    }
    if(boxes.size() < 5)
    {
        logShape("needs a floor and four walls with box colliders");
        return std::nullopt;
    }

    const auto lowerTop = [](const WorldBox& a, const WorldBox& b)
    {
        return a.max.Y < b.max.Y;
    };
    const auto floorBox = std::ranges::min_element(boxes, lowerTop);
    const WorldBox floor = *floorBox;
    const Canape::Vector3 floorCenter = (floor.min + floor.max) * 0.5f;
    std::optional<WorldBox> walls[4];
    bool surrounded = true;
    for(auto box = boxes.begin(); box != boxes.end(); ++box)
    {
        if(box == floorBox) continue;

        const Canape::Vector3 center = (box->min + box->max) * 0.5f;
        std::optional<WorldBox>& wall = walls[sideOf(center - floorCenter)];
        surrounded = surrounded && !wall.has_value();
        wall = *box;
    }
    surrounded = surrounded && std::ranges::all_of(
        walls,
        [](const auto& wall)
        {
            return wall.has_value();
        }
    );
    if(!surrounded)
    {
        logShape("needs exactly one wall on each side of the floor");
        return std::nullopt;
    }

    const float lowestWallTop = std::ranges::min(
        walls,
        {},
        [](const auto& wall)
        {
            return wall->max.Y;
        }
    )->max.Y;
    const Canape::Vector3 min{
        walls[0]->max.X,
        floor.max.Y,
        walls[2]->max.Z
    };
    const Canape::Vector3 max{
        walls[1]->min.X,
        floor.max.Y + (lowestWallTop - floor.max.Y) * 0.5f,
        walls[3]->min.Z
    };
    if(!(min < max))
    {
        logShape("the walls leave no room above the floor");
        return std::nullopt;
    }
    return CupRegion{ (min + max) * 0.5f, (max - min) * 0.5f };
}

bool GoalCup::HoldsPayload() const
{
    const auto region = GetRegion();
    const auto* object = GetGameObject(payload);
    const auto* body = object ? object->GetComponent<Canape::RigidBodyComponent>() : nullptr;
    if(!region || body == nullptr) return false;

    const Canape::RigidBodies inside = GetOwnerObject()->GetScene()->GetPhysics().QueryBox(region->center, region->halfExtent);
    return std::ranges::find(inside, body) != inside.end();
}

void GoalCup::FixedUpdate()
{
    show(HoldsPayload());
}

void GoalCup::show(bool solved)
{
    if(solved == shownSolved) return;

    const std::string& name = solved ? solvedMaterial : idleMaterial;
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
        shownSolved = solved;
        lookLogged = false;
    }
    else if(!lookLogged)
    {
        CN_LOG_GAME("puzzle", "'", GetOwnerObject()->GetName(), "': material '", name, "' could not be set; the cup keeps its old look");
        lookLogged = true;
    }
}

void GoalCup::logShape(const char* why) const
{
    if(shapeLogged) return;

    CN_LOG_GAME("puzzle", "'", GetOwnerObject()->GetName(), "' holds nothing: it ", why);
    shapeLogged = true;
}
