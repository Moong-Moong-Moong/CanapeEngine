#pragma once

#include <Engine.h>

class GarbageCollector final : public Canape::ScriptComponent
{
    RTTR_ENABLE(Canape::ScriptComponent)
    RTTR_REGISTRATION_FRIEND

public:
    explicit GarbageCollector(Canape::Key key = Canape::Key::Delete)
        : m_Key(key)
    {
    }

protected:
    void Update() override;

private:
    Canape::Key m_Key;
};
