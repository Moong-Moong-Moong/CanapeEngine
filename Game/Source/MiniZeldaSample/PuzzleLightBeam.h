#pragma once

#include <Engine.h>

#include <string>

// 퍼즐 석상 위로 하늘에서 내려오는 빛기둥. 이 오브젝트의 자식 메쉬들(반투명 실린더)이 빛기둥이다.
// puzzleName 퍼즐(TiltMazePuzzle / LeverPuzzle / MarbleRun)이 풀리면 fadeTime초 동안 가늘어지며 사라지고,
// 개발자 모드 리셋으로 다시 안 풀린 상태가 되면 되살아난다. 켜져 있는 동안은 폭이 살짝 일렁인다.
class PuzzleLightBeam final : public Canape::ScriptComponent
{
    RTTR_ENABLE(Canape::ScriptComponent)
    RTTR_REGISTRATION_FRIEND

protected:
    void Start() override;
    void Update() override;

private:
    bool IsPuzzleSolved() const;
    void SetBeamVisible(bool visible);

    //***** Authoring
    std::string m_PuzzleName;
    float m_FadeTime = 1.2f;
    float m_PulseAmount = 0.05f; // 폭 일렁임 비율
    float m_PulseSpeed = 1.6f;   // 초당 라디안

    //***** Runtime (직렬화 안 함)
    Canape::Vector3 m_BaseScale{ 1.0f, 1.0f, 1.0f };
    float m_Amount = 1.0f; // 1 = 켜짐, 0 = 사라짐
    float m_Clock = 0.0f;
    bool m_Visible = true;
};
