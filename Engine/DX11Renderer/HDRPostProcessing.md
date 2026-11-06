# HDR 및 PostProcessing 구현 결과

## 변경 범위와 파일

초기 HDR 파이프라인 변경은 `Engine/DX11Renderer/` 내부다. 현재 RenderView도 이 모듈의
`Scene/RenderScene.h`에 정의되어 있다. 이후 사용자가 요청한 런타임 Tone Mapping 토글을 위해
`Engine/Source/Renderer/Renderer.h/.cpp`, `Editor/Source/Panels/RenderDebugMenu.h/.cpp`,
`Docs/RendererDebugQuickTest.md`에 설정 전달과 UI/설명을 추가했다. Application, Scene,
다른 프로젝트 설정은 수정하지 않았다.

| 파일 | 변경 사항 |
| --- | --- |
| `Data/PostProcessSettings.h` (신규) | DX11 타입을 포함하지 않는 공용 CPU 설정 |
| `Scene/RenderScene.h` | 기존 RenderView 끝에 postProcess 멤버 및 include 추가 |
| `DX11Renderer.h`, `DX11Renderer.cpp` | 개발용 SetPostProcessSettings 진입점과 Impl 전달 |
| `Internal/DX11RendererImpl.h`, `.cpp` | 내부 설정 보관, FP16 SceneColor, PostProcessBuffer 생성 |
| `Frame/DX11FrameExecutor.cpp` | ExecutePostProcessing / ExecuteFinalCompositePass, 매 출력 전 설정 업로드 |
| `Pass/DX11FinalOutputPass.h`, `.cpp` | 기존 Fullscreen Triangle에서 b8 바인딩과 해제 |
| `Data/ShaderConstants.h` | 16바이트 GPU 구조체와 슬롯, 크기/정렬/오프셋 검증 |
| `Shader/FinalOutput.hlsl` | Reinhard, 설정 및 Debug 분기, 최종 SDR Clamp |
| `Shader/Mesh.hlsl` | FP16 Linear 출력에 맞게 기존 주석 정정 |
| `DX11Renderer.vcxproj`, `.filters` | 새 설정 헤더 등록 |
| `Tests/ShadowValidation.cpp` | FP16 readback 지원 및 HDR GPU 통합 검증 |
| `HDRPostProcessing.md` (신규) | 구현 및 검증 결과, 향후 연동 지점 |

별도 RenderTarget 클래스는 현재 저장소에 없다. 기존 Impl의 Texture/RTV/SRV
소유 구조와 초기화/리사이즈 경로를 재사용했다. 프레임마다 타겟을 재생성하지 않는다.
기존 DisplayTexture는 에디터가 GetSceneTexture로 표시하는 최종 SDR 출력이므로 유지했다.
새 중간 타겟, Pool, Ping-Pong Buffer, RenderGraph는 추가하지 않았다.

## 최종 실행 순서

```text
BeginFrame: 필요 시 리사이즈, Linear FP16 SceneColor / Depth 클리어
DrawScene:
  장면/조명/상수 준비
  ShadowPass (활성화된 경우)
  MainPass Opaque / Masked → FP16 SceneColor
  Skybox → FP16 SceneColor
  MainPass Transparent → FP16 SceneColor
EndFrame:
  ExecutePostProcessing
    향후 독립 HDR 효과의 실행 위치 (현재 추가 패스 없음)
    ExecuteFinalCompositePass
      기존 Debug 입력/파이프라인 선택, 필요한 Depth 상수 업로드
      PostProcessConstants 업로드
      기존 FinalOutputPass::Execute
        SceneColor 또는 Debug Depth SRV와 b8 연결
        Fullscreen Triangle Draw(3, 0) 한 번
        t0, b0, b8 해제
  기존 Overlay (설정된 경우)
  Present
```

Overlay가 없으면 BackBuffer sRGB RTV로 바로 출력한다. Overlay가 있으면 같은 최종
Draw를 DisplayTexture의 sRGB RTV로 출력한 뒤 기존 UI 콜백이 UNORM BackBuffer에
에디터 화면을 그린다. 이 UI 경로는 이미 sRGB 인코딩된 DisplayTexture를 UNORM SRV로
표시하는 기존 계약을 유지한다. 별도 복사나 두 번째 최종 합성 Draw를 추가하지 않았다.

## 설정과 데이터 흐름

```cpp
struct PostProcessSettings
{
    bool enabled = true;
    bool toneMappingEnabled = true;
};

struct RenderView
{
    Matrix4 view{};
    Matrix4 projection{};
    Vector3 cameraPosition{};
    PostProcessSettings postProcess{};
};
```

현재 실제 설정 원본은 `DX11Renderer::Impl::postProcessSettings_` 한 곳이다.
`DX11Renderer::SetPostProcessSettings` → `Impl::SetPostProcessSettings` → 내부 설정 →
`ExecuteFinalCompositePass`의 GPU 값 변환/업로드 순서로 사용한다.
초기화 시 두 설정 모두 true다. RenderView.postProcess는 저장/전달 가능한 공용 입력으로만
준비했고, 현재 장면에 false를 설정해도 렌더링 설정에는 영향을 주지 않는다.
Override나 Dirty Flag 시스템은 없다.

