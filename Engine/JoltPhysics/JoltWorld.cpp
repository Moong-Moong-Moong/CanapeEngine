#include "JoltPhysics/JoltWorld.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <vector>

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include "JoltPhysics/JoltConvert.h"
#include "JoltPhysics/JoltRuntimeImpl.h"

namespace Canape
{
    namespace
    {
        constexpr JPH::ObjectLayer StaticLayer = 0;
        constexpr JPH::ObjectLayer MovingLayer = 1;
        constexpr JPH::uint ObjectLayerCount = 2;
        constexpr JPH::BroadPhaseLayer StaticBroadPhase{ 0 };
        constexpr JPH::BroadPhaseLayer MovingBroadPhase{ 1 };
        constexpr JPH::uint BroadPhaseLayerCount = 2;

        // A leaf shape with a friction override stores this flag plus the float bits in its user data.
        constexpr std::uint64_t FrictionOverrideFlag = std::uint64_t{ 1 } << 32;
        constexpr float DefaultDensity = 1000.0f;
        constexpr float UnitLengthSquaredTolerance = 1.0e-3f;

        std::atomic<std::uint32_t> s_NextWorldTag = 1;

        std::uint32_t NextWorldTag()
        {
            std::uint32_t tag = 0;
            while (tag == 0)
            {
                tag = s_NextWorldTag.fetch_add(1);
            }
            return tag;
        }

        bool IsFinite(const Vector3& v)
        {
            return std::isfinite(v.X) && std::isfinite(v.Y) && std::isfinite(v.Z);
        }

        bool IsFinite(const Quaternion& q)
        {
            return std::isfinite(q.X) && std::isfinite(q.Y) && std::isfinite(q.Z) && std::isfinite(q.W);
        }

        bool IsUsableRotation(const Quaternion& q)
        {
            if (!IsFinite(q))
            {
                return false;
            }
            const float lengthSquared = q.X * q.X + q.Y * q.Y + q.Z * q.Z + q.W * q.W;
            return std::abs(lengthSquared - 1.0f) <= UnitLengthSquaredTolerance;
        }

        JPH::Quat ToJoltUnit(const Quaternion& q)
        {
            const JPH::Quat result = ToJolt(q);
            return result.IsNormalized() ? result : result.Normalized();
        }

        bool IsUsablePose(const PhysicsPose& pose)
        {
            return IsFinite(pose.position) && IsUsableRotation(pose.rotation);
        }

        bool IsIdentityPose(const PhysicsPose& pose)
        {
            return pose.position.X == 0.0f && pose.position.Y == 0.0f && pose.position.Z == 0.0f
                && pose.rotation.X == 0.0f && pose.rotation.Y == 0.0f && pose.rotation.Z == 0.0f && pose.rotation.W == 1.0f;
        }

        JPH::EMotionType ToJoltMotion(PhysicsMotion motion)
        {
            switch (motion)
            {
            case PhysicsMotion::Static:
                return JPH::EMotionType::Static;
            case PhysicsMotion::Kinematic:
                return JPH::EMotionType::Kinematic;
            case PhysicsMotion::Dynamic:
                return JPH::EMotionType::Dynamic;
            }
            return JPH::EMotionType::Static;
        }

        JPH::ObjectLayer LayerOf(PhysicsMotion motion)
        {
            return motion == PhysicsMotion::Static ? StaticLayer : MovingLayer;
        }

        float FrictionOf(const JPH::Body& body, const JPH::SubShapeID& subShape)
        {
            JPH::SubShapeID remainder;
            const JPH::Shape* leaf = body.GetShape()->GetLeafShape(subShape, remainder);
            if (leaf != nullptr)
            {
                const std::uint64_t userData = leaf->GetUserData();
                if ((userData & FrictionOverrideFlag) != 0)
                {
                    return std::bit_cast<float>(static_cast<std::uint32_t>(userData));
                }
            }
            return body.GetFriction();
        }

        float CombineFriction(const JPH::Body& body1, const JPH::SubShapeID& subShape1, const JPH::Body& body2, const JPH::SubShapeID& subShape2)
        {
            return std::sqrt(FrictionOf(body1, subShape1) * FrictionOf(body2, subShape2));
        }

