#pragma once

#include <Engine.h>

#include <string>

// 무작위 간격(minInterval ~ maxInterval초)마다 소리를 한 번 낸다. 드래곤 울음 같은 환경음용.
// spatial이면 이 오브젝트 위치에서 3D로(멀리서 들리도록 거리를 넉넉히), 아니면 2D로 재생한다.
class RandomIntervalSound final : public Canape::ScriptComponent
{
    RTTR_ENABLE(Canape::ScriptComponent)
    RTTR_REGISTRATION_FRIEND

protected:
    void Start() override;
    void Update() override;

private:
    float NextInterval() const;

    //***** Authoring
    std::string m_Clip = "Content/Audio/DragonRoar.mp3";
    float m_MinInterval = 60.0f;
    float m_MaxInterval = 120.0f;
    float m_Volume = 1.0f;
    bool m_Spatial = true;
    float m_MinDistance = 20.0f;
    float m_MaxDistance = 250.0f;

    //***** Runtime (직렬화 안 함)
    float m_Timer = 0.0f;
};
