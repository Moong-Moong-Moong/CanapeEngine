# Skinned Prepared 데이터 연결

`ScenePreparation::PrepareScene`에서 Static/Position 및 Skinned 입력을 같은
`PreparedMeshDraw` 배열과 alpha 큐로 변환한다. 이후 Main/Shadow 제출에서 VS b9 팔레트
업로드/바인딩까지 연결한다.

## 데이터 계약

- `PreparedScene::BonePalettes`는 입력 `RenderScene::bonePalettes`의 소유 사본이다.
  원본 씬을 수정하거나 초기화해도 준비된 팔레트는 유지된다.
- `PreparedMeshDraw::PaletteOffset/PaletteCount`는 이 사본의 범위이다.
  정렬은 draw 인덱스만 이동하므로 팔레트 연결은 유지된다.
- `MeshItem`은 Position/Static 메시만 허용하며 두 팔레트 필드는 0이다.
  `SkinnedMeshItem`은 Skinned 메시만 허용한다.
- 같은 인스턴스의 Submesh는 팔레트 범위를 공유하고, 다른 인스턴스는 같은
  메시 리소스를 사용해도 별도 팔레트 범위를 참조할 수 있다.
- `ShaderConstants::MaxSkinMatrices = 256`은 HLSL 팔레트 용량과 일치해야 한다.
- 메시 리소스/서브메시는 기존 런타임 검증을 유지한다. Skinned 형식,
  1~256개 팔레트 길이, 배열 범위는 Debug assert로만 확인하고 Release에서는
  호출자가 계약을 보장한다. 매 draw의 행렬 NaN/Infinity 검사는 제거했다.
  유한한 행렬 제공 역시 호출자의 책임이며 AddBonePalette에는 검증을 추가하지 않았다.
- 잘못된 draw만 제외하고 나머지 준비를 계속한다. `error`에는 이 씬에서
  마지막으로 거부한 draw의 사유를 유지한다. 새 씬 준비를 시작할 때 초기화한다.
  실패한 준비 함수의 출력은 비워 재사용 시 이전 팔레트가 남지 않도록 한다.
- alpha 분류/Depth 계산은 공유한다. Transparent는 양쪽 입력을 추가한 뒤
  전체를 stable sort한다. Depth는 기존처럼 카메라 공간의 오브젝트 원점 기준이다.
- Skinned draw도 Opaque/Masked이면 ShadowCasters에 추가한다. Transparent는 제외한다.

## Main/Shadow 제출과 다음 작업

`Impl::CreatePipeline`에서 16,384바이트 DEFAULT 상수버퍼를 생성하고 Shutdown에서
해제한다. GetDrawContext는 이 버퍼를 전달하고, ExecuteScene과 RenderShadowPass는
각각 PreparedScene의 팔레트를 span으로 연결한다. span은 해당 패스의 제출이 끝날 때까지
유효하며 프레임 밖으로 보관하지 않는다.

`MeshDraw::SubmitMain`과 `SubmitShadow`는 draw마다 UploadAndBindSkinning을 호출한다. 선택 범위를
256개 전체 크기의 SkinningConstants에 복사하고 ConstantBuffers::Upload를 통해
UpdateSubresource한 뒤 RenderBindings::BindSkinning으로 VS b9에 바인딩한다.
남은 행렬은 Identity로 초기화한다. CPU 행렬은 전치하지 않는다.
Static/Position 제출은 b9를 null로 해제한다. Main의 Skinned 제출 가드는 제거했다.
메시 제출은 수집된 큐 또는 명시적인 RenderScene을 DrawScene으로 소비하는 경로로
통일했다. 개별 메시 즉시 제출과 별도 카메라 설정 API는 제거했다.

Skinned VS/InputLayout은 Initialize에서 생성하고 Resolve에서 선택하도록 연결했다.
입력은 SkinnedVertex의 offsetof를 사용하며, Joints는 R16G16B16A16_UINT,
Weights는 R32G32B32A32_FLOAT이다. 정점 구조의 크기/offset은 static_assert로 명시했다.

`ShadowDepth.hlsl`은 SKINNED_VERTEX 변형에서 동일한 ApplySkinning을 재사용한다.
본 변환 후 World와 LightViewProjection을 적용하고, UV는 Masked 픽셀 셰이더로
전달한다. InitializeShadows에서 스킨드 Shadow VS 및 Position/UV/Joints/Weights
입력 레이아웃을 생성하고 ResolveShadow에서 선택한다. `SubmitShadow`의 Skinned
조기 반환과 ShadowCasters 제외는 제거했다.

정점의 활성 Joint 인덱스가 각 draw의 paletteCount 안에 있는지는 아직 검증하지
않는다. 메시 생성 단계에서 RequiredSkinMatrixCount 등의 메타데이터를 보존하는
작업은 후속으로 남아 있다. 현재는 유효한 활성 Joint 인덱스를 호출자가 보장한다.
본 계층/포즈는 GPU 메시 리소스에 저장하지 않는다.
Tangent 임포트 보완과 본의 비균일 스케일 Normal/TBN 지원도 이번 범위에서 제외했다.

## 검증

저장소 루트에서 `./Engine/DX11Renderer/Tests/ValidatePreparedScene.ps1`을 실행한다.
Visual Studio C++와 Windows SDK 및 WARP debug layer가 필요하다. 테스트는 제품
프로젝트에 편입하지 않고 실제 DX11Renderer 라이브러리와 독립 실행 파일로 링크한다.
산출물은 `Intermediate/PreparedSceneValidation/`에 생성한다.

스킨드 Shadow 연결 후 다음 검증을 수행했다.

- `ValidatePreparedScene.ps1`: Debug/Shipping 렌더러 빌드와 각 구성의 독립 WARP 테스트 통과.
- 공통 Opaque/Masked 큐, 혼합 Transparent 전체 정렬 및 동일 깊이의 안정 순서 확인.
- 공유 메시의 인스턴스별 범위, Submesh 간 범위 공유, World/Submesh 보존 확인.
- 원본 팔레트 변경/씬 초기화 후 Prepared 팔레트 사본 유지 확인.
- 유효한 팔레트 끝 경계, 잘못된 리소스/서브메시, 출력 재사용 및 빈 씬 초기화 확인.
- `ValidateMeshShaders.ps1`: 스킨드 Shadow 변형을 포함한 셰이더 컴파일과
  기존 Main 스키닝 GPU 검증 통과.
- `ValidateShadows.ps1 -SkinnedOnly`: 실제 DrawScene 경로의 그림자 깊이를
  CPU로 포즈를 적용한 Static 메시와 비교하여 일치 확인. 포즈 변경, World,
  정규화되지 않은 가중치, 팔레트 인덱스 255, 인스턴스별 팔레트 범위,
  Masked UV/알파 클리핑, Transparent 제외 및 Static 제출의 b9 해제 확인.
- 스킨드 그림자 GPU 검증에서 D3D11 debug layer 경고/오류 없음.

`ValidateShadows.ps1` 전체 회귀 테스트는 Skybox/HDR/specular IBL/material maps와
스킨드 그림자 검증까지 통과하지만 `visible cast shadow on separate receiver`에서
실패한다. 수신면 RGB는 기존 기록과 동일한 167,156,153 → 122,106,96이며,
빨강 채널 감소량 45가 기존 판정 기준인 50 초과를 만족하지 못한다.
이 실패는 스킨드 Shadow 연결 이전에도 기록되어 있다. `near`의 Windows 매크로
충돌은 테스트의 지역 함수 이름을 nearColor로 변경하여 해소했고, 밝기 판정 기준은
변경하지 않았다.