        JPH::ShapeRefC BuildLeaf(const PhysicsShapePart& part)
        {
            std::uint64_t userData = 0;
            if (part.friction)
            {
                if (!std::isfinite(*part.friction) || *part.friction < 0.0f)
                {
                    return nullptr;
                }
                userData = FrictionOverrideFlag | std::bit_cast<std::uint32_t>(*part.friction);
            }

            switch (part.type)
            {
            case PhysicsShapeType::Box:
            {
                const Vector3& h = part.halfExtent;
                if (!IsFinite(h) || std::min({ h.X, h.Y, h.Z }) <= 0.0f)
                {
                    return nullptr;
                }
                JPH::BoxShapeSettings settings(ToJolt(h), JPH::cDefaultConvexRadius);
                settings.SetEmbedded();
                settings.mDensity = DefaultDensity;
                settings.mUserData = userData;
                const auto result = settings.Create();
                return result.HasError() ? nullptr : result.Get();
            }
            case PhysicsShapeType::Sphere:
            {
                if (!std::isfinite(part.radius) || part.radius <= 0.0f)
                {
                    return nullptr;
                }
                JPH::SphereShapeSettings settings(part.radius);
                settings.SetEmbedded();
                settings.mDensity = DefaultDensity;
                settings.mUserData = userData;
                const auto result = settings.Create();
                return result.HasError() ? nullptr : result.Get();
            }
            }
            return nullptr;
        }

        bool NeedsCompound(const PhysicsBodyDesc& desc)
        {
            return desc.parts.size() != 1 || desc.parts[0].friction.has_value() || !IsIdentityPose(desc.parts[0].localPose);
        }

        bool HasFrictionOverride(const PhysicsBodyDesc& desc)
        {
            return std::any_of(desc.parts.begin(), desc.parts.end(), [](const PhysicsShapePart& part) { return part.friction.has_value(); });
        }

        JPH::ShapeRefC BuildShape(const PhysicsBodyDesc& desc)
        {
            if (desc.parts.empty())
            {
                return nullptr;
            }
            if (!NeedsCompound(desc))
            {
                return BuildLeaf(desc.parts[0]);
            }

            JPH::StaticCompoundShapeSettings compound;
            compound.SetEmbedded();
            for (const PhysicsShapePart& part : desc.parts)
            {
                if (!IsUsablePose(part.localPose))
                {
                    return nullptr;
                }
                const JPH::ShapeRefC leaf = BuildLeaf(part);
                if (leaf == nullptr)
                {
                    return nullptr;
                }
                compound.AddShape(ToJolt(part.localPose.position), ToJoltUnit(part.localPose.rotation), leaf);
            }
            const auto result = compound.Create();
            return result.HasError() ? nullptr : result.Get();
        }

        bool IsUsableDesc(const PhysicsBodyDesc& desc)
        {
            const auto nonNegative = [](float value) { return std::isfinite(value) && value >= 0.0f; };
            return IsUsablePose(desc.pose) && nonNegative(desc.mass) && nonNegative(desc.friction) && nonNegative(desc.restitution)
                && nonNegative(desc.linearDamping) && nonNegative(desc.angularDamping);
        }
    }

    struct JoltWorld::Impl
    {
        struct BodySlot
        {
            JPH::BodyID id;
            std::uint32_t generation = 1;
            std::uint64_t serial = 0;
            PhysicsMotion motion = PhysicsMotion::Static;
            float mass = 0.0f;
            bool alive = false;
        };

        JoltRuntime::Impl* runtime = nullptr;
        std::uint32_t tag = NextWorldTag();
        std::uint32_t maxBodies = 0;
        // Built only with a valid runtime, since Jolt allocates through its allocator; the tables outlive the system.
        std::unique_ptr<JPH::ObjectLayerPairFilterTable> layerPairs;
        std::unique_ptr<JPH::BroadPhaseLayerInterfaceTable> broadPhaseLayers;
        std::unique_ptr<JPH::ObjectVsBroadPhaseLayerFilterTable> layerVsBroadPhase;
        std::unique_ptr<JPH::PhysicsSystem> physics;
        std::vector<BodySlot> slots;
        std::vector<std::uint32_t> freeSlots;
        std::size_t liveBodies = 0;
        std::uint64_t nextSerial = 0;
        bool broadPhaseOptimized = false;

