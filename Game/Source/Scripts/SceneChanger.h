#pragma once

#include <Engine.h>

class SceneChanger final : public Canape::MonoBehaviour
{
public:
    using SceneFactory = Canape::Scene* (*)(Canape::SceneManager&);

    explicit SceneChanger(SceneFactory nextScene, Canape::Key key = Canape::Key::Space)
        : m_NextScene(nextScene)
        , m_Key(key)
    {
    }

protected:
    void Update() override;

private:
    SceneFactory m_NextScene;
    Canape::Key m_Key;
};
