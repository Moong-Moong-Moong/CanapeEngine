#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "JoltPhysics/JoltRuntime.h"
#include "JoltPhysics/PhysicsTypes.h"

namespace Canape
{
    using PhysicsBodyIds = std::vector<PhysicsBodyId>;

    // One Jolt PhysicsSystem. Single threaded; the runtime must outlive the world.
    // Failures are values: an invalid body id, or a PhysicsStepError from Step.
    class JoltWorld
    {
    public:
        explicit JoltWorld(JoltRuntime& runtime, const PhysicsWorldDesc& desc = {});
        ~JoltWorld();

        JoltWorld(const JoltWorld&) = delete;
        JoltWorld& operator=(const JoltWorld&) = delete;

        PhysicsBodyId CreateBody(const PhysicsBodyDesc& desc);
        void DestroyBody(PhysicsBodyId id);
        PhysicsHingeId CreateHinge(const PhysicsHingeDesc& desc);
        // Disables the hinge and wakes its moving bodies; the id stays valid and the angle keeps reading.
        void ReleaseHinge(PhysicsHingeId id);
        // Removes the hinge; the id becomes invalid.
        void DestroyHinge(PhysicsHingeId id);

        // Changes a dynamic body's velocity at once and wakes it;
        // call before ApplyWater so a woken body floats on this Step.
        void AddImpulse(PhysicsBodyId id, const Vector3& impulse);
        void AddImpulseAt(PhysicsBodyId id, const Vector3& impulse, const Vector3& worldPoint);
        // Sets the velocity that brings a kinematic body to the pose over the next Step, which must use the same dt.
        // A no-op for any other body.
        void MoveKinematic(PhysicsBodyId id, const PhysicsPose& target, float dt);
        // Buoyancy and drag over the next Step, which must use the same dt, for the awake dynamic bodies in the box, in creation order.
        void ApplyWater(const PhysicsWaterDesc& water, float dt);

        PhysicsStepError Step(float dt);

        bool IsValid(PhysicsBodyId id) const;
        PhysicsPose GetPose(PhysicsBodyId id) const;
        Vector3 GetLinearVelocity(PhysicsBodyId id) const;
        Vector3 GetAngularVelocity(PhysicsBodyId id) const;
        // The velocity of a world-space point moving with the body; zero for a missing body or a point that is not finite.
        Vector3 GetPointVelocity(PhysicsBodyId id, const Vector3& worldPoint) const;
        float GetMass(PhysicsBodyId id) const;
        bool IsAwake(PhysicsBodyId id) const;
        PhysicsMotion GetMotion(PhysicsBodyId id) const;

        bool IsValid(PhysicsHingeId id) const;
        bool IsHingeHeld(PhysicsHingeId id) const;
        float GetHingeAngle(PhysicsHingeId id) const;

        std::size_t GetBodyCount() const;
        PhysicsWorldStats GetStats() const;

        // Moving bodies whose shape overlaps the box, sleeping ones included, in creation order.
        PhysicsBodyIds QueryBox(const Vector3& center, const Vector3& halfExtent) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
