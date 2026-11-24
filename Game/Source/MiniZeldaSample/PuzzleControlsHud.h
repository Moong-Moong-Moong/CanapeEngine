#pragma once

#include <Engine.h>

#include <initializer_list>
#include <string>
#include <utility>

// 퍼즐 조작법 안내 패널. 화면 오른쪽 위의 회색 패널에 흰 글씨로, 모든 퍼즐이 같은 크기로 보인다.
// 석판 스크립트가 Start에서 Create로 만들고, 퍼즐 카메라로 바꿀 때 SetShown(true), 돌아올 때 SetShown(false)한다.
class PuzzleControlsHud
{
public:
    // owner의 오브젝트에 화면 캔버스를 붙이고 패널을 만든다. 처음에는 숨겨져 있다.
    // lines: { 입력, 동작 } 쌍. "입력 - 동작"으로 한 줄씩 보여준다. height가 0이면 퍼즐 공통 높이.
    void Create(const Canape::ScriptComponent& owner, std::initializer_list<std::pair<std::string, std::string>> lines,
        const std::string& title = "조작법", float height = 0.0f);
    void SetShown(bool shown);

private:
    Canape::UICanvasComponent* m_Canvas = nullptr;
};
