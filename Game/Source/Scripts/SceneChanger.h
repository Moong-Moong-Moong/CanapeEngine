#pragma once

#include <Engine.h>

#include <string>
#include <utility>

class SceneChanger final : public Canape::ScriptComponent
{
    RTTR_ENABLE(Canape::ScriptComponent)
    RTTR_REGISTRATION_FRIEND

public:
    explicit SceneChanger(std::string nextScene = {}, Canape::Key key = Canape::Key::Space)
        : m_NextScene(std::move(nextScene))
        , m_Key(key)
    {
    }

protected:
    void Update() override;

private:
    std::string m_NextScene;
    Canape::Key m_Key;
};
