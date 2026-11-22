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
    // Esc나 포커스를 잃으면(Alt+Tab 등) 푼다. 잠기지 않은 상태에서 Esc를 누르면 종료한다.
    void UpdateCursorLock();
    void SetCursorLocked(bool locked);

    bool m_CursorLocked = false;
    bool m_WasFocused = false;
};
