#include "Puzzle/HingeRelease.h"

#include <vector>

#include "CNDebug/DebugLog.h"

RTTR_REGISTRATION
{
    rttr::registration::class_<HingeRelease>("HingeRelease")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr)
        .property("solutionTick", &HingeRelease::solutionTick)
        .property("controlTick", &HingeRelease::controlTick);
}

namespace
{
    // The disabled bodies below the node, nearest first; what is below a rider rides with it.
    void collectRiders(const Canape::TransformComponent& node, std::vector<Canape::RigidBodyComponent*>& riders)
    {
        for(auto* child : node.GetChildren())
        {
            auto* body = child->GetOwnerObject()->GetComponent<Canape::RigidBodyComponent>();
            if(body == nullptr)
            {
                collectRiders(*child, riders);
            }
            else if(!body->IsEnabled())
            {
                riders.push_back(body);
            }
        }
    }
}

void HingeRelease::fire(Canape::HingeComponent& hinge)
{
    if(const auto* arm = GetComponent<Canape::RigidBodyComponent>())
    {
        detachRiders(*arm);
    }
    hinge.Release();
    if(auto* arm = GetComponent<Canape::RigidBodyComponent>())
    {
        arm->SetEnabled(false);
    }
    hideArm();
    applied = true;
}

// Each rider keeps its world pose and the velocity of its point on the arm; the impulse reaches its body once made.
void HingeRelease::detachRiders(const Canape::RigidBodyComponent& arm)
{
    using namespace Canape;

    std::vector<RigidBodyComponent*> riders;
    collectRiders(*GetTransform(), riders);
    TransformComponent* parent = GetTransform()->GetParent();
    for(RigidBodyComponent* rider : riders)
    {
        TransformComponent& transform = *rider->GetTransform();
        const Vector3 position = transform.GetPosition();
        const Quaternion rotation = transform.GetRotation();
        const Vector3 velocity = arm.GetVelocityAt(position);
        transform.SetParent(parent);
        if(parent)
        {
            transform.SetLocalPosition(parent->GetWorldToLocalMatrix().TransformPoint(position));
            transform.SetLocalRotation((parent->GetRotation().Inverse() * rotation).Normalized());
        }
        else
        {
            transform.SetLocalPosition(position);
            transform.SetLocalRotation(rotation);
        }
        rider->SetEnabled(true);
        if(rider->GetMass() > 0.0f)
        {
            rider->AddImpulse(velocity * rider->GetMass());
        }
        else
        {
            CN_LOG_GAME("puzzle", "'", rider->GetOwnerObject()->GetName(), "' has no mass of its own, so it leaves the arm at rest");
        }
    }
}

void HingeRelease::hideArm()
{
    std::vector<Canape::MeshRendererComponent*> renderers;
    collectBelow(*GetTransform(), renderers);
    for(auto* renderer : renderers)
    {
        renderer->SetEnabled(false);
    }
}
