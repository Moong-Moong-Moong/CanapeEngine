#pragma once

#include <Engine.h>

#include <string>
#include <vector>

#include "MiniZeldaSample/PuzzleControlsHud.h"

// 개발자 모드. 퍼즐을 조작하고 있지 않을 때(필드에서) P를 짧은 시간 안에 연타하면 켜지고, O를 누르면 꺼진다.
// 켜져 있는 동안 오른쪽 위에 명령 목록이 보이고 숫자 키로 바로 실행한다.
//   1 / 2 / 3: 첫 번째 / 두 번째 / 세 번째 퍼즐 리셋(클리어 상태 포함)
//   4 / 5 / 6: 첫 번째 / 두 번째 / 세 번째 횃불 켜기
// 퍼즐은 이름(puzzle1~3Name)으로, 문은 gateName으로 찾는다. 퍼즐 석판에 들어가 있거나 연출 중에는 숨는다.
class DevMode final : public Canape::ScriptComponent
{
    RTTR_ENABLE(Canape::ScriptComponent)
    RTTR_REGISTRATION_FRIEND

protected:
    void Start() override;
    void Update() override;

private:
    bool IsAnyConsoleActive() const;
    void ResetPuzzle(int index);
    void LightTorch(int index);

    //***** Authoring
    std::string m_Puzzle1Name = "TiltMazePuzzle";
    std::string m_Puzzle2Name = "LeverPuzzle";
    std::string m_Puzzle3Name = "MarbleRun";
    std::string m_GateName = "AncientGate";
    int m_TapCount = 5;          // 이만큼 P를 누르면 켜진다
    float m_TapWindow = 1.5f;    // 그 횟수를 이 시간(초) 안에 눌러야 한다

    //***** Runtime (직렬화 안 함)
    bool m_On = false;
    std::vector<float> m_TapTimes;
    float m_Time = 0.0f;
    bool m_Shown = false;
    PuzzleControlsHud m_Hud;
};
