# UI 데이터를 RenderScene으로 받기 위한 렌더러 가이드

이 문서는 **렌더러 측** 변경 방향을 정한다. 목표는 게임 UI의 제출 경로를 `RenderScene`으로 통일하면서, UI에 필요한 별도 패스와 출력 순서를 유지하는 것이다. UI 요소 수집, 레이아웃, 입력 및 호출자의 구성 방식은 이 문서의 범위에 넣지 않는다. 구현 시에는 링크된 현재 코드를 기준으로 다시 확인한다.

## 결정할 구조

`RenderScene`에 `UIDrawList`를 **메시 항목과 별도 필드**로 둔다. `UIDrawList`는 그 프레임에 그릴 화면 공간 정점, 인덱스, 순서 있는 명령을 담는다. UI를 `MeshItem`, `SkinnedMeshItem`, `PreparedScene::Opaque` 등의 3D 메시 목록에 넣지 않는다.

```cpp
struct RenderScene
{
    RenderView camera;
    std::vector<MeshItem> items;
    std::vector<SkinnedMeshItem> skinnedItems;
    // ... 조명, 안개, 스카이박스 등 기존 필드
    UIDrawList ui;
};
```

렌더러의 `DrawScene()`은 기존 3D 패스를 실행하고 `scene.ui`를 **렌더러가 소유하는 프레임 UI 대기 목록**으로 이관한다. `EndFrame()`은 최종 출력 합성 뒤 대기 목록을 기존 `UIPass`로 실행한다. 그러면 최외곽에서 UI를 직접 `SetUIDrawList()`로 제출하는 별도 경로가 필요 없어진다. `DrawScene()`에서 UI를 3D 렌더 타깃에 바로 그리는 설계는 목표가 아니다.

```text
RenderScene { 3D 데이터, UIDrawList }
       │
       ▼
DX11Renderer::DrawScene
       ├─ ScenePreparation::PrepareScene → 그림자/메인/스카이박스 패스
       └─ UIDrawList → 렌더러 소유의 이번 프레임 UI 대기 목록
                                      │
                                      ▼
DX11Renderer::EndFrame
       ├─ HDR 장면의 최종 출력 합성
       ├─ UIPass::Execute(UI 대기 목록)
       ├─ 에디터 오버레이가 있으면 실행
       └─ Present
```

이 안에서 `RenderScene`은 **제출 데이터의 공통 입구**이고, 메시 패스와 UI 패스는 각자 필요한 시점에 데이터를 소비한다. 기존에도 `RenderScene`은 메시 외에 조명·안개·스카이박스를 별도 필드로 받는다. UI 전용 필드는 그 구조와 맞는다.

## 현재 렌더러 계약

### 3D 경로

- [Renderer.h](../Engine/Source/Renderer/Renderer.h)의 `AddRenderItem`, `AddSkinnedRenderItem`, `AddLight` 등은 렌더러의 수집 API다. 구현은 에셋 ID를 GPU 리소스 ID로 해석한 후 DX11 백엔드에 전달한다.
- [DX11RendererImpl.h](../Engine/DX11Renderer/Internal/DX11RendererImpl.h)의 `QueuedScene`이 수집된 `RenderScene`을 소유한다. [DX11Renderer.cpp](../Engine/DX11Renderer/DX11Renderer.cpp)의 인자 없는 `DrawScene()`이 이 큐를 실행하고 비운다. 외부의 `DrawScene(const RenderScene&)` 경로도 존재한다.
- [DX11ScenePreparation.cpp](../Engine/DX11Renderer/Frame/DX11ScenePreparation.cpp)는 메시를 `PreparedScene::Draws`로 준비하고, 불투명·마스크·투명·그림자 목록에는 그 배열의 **인덱스**를 담는다. 투명 항목은 깊이순으로 정렬한다.
- [DX11FrameExecutor.cpp](../Engine/DX11Renderer/Frame/DX11FrameExecutor.cpp)의 `ExecuteScene()`은 그림자 → 불투명/마스크 → 스카이박스 → 투명 순서로 실행한다. `EndFrame()`에서 최종 출력 합성과 Present를 처리한다.

### 현재 UI 경로와 데이터 형태

