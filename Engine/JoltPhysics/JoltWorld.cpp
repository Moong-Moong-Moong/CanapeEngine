#include "JoltPhysics/JoltWorld.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyLockMulti.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include "JoltPhysics/JoltConvert.h"
#include "JoltPhysics/JoltRuntimeImpl.h"
#include "JoltPhysics/SlotMap.h"

#include "Math/Quaternion.h"
#include "Math/Scalar.h"
#include "Math/Vector3.h"

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

        JPH::Quat ToJoltUnit(const Quaternion& q)
        {
            const JPH::Quat result = ToJolt(q);
            return result.IsNormalized() ? result : result.Normalized();
        }

        bool IsUsablePose(const PhysicsPose& pose)
        {
            return IsFinite(pose.position) && IsUsableRotation(pose.rotation);
        }

        std::optional<JPH::Vec3> ToJoltUnitAxis(const Vector3& v)
        {
            if (!IsFinite(v)) return std::nullopt;

            const JPH::Vec3 axis = ToJolt(v);
            if (std::abs(axis.LengthSq() - 1.0f) > UnitLengthSquaredTolerance)
            {
                return std::nullopt;
            }
            return axis.Normalized();
        }

        // A body that reached the target would otherwise keep a velocity of a few ulps and never read as resting.
        bool HasReachedTarget(const JPH::RVec3& position, const JPH::Quat& rotation, const Vector3& targetPosition, const JPH::Quat& targetRotation)
        {
            constexpr float KinematicRotationTolerance = 1.0e-6f;
            constexpr float KinematicPositionToleranceInEpsilons = 4.0f;

            const Vector3 delta = ToCanape(JPH::Vec3(position)) - targetPosition;
            const float scale = std::max({ 1.0f, std::abs(targetPosition.X), std::abs(targetPosition.Y), std::abs(targetPosition.Z) });
            const float positionTolerance = KinematicPositionToleranceInEpsilons * std::numeric_limits<float>::epsilon() * scale;
            const bool positionMissed =
                std::abs(delta.X) > positionTolerance ||
                std::abs(delta.Y) > positionTolerance ||
                std::abs(delta.Z) > positionTolerance;
            if (positionMissed)
            {
                return false;
            }
            const JPH::Quat step = targetRotation * rotation.Conjugated();
            return step.GetXYZ().Length() <= KinematicRotationTolerance;
        }

        bool IsIdentityPose(const PhysicsPose& pose)
        {
            return pose.position == Vector3::Zero &&
                pose.rotation == Quaternion::Identity;
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
                if (!IsNonNegative(*part.friction))
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
                if (!IsPositive(h))
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
                if (!IsPositive(part.radius))
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
            return IsUsablePose(desc.pose) && IsValidSettings(desc.settings);
        }
    }

    struct JoltWorld::Impl
    {
        struct BodySlot
        {
            JPH::BodyID id;
            std::uint64_t serial = 0;
            PhysicsMotion motion = PhysicsMotion::Static;
            float mass = 0.0f;
        };

        struct HingeSlot
        {
            JPH::Ref<JPH::HingeConstraint> constraint;
            JPH::BodyID body;
            std::uint32_t bodyIndex = 0;
            // Empty for a hinge fixed to the world.
            std::optional<std::uint32_t> mountIndex;
            JPH::BodyID mount;
            bool mountMoves = false;
        };

        using BodyHandle = SlotHandle<BodySlot>;
        using HingeHandle = SlotHandle<HingeSlot>;

        JoltRuntime::Impl* runtime = nullptr;
        std::uint32_t tag = NextWorldTag();
        std::uint32_t maxBodies = 0;
        // Built only with a valid runtime, since Jolt allocates through its allocator; the tables outlive the system.
        std::unique_ptr<JPH::ObjectLayerPairFilterTable> layerPairs;
        std::unique_ptr<JPH::BroadPhaseLayerInterfaceTable> broadPhaseLayers;
        std::unique_ptr<JPH::ObjectVsBroadPhaseLayerFilterTable> layerVsBroadPhase;
        std::unique_ptr<JPH::PhysicsSystem> physics;

        SlotMap<BodySlot> bodySlots;
        SlotMap<HingeSlot> hingeSlots;

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
            hingeSlots.ForEach([this](const HingeSlot& slot)
            {
                physics->RemoveConstraint(slot.constraint.GetPtr());
            });
            bodySlots.ForEach([this](const BodySlot& slot)
            {
                JPH::BodyInterface& bodies = physics->GetBodyInterfaceNoLock();
                bodies.RemoveBody(slot.id);
                bodies.DestroyBody(slot.id);
            });
        }

        Impl(const Impl&) = delete;
        Impl& operator=(const Impl&) = delete;

        bool HasRuntime() const
        {
            return runtime != nullptr && physics != nullptr;
        }

        const BodySlot* Find(PhysicsBodyId id) const
        {
            return id.world == tag ?
                bodySlots.Get(BodyHandle{ id.index, id.generation }) :
                nullptr;
        }

        PhysicsBodyId ToId(BodyHandle handle) const
        {
            return PhysicsBodyId{ handle.index, handle.generation, tag };
        }

        const HingeSlot* FindHinge(PhysicsHingeId id) const
        {
            return id.world == tag ?
                hingeSlots.Get(HingeHandle{ id.index, id.generation }) :
                nullptr;
        }

        void RemoveHingesOf(std::uint32_t bodyIndex)
        {
            const auto onBody = [bodyIndex](const HingeSlot& slot)
            {
                return slot.bodyIndex == bodyIndex || slot.mountIndex == bodyIndex;
            };

            for (const HingeHandle handle : hingeSlots.Finds(onBody))
            {
                physics->RemoveConstraint(hingeSlots.Get(handle)->constraint.GetPtr());
                hingeSlots.Remove(handle);
            }
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
        if (!impl.HasRuntime()) return {};

        const bool hasRoom = impl.bodySlots.GetCount() < impl.maxBodies;
        if (!hasRoom || !IsUsableDesc(desc))
        {
            return {};
        }

        const JPH::ShapeRefC shape = BuildShape(desc);
        if (shape == nullptr) return {};

        const PhysicsBodySettings& bodySettings = desc.settings;
        JPH::BodyCreationSettings settings(
            shape,
            ToJolt(desc.pose.position),
            ToJoltUnit(desc.pose.rotation),
            ToJoltMotion(bodySettings.motion),
            LayerOf(bodySettings.motion)
        );
        settings.mFriction = bodySettings.friction;
        settings.mRestitution = bodySettings.restitution;
        settings.mLinearDamping = bodySettings.linearDamping;
        settings.mAngularDamping = bodySettings.angularDamping;
        // Reduction merges coplanar contacts into one manifold that keeps a single part id.
        settings.mUseManifoldReduction = !HasFrictionOverride(desc);
        const bool dynamic = bodySettings.motion == PhysicsMotion::Dynamic;
        if (dynamic && bodySettings.mass > 0.0f)
        {
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = bodySettings.mass;
        }

        const JPH::EActivation activation = bodySettings.motion == PhysicsMotion::Static ?
            JPH::EActivation::DontActivate :
            JPH::EActivation::Activate;
        JPH::BodyInterface& bodies = impl.physics->GetBodyInterfaceNoLock();
        const JPH::BodyID bodyId = bodies.CreateAndAddBody(settings, activation);
        if (bodyId.IsInvalid())
        {
            return {};
        }

        const Impl::BodyHandle handle = impl.bodySlots.Add(
            Impl::BodySlot{
                .id = bodyId,
                .serial = impl.nextSerial++,
                .motion = bodySettings.motion,
                .mass = dynamic ?
                    settings.GetMassProperties().mMass :
                    0.0f
            }
        );
        bodies.SetUserData(bodyId, handle.index);
        return impl.ToId(handle);
    }

    void JoltWorld::DestroyBody(PhysicsBodyId id)
    {
        Impl& impl = *m_Impl;
        if (!impl.HasRuntime()) return;

        const auto slot = impl.Find(id);
        if (slot == nullptr) return;

        impl.RemoveHingesOf(id.index);
        JPH::BodyInterface& bodies = impl.physics->GetBodyInterfaceNoLock();
        bodies.RemoveBody(slot->id);
        bodies.DestroyBody(slot->id);
        impl.bodySlots.Remove(Impl::BodyHandle{ id.index, id.generation });
    }

    PhysicsHingeId JoltWorld::CreateHinge(const PhysicsHingeDesc& desc)
    {
        constexpr float PerpendicularTolerance = 1.0e-4f;

        Impl& impl = *m_Impl;
        if (!impl.HasRuntime()) return {};

        const Impl::BodySlot* bodySlot = impl.Find(desc.body);
        if (bodySlot == nullptr) return {};

        const bool dynamicBody = bodySlot->motion == PhysicsMotion::Dynamic;
        if (!dynamicBody)
        {
            return {};
        }

        const Impl::BodySlot* mountSlot = desc.mount.IsValid() ? impl.Find(desc.mount) : nullptr;
        const bool usableMount = !desc.mount.IsValid() || (mountSlot != nullptr && desc.mount != desc.body);
        if (!usableMount)
        {
            return {};
        }

        const std::optional<JPH::Vec3> axis = ToJoltUnitAxis(desc.axis);
        const std::optional<JPH::Vec3> normal = ToJoltUnitAxis(desc.normal);
        if (!axis || !normal) return{};

        const bool perpendicularAxes = std::abs(axis->Dot(*normal)) < PerpendicularTolerance;
        const bool usableJoint = IsFinite(desc.pivot) &&
            IsValidHingeLimits(desc.minAngle, desc.maxAngle) &&
            IsNonNegative(desc.maxFrictionTorque);
        if (!perpendicularAxes || !usableJoint)
        {
            return {};
        }

        JPH::HingeConstraintSettings settings;
        settings.SetEmbedded();
        settings.mSpace = JPH::EConstraintSpace::WorldSpace;
        settings.mPoint1 = settings.mPoint2 = ToJolt(desc.pivot);
        settings.mHingeAxis1 = settings.mHingeAxis2 = *axis;
        settings.mNormalAxis1 = settings.mNormalAxis2 = *normal;
        settings.mLimitsMin = desc.minAngle;
        settings.mLimitsMax = desc.maxAngle;
        settings.mMaxFrictionTorque = desc.maxFrictionTorque;

        // In world space the settings read both poses, so both bodies are locked while the constraint is made.
        JPH::Ref<JPH::HingeConstraint> constraint;
        if (mountSlot != nullptr)
        {
            const JPH::BodyID ids[2]{ mountSlot->id, bodySlot->id };
            JPH::BodyLockMultiWrite lock(impl.physics->GetBodyLockInterfaceNoLock(), ids, 2);
            JPH::Body* mountBody = lock.GetBody(0);
            JPH::Body* heldBody = lock.GetBody(1);
            if (mountBody == nullptr || heldBody == nullptr)
            {
                return {};
            }
            constraint = static_cast<JPH::HingeConstraint*>(settings.Create(*mountBody, *heldBody));
        }
        else
        {
            JPH::BodyLockWrite lock(impl.physics->GetBodyLockInterfaceNoLock(), bodySlot->id);
            if (!lock.Succeeded())
            {
                return {};
            }
            constraint = static_cast<JPH::HingeConstraint*>(settings.Create(JPH::Body::sFixedToWorld, lock.GetBody()));
        }
        impl.physics->AddConstraint(constraint.GetPtr());

        const Impl::HingeHandle handle = impl.hingeSlots.Add(
            Impl::HingeSlot{
                .constraint = std::move(constraint),
                .body = bodySlot->id,
                .bodyIndex = desc.body.index,
                .mountIndex = mountSlot ? std::optional{ desc.mount.index } : std::nullopt,
                .mount = mountSlot ? mountSlot->id : JPH::BodyID{},
                .mountMoves = mountSlot != nullptr && mountSlot->motion != PhysicsMotion::Static
            }
        );
        return PhysicsHingeId{
            handle.index,
            handle.generation,
            impl.tag
        };
    }

    void JoltWorld::ReleaseHinge(PhysicsHingeId id)
    {
        const auto slot = m_Impl->FindHinge(id);
        if (slot == nullptr) return;

        if (!slot->constraint->GetEnabled())
        {
            return;
        }
        slot->constraint->SetEnabled(false);

        JPH::BodyInterface& bodies = m_Impl->physics->GetBodyInterfaceNoLock();
        bodies.ActivateBody(slot->body);
        if (slot->mountMoves)
        {
            bodies.ActivateBody(slot->mount);
        }
    }

    void JoltWorld::DestroyHinge(PhysicsHingeId id)
    {
        Impl& impl = *m_Impl;
        const auto slot = impl.FindHinge(id);
        if (slot == nullptr) return;

        impl.physics->RemoveConstraint(slot->constraint.GetPtr());
        impl.hingeSlots.Remove(Impl::HingeHandle{ id.index, id.generation });
    }

    void JoltWorld::AddImpulse(PhysicsBodyId id, const Vector3& impulse)
    {
        const auto slot = m_Impl->Find(id);
        if (slot == nullptr) return;

        const bool dynamicBody = slot->motion == PhysicsMotion::Dynamic;
        if (!dynamicBody || !IsFinite(impulse))
        {
            return;
        }
        m_Impl->physics->GetBodyInterfaceNoLock().AddImpulse(
            slot->id,
            ToJolt(impulse)
        );
    }

    void JoltWorld::AddImpulseAt(PhysicsBodyId id, const Vector3& impulse, const Vector3& worldPoint)
    {
        const auto slot = m_Impl->Find(id);
        if (slot == nullptr) return;

        const bool dynamicBody = slot->motion == PhysicsMotion::Dynamic;
        if (!dynamicBody || !IsFinite(impulse) || !IsFinite(worldPoint))
        {
            return;
        }
        m_Impl->physics->GetBodyInterfaceNoLock().AddImpulse(
            slot->id,
            ToJolt(impulse),
            ToJolt(worldPoint)
        );
    }

    void JoltWorld::MoveKinematic(PhysicsBodyId id, const PhysicsPose& target, float dt)
    {
        const auto slot = m_Impl->Find(id);
        if (slot == nullptr) return;

        const bool kinematicBody = slot->motion == PhysicsMotion::Kinematic;
        if (!kinematicBody || !IsUsablePose(target) || !IsPositive(dt))
        {
            return;
        }

        JPH::BodyInterface& bodies = m_Impl->physics->GetBodyInterfaceNoLock();
        const JPH::Quat targetRotation = ToJoltUnit(target.rotation);
        JPH::RVec3 position;
        JPH::Quat rotation;
        bodies.GetPositionAndRotation(slot->id, position, rotation);
        if (HasReachedTarget(position, rotation, target.position, targetRotation))
        {
            bodies.SetLinearAndAngularVelocity(
                slot->id,
                JPH::Vec3::sZero(),
                JPH::Vec3::sZero()
            );
            return;
        }
        bodies.MoveKinematic(slot->id, ToJolt(target.position), targetRotation, dt);
    }

    void JoltWorld::ApplyWater(const PhysicsWaterDesc& water, float dt)
    {
        const Impl& impl = *m_Impl;
        if (!impl.HasRuntime()) return;

        const bool usableBox = IsFinite(water.center) && IsPositive(water.halfExtent);
        const bool usableFluid = IsPositive(water.density) && IsNonNegative(water.linearDrag) && IsNonNegative(water.angularDrag);
        if (!IsPositive(dt) || !usableBox || !usableFluid)
        {
            return;
        }

        const JPH::RVec3 surface = ToJolt(water.center + Vector3(0.0f, water.halfExtent.Y, 0.0f));
        const JPH::Vec3 gravity = impl.physics->GetGravity();
        for (const PhysicsBodyId id : QueryBox(water.center, water.halfExtent))
        {
            const auto slot = impl.Find(id);
            if(slot == nullptr) continue;

            const bool buoyant = slot->motion == PhysicsMotion::Dynamic && slot->mass > 0.0f;
            if (!buoyant)
            {
                continue;
            }
            // The Body overload, unlike the BodyInterface one, does not wake a sleeping body.
            JPH::BodyLockWrite lock(impl.physics->GetBodyLockInterfaceNoLock(), slot->id);
            const bool awake = lock.Succeeded() && lock.GetBody().IsActive();
            if (!awake)
            {
                continue;
            }
            JPH::Body& body = lock.GetBody();
            body.ApplyBuoyancyImpulse(
                surface,
                JPH::Vec3::sAxisY(),
                water.density * body.GetShape()->GetVolume() / slot->mass,
                water.linearDrag,
                water.angularDrag,
                JPH::Vec3::sZero(),
                gravity,
                dt
            );
        }
    }

    PhysicsStepError JoltWorld::Step(float dt)
    {
        PhysicsStepError error = PhysicsStepError::None;
        Impl& impl = *m_Impl;
        if (!impl.HasRuntime()) error |= PhysicsStepError::NoRuntime;
        if (!IsNonNegative(dt)) error |= PhysicsStepError::InvalidTimeStep;
        if (error != PhysicsStepError::None) return error;

        if (!impl.broadPhaseOptimized)
        {
            impl.physics->OptimizeBroadPhase();
            impl.broadPhaseOptimized = true;
        }

        const JPH::EPhysicsUpdateError update = impl.physics->Update(
            dt,
            1,
            impl.runtime->tempAllocator.get(),
            impl.runtime->jobSystem.get()
        );

        const auto raised = [update](JPH::EPhysicsUpdateError flag)
        {
            return (static_cast<JPH::uint32>(update) & static_cast<JPH::uint32>(flag)) != 0;
        };

        if (raised(JPH::EPhysicsUpdateError::ManifoldCacheFull))
        {
            error |= PhysicsStepError::ManifoldCacheFull;
        }
        if (raised(JPH::EPhysicsUpdateError::BodyPairCacheFull))
        {
            error |= PhysicsStepError::BodyPairCacheFull;
        }
        if (raised(JPH::EPhysicsUpdateError::ContactConstraintsFull))
        {
            error |= PhysicsStepError::ContactConstraintsFull;
        }
        return error;
    }

    bool JoltWorld::IsValid(PhysicsBodyId id) const
    {
        return m_Impl->Find(id) != nullptr;
    }

    PhysicsPose JoltWorld::GetPose(PhysicsBodyId id) const
    {
        const auto slot = m_Impl->Find(id);
        if (slot == nullptr) return {};

        JPH::RVec3 position;
        JPH::Quat rotation;
        m_Impl->physics->GetBodyInterfaceNoLock().GetPositionAndRotation(
            slot->id,
            position,
            rotation
        );
        return PhysicsPose{
            ToCanape(JPH::Vec3(position)),
            ToCanape(rotation)
        };
    }

    Vector3 JoltWorld::GetLinearVelocity(PhysicsBodyId id) const
    {
        const auto slot = m_Impl->Find(id);
        if (slot == nullptr) return {};

        return ToCanape(m_Impl->physics->GetBodyInterfaceNoLock().GetLinearVelocity(slot->id));
    }

    Vector3 JoltWorld::GetAngularVelocity(PhysicsBodyId id) const
    {
        const auto slot = m_Impl->Find(id);
        if (slot == nullptr) return {};

        return ToCanape(m_Impl->physics->GetBodyInterfaceNoLock().GetAngularVelocity(slot->id));
    }

    Vector3 JoltWorld::GetPointVelocity(PhysicsBodyId id, const Vector3& worldPoint) const
    {
        const auto slot = m_Impl->Find(id);
        if (slot == nullptr || !IsFinite(worldPoint)) return {};

        return ToCanape(m_Impl->physics->GetBodyInterfaceNoLock().GetPointVelocity(slot->id, ToJolt(worldPoint)));
    }

    float JoltWorld::GetMass(PhysicsBodyId id) const
    {
        const auto slot = m_Impl->Find(id);
        if (slot == nullptr) return 0.0f;

        return slot->mass;
    }

    bool JoltWorld::IsAwake(PhysicsBodyId id) const
    {
        const auto slot = m_Impl->Find(id);

        return slot != nullptr &&
            m_Impl->physics->GetBodyInterfaceNoLock().IsActive(slot->id);
    }

    PhysicsMotion JoltWorld::GetMotion(PhysicsBodyId id) const
    {
        const auto slot = m_Impl->Find(id);
        if (slot == nullptr) return PhysicsMotion::Static;

        return slot->motion;
    }

    bool JoltWorld::IsValid(PhysicsHingeId id) const
    {
        return m_Impl->FindHinge(id) != nullptr;
    }

    bool JoltWorld::IsHingeHeld(PhysicsHingeId id) const
    {
        const auto slot = m_Impl->FindHinge(id);
        return slot != nullptr && slot->constraint->GetEnabled();
    }

    float JoltWorld::GetHingeAngle(PhysicsHingeId id) const
    {
        const auto slot = m_Impl->FindHinge(id);
        if (slot == nullptr) return 0.0f;

        return slot->constraint->GetCurrentAngle();
    }

    std::size_t JoltWorld::GetBodyCount() const
    {
        return m_Impl->bodySlots.GetCount();
    }

    PhysicsWorldStats JoltWorld::GetStats() const
    {
        const Impl& impl = *m_Impl;
        if (!impl.HasRuntime()) return {};

        return PhysicsWorldStats{
            impl.bodySlots.GetCount(),
            impl.physics->GetNumActiveBodies(JPH::EBodyType::RigidBody)
        };
    }

    PhysicsBodyIds JoltWorld::QueryBox(const Vector3& center, const Vector3& halfExtent) const
    {
        const Impl& impl = *m_Impl;
        if (!impl.HasRuntime()) return {};

        if (!IsFinite(center) || !IsPositive(halfExtent))
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
        impl.physics->GetNarrowPhaseQueryNoLock().CollideShape(
            shape.Get(),
            JPH::Vec3::sReplicate(1.0f),
            JPH::RMat44::sTranslation(ToJolt(center)),
            JPH::CollideShapeSettings{},
            JPH::RVec3::sZero(),
            hits,
            JPH::SpecifiedBroadPhaseLayerFilter(MovingBroadPhase),
            JPH::SpecifiedObjectLayerFilter(MovingLayer)
        );

        // Hits arrive in broad phase order; serials are unique per live body.
        std::vector<Impl::BodyHandle> handles;
        handles.reserve(hits.mHits.size());
        for (const JPH::CollideShapeResult& hit : hits.mHits)
        {
            handles.push_back(impl.bodySlots.HandleAt(static_cast<std::uint32_t>(
                impl.physics->GetBodyInterfaceNoLock().GetUserData(hit.mBodyID2)
            )));
        }
        const auto bySerial = [&impl](Impl::BodyHandle a, Impl::BodyHandle b) {
            return impl.bodySlots.Get(a)->serial < impl.bodySlots.Get(b)->serial;
        };
        
        std::sort(handles.begin(), handles.end(), bySerial);
        handles.erase(std::unique(handles.begin(), handles.end()), handles.end());

        PhysicsBodyIds found;
        found.reserve(handles.size());
        for (const auto handle : handles)
        {
            found.push_back(impl.ToId(handle));
        }
        return found;
    }
}
