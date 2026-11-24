#include "MiniZeldaSample/PuzzleLightBeam.h"

#include "MiniZeldaSample/LeverPuzzle.h"
#include "MiniZeldaSample/MarbleRun.h"
#include "MiniZeldaSample/TiltMazePuzzle.h"

#include <algorithm>
#include <cmath>

using namespace Canape;

RTTR_REGISTRATION
{
    rttr::registration::class_<PuzzleLightBeam>("PuzzleLightBeam")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr)
        .property("puzzleName", &PuzzleLightBeam::m_PuzzleName)
        .property("fadeTime", &PuzzleLightBeam::m_FadeTime)
        .property("pulseAmount", &PuzzleLightBeam::m_PulseAmount)
        .property("pulseSpeed", &PuzzleLightBeam::m_PulseSpeed);
}

bool PuzzleLightBeam::IsPuzzleSolved() const
{
    GameObject* puzzle = m_PuzzleName.empty() ? nullptr : FindGameObjectByName(m_PuzzleName);
    if (!puzzle)
    {
        return false;
    }
    if (const auto* tilt = puzzle->GetComponent<TiltMazePuzzle>()) return tilt->IsSolved();
    if (const auto* lever = puzzle->GetComponent<LeverPuzzle>()) return lever->IsSolved();
    if (const auto* marble = puzzle->GetComponent<MarbleRun>()) return marble->IsSolved();
    return false;
}

void PuzzleLightBeam::SetBeamVisible(bool visible)
{
    m_Visible = visible;
    for (TransformComponent* child : GetTransform()->GetChildren())
    {
        if (GameObject* object = child->GetOwnerObject())
        {
            for (MeshRendererComponent* renderer : object->GetComponents<MeshRendererComponent>())
            {
                renderer->SetVisible(visible);
            }
        }
    }
}

void PuzzleLightBeam::Start()
{
    m_BaseScale = GetTransform()->GetScale();
    m_Amount = IsPuzzleSolved() ? 0.0f : 1.0f;
    SetBeamVisible(m_Amount > 0.0f);
}

void PuzzleLightBeam::Update()
{
    const float dt = Time::DeltaTime();
    m_Clock += dt;

    const float target = IsPuzzleSolved() ? 0.0f : 1.0f;
    const float step = m_FadeTime > 0.0f ? dt / m_FadeTime : 1.0f;
    m_Amount = m_Amount < target ? std::min(target, m_Amount + step) : std::max(target, m_Amount - step);

    const bool visible = m_Amount > 0.001f;
    if (visible != m_Visible)
    {
        SetBeamVisible(visible);
    }
    if (!visible)
    {
        return;
    }

    // 사라질 때는 가늘어지게(ease-in), 켜져 있을 때는 폭이 천천히 일렁인다.
    const float eased = m_Amount * m_Amount * (3.0f - 2.0f * m_Amount);
    const float pulse = 1.0f + m_PulseAmount * std::sin(m_Clock * m_PulseSpeed);
    const float width = eased * pulse;
    GetTransform()->SetScale({ m_BaseScale.X * width, m_BaseScale.Y, m_BaseScale.Z * width });
}