- 현재 공개 진입점인 `Renderer::SetUIDrawList(const UIDrawList*)`는 [DX11Renderer.cpp](../Engine/DX11Renderer/DX11Renderer.cpp)의 `SetUIDrawList()`로 전달된다. `Impl::PendingUI`는 목록을 **소유하지 않는 포인터**로 `EndFrame()`까지 보관한다. 이 직접 제출 경로를 `RenderScene` 제출 경로로 교체하려는 것이다.
- [UIDrawList.h](../Engine/DX11Renderer/Data/UIDrawList.h)의 목록은 `Vertices`, `Indices`, `Commands`를 소유한다. 사각형 하나는 정점 4개와 인덱스 6개다. 명령에는 텍스처 리소스 ID, 클립 영역, 인덱스 범위가 들어간다. 명령 순서가 UI의 겹침 순서다.
- [DX11UIPass.cpp](../Engine/DX11Renderer/Pass/DX11UIPass.cpp)는 정점과 인덱스를 동적 GPU 버퍼에 업로드한다. 필요한 버퍼 용량은 늘려서 재사용한다. 명령마다 텍스처·시저를 설정하고 `DrawIndexed`를 호출한다. 이 방식은 `RenderScene`에 목록을 넣더라도 유지할 수 있다.
- [UI.hlsl](../Engine/DX11Renderer/Shader/UI.hlsl)은 렌더 타깃의 좌상단 픽셀 좌표를 사용한다. [DX11FrameExecutor.cpp](../Engine/DX11Renderer/Frame/DX11FrameExecutor.cpp)의 UI 실행은 최종 출력 합성 **뒤**, 에디터 오버레이 **앞**이다. UI는 UNORM 출력 뷰에 그려 현재의 색·알파 합성 결과를 유지한다.

## 렌더러에서 바꿀 부분

### 1. RenderScene의 UI 필드와 수집 API

[RenderScene.h](../Engine/DX11Renderer/Scene/RenderScene.h)에 `UIDrawList ui`를 추가한다. 렌더러의 수집형 API를 유지하는 경우, `Renderer`/`DX11Renderer`에 `SubmitUIDrawList(UIDrawList&&)` 같은 메서드를 제공해 **`QueuedScene.ui`에 넣는다**. 이 메서드는 `PendingUI`에 직접 저장하는 새 이름의 `SetUIDrawList`가 되어서는 안 된다. 현 UI 목록은 여러 캔버스의 그리기 결과를 이미 하나로 모으므로 한 `RenderScene`당 UI 목록 하나를 계약으로 삼는다. 같은 씬에 두 번 제출하면 조용히 앞 목록을 덮어쓰지 말고 중복 제출을 진단한다.

### 2. DrawScene에서 EndFrame으로 소유권 이관

`EndFrame()`이 실행될 때는 인자 없는 `DrawScene()`의 `QueuedScene`이 이미 비워져 있다. 따라서 `RenderScene.ui`를 가리키는 포인터를 `PendingUI`로 보관하면 수명이 끝난 데이터를 참조할 수 있다. `PendingUI`를 포인터 대신 렌더러 **소유**의 `UIDrawList` 또는 프레임 UI 목록으로 바꾼다.

- **내부 `QueuedScene` 경로:** 3D 준비가 끝난 뒤 `QueuedScene.ui`의 벡터를 `std::move`로 대기 목록에 넘긴다. 현재 인자 없는 `DrawScene()`은 `DrawScene(const RenderScene&)`로 전달하므로, 그 `const` 경유를 그대로 두고 `std::move`만 붙여서는 복사가 사라지지 않는다. 소유한 큐를 소비하는 경로를 따로 마련한다.
- **외부 `DrawScene(const RenderScene&)` 경로:** 호출자의 씬은 수정할 수 없다. UI가 있다면 대기 목록으로 **복사**해야 한다. 복사를 피할 호출자에게는 `DrawScene(RenderScene&&)`처럼 소유권을 넘기는 오버로드를 추가할 수 있다. 외부 씬의 포인터를 `EndFrame()`까지 빌리는 방식은 선택하지 않는다.
- **`EndFrame()` 및 종료 경로:** UI를 그린 뒤 대기 목록을 비운다. `BeginFrame()` 실패 시 아직 `QueuedScene.ui`에 남아 있는 제출도 폐기한다. UI 패스 실패나 초기화 해제 후에도 다음 프레임에 이전 UI가 나타나지 않아야 한다. 프레임이 활성화되지 않은 `DrawScene()` 호출도 UI를 다음 프레임으로 넘기지 않게 처리한다.

기존 [DX11Renderer.h](../Engine/DX11Renderer/DX11Renderer.h)의 `DrawScene(const RenderScene&)` 주석은 “`BeginFrame`과 `EndFrame` 사이에 동기 소비”라고 설명한다. UI의 GPU 실행이 `EndFrame()`으로 미뤄지면 이 설명을 고쳐야 한다. 제안한 방식에서는 **호출자 데이터를 `DrawScene()` 안에서 복사·이동해 렌더러가 소유**하므로, 호출자가 원본을 `EndFrame()`까지 보관할 필요는 없다.

