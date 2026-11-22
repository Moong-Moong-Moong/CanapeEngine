#include "MiniZeldaSample/DevMode.h"

#include "MiniZeldaSample/AncientGate.h"
#include "MiniZeldaSample/LeverConsole.h"
#include "MiniZeldaSample/LeverPuzzle.h"
#include "MiniZeldaSample/LeverTree.h"
#include "MiniZeldaSample/MarbleConsole.h"
#include "MiniZeldaSample/MarbleRun.h"
#include "MiniZeldaSample/TiltMazeConsole.h"
#include "MiniZeldaSample/TiltMazePuzzle.h"
#include "CNDebug/DebugLog.h"

#include <algorithm>

using namespace Canape;

RTTR_REGISTRATION
{
    rttr::registration::class_<DevMode>("DevMode")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr)
        .property("puzzle1Name", &DevMode::m_Puzzle1Name)
        .property("puzzle2Name", &DevMode::m_Puzzle2Name)
        .property("puzzle3Name", &DevMode::m_Puzzle3Name)
        .property("gateName", &DevMode::m_GateName)
        .property("tapCount", &DevMode::m_TapCount)
        .property("tapWindow", &DevMode::m_TapWindow);
}

namespace
{
    LeverTree* FindTreeBelow(TransformComponent& node)
    {
        if (GameObject* object = node.GetOwnerObject())
        {
            if (auto* tree = object->GetComponent<LeverTree>()) return tree;
        }
        for (TransformComponent* child : node.GetChildren())
        {
            if (child)
            {
                if (LeverTree* tree = FindTreeBelow(*child)) return tree;
            }
        }
        return nullptr;
    }
}

void DevMode::Start()
{
    m_Hud.Create(*this, {
        { "1", "첫 번째 퍼즐 리셋" },
        { "2", "두 번째 퍼즐 리셋" },
        { "3", "세 번째 퍼즐 리셋" },
        { "4", "첫 번째 횃불 켜기" },
        { "5", "두 번째 횃불 켜기" },
        { "6", "세 번째 횃불 켜기" },
        { "O", "개발자 모드 끄기" },
    }, "개발자 모드", 380.0f);
}

bool DevMode::IsAnyConsoleActive() const
{
    for (GameObject* object : GetOwnerObject()->GetScene()->GetAllGameObjects())
    {
        if (!object) continue;
        if (const auto* tilt = object->GetComponent<TiltMazeConsole>(); tilt && tilt->IsActive()) return true;
        if (const auto* lever = object->GetComponent<LeverConsole>(); lever && lever->IsActive()) return true;
        if (const auto* marble = object->GetComponent<MarbleConsole>(); marble && marble->IsActive()) return true;
    }
    return false;
}

void DevMode::ResetPuzzle(int index)
{
    const std::string* names[3] = { &m_Puzzle1Name, &m_Puzzle2Name, &m_Puzzle3Name };
    GameObject* puzzle = FindGameObjectByName(*names[index]);
    if (!puzzle)
    {
        CN_LOG_GAME("DevMode", "no puzzle named '", *names[index], "'");
        return;
    }

    if (auto* tilt = puzzle->GetComponent<TiltMazePuzzle>())
    {
        tilt->DevReset();
    }
    else if (auto* lever = puzzle->GetComponent<LeverPuzzle>())
    {
        lever->DevReset();
        if (LeverTree* tree = FindTreeBelow(*puzzle->GetTransform()))
        {
            tree->ResetTree();
        }
    }
    else if (auto* marble = puzzle->GetComponent<MarbleRun>())
    {
        marble->DevReset();
    }

    if (GameObject* gate = FindGameObjectByName(m_GateName))
    {
        if (auto* ancientGate = gate->GetComponent<AncientGate>())
        {
            ancientGate->DevForgetPuzzle(index);
        }
    }
    CN_LOG_GAME("DevMode", "reset puzzle ", index + 1, " ('", *names[index], "')");
}

void DevMode::LightTorch(int index)
{
    GameObject* gate = FindGameObjectByName(m_GateName);
    auto* ancientGate = gate ? gate->GetComponent<AncientGate>() : nullptr;
    if (!ancientGate)
    {
        CN_LOG_GAME("DevMode", "no gate named '", m_GateName, "'");
        return;
    }
    ancientGate->DevLightTorch(index);
    CN_LOG_GAME("DevMode", "light torch ", index + 1);
}

void DevMode::Update()
{
    m_Time += Time::UnscaledDeltaTime();
    const InputManager& input = Application::Get().GetInputManager();
    const bool cinematic = AncientGate::IsCinematicPlaying();

    if (!m_On)
    {
        if (input.IsKeyPressed(Key::P))
        {
            // 퍼즐을 조작하고 있거나 연출 중이면 세지 않는다.
            if (cinematic || IsAnyConsoleActive())
            {
                m_TapTimes.clear();
            }
            else
            {
                m_TapTimes.push_back(m_Time);
                std::erase_if(m_TapTimes, [this](float t) { return t < m_Time - m_TapWindow; });
                CN_LOG_GAME("DevMode", "P ", m_TapTimes.size(), "/", m_TapCount);
                if (static_cast<int>(m_TapTimes.size()) >= std::max(m_TapCount, 1))
                {
                    m_On = true;
                    m_TapTimes.clear();
                    CN_LOG_GAME("DevMode", "on");
                }
            }
        }
    }
    else if (input.IsKeyPressed(Key::O))
    {
        m_On = false;
        CN_LOG_GAME("DevMode", "off");
    }

    // 켜져 있어도 석판 조작/연출 중에는 숨기고 명령도 받지 않는다(퍼즐 안내 패널과 겹치지 않게).
    const bool usable = m_On && !cinematic && !IsAnyConsoleActive();
    if (usable)
    {
        const Key keys[6] = { Key::Num1, Key::Num2, Key::Num3, Key::Num4, Key::Num5, Key::Num6 };
        for (int i = 0; i < 6; ++i)
        {
            if (input.IsKeyPressed(keys[i]))
            {
                if (i < 3) ResetPuzzle(i);
                else LightTorch(i - 3);
            }
        }
    }
    if (usable != m_Shown)
    {
        m_Shown = usable;
        m_Hud.SetShown(usable);
    }
}
