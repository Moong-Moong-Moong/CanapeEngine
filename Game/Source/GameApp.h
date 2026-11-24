#pragma once

#include <Engine.h>

class GameApp final : public Canape::Application
{
public:
    GameApp();

protected:
    void OnInit() override;
    void OnUpdate(float deltaTime) override;
    void OnShutdown() override;

private:
    // 마우스 포인터 잠금: 숨기고 창 안에 가둔다. 창이 활성화되거나 창 안을 클릭하면 잠그고,
    // Esc나 포커스를 잃으면(Alt+Tab 등) 푼다. 풀린 상태에서 Esc를 한 번 더 누르면 종료한다.
    // 스크립트가 포인터를 요청하는 동안(InputManager::SetCursorRequested, 예: 레버 퍼즐)에도 풀어서 보여 준다.
    void UpdateCursorLock();
    void SetCursorLocked(bool locked);

    bool m_CursorLocked = false;
    bool m_WasFocused = false;
    bool m_ReleasedByUser = false; // Esc로 푼 상태(창 안을 클릭하거나 다시 활성화되면 해제)
};