### 3. UI 패스는 기존 위치에 유지

`ScenePreparation::PrepareScene`의 3D 분류, `DX11MainPass`, `DX11ShadowPass`에 UI를 끼워 넣지 않는다. `EndFrame()`의 최종 출력 합성 → `UIPass::Execute` → 에디터 오버레이 순서를 유지한다. `UIDrawList`가 `RenderScene`을 통해 들어왔다는 이유로 톤 매핑 전의 HDR 장면 타깃에 UI를 그리지 않는다.

한 프레임에 `DrawScene()`이 여러 번 호출될 수 있다. 현재의 직접 `SetUIDrawList()`는 한 프레임에 마지막으로 설정한 목록 한 개만 사용하지만, `RenderScene`별 UI를 받게 되면 정책을 정해야 한다. **권장 정책은 제출된 비어 있지 않은 목록을 호출 순서대로 모아 `EndFrame()`에서 차례로 실행하는 것**이다. 구현에는 `std::vector<UIDrawList>` 같은 소유 목록을 사용할 수 있다. 일반적인 한 씬·한 UI 목록 프레임에는 요소가 하나뿐이다. 이 정책을 택했다면 공개 API 주석과 테스트에 명시한다.

## 변경하지 않을 데이터 처리

- 화면 공간 UI는 렌더 타깃 픽셀 좌표와 명령 순서를 사용한다. 3D 카메라 행렬, 깊이 정렬, 조명과 그림자 처리 대상이 아니다.
- `UIDrawList`의 정점·인덱스는 영구 메시 에셋이 아니라 **그 프레임의 준비된 UI 기하**다. 기존의 동적 GPU 버퍼 업로드와 연속 명령 배칭을 사용할 수 있다. `RenderScene`에 넣는다는 이유만으로 `MeshResourceId`를 만들 필요는 없다.
- 빈 텍스처 ID는 단색 UI에 쓰는 흰 텍스처를 뜻한다. 파괴된 유효 ID의 명령은 건너뛴다. 명령의 클립, 색 공간 및 알파 블렌딩 계약을 유지한다.
- UI 기하가 작성된 타깃 크기와 `EndFrame()`의 실제 UI 출력 타깃 크기는 같아야 한다. 현재 UI 목록 생성보다 `BeginFrame()`의 리사이즈 확정이 늦을 수 있으므로, 이 순서를 통합 검증한다. 렌더러에서 크기가 다른 목록을 받는 경우의 계약을 명확히 한다.

## 검증 기준

1. 내부 `QueuedScene`과 외부 `DrawScene(const RenderScene&)` 양쪽에서 UI가 `EndFrame()`에 표시된다. 외부 씬을 `DrawScene()` 직후 수정·파괴해도 대기 중인 UI 데이터는 유효하다.
2. 렌더러의 수집 API가 UI를 `QueuedScene.ui`에 저장하며, 직접 `SetUIDrawList()` 경로가 제거된다. UI가 없는 `RenderScene`은 기존 3D 출력과 같다.
3. 최종 출력 합성, UI, 에디터 오버레이의 순서와 UNORM 합성 결과가 유지된다. UI 명령의 순서, 텍스처, 시저, 배칭 결과가 유지된다.
4. 여러 `DrawScene()` 호출의 UI 목록이 정해진 정책대로 보이고, 빈 목록·프레임 실패·리사이즈·종료 후 이전 UI가 남지 않는다.
5. [UIPassValidation.cpp](../Engine/DX11Renderer/Tests/UIPassValidation.cpp)의 WARP/D3D11 검증을 새 제출 경로로 갱신해 통과시킨다. 기존 씬 준비와 그림자 검증도 영향 범위에 맞게 실행한다. 실제 에디터와 게임 화면에서 UI 크기 및 겹침 순서를 확인한다.

## AI 작업자가 보고할 내용

구현 전에는 현재 `QueuedScene`의 초기화 지점, `DrawScene()` 두 경로, `PendingUI`의 수명, `EndFrame()`의 출력 타깃을 코드에서 확인한다. 변경 후에는 **제출 API → `RenderScene.ui` → 렌더러 소유 대기 목록 → `EndFrame()` UI 패스**의 실제 호출 경로를 파일과 함께 설명한다. 복사와 이동이 각각 발생하는 경로, 여러 씬 제출 정책, 갱신한 테스트와 확인하지 못한 실행 환경도 보고한다.
