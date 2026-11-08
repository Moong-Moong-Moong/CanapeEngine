#include "Physics/RigidBodyShape.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <optional>

#include "Math/Quaternion.h"
#include "Physics/BoxColliderComponent.h"
#include "Physics/ColliderComponent.h"
#include "Physics/SphereColliderComponent.h"
#include "Scene/Component/Component.h"
#include "Scene/GameObject/GameObject.h"

namespace Canape
{
    namespace
    {
        struct WorldScale {
            Vector3 lossy = Vector3::One;
            bool uniformAncestors = true;
        };

        struct ColliderShape {
            PhysicsShapePart part;
            Vector3 extent{};
            bool usableSize = false;
            bool usableFriction = true;
            bool nonUniformSphere = false;
        };

        // The body's world pose; every collider part is placed relative to it.
        struct BodyFrame {
            const TransformComponent& transform;
            Vector3 position;
            Quaternion inverseRotation;
        };

        // One collider measured in the body frame: its part, its record and what the shape notes about it.
        struct MeasuredCollider {
            PhysicsShapePart part;
            ColliderRecord record;
            bool usableSize = false;
            bool usableFriction = true;
            bool nonUniformAncestors = false;
            bool nonUniformSphere = false;
        };

        ColliderShape ToShape(const BoxColliderComponent& box, const Vector3& scale)
        {
            const auto extent = Abs(box.GetSize()) * scale * 0.5f;

            return ColliderShape {
                .part = PhysicsShapePart {
                    .type = PhysicsShapeType::Box,
                    .halfExtent = extent
                },
                .extent = extent,
                .usableSize = IsUsableExtent(extent)
            };
        }

        ColliderShape ToShape(const SphereColliderComponent& sphere, const Vector3& scale)
        {
            const auto radius = std::fabs(sphere.GetRadius()) * Max(scale);
            const auto extent = ToVector3(radius);

            return ColliderShape {
                .part = PhysicsShapePart {
                    .type = PhysicsShapeType::Sphere,
                    .radius = radius
                },
                .extent = extent,
                .usableSize = IsUsableExtent(extent),
                // A non-uniform scale cannot shape a sphere;
                // the largest axis is used so the sphere covers the mesh.
                .nonUniformSphere = !IsUniform(scale)
            };
        }

        WorldScale measureWorldScale(const TransformComponent& transform)
        {
            bool uniformAncestors = true;
            for(const auto* node = transform.GetParent(); node; node = node->GetParent())
            {
                uniformAncestors = uniformAncestors && IsUniform(node->GetLocalScale());
            }

            return WorldScale {
                .lossy = transform.GetLossyScale(),
                .uniformAncestors = uniformAncestors
            };
        }

        bool hasRigidBody(const GameObject& object)
        {
            return object.GetComponent<RigidBodyComponent>() != nullptr;
        }

        RigidBodyIssue findPlacementIssue(const RigidBodyComponent& body)
        {
            if(body.GetOwnerObject()->GetComponent<RigidBodyComponent>() != &body)
            {
                return RigidBodyIssue::DuplicateOnObject;
            }

            for(const auto* node = body.GetTransform()->GetParent(); node; node = node->GetParent())
            {
                if(hasRigidBody(*node->GetOwnerObject()))
                {
                    return RigidBodyIssue::NestedBody;
                }
            }
            return RigidBodyIssue::None;
        }

        // A body shapes itself from colliders on its own object and on descendants that have no body of their own.
        template<typename Visit>
            requires std::invocable<Visit, const ColliderComponent&>
        void forEachBodyCollider(const TransformComponent& node, Visit&& visit)
        {
            for(const ColliderComponent* collider : node.GetOwnerObject()->GetComponents<ColliderComponent>())
            {
                if(IsPhysicsRunnable(*collider))
                {
                    visit(*collider);
                }
            }

            for(const auto* child : node.GetChildren())
            {
                if(!hasRigidBody(*child->GetOwnerObject()))
                {
                    forEachBodyCollider(*child, std::forward<Visit>(visit));
                }
            }
        }

        ColliderShape makeColliderShape(const ColliderComponent& collider, const Vector3& scale)
        {
            ColliderShape shape;
            if(const auto* box = dynamic_cast<const BoxColliderComponent*>(&collider))
            {
                shape = ToShape(*box, scale);
            }
            else if(const auto* sphere = dynamic_cast<const SphereColliderComponent*>(&collider))
            {
                shape = ToShape(*sphere, scale);
            }
            else
            {
                assert(false && "Unhandled collider type");
            }

            if(collider.GetOverrideFriction())
            {
                shape.part.friction = collider.GetFriction();
                shape.usableFriction = IsNonNegative(collider.GetFriction());
            }

            return shape;
        }

