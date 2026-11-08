#pragma once

#include "JoltPhysics/PhysicsTypes.h"
#include "Math/Vector3.h"
#include "Scene/Component/Component.h"

#include <cstdint>

namespace Canape
{
	class PhysicsScene;

	// Why a RigidBody that should have a body has none.
	enum class RigidBodyIssue : std::uint8_t
	{
		None,
		NestedBody,
		DuplicateOnObject,
		NoCollider,
		InvalidExtent,
		InvalidSettings,
		BackendRefused,
	};

	inline constexpr bool IsAuthoringIssue(RigidBodyIssue issue)
	{
		return issue == RigidBodyIssue::NestedBody ||
            issue == RigidBodyIssue::DuplicateOnObject ||
			issue == RigidBodyIssue::NoCollider ||
            issue == RigidBodyIssue::InvalidExtent ||
            issue == RigidBodyIssue::InvalidSettings;
	}

	inline const char* IssueText(RigidBodyIssue issue)
	{
		switch (issue)
		{
		case RigidBodyIssue::NestedBody:
            return "nested bodies are not supported";
		case RigidBodyIssue::DuplicateOnObject:
            return "a second RigidBody on one object is ignored";
		case RigidBodyIssue::NoCollider:
            return "no collider";
		case RigidBodyIssue::InvalidExtent:
            return "a collider size or radius is not finite or not above zero";
		case RigidBodyIssue::InvalidSettings:
            return "mass, friction, restitution and damping must be finite and 0 or more";
		case RigidBodyIssue::BackendRefused:
            return "the backend refused the body";
		default:
            return "none";
		}
	}

	class RigidBodyComponent final : public Component
	{
		RTTR_ENABLE(Component)

	public:
		RigidBodyComponent() = default;

		//***** Authoring
		// Changes made after the body exists take effect when the body is rebuilt.
		const PhysicsBodySettings& GetSettings() const { return m_Settings; }

		PhysicsMotion GetMotion() const { return m_Settings.motion; }
		void SetMotion(PhysicsMotion motion) { m_Settings.motion = motion; }
		// 0 takes the shape's mass at 1000 kg/m^3.
		float GetMass() const { return m_Settings.mass; }
		void SetMass(float mass) { m_Settings.mass = mass; }
		float GetFriction() const { return m_Settings.friction; }
		void SetFriction(float friction) { m_Settings.friction = friction; }
		float GetRestitution() const { return m_Settings.restitution; }
		void SetRestitution(float restitution) { m_Settings.restitution = restitution; }
		float GetLinearDamping() const { return m_Settings.linearDamping; }
		void SetLinearDamping(float damping) { m_Settings.linearDamping = damping; }
		float GetAngularDamping() const { return m_Settings.angularDamping; }
		void SetAngularDamping(float damping) { m_Settings.angularDamping = damping; }

		//***** Runtime
		// The owning scene's PhysicsScene keeps the runtime state; these forward to it.
		bool HasBody() const;
		bool HasFailed() const { return GetIssue() != RigidBodyIssue::None; }
		RigidBodyIssue GetIssue() const;

		// Before the body exists the impulse is queued in call order and applied right after the body is created
		// at the next sync; the queue is dropped at that sync when there is still no body, for any reason.
		void AddImpulse(const Vector3& impulse);
		// Where a kinematic body is to be after the next step; the body is carried there by velocity.
		// A kinematic body without a target holds the pose it was created at.
		void SetKinematicTarget(const PhysicsPose& target);
		// The velocity of a world-space point moving with the body, as of the last step; zero without a body.
		Vector3 GetVelocityAt(const Vector3& worldPoint) const;

	private:
		// Settings that fail IsValidSettings are an authoring issue.
		PhysicsBodySettings m_Settings;
	};
}
