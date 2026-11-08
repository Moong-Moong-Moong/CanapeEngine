#include "Core/Reflection/Reflection.h"
#include "DX11Renderer/Data/MaterialDesc.h"
#include "DX11Renderer/Scene/RenderScene.h"
#include "Input/InputManager.h"
#include "JoltPhysics/PhysicsTypes.h"
#include "Math/Quaternion.h"
#include "Math/Scalar.h"
#include "Math/Vector2.h"
#include "Math/Vector3.h"
#include "Math/Vector4.h"
#include "Physics/BoxColliderComponent.h"
#include "Physics/ColliderComponent.h"
#include "Physics/HingeComponent.h"
#include "Physics/RigidBodyComponent.h"
#include "Physics/SphereColliderComponent.h"
#include "Physics/WaterVolumeComponent.h"
#include "Scene/Component/CameraComponent.h"
#include "Scene/Component/Component.h"
#include "Scene/Component/FogComponent.h"
#include "Scene/Component/LightComponent.h"
#include "Scene/Component/MeshRendererComponent.h"
#include "Scene/Component/RenderableComponent.h"
#include "Scene/Component/ScriptComponent.h"
#include "Scene/Component/SkyboxComponent.h"
#include "Scene/Component/TransformComponent.h"

#define CN_KEY_VALUE(name) rttr::value(#name, Key::name)

namespace Canape
{
	namespace
	{
		void RegisterMathTypes()
		{
			using namespace rttr;

			registration::class_<Vector2>("Vector2")
				.constructor<>()(policy::ctor::as_object)
				.property("X", &Vector2::X)
				.property("Y", &Vector2::Y);

			registration::class_<Vector3>("Vector3")
				.constructor<>()(policy::ctor::as_object)
				.property("X", &Vector3::X)
				.property("Y", &Vector3::Y)
				.property("Z", &Vector3::Z);

			registration::class_<Vector4>("Vector4")
				.constructor<>()(policy::ctor::as_object)
				.property("X", &Vector4::X)
				.property("Y", &Vector4::Y)
				.property("Z", &Vector4::Z)
				.property("W", &Vector4::W);

			registration::class_<Quaternion>("Quaternion")
				.constructor<>()(policy::ctor::as_object)
				.property("X", &Quaternion::X)
				.property("Y", &Quaternion::Y)
				.property("Z", &Quaternion::Z)
				.property("W", &Quaternion::W);
		}

		void RegisterEnums()
		{
			using namespace rttr;

			registration::enumeration<LightType>("LightType")
			(
				value("Directional", LightType::Directional),
				value("Point", LightType::Point),
				value("Spot", LightType::Spot)
			);

			registration::enumeration<FogMode>("FogMode")
			(
				value("Disabled", FogMode::Disabled),
				value("Linear", FogMode::Linear),
				value("Exp", FogMode::Exp),
				value("Exp2", FogMode::Exp2)
			);

			registration::enumeration<AlphaMode>("AlphaMode")
			(
				value("Opaque", AlphaMode::Opaque),
				value("Masked", AlphaMode::Masked),
				value("Transparent", AlphaMode::Transparent)
			);

			registration::class_<PhysicsPose>("PhysicsPose")
				.constructor<>()(policy::ctor::as_object)
				.property("position", &PhysicsPose::position)
				.property("rotation", &PhysicsPose::rotation);

			registration::enumeration<PhysicsMotion>("PhysicsMotion")
			(
				value("Static", PhysicsMotion::Static),
				value("Kinematic", PhysicsMotion::Kinematic),
				value("Dynamic", PhysicsMotion::Dynamic)
			);

			registration::enumeration<Easing>("Easing")
			(
				value("Linear", Easing::Linear),
				value("Smooth", Easing::Smooth),
				value("EaseIn", Easing::EaseIn),
				value("EaseOut", Easing::EaseOut)
			);

			registration::enumeration<Key>("Key")
			(
				CN_KEY_VALUE(None), CN_KEY_VALUE(Backspace), CN_KEY_VALUE(Tab), CN_KEY_VALUE(Enter),
				CN_KEY_VALUE(Shift), CN_KEY_VALUE(Ctrl), CN_KEY_VALUE(Alt), CN_KEY_VALUE(Escape),
				CN_KEY_VALUE(Space), CN_KEY_VALUE(Left), CN_KEY_VALUE(Up), CN_KEY_VALUE(Right),
				CN_KEY_VALUE(Down), CN_KEY_VALUE(Delete),
				CN_KEY_VALUE(Num0), CN_KEY_VALUE(Num1), CN_KEY_VALUE(Num2), CN_KEY_VALUE(Num3), CN_KEY_VALUE(Num4),
				CN_KEY_VALUE(Num5), CN_KEY_VALUE(Num6), CN_KEY_VALUE(Num7), CN_KEY_VALUE(Num8), CN_KEY_VALUE(Num9),
				CN_KEY_VALUE(A), CN_KEY_VALUE(B), CN_KEY_VALUE(C), CN_KEY_VALUE(D), CN_KEY_VALUE(E), CN_KEY_VALUE(F),
				CN_KEY_VALUE(G), CN_KEY_VALUE(H), CN_KEY_VALUE(I), CN_KEY_VALUE(J), CN_KEY_VALUE(K), CN_KEY_VALUE(L),
				CN_KEY_VALUE(M), CN_KEY_VALUE(N), CN_KEY_VALUE(O), CN_KEY_VALUE(P), CN_KEY_VALUE(Q), CN_KEY_VALUE(R),
				CN_KEY_VALUE(S), CN_KEY_VALUE(T), CN_KEY_VALUE(U), CN_KEY_VALUE(V), CN_KEY_VALUE(W), CN_KEY_VALUE(X),
				CN_KEY_VALUE(Y), CN_KEY_VALUE(Z),
				CN_KEY_VALUE(F1), CN_KEY_VALUE(F2), CN_KEY_VALUE(F3), CN_KEY_VALUE(F4), CN_KEY_VALUE(F5), CN_KEY_VALUE(F6),
				CN_KEY_VALUE(F7), CN_KEY_VALUE(F8), CN_KEY_VALUE(F9), CN_KEY_VALUE(F10), CN_KEY_VALUE(F11), CN_KEY_VALUE(F12),
				CN_KEY_VALUE(Backquote)
			);
		}

