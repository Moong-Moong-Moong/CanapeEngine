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
};
