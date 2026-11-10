#pragma once

#include <cstdint>

#include <Engine.h>

// The toggle key switches a physics stats line in the debug log, once per simulated second.
// TODO: [Editor] the stats could be an editor panel or a console command.
class PhysicsStatsLog final: public Canape::ScriptComponent {
    RTTR_ENABLE(Canape::ScriptComponent)
    RTTR_REGISTRATION_FRIEND

private:
    Canape::Key toggleKey = Canape::Key::I;

    // Runtime state, not registered.
    bool logging = false;
    std::uint64_t loggedTicks = 0;

public:
    PhysicsStatsLog() = default;

    bool IsLogging() const { return logging; }

protected:
    void FixedUpdate() override;
    void Update() override;
};
