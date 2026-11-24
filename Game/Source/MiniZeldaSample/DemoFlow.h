#pragma once

#include <Engine.h>

#include <string>

#include "MiniZeldaSample/PuzzleControlsHud.h"

// 데모 전체 흐름: 인트로 -> 플레이 -> 엔딩 -> 재시작.
//
// - 인트로: 모닥불을 가까이 비추는 introCamera로 시작하고, 화면 위쪽 가운데에 "좌클릭 시 데모 시작"을 띄운다.
//   좌클릭하면 introCamera가 blendTime초 동안 주인공 카메라(mainCamera) 자리로 부드럽게 날아가고,
//   도착하면 주인공 카메라로 바꾸고 조작을 넘긴다.
// - 조작을 넘긴 뒤 controlsTime초 동안 오른쪽 위에 조작법 패널(퍼즐 패널과 같은 모양)을 보여 준다.
//   퍼즐 석판이나 연출로 카메라가 바뀌어 있는 동안에는 잠깐 숨긴다.
// - 엔딩: 고대의 문이 열린 뒤 주인공이 gateTrigger 근처(문 안쪽)로 들어가면, 조작을 막고 카메라가 다시 모닥불로
//   날아가면서 "데모를 플레이해주셔서 감사합니다"와 그 아래 "좌클릭 시 재시작"을 띄운다. 좌클릭하면 씬을 다시 연다.
class DemoFlow final : public Canape::ScriptComponent
{
    RTTR_ENABLE(Canape::ScriptComponent)
    RTTR_REGISTRATION_FRIEND

protected:
    void Start() override;
    void Update() override;

private:
    enum class Phase { Intro, BlendIn, Playing, BlendOut, Ending };

    void BuildUi();
    void ShowIntroText();
    void ShowEndingText();
    void HideTexts();
    void SetPlayerControl(bool enabled);
    void SetCamera(const Handle<Canape::GameObject>& cameraObject);
    bool IsPlayerInsideGate();
    void UpdateBlend(const Canape::Vector3& fromPosition, const Canape::Quaternion& fromRotation,
        const Canape::Vector3& toPosition, const Canape::Quaternion& toRotation);
    void Restart();

    //***** Authoring
    Handle<Canape::GameObject> m_IntroCamera{}; // 모닥불을 비추는 카메라(시작/엔딩)
    Handle<Canape::GameObject> m_MainCamera{};  // 주인공을 따라가는 카메라(BackViewCamera)
    Handle<Canape::GameObject> m_Gate{};        // AncientGate가 붙은 오브젝트
    Handle<Canape::GameObject> m_GateTrigger{}; // 문 안쪽 판정 위치(문 사이의 어둠 판)
    std::string m_PlayerName = "Player";
    float m_TriggerRadius = 1.6f;   // gateTrigger에서 이 거리(수평, m) 안으로 들어가면 엔딩
    float m_BlendTime = 3.0f;       // 카메라가 날아가는 시간(초)
    float m_ControlsTime = 30.0f;   // 조작법 패널을 보여 주는 시간(초)
    std::string m_RestartScene = "Taejun/SampleScene.json"; // 씬 폴더 기준. 에디터 플레이 중이면 플레이 시작 상태로 재시작

    //***** Runtime (직렬화 안 함)
    Phase m_Phase = Phase::Intro;
    float m_Timer = 0.0f;
    float m_ControlsTimer = 0.0f;
    Canape::Vector3 m_IntroPosition{};
    Canape::Quaternion m_IntroRotation = Canape::Quaternion::Identity;
    Canape::Vector3 m_BlendFromPosition{};
    Canape::Quaternion m_BlendFromRotation = Canape::Quaternion::Identity;
    PuzzleControlsHud m_ControlsHud;
    Canape::UICanvasComponent* m_TextCanvas = nullptr;
    Canape::UIText* m_TitleShadow = nullptr;
    Canape::UIText* m_Title = nullptr;
    Canape::UIText* m_SubShadow = nullptr;
    Canape::UIText* m_Sub = nullptr;
};