        Impl(JoltRuntime::Impl* runtimeImpl, const PhysicsWorldDesc& desc)
            : runtime(runtimeImpl)
            , maxBodies(std::clamp<std::uint32_t>(desc.maxBodies, 1u, JPH::PhysicsSystem::cMaxBodiesLimit))
        {
            if (runtime == nullptr)
            {
                return;
            }
            layerPairs = std::make_unique<JPH::ObjectLayerPairFilterTable>(ObjectLayerCount);
            broadPhaseLayers = std::make_unique<JPH::BroadPhaseLayerInterfaceTable>(ObjectLayerCount, BroadPhaseLayerCount);
            layerPairs->EnableCollision(MovingLayer, StaticLayer);
            layerPairs->EnableCollision(MovingLayer, MovingLayer);
            broadPhaseLayers->MapObjectToBroadPhaseLayer(StaticLayer, StaticBroadPhase);
            broadPhaseLayers->MapObjectToBroadPhaseLayer(MovingLayer, MovingBroadPhase);
            layerVsBroadPhase = std::make_unique<JPH::ObjectVsBroadPhaseLayerFilterTable>(
                *broadPhaseLayers, BroadPhaseLayerCount, *layerPairs, ObjectLayerCount);

            const JPH::uint maxBodyPairs = std::clamp<std::uint32_t>(desc.maxBodyPairs, 1u, JPH::PhysicsSystem::cMaxBodyPairsLimit);
            const JPH::uint maxContacts =
                std::clamp<std::uint32_t>(desc.maxContactConstraints, 1u, JPH::PhysicsSystem::cMaxContactConstraintsLimit);
            physics = std::make_unique<JPH::PhysicsSystem>();
            physics->Init(maxBodies, 0, maxBodyPairs, maxContacts, *broadPhaseLayers, *layerVsBroadPhase, *layerPairs);
            physics->SetGravity(IsFinite(desc.gravity) ? ToJolt(desc.gravity) : JPH::Vec3::sZero());
            physics->SetCombineFriction(CombineFriction);
        }

        ~Impl()
        {
            for (const BodySlot& slot : slots)
            {
                if (slot.alive)
                {
                    JPH::BodyInterface& bodies = physics->GetBodyInterfaceNoLock();
                    bodies.RemoveBody(slot.id);
                    bodies.DestroyBody(slot.id);
                }
            }
        }

        Impl(const Impl&) = delete;
        Impl& operator=(const Impl&) = delete;

        const BodySlot* Find(PhysicsBodyId id) const
        {
            if (id.world != tag || id.index >= slots.size())
            {
                return nullptr;
            }
            const BodySlot& slot = slots[id.index];
            return slot.alive && slot.generation == id.generation ? &slot : nullptr;
        }

        PhysicsBodyId Handle(std::uint32_t index) const
        {
            return PhysicsBodyId{ index, slots[index].generation, tag };
        }
    };

    JoltWorld::JoltWorld(JoltRuntime& runtime, const PhysicsWorldDesc& desc)
        : m_Impl(std::make_unique<Impl>(runtime.m_Impl.get(), desc))
    {
    }

    JoltWorld::~JoltWorld() = default;

    PhysicsBodyId JoltWorld::CreateBody(const PhysicsBodyDesc& desc)
    {
        Impl& impl = *m_Impl;
        if (impl.physics == nullptr || impl.liveBodies >= impl.maxBodies || !IsUsableDesc(desc))
        {
            return {};
        }
        const JPH::ShapeRefC shape = BuildShape(desc);
        if (shape == nullptr)
        {
            return {};
        }

        const std::uint32_t index = impl.freeSlots.empty() ? static_cast<std::uint32_t>(impl.slots.size()) : impl.freeSlots.back();

        JPH::BodyCreationSettings settings(shape, ToJolt(desc.pose.position), ToJoltUnit(desc.pose.rotation), ToJoltMotion(desc.motion),
            LayerOf(desc.motion));
        settings.mFriction = desc.friction;
        settings.mRestitution = desc.restitution;
        settings.mLinearDamping = desc.linearDamping;
        settings.mAngularDamping = desc.angularDamping;
        settings.mUserData = index;
        // Reduction merges coplanar contacts into one manifold that keeps a single part id.
        settings.mUseManifoldReduction = !HasFrictionOverride(desc);
        const bool dynamic = desc.motion == PhysicsMotion::Dynamic;
        if (dynamic && desc.mass > 0.0f)
        {
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = desc.mass;
        }

        const JPH::EActivation activation = desc.motion == PhysicsMotion::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate;
        const JPH::BodyID bodyId = impl.physics->GetBodyInterfaceNoLock().CreateAndAddBody(settings, activation);
        if (bodyId.IsInvalid())
        {
            return {};
        }

        if (impl.freeSlots.empty())
        {
            impl.slots.emplace_back();
        }
        else
        {
            impl.freeSlots.pop_back();
        }
        Impl::BodySlot& slot = impl.slots[index];
        slot.id = bodyId;
        slot.serial = impl.nextSerial++;
        slot.motion = desc.motion;
        slot.mass = dynamic ? settings.GetMassProperties().mMass : 0.0f;
        slot.alive = true;
        ++impl.liveBodies;
        return impl.Handle(index);
    }

