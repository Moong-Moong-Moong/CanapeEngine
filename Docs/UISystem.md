# 게임 UI 시스템

화면 HUD와 3D 월드 패널용 UI. 에디터 UI(ImGui)와는 별개이며 Runtime만으로 동작한다.

- 1단계: 사각형 레이아웃, 단색·텍스처 이미지, 채움 게이지, 버튼, 마우스 입력.
- 2단계: 텍스트(한글 동적 글리프 아틀라스, 자동 줄바꿈), 9-slice, 자식 클리핑, 가로·세로 박스.
- 4-1단계: 월드 공간 캔버스(`UICanvasMode::WorldSpace`), 씬 깊이에 의한 가려짐, 거리에 맞춘 글자 굽기.
- 다음: 4-2 따라다니는 UI(머리 위 체력바), 4-3 월드 패널 입력, 텍스트 입력(IME)·트윈·저장.

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

    auto* label = pause->AddChild<Canape::UIText>("PauseLabel");
    label->SetAnchors({ 0.0f, 0.0f }, { 1.0f, 1.0f }); // 버튼을 꽉 채우고
    label->SetSize({ 0.0f, 0.0f });
    label->SetText("일시정지");
    label->SetFontSize(24.0f);
    label->SetHorizontalAlign(Canape::UITextAlign::Center);
    label->SetVerticalAlign(Canape::UITextAlign::Center);
}
```

예제 전체: `Game/Source/Scripts/UIDemoHud.cpp`, 씬 `UISample` (`Game.exe -scene UISample`).

## 레이아웃 규칙

Unity RectTransform과 같다.

- 앵커(0~1): 부모 사각형 안의 기준 영역. 최소·최대가 같으면 한 점, 다르면 늘어나는 영역.
- 피벗(0~1): 요소 안에서 위치의 기준점.
- 위치: 앵커 기준점에서 피벗까지의 거리.
- 크기: 늘어나는 앵커라면 앵커 폭에 더해지는 값(음수면 안쪽으로 줄어듦).
- 위치·크기 단위는 캔버스 기준 해상도의 픽셀. 실제 픽셀 = 값 × 캔버스 스케일.
- 스케일은 기준 해상도와 출력 크기로 정한다. `SetMatchWidthOrHeight`: 0 = 너비, 1 = 높이(기본).
- `SetFitContent(true)`: 크기를 내용(`GetPreferredSize`)으로 정한다. 텍스트는 글자 영역, 박스는 자식 합.
- 모서리는 정수 픽셀로 맞춘다. 레이아웃은 매 프레임 전체를 다시 계산한다(HUD 규모에서는 비용이 작다).

### 가로·세로 박스

`UIHorizontalBox`, `UIVerticalBox`는 자식을 한 줄로 차례대로 놓는다.

- 자식의 앵커·위치는 무시하고, 진행 방향 크기는 자식이 원하는 크기(`GetPreferredSize`)로 정한다.
- `SetSpacing`(자식 사이 간격), `SetPadding`(안쪽 여백)은 캔버스 기준 픽셀이다.
- `SetChildAlign`: 수직 축에서 Start / Center / End, 또는 Stretch(꽉 채움).
- 세로 박스에서 줄바꿈 텍스트는 정해진 폭에서 다시 재서 높이를 정한다.
- 숨긴 자식(`SetVisible(false)`)은 자리를 차지하지 않는다.

## 텍스트

- `UIText`는 UTF-8 문자열을 그린다. 잘못된 바이트는 U+FFFD로 바뀐다.
- 기본 폰트: `Content/Fonts/Pretendard-Regular.ttf` (SIL OFL 1.1, 라이선스 `Pretendard-OFL.txt`).
  `SetFont`로 요소마다 다른 폰트(`UIFont::LoadFromFile`)를 쓸 수 있다.
- 글자 크기(`SetFontSize`)는 캔버스 기준 픽셀(em)이다. 캔버스 스케일을 곱한 뒤 정수 픽셀로 맞춰 굽는다.
- 정렬: `SetHorizontalAlign` / `SetVerticalAlign` (Start, Center, End).
- `SetWrap(true)`: 요소 폭에 맞춰 공백에서 줄을 바꾼다(영문 단어, 한글 어절). 폭보다 긴 단어는 글자 단위로 자른다.
  `\n`은 항상 줄바꿈이다. 줄 간격은 `SetLineSpacing`(배수).
- 글리프는 1024×1024 아틀라스에 필요한 것만 그때그때 굽고, 프레임마다 새로 구운 영역만 GPU에 올린다.
  아틀라스가 가득 차면 다음 프레임에 비우고 다시 굽는다(그 프레임에 자리가 없던 글자는 한 프레임 빠진다).
- 단색 사각형도 아틀라스의 흰 텍셀로 그려서, 글자와 패널이 한 드로우 콜로 묶인다.
- 커닝은 폰트의 kern·GPOS 쌍 정보를 쓴다. 힌팅은 없다(stb_truetype).

## 9-slice와 클리핑

- `UIImage::SetNineSlice({ UV 테두리(0~1), 화면 테두리(기준 픽셀) })`: 모서리는 그대로 두고 가운데만 늘린다.
  `UINineSlice::FromPixels`로 텍스처 픽셀 단위 테두리에서 만들 수 있다. 사각형이 테두리 합보다 작으면 테두리를 줄인다.
- 9-slice 이미지에 채움 비율을 주면 전체를 그리고 채운 부분만 잘라 낸다(둥근 게이지).
- `UIImage::SetTextureResource`: 코드로 만든 GPU 텍스처(`Renderer::CreateTexture`)를 바로 쓴다. 수명은 만든 쪽이 관리한다.
- `UIElement::SetClipChildren(true)`: 자식을 이 요소 사각형 밖으로 그리지 않고, 밖에서는 자식이 입력도 받지 않는다.
  중첩된 클립은 교집합이 되며, 클립 밖으로 완전히 나간 사각형은 그리기 목록에 넣지 않는다.

## 월드 공간 캔버스

```cpp
auto* canvas = AddComponent<Canape::UICanvasComponent>();
canvas->SetMode(Canape::UICanvasMode::WorldSpace);
canvas->SetCanvasSize({ 480.0f, 260.0f });   // 캔버스 픽셀
canvas->SetWorldUnitsPerPixel(0.005f);        // → 2.4m × 1.3m
// 이후 루트 아래 요소 구성은 화면 캔버스와 같다.
```

- 붙은 GameObject의 Transform(위치·회전·스케일)을 따른다. 캔버스 평면은 오브젝트의 로컬 XY 평면이고,
  `SetCanvasPivot`(기본 가운데)이 오브젝트 원점에 온다. 로컬 -Z 쪽에서 볼 때 글자가 바로 읽힌다.
- 씬 물체에 가려진다(`SetDepthTest(false)`로 끌 수 있음). 깊이는 쓰지 않으므로 반투명 패널이 뒤의 UI를 가리지 않는다.
- 조명·포그를 받지 않는다(톤매핑 뒤에 그린다).
- 월드 캔버스를 카메라에서 먼 것부터 그리고, 화면 HUD는 항상 그 위에 그린다.
- 글자는 캔버스 픽셀 1개가 화면에서 차지하는 크기를 2의 거듭제곱(1/4~4배)으로 올려 맞춰 굽는다
  (`GetRasterFactor`). 가까이 가도 선명하고, 카메라가 움직여도 아틀라스가 마구 늘지 않는다.
- 카메라는 렌더러와 같은 뷰(`Renderer::ResolveView`: 에디터 씬 뷰 카메라 또는 메인 카메라)를 쓴다.
- 아직 안 되는 것: 월드 캔버스 안의 `SetClipChildren`은 GPU에서 잘라 내지 않는다(밖으로 완전히 나간 사각형만 뺀다).
  월드 패널은 입력을 받지 않는다(4-3단계). 패널 뒷면에서는 글자가 좌우 반전되어 보인다.

예제: `Game/Source/Scripts/UIDemoWorldPanel.cpp` (`UISample` 씬, 오른쪽 적 뒤의 패널).

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
- 그리기 명령마다 변환(`UIDrawTransform`, 캔버스 픽셀 → 클립 공간)을 가진다. 화면 명령은 렌더러가 직교 투영을 쓴다.
- 월드 명령의 가려짐은 깊이-스텐실 뷰를 묶지 않고 픽셀 셰이더가 카메라 깊이 텍스처를 읽어 비교한다.
  카메라 깊이는 MSAA(4x)일 수 있어 단일 샘플 UI 타깃과 함께 묶을 수 없기 때문이다(MSAA면 샘플 0을 읽는다).
  이를 위해 카메라 깊이 텍스처는 모든 구성(Shipping 포함)에서 셰이더로 읽을 수 있게 만든다.
- UI 텍스처는 `TextureFormat::RGBA8_UNORM`으로 올리는 것을 권장한다. sRGB 텍스처도 셰이더가 보정해 같은 색으로 그린다.
- 같은 텍스처·클립의 연속 사각형은 한 드로우 콜로 묶인다.

## 저장

캔버스는 코드로만 만들며 씬 파일에 저장되지 않는다. 에디터 플레이를 멈추면 함께 사라진다.

## 검증

```powershell
powershell -ExecutionPolicy Bypass -File Engine/Source/UI/Tests/ValidateUI.ps1
```

저장소 루트에서 실행한다. Runtime을 한 번 빌드해 vcpkg가 `stb`를 설치해 둬야 한다.

- `UICoreValidation`(CPU): 스케일, 레이아웃, 채움, 배칭, hover/press/click, 입력 소비, 삭제 안전성,
  UTF-8 디코딩, 줄바꿈, 글리프 아틀라스(패킹·업로드·가득 참), UIText 정렬, 9-slice, 클리핑, 박스,
  월드 캔버스 행렬·굽는 배율·배율별 글자 크기·변환별 배치 분리.
- `UIPassValidation`(WARP + D3D11 디버그 레이어): 픽셀 리드백으로 색, 감마 공간 합성,
  UNORM/sRGB 텍스처, 시저, 그리는 순서, `UpdateTexture`, 리사이즈, 버퍼 증가,
  월드 명령의 행렬·씬 깊이 가려짐·캔버스별 끄기를 1x와 4x MSAA 깊이에서 확인.
