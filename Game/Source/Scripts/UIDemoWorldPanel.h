#pragma once

#include <Engine.h>

//***** 월드 공간 UI 확인용 패널(4-1단계)
//*** 붙은 GameObject의 Transform 위치·회전에 2.4m × 1.3m 패널을 세운다.
//*** 앞에 있는 물체에 가려지고, 다가가면 글자를 더 큰 배율로 다시 구워 선명하게 보인다.
class UIDemoWorldPanel final : public Canape::ScriptComponent
{
    RTTR_ENABLE(Canape::ScriptComponent)
    RTTR_REGISTRATION_FRIEND

public:
    UIDemoWorldPanel() = default;

protected:
    void Start() override;
    void Update() override;
    void OnDestroy() override;

private:
    float m_WorldUnitsPerPixel = 0.005f;
    float m_Time = 0.0f;
    float m_ShownRaster = 0.0f;

    Canape::TextureResourceId m_RoundedTexture;
    Canape::UICanvasComponent* m_Canvas = nullptr;
    Canape::UIImage* m_GaugeFill = nullptr;
    Canape::UIText* m_RasterText = nullptr;
};