    void JoltWorld::DestroyBody(PhysicsBodyId id)
    {
        Impl& impl = *m_Impl;
        if (impl.Find(id) == nullptr)
        {
            return;
        }
        Impl::BodySlot& slot = impl.slots[id.index];
        JPH::BodyInterface& bodies = impl.physics->GetBodyInterfaceNoLock();
        bodies.RemoveBody(slot.id);
        bodies.DestroyBody(slot.id);
        slot.alive = false;
        ++slot.generation;
        impl.freeSlots.push_back(id.index);
        --impl.liveBodies;
    }

    PhysicsStepError JoltWorld::Step(float dt)
    {
        Impl& impl = *m_Impl;
        if (!std::isfinite(dt) || dt < 0.0f)
        {
            return PhysicsStepError::InvalidTimeStep;
        }
        if (impl.runtime == nullptr || impl.physics == nullptr)
        {
            return PhysicsStepError::NoRuntime;
        }
        if (!impl.broadPhaseOptimized)
        {
            impl.physics->OptimizeBroadPhase();
            impl.broadPhaseOptimized = true;
        }

        const JPH::EPhysicsUpdateError update = impl.physics->Update(dt, 1, impl.runtime->tempAllocator.get(), impl.runtime->jobSystem.get());

        const auto raised = [update](JPH::EPhysicsUpdateError flag) { return (static_cast<JPH::uint32>(update) & static_cast<JPH::uint32>(flag)) != 0; };
        PhysicsStepError error = PhysicsStepError::None;
        if (raised(JPH::EPhysicsUpdateError::ManifoldCacheFull))
        {
            error = error | PhysicsStepError::ManifoldCacheFull;
        }
        if (raised(JPH::EPhysicsUpdateError::BodyPairCacheFull))
        {
            error = error | PhysicsStepError::BodyPairCacheFull;
        }
        if (raised(JPH::EPhysicsUpdateError::ContactConstraintsFull))
        {
            error = error | PhysicsStepError::ContactConstraintsFull;
        }
        return error;
    }

    void JoltWorld::AddImpulse(PhysicsBodyId id, const Vector3& impulse)
    {
        const Impl::BodySlot* slot = m_Impl->Find(id);
        if (slot == nullptr || slot->motion != PhysicsMotion::Dynamic || !IsFinite(impulse))
        {
            return;
        }
        m_Impl->physics->GetBodyInterfaceNoLock().AddImpulse(slot->id, ToJolt(impulse));
    }

    void JoltWorld::AddImpulseAt(PhysicsBodyId id, const Vector3& impulse, const Vector3& worldPoint)
    {
        const Impl::BodySlot* slot = m_Impl->Find(id);
        if (slot == nullptr || slot->motion != PhysicsMotion::Dynamic || !IsFinite(impulse) || !IsFinite(worldPoint))
        {
            return;
        }
        m_Impl->physics->GetBodyInterfaceNoLock().AddImpulse(slot->id, ToJolt(impulse), ToJolt(worldPoint));
    }

    bool JoltWorld::IsValid(PhysicsBodyId id) const
    {
        return m_Impl->Find(id) != nullptr;
    }

