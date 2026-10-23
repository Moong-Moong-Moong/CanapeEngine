#pragma once

#include <Engine.h>

class GarbageCollector final : public Canape::ScriptComponent
{
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