		void RegisterAssetTypes()
		{
			using namespace rttr;

			registration::class_<MaterialDesc>("MaterialDesc")
				.constructor<>()(policy::ctor::as_object)
				.property("baseColor", &MaterialDesc::baseColor)
				.property("roughness", &MaterialDesc::roughness)
				.property("metallic", &MaterialDesc::metallic)
				.property("alphaMode", &MaterialDesc::alphaMode)
				.property("alphaCutoff", &MaterialDesc::alphaCutoff);
		}

		void RegisterComponents()
		{
			using namespace rttr;

			registration::class_<Component>("Component")
				.property("enabled", &Component::IsEnabled, &Component::SetEnabled);

			registration::class_<TransformComponent>("TransformComponent")
				.property("localPosition", &TransformComponent::GetLocalPosition, &TransformComponent::SetLocalPosition)
				.property("localRotation", &TransformComponent::GetLocalRotation, &TransformComponent::SetLocalRotation)
				.property("localScale", &TransformComponent::GetLocalScale, &TransformComponent::SetLocalScale)
				.property("parent", &TransformComponent::GetParent, &TransformComponent::SetParent);

			registration::class_<CameraComponent>("CameraComponent")
				.constructor<>()(policy::ctor::as_raw_ptr)
				.property("fieldOfView", &CameraComponent::GetFieldOfView, &CameraComponent::SetFieldOfView)
				.property("nearClip", &CameraComponent::GetNearClip, &CameraComponent::SetNearClip)
				.property("farClip", &CameraComponent::GetFarClip, &CameraComponent::SetFarClip);

			registration::class_<LightComponent>("LightComponent")
				.constructor<>()(policy::ctor::as_raw_ptr)
				.property("type", &LightComponent::GetType, &LightComponent::SetType)
				.property("color", &LightComponent::GetColor, &LightComponent::SetColor)
				.property("intensity", &LightComponent::GetIntensity, &LightComponent::SetIntensity)
				.property("range", &LightComponent::GetRange, &LightComponent::SetRange)
				.property("innerAngle", &LightComponent::GetInnerAngle, &LightComponent::SetInnerAngle)
				.property("outerAngle", &LightComponent::GetOuterAngle, &LightComponent::SetOuterAngle)
				.property("castShadow", &LightComponent::GetCastShadow, &LightComponent::SetCastShadow);

			registration::class_<FogComponent>("FogComponent")
				.constructor<>()(policy::ctor::as_raw_ptr)
				.property("mode", &FogComponent::GetMode, &FogComponent::SetMode)
				.property("color", &FogComponent::GetColor, &FogComponent::SetColor)
				.property("startDistance", &FogComponent::GetStartDistance, &FogComponent::SetStartDistance)
				.property("endDistance", &FogComponent::GetEndDistance, &FogComponent::SetEndDistance)
				.property("density", &FogComponent::GetDensity, &FogComponent::SetDensity);

			registration::class_<SkyboxComponent>("SkyboxComponent")
				.constructor<>()(policy::ctor::as_raw_ptr)
				.property("cubemap", &SkyboxComponent::GetCubemapPath, &SkyboxComponent::SetCubemapPath)
				.property("brightness", &SkyboxComponent::GetBrightness, &SkyboxComponent::SetBrightness)
				.property("rotation", &SkyboxComponent::GetRotation, &SkyboxComponent::SetRotation)
				.property("ambientColor", &SkyboxComponent::GetAmbientColor, &SkyboxComponent::SetAmbientColor)
				.property("ambientIntensity", &SkyboxComponent::GetAmbientIntensity, &SkyboxComponent::SetAmbientIntensity);

			registration::class_<RenderableComponent>("RenderableComponent");

			registration::class_<MeshRendererComponent>("MeshRendererComponent")
				.constructor<>()(policy::ctor::as_raw_ptr)
				.property("mesh", &MeshRendererComponent::GetMeshPath, &MeshRendererComponent::SetMeshPath)
				.property("materials", &MeshRendererComponent::GetMaterialNames, &MeshRendererComponent::SetMaterialNames);

			registration::class_<ScriptComponent>("ScriptComponent");

			registration::class_<RigidBodyComponent>("RigidBodyComponent")
				.constructor<>()(policy::ctor::as_raw_ptr)
				.property("motion", &RigidBodyComponent::GetMotion, &RigidBodyComponent::SetMotion)
				.property("mass", &RigidBodyComponent::GetMass, &RigidBodyComponent::SetMass)
				.property("friction", &RigidBodyComponent::GetFriction, &RigidBodyComponent::SetFriction)
				.property("restitution", &RigidBodyComponent::GetRestitution, &RigidBodyComponent::SetRestitution)
				.property("linearDamping", &RigidBodyComponent::GetLinearDamping, &RigidBodyComponent::SetLinearDamping)
				.property("angularDamping", &RigidBodyComponent::GetAngularDamping, &RigidBodyComponent::SetAngularDamping);

			registration::class_<HingeComponent>("HingeComponent")
				.constructor<>()(policy::ctor::as_raw_ptr)
				.property("axis", &HingeComponent::GetAxis, &HingeComponent::SetAxis)
				.property("minAngle", &HingeComponent::GetMinAngle, &HingeComponent::SetMinAngle)
				.property("maxAngle", &HingeComponent::GetMaxAngle, &HingeComponent::SetMaxAngle)
				.property("maxFrictionTorque", &HingeComponent::GetMaxFrictionTorque, &HingeComponent::SetMaxFrictionTorque);

			registration::class_<ColliderComponent>("ColliderComponent")
				.property("overrideFriction", &ColliderComponent::GetOverrideFriction, &ColliderComponent::SetOverrideFriction)
				.property("friction", &ColliderComponent::GetFriction, &ColliderComponent::SetFriction);

			registration::class_<BoxColliderComponent>("BoxColliderComponent")
				.constructor<>()(policy::ctor::as_raw_ptr)
				.property("size", &BoxColliderComponent::GetSize, &BoxColliderComponent::SetSize);

			registration::class_<SphereColliderComponent>("SphereColliderComponent")
				.constructor<>()(policy::ctor::as_raw_ptr)
				.property("radius", &SphereColliderComponent::GetRadius, &SphereColliderComponent::SetRadius);

			registration::class_<WaterVolumeComponent>("WaterVolumeComponent")
				.constructor<>()(policy::ctor::as_raw_ptr)
				.property("size", &WaterVolumeComponent::GetSize, &WaterVolumeComponent::SetSize)
				.property("density", &WaterVolumeComponent::GetDensity, &WaterVolumeComponent::SetDensity)
				.property("linearDrag", &WaterVolumeComponent::GetLinearDrag, &WaterVolumeComponent::SetLinearDrag)
				.property("angularDrag", &WaterVolumeComponent::GetAngularDrag, &WaterVolumeComponent::SetAngularDrag);
		}
	}

	void RegisterEngineTypes()
	{
		static bool registered = false;
		if (registered)
		{
			return;
		}
		registered = true;

		RegisterMathTypes();
		RegisterEnums();
		RegisterAssetTypes();
		RegisterComponents();
	}
}