    PhysicsPose JoltWorld::GetPose(PhysicsBodyId id) const
    {
        const Impl::BodySlot* slot = m_Impl->Find(id);
        if (slot == nullptr)
        {
            return {};
        }
        JPH::RVec3 position;
        JPH::Quat rotation;
        m_Impl->physics->GetBodyInterfaceNoLock().GetPositionAndRotation(slot->id, position, rotation);
        return PhysicsPose{ ToCanape(JPH::Vec3(position)), ToCanape(rotation) };
    }

    Vector3 JoltWorld::GetLinearVelocity(PhysicsBodyId id) const
    {
        const Impl::BodySlot* slot = m_Impl->Find(id);
        return slot == nullptr ? Vector3{} : ToCanape(m_Impl->physics->GetBodyInterfaceNoLock().GetLinearVelocity(slot->id));
    }

    Vector3 JoltWorld::GetAngularVelocity(PhysicsBodyId id) const
    {
        const Impl::BodySlot* slot = m_Impl->Find(id);
        return slot == nullptr ? Vector3{} : ToCanape(m_Impl->physics->GetBodyInterfaceNoLock().GetAngularVelocity(slot->id));
    }

    float JoltWorld::GetMass(PhysicsBodyId id) const
    {
        const Impl::BodySlot* slot = m_Impl->Find(id);
        return slot == nullptr ? 0.0f : slot->mass;
    }

    bool JoltWorld::IsAwake(PhysicsBodyId id) const
    {
        const Impl::BodySlot* slot = m_Impl->Find(id);
        return slot != nullptr && m_Impl->physics->GetBodyInterfaceNoLock().IsActive(slot->id);
    }

    PhysicsMotion JoltWorld::GetMotion(PhysicsBodyId id) const
    {
        const Impl::BodySlot* slot = m_Impl->Find(id);
        return slot == nullptr ? PhysicsMotion::Static : slot->motion;
    }

    std::size_t JoltWorld::GetBodyCount() const
    {
        return m_Impl->liveBodies;
    }

    PhysicsWorldStats JoltWorld::GetStats() const
    {
        const Impl& impl = *m_Impl;
        if (impl.physics == nullptr)
        {
            return {};
        }
        return PhysicsWorldStats{ impl.liveBodies, impl.physics->GetNumActiveBodies(JPH::EBodyType::RigidBody) };
    }

    PhysicsBodyIds JoltWorld::QueryBox(const Vector3& center, const Vector3& halfExtent) const
    {
        const Impl& impl = *m_Impl;
        if (impl.physics == nullptr || !IsFinite(center) || !IsFinite(halfExtent) || halfExtent.X <= 0.0f || halfExtent.Y <= 0.0f || halfExtent.Z <= 0.0f)
        {
            return {};
        }

        // Radius 0: the default convex radius would round the corners of the query.
        JPH::BoxShapeSettings box(ToJolt(halfExtent), 0.0f);
        box.SetEmbedded();
        const auto shape = box.Create();
        if (shape.HasError())
        {
            return {};
        }

        JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> hits;
        impl.physics->GetNarrowPhaseQueryNoLock().CollideShape(shape.Get(), JPH::Vec3::sReplicate(1.0f), JPH::RMat44::sTranslation(ToJolt(center)),
            JPH::CollideShapeSettings{}, JPH::RVec3::sZero(), hits, JPH::SpecifiedBroadPhaseLayerFilter(MovingBroadPhase),
            JPH::SpecifiedObjectLayerFilter(MovingLayer));

        // Hits arrive in broad phase order; serials are unique per live body.
        std::vector<std::uint32_t> indices;
        indices.reserve(hits.mHits.size());
        for (const JPH::CollideShapeResult& hit : hits.mHits)
        {
            indices.push_back(static_cast<std::uint32_t>(impl.physics->GetBodyInterfaceNoLock().GetUserData(hit.mBodyID2)));
        }
        const auto bySerial = [&impl](std::uint32_t a, std::uint32_t b) { return impl.slots[a].serial < impl.slots[b].serial; };
        std::sort(indices.begin(), indices.end(), bySerial);
        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());

        PhysicsBodyIds found;
        found.reserve(indices.size());
        for (const std::uint32_t index : indices)
        {
            found.push_back(impl.Handle(index));
        }
        return found;
    }
}
