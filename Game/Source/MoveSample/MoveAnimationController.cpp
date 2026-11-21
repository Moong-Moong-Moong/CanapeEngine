#include "MoveSample/MoveAnimationController.h"
#include "Scene/GameObject/GameObject.h"
#include "Scene/Component/AnimatorComponent.h"
#include "FSM/StateMachine.h"
#include "input/InputManager.h"

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
    if (input.IsKeyDown(Key::Up)) lastZ = 1.0f;
    if (input.IsKeyDown(Key::Down)) lastZ = -1.0f;
    if (input.IsKeyDown(Key::Right)) lastX = 1.0f;
    if (input.IsKeyDown(Key::Left)) lastX = -1.0f;

    move.X = lastX;
    move.Z = lastZ;

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
    m_stateMachine->SetBool("attackInput", input.IsKeyPressed(Key::D));
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
