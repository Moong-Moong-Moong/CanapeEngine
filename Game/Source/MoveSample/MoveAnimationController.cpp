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

    Vector3 move = Vector3::Zero;
    if (input.IsKeyDown(Key::Up)) move.Z += 1.0f;
    if (input.IsKeyDown(Key::Down)) move.Z -= 1.0f;
    if (input.IsKeyDown(Key::Left)) move.X -= 1.0f;
    if (input.IsKeyDown(Key::Right)) move.X += 1.0f;
    Vector3 moveAmount = move * m_moveSpeed * Time::DeltaTime();

    TransformComponent* transform = GetTransform();
    transform->SetLocalPosition(transform->GetLocalPosition() + moveAmount);

    //***** 캐릭터 회전
    if (!move.IsNearlyZero())
    {
        Vector3 dir = move.Normalized();
        Quaternion target = Quaternion::LookRotation(dir);
        Quaternion rotationAmount = Quaternion::RotateTowards(
            transform->GetLocalRotation(), target, Math::ToRadians(m_turnSpeed) * Time::DeltaTime()
        );
        transform->SetLocalRotation(rotationAmount);

    }

    //***** StateMachine 컨트롤
    m_stateMachine->SetFloat("moveSpeed", move.Length());

    //***** 애니메이션 실행
    m_stateMachine->Update(Time::DeltaTime());
}


void MoveAnimationController::FixedUpdate()
{
}