        MeasuredCollider measureCollider(const ColliderComponent& collider, const BodyFrame& frame)
        {
            const auto* transform = collider.GetTransform();
            const auto scale = measureWorldScale(*transform);

            auto shape = makeColliderShape(collider, scale.lossy);
            if(transform != &frame.transform)
            {
                shape.part.localPose.position = frame.inverseRotation.Rotate(transform->GetPosition() - frame.position);
                shape.part.localPose.rotation = (frame.inverseRotation * transform->GetRotation()).Normalized();
            }

            return MeasuredCollider {
                .part = shape.part,
                .record = ColliderRecord {
                    .collider = &collider,
                    .parent = transform->GetParent(),
                    .extent = shape.extent
                },
                .usableSize = shape.usableSize,
                .usableFriction = shape.usableFriction,
                .nonUniformAncestors = !scale.uniformAncestors,
                .nonUniformSphere = shape.nonUniformSphere
            };
        }

        bool sameRecord(const ColliderRecord& lhs, const ColliderRecord& rhs)
        {
            return lhs.collider == rhs.collider
                && lhs.parent == rhs.parent
                && SameBits(lhs.extent, rhs.extent);
        }

        bool sameFriction(const std::optional<float>& lhs, const std::optional<float>& rhs)
        {
            return lhs.has_value() == rhs.has_value()
                && (!lhs || SameBits(*lhs, *rhs));
        }

        bool samePart(const PhysicsShapePart& lhs, const PhysicsShapePart& rhs)
        {
            const auto sameFriction = lhs.friction.has_value() == rhs.friction.has_value()
                && SameBits(lhs.friction.value_or(0.0f), rhs.friction.value_or(0.0f));

            return lhs.type == rhs.type
                && SameBits(lhs.halfExtent, rhs.halfExtent)
                && SameBits(lhs.radius, rhs.radius)
                && SameBits(lhs.localPose.position, rhs.localPose.position)
                && SameBits(lhs.localPose.rotation, rhs.localPose.rotation)
                && sameFriction;
        }
    }

    bool IsPhysicsRunnable(const Component& component)
    {
        const GameObject& owner = *component.GetOwnerObject();
        return component.IsEnabled()
            && !component.IsPendingDestroy()
            && owner.IsActive()
            && !owner.IsPendingDestroy();
    }

    void RigidBodyShape::Gather(const RigidBodyComponent& body)
    {
        Clear();

        const auto* bodyTransform = body.GetTransform();
        bodyParent = bodyTransform->GetParent();

        issue = findPlacementIssue(body);
        if(issue != RigidBodyIssue::None) return;

        const BodyFrame frame {
            .transform = *bodyTransform,
            .position = bodyTransform->GetPosition(),
            .inverseRotation = bodyTransform->GetRotation().Normalized().Inverse()
        };
        bool invalidExtent = false;
        bool invalidFriction = false;
        forEachBodyCollider(*bodyTransform, [&](const ColliderComponent& collider)
        {
            const auto measured = measureCollider(collider, frame);
            parts.push_back(measured.part);
            records.push_back(measured.record);
            invalidExtent |= !measured.usableSize;
            invalidFriction |= !measured.usableFriction;
            nonUniformAncestors |= measured.nonUniformAncestors;
            nonUniformSphere |= measured.nonUniformSphere;
        });

        if(invalidExtent)
        {
            issue = RigidBodyIssue::InvalidExtent;
        }
        else if(!IsValidSettings(body.GetSettings()) || invalidFriction)
        {
            issue = RigidBodyIssue::InvalidSettings;
        }
        else if(parts.empty())
        {
            issue = RigidBodyIssue::NoCollider;
        }
    }

    void RigidBodyShape::Clear() {
        records.clear();
        parts.clear();
        bodyParent = nullptr;
        issue = RigidBodyIssue::None;
        nonUniformAncestors = false;
        nonUniformSphere = false;
    }

    bool RigidBodyShape::HasSameColliders(const RigidBodyShape& other) const
    {
        constexpr auto colliderOf = &ColliderRecord::collider;
        return std::ranges::equal(records, other.records, {}, colliderOf, colliderOf);
    }

    bool RigidBodyShape::HasSameRecords(const RigidBodyShape& other) const
    {
        return bodyParent == other.bodyParent
            && std::ranges::equal(records, other.records, sameRecord);
    }

    bool RigidBodyShape::IsSameAs(const RigidBodyShape& other) const
    {
        return issue == other.issue
            && HasSameRecords(other)
            && std::ranges::equal(parts, other.parts, samePart);
    }
}
