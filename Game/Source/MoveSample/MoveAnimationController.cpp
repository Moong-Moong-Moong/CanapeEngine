#include "MoveSample/MoveAnimationController.h"
#include "Scene/GameObject/GameObject.h"
#include "Scene/Component/AnimatorComponent.h"
#include "FSM/StateMachine.h"
#include "input/InputManager.h"
#include "Scene/Scene.h"
#include "Scene/Component/CameraComponent.h"

RTTR_REGISTRATION
{
    rttr::registration::class_<MoveAnimationController>("MoveAnimationController")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr);
}

void MoveAnimationController::Awake()
{

}

void MoveAnimationController::Start()
{
    m_stateMachine = GetOwnerObject()->GetComponent<StateMachine>();
    m_stateMachine->Initialize();
}

void MoveAnimationController::Update()
{
    //***** 캐릭터 이동
    InputManager& input = Application::Get().GetInputManager();

    float lastX = 0.0f;
    float lastZ = 0.0f;

    Vector3 move = Vector3::Zero;
    if (input.IsKeyDown(Key::W)) lastZ = 1.0f;
    if (input.IsKeyDown(Key::S)) lastZ = -1.0f;
    if (input.IsKeyDown(Key::D)) lastX = 1.0f;
    if (input.IsKeyDown(Key::A)) lastX = -1.0f;

    //*** 카메라 기준 이동: W는 카메라가 보는 방향, D는 카메라의 오른쪽 (수평면 기준)
    Vector3 forward = Vector3::Forward;
    if (CameraComponent* camera = GetOwnerObject()->GetScene()->GetMainCamera())
    {
        const Vector3 cameraForward = camera->GetTransform()->GetForward();
        const Vector3 flat(cameraForward.X, 0.0f, cameraForward.Z);
        if (!flat.IsNearlyZero())
        {
            forward = flat.Normalized();
        }
    }
    const Vector3 right(forward.Z, 0.0f, -forward.X);

    move = right * lastX + forward * lastZ;
    if (move.Length() > 1.0f)
    {
        move = move.Normalized(); // 대각선도 같은 속도
    }

    const bool attacking = m_stateMachine->IsStateActive("AttackState");

    TransformComponent* transform = GetTransform();
    if (!attacking)
    {
        Vector3 moveAmount = move * m_moveSpeed * Time::DeltaTime();
        transform->SetPosition(transform->GetPosition() + moveAmount);
    }

    //***** 캐릭터 회전
    if (!attacking)
    {
        TurnTowards(move);
    }

    //***** StateMachine 컨트롤
    m_stateMachine->SetFloat("moveSpeed", move.Length());
    m_stateMachine->SetBool("attackInput", input.IsKeyPressed(Key::F));
}

void MoveAnimationController::LateUpdate()
{
    //***** 애니메이션 실행
    m_stateMachine->Update(Time::DeltaTime());
}

void MoveAnimationController::SetMoveSpeed(float speed)
{
    m_moveSpeed = speed;
}

void MoveAnimationController::FixedUpdate()
{
}

void MoveAnimationController::TurnTowards(const Vector3& direction)
{
    if (direction.IsNearlyZero())
    {
        return;
    }

    TransformComponent* transform = GetTransform();
    Quaternion target = Quaternion::LookRotation(direction.Normalized());
    Quaternion rotationAmount = Quaternion::RotateTowards(
        transform->GetRotation(), target, Math::ToRadians(m_turnSpeed) * Time::DeltaTime()
    );
    transform->SetRotation(rotationAmount);
}
