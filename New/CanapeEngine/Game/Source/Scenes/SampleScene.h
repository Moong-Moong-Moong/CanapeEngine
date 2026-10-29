#pragma once

#include <Engine.h>

class SampleScene
{
public:
    static constexpr char Name[] = "SampleScene";

    static Canape::Scene* Create(Canape::SceneManager& sceneManager);
};
