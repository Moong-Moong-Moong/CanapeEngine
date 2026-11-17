# 게임 UI 시스템 (1단계)

화면 공간 HUD용 UI. 에디터 UI(ImGui)와는 별개이며 Runtime만으로 동작한다.
1단계 범위는 사각형 레이아웃, 단색·텍스처 이미지, 채움 게이지, 버튼, 마우스 입력이다.
텍스트, 9-slice, 클리핑, 박스 레이아웃은 2단계에서 추가한다.

## 사용법

스크립트에서 `UICanvasComponent`를 붙이고 루트 아래에 요소를 만든다.

```cpp
void PlayerHud::Start()
{
    auto* canvas = AddComponent<Canape::UICanvasComponent>(Canape::Vector2{ 1920.0f, 1080.0f });
    Canape::UIElement& root = canvas->GetRoot();

    auto* hp = root.AddChild<Canape::UIImage>("HpBar");
    hp->SetPosition({ 32.0f, 32.0f });   // 기본 앵커·피벗은 왼쪽 위
    hp->SetSize({ 420.0f, 36.0f });
    hp->SetColor({ 214, 64, 69 });

    auto* pause = root.AddChild<Canape::UIButton>("Pause");
    pause->SetAnchor({ 1.0f, 0.0f });    // 오른쪽 위에 고정
    pause->SetPivot({ 1.0f, 0.0f });
    pause->SetPosition({ -32.0f, 32.0f });
    pause->SetSize({ 160.0f, 56.0f });
    pause->OnClicked = [this] { TogglePause(); };
}
```

예제: `Game/Source/Scripts/UIDemoHud.cpp`, 씬 `UISample` (`Game.exe -scene UISample`).

## 레이아웃 규칙

Unity RectTransform과 같다.

- 앵커(0~1): 부모 사각형 안의 기준 영역. 최소·최대가 같으면 한 점, 다르면 늘어나는 영역.
- 피벗(0~1): 요소 안에서 위치의 기준점.
- 위치: 앵커 기준점에서 피벗까지의 거리.
- 크기: 늘어나는 앵커라면 앵커 폭에 더해지는 값(음수면 안쪽으로 줄어듦).
- 위치·크기 단위는 캔버스 기준 해상도의 픽셀. 실제 픽셀 = 값 × 캔버스 스케일.
- 스케일은 기준 해상도와 출력 크기로 정한다. `SetMatchWidthOrHeight`: 0 = 너비, 1 = 높이(기본).
- 모서리는 정수 픽셀로 맞춘다. 레이아웃은 매 프레임 전체를 다시 계산한다(HUD 규모에서는 비용이 작다).

## 입력

- 히트 테스트는 나중에 그려진 요소부터 한다. `UIButton`만 기본으로 입력을 받는다(`SetHitTestable`로 변경).
- 클릭은 누른 버튼 위에서 뗐을 때만 발생한다. 누른 채 밖으로 나가면 취소된다.
- UI가 포인터를 쓰는 프레임에는 `InputManager::IsMouseDown/Pressed/Released`와 `GetMouseWheel`이 비어 있다.
  버튼 클릭이 게임 입력으로 새지 않게 하기 위함이다. 원본 상태는 `GetInput()`으로 읽는다.
- 에디터 플레이 중에는 씬 뷰 이미지 위의 마우스만 게임 UI에 전달된다.
- 요소 삭제(`RemoveFromParent`)는 프레임 경계에서 처리되므로 클릭 콜백 안에서 지워도 안전하다.

## 렌더링

- `UISystem`이 캔버스를 정렬 순서대로 그려 `UIDrawList`를 만들고, 렌더러가 최종 출력 직후에 그린다.
  에디터 오버레이(ImGui)는 그 위에 그려진다.
- 출력 타깃의 UNORM 뷰에 그리므로 반투명 합성이 sRGB 값 기준(이미지 편집기와 같은 결과)이다.
- UI 텍스처는 `TextureFormat::RGBA8_UNORM`으로 올리는 것을 권장한다. sRGB 텍스처도 셰이더가 보정해 같은 색으로 그린다.
- 같은 텍스처·클립의 연속 사각형은 한 드로우 콜로 묶인다.
- `DX11Renderer::UpdateTexture`로 텍스처 일부를 고쳐 쓸 수 있다(2단계 폰트 아틀라스용).

## 저장

1단계 캔버스는 코드로만 만들며 씬 파일에 저장되지 않는다. 에디터 플레이를 멈추면 함께 사라진다.

## 검증

```powershell
powershell -ExecutionPolicy Bypass -File Engine/Source/UI/Tests/ValidateUI.ps1
```

- `UICoreValidation`: 스케일, 레이아웃, 채움, 배칭, hover/press/click, 입력 소비, 삭제 안전성(CPU).
- `UIPassValidation`: WARP + D3D11 디버그 레이어에서 픽셀 리드백으로 색, 감마 공간 합성,
  UNORM/sRGB 텍스처, 시저, 그리는 순서, `UpdateTexture`, 리사이즈, 버퍼 증가를 확인.