enabled 또는 toneMappingEnabled가 false여도 최종 합성은 계속 실행되어
HDR → SDR Clamp 및 sRGB 출력이 이루어진다.

## GPU 상수버퍼

기존 b0~b6는 Object/Material/View/Light/Scene/Shadow/Skybox, b7은 PCSS에 사용한다.
새 PostProcessConstants는 **PS b8**, 16바이트 정렬/크기다.

| 오프셋 | CPU / HLSL | 형식 | 출처 |
| --- | --- | --- | --- |
| 0 | PostProcessingEnabled | uint32_t / uint | 내부 enabled를 0 또는 1로 변환 |
| 4 | ToneMappingEnabled | uint32_t / uint | 내부 toneMappingEnabled를 0 또는 1로 변환 |
| 8 | DebugViewEnabled | uint32_t / uint | 기존 DebugSettings의 enabled 및 Final 외 모드 여부 |
| 12 | Padding / PostProcessPadding | uint32_t / uint | 0 |

DebugViewEnabled는 효과 설정과 별개인 현재 출력 상태다. CPU bool 메모리를 GPU에
직접 복사하지 않는다. 초기화 시 버퍼를 만들고 최종 합성 직전에 매번
UpdateSubresource로 갱신한다. CPU 정렬, 크기, 멤버 오프셋은 static_assert로 확인한다.

## 포맷과 색상 처리

| 리소스 | Texture | RTV | SRV |
| --- | --- | --- | --- |
| HDR SceneColor | R16G16B16A16_FLOAT | R16G16B16A16_FLOAT | R16G16B16A16_FLOAT |
| 기존 SwapChain BackBuffer | R8G8B8A8_UNORM | R8G8B8A8_UNORM_SRGB (장면 직접 출력) | 없음 |
| 기존 에디터 DisplayTexture | R8G8B8A8_TYPELESS | R8G8B8A8_UNORM_SRGB | R8G8B8A8_UNORM (기존 UI 표시 계약) |

Overlay 전용 BackBuffer UNORM RTV는 기존대로 유지한다. SwapChain 생성 포맷, Depth,
ShadowMap 포맷은 변경하지 않았다.

기존 BaseColor 및 Cubemap의 sRGB SRV가 샘플을 Linear로 디코딩한다. Mesh 조명,
Skybox 밝기와 투명 블렌딩 결과는 FP16 SceneColor에 Linear RGB로 저장된다.
Mesh/Lighting/Skybox에는 최종 HDR 색상을 saturate하는 코드가 없었다.
내적, 거리 감쇠, 그림자 visibility와 Debug 표시의 수학적 Clamp는 유지했다.

FinalOutput.hlsl의 분리된 `ToneMapReinhard(color)`는 `color / (1 + color)`다.
활성화 조건은 PostProcessingEnabled && ToneMappingEnabled && !DebugViewEnabled다.
Tone Mapping 입력의 음수만 0으로 제한하며 노출 배율은 기본 1이다.
최종 SDR 경계에서만 saturate하고 Linear 값을 반환한다. RGB → sRGB 변환은 RTV가
수행하며 셰이더에서 pow/gamma 보정을 중복 적용하지 않는다.
Bloom, Blur, Exposure 조절, Saturation, Vignette, MSAA, FXAA, LDR 후처리는 구현하지 않았다.

## Debug View

WorldNormal/UV/MipLOD/ShadowVisibility/PcssFilterRadius는 기존 MainPass Debug 셰이더가
SceneColor에 만든다. Debug가 활성화되고 Final 외 모드이면 b8의 DebugViewEnabled로
Tone Mapping만 우회한다. Debug 비활성화 상태의 저장된 viewMode는 영향을 주지 않는다.
Final 모드에서는 DebugSettings.enabled 여부와 무관하게 효과 설정을 따른다.

CameraDepth/ShadowDepth는 기존 PrepareFinalOutput의 입력 선택과 DepthDebug 셰이더를
유지하므로 Reinhard를 거치지 않는다. 이 출력도 동일한 sRGB 최종 타겟을 사용한다.
RendererDebugSettings와 기존 디버그 색상 계산, Depth 범위는 변경하지 않았다.

## 빌드 및 실행 검증 (2026-11-04)

- Visual Studio 18 Community / v145, x64 Debug / Release / Shipping 라이브러리 빌드 성공.
- 기존 검증 방식에 따라 `_CL_=/Z7`을 사용했다. 로컬 PDB-manager C1902 회피용이며
  프로젝트 설정은 바꾸지 않았다. D9025 옵션 재정의 및 기존 C4324 정렬 경고는 남아 있다.
- `Engine/DX11Renderer/Tests/ValidateShadows.ps1` 실행 성공.
  fxc /WX로 Mesh/Shadow/Debug/Fullscreen/Skybox 셰이더를 컴파일했다.
