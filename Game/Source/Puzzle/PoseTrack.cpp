// rttr declares the element wrapper of a sequence container it never defines.
#pragma warning(push)
#pragma warning(disable : 4506)

#include "Puzzle/PoseTrack.h"

#include "Math/Quaternion.h"
#include "Math/Scalar.h"
#include "Math/Vector3.h"

RTTR_REGISTRATION
{
    rttr::registration::class_<PoseKey>("PoseKey")
        .constructor<>()(rttr::policy::ctor::as_object)
        .property("tick", &PoseKey::tick)
        .property("pose", &PoseKey::pose)
        .property("easing", &PoseKey::easing);

    rttr::registration::class_<PoseTrack>("PoseTrack")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr)
        .property("solutionKeys", &PoseTrack::solutionKeys)
        .property("controlKeys", &PoseTrack::controlKeys);
}

Canape::PhysicsPose EvaluatePoseKeys(const PoseKeys& keys, const Canape::PhysicsPose& start, std::uint32_t tick)
{
    PoseKey from{ 0, start };
    for(const PoseKey& key : keys)
    {
        if(key.tick <= from.tick) continue;

        if(tick >= key.tick)
        {
            from = key;
            continue;
        }
        if(tick == from.tick) return from.pose;

        const float u = static_cast<float>(tick - from.tick) / static_cast<float>(key.tick - from.tick);
        const float s = Canape::Ease(key.easing, u);
        return {
            .position = Canape::Lerp(from.pose.position, key.pose.position, s),
            .rotation = from.pose.rotation == key.pose.rotation ?
                from.pose.rotation :
                Canape::Nlerp(from.pose.rotation, key.pose.rotation, s)
        };
    }
    return from.pose;
}

void PoseTrack::Start()
{
    const auto& transform = *GetTransform();
    startPose = Canape::PhysicsPose{
        .position = transform.GetLocalPosition(),
        .rotation = transform.GetLocalRotation()
    };
}

Canape::PhysicsPose PoseTrack::PoseAt(PuzzleMode mode, std::uint32_t tick) const
{
    return EvaluatePoseKeys(GetKeys(mode), startPose, tick);
}

Canape::PhysicsPose PoseTrack::WorldPoseAt(PuzzleMode mode, std::uint32_t tick) const
{
    const auto local = PoseAt(mode, tick);
    const auto* parent = GetTransform()->GetParent();
    if(parent == nullptr) return local;

    return Canape::PhysicsPose{
        .position = parent->GetLocalToWorldMatrix().TransformPoint(local.position),
        .rotation = parent->GetRotation() * local.rotation
    };
}

#pragma warning(pop)