- WARP + D3D11 Debug Layer에서 실제 렌더러를 링크해 실행하고 GPU readback으로 확인:
  - SceneColor에 Linear `(4, 2, 0.5)`가 그대로 저장되고 Reinhard 후 sRGB 예상값과
    출력이 채널당 1바이트 오차 이내로 일치.
  - 설정 토글 네 조합, RenderView 설정이 현재 내부 기본값을 덮어쓰지 않는 동작.
  - CameraDepth / WorldNormal / UV 우회, Debug Final 정상 적용 및 Debug 비활성화 동작.
  - sRGB 값 128의 BaseColor 샘플이 Linear 약 0.21586으로 저장.
  - Skybox 밝기 4가 SceneColor에 4로 저장.
  - 기존 에디터 DisplayTexture, Scene 크기 변경, SwapChain 크기 변경 후 포맷/출력 유지.
  - 프레임 간 SceneColor 재사용, 메시 1회 + 최종 합성 1회의 Draw 통계, t0/b8 해제.
- 기존 Skybox, Directional/Point 조명, 그림자/PCSS, Alpha Mask/Transparent,
  다중 장면/직접 드로우/Overlay/계측 프레임 계약 검증 통과.
- D3D11 Debug Layer 경고/오류 0건. 테스트의 잘못된 Shadow 해상도 실패 로그는
  기존 실패 주입 검증의 예상 출력이다.
- git diff --check 및 CRLF 검사 통과. 변경 파일 인코딩을 유지했다.
- 전체 Editor/Game 애플리케이션 실행 및 실제 GPU 화면 육안 검증은 수행하지 않았다.

검증 로그는 `Intermediate/hdr-validation.log`, `Intermediate/hdr-release-build.log`,
`Intermediate/hdr-shipping-build.log`에 있다. 검증 산출물은 버전 관리에 추가하지 않았다.

## 향후 엔진 연동에 필요한 변경 (이번에는 미구현)

1. 엔진의 RenderView를 만드는 위치(현재 Renderer.cpp의 카메라 뷰 구성 또는
   SetViewOverride 입력)에서 Camera/Scene/Volume 등의 최종 값을 계산해
   `RenderView.postProcess`에 채운다. 기존 SetRenderView가 필드를 포함한 뷰를 전달한다.
2. 백엔드 `ExecuteScene`에서 `scene.camera.postProcess`를 현재 프레임의 유일한 설정
   원본으로 선택하고, `ExecuteFinalCompositePass`의 개발용 내부 기본값 참조를 대체한다.
   해당 전환 위치를 코드 주석으로 표시했다. 내부 테스트 설정과의 Override 시스템은 만들지 않는다.
3. 여러 DrawScene을 한 SceneColor에 누적하는 경우 최종 합성에 사용할 뷰를 정하고,
   직접 DrawMesh만 쓰는 프레임의 기본값도 정한다. 실제 카메라별 독립 출력은 별도 후속 범위다.

현재 카메라/장면별 PostProcessSettings 계산 로직을 추가할 필요는 없다.

## 런타임 Tone Mapping 토글

Scene 뷰의 `Render Debug > Post Processing > Tone Mapping`에서 켜고 끈다.
Debug View 활성화와 독립적이며 편집/Play 모드 모두 다음 렌더링 프레임부터 반영된다.
Final 외 Debug View는 기존대로 Tone Mapping을 우회하고 메뉴에 우회 상태를 표시한다.
Reset All은 PostProcessSettings 기본값(활성화, Tone Mapping 켜짐)도 복원한다.

메뉴 → `Renderer::SetToneMappingEnabled` → `DX11Renderer::SetPostProcessSettings` →
기존 Impl 설정 → b8 업로드 순서다. UI/Renderer의 getter는 백엔드 설정을 직접 읽으며
별도의 설정 원본을 만들지 않는다. Tone Mapping 이외의 설정 필드는 유지한다.
RenderView.postProcess를 실제 입력으로 전환하는 작업은 여전히 미구현이다.

런타임 토글 추가 검증:

- Renderer 및 Editor 변경 소스 컴파일 성공, 기존 HDR/Shadow WARP 통합 검증 재통과.
- 기본 Editor 링크는 로컬 MSPDB140.DLL 버전 오류(LNK1101), 이후 출력 파일 잠금
  오류(LNK1104)가 발생했다. 프로젝트 파일의 빌드 설정은 변경하지 않았다.
- Intermediate/ToneMappingValidation.props를 해당 검증 호출에만 적용해 PDB 생성을
  제외하고 별도 이름으로 링크 성공:
  `Binaries/x64/Debug/Editor/EditorToneMapping.exe`.
- 로그: `Intermediate/tone-mapping-editor-validation-build.log`,
  `Intermediate/tone-mapping-validation.log`.
- 실제 화면에서 메뉴 클릭 및 편집/Play 전환을 통한 육안 검증은 수행하지 않았다.
