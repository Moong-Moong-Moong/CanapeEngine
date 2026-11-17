# Mesh VS/PS 분리

이번 변경은 HLSL 구현과 독립 검증 코드에 한정한다. 제품 C++ 코드, InputLayout,
VertexFormat, RenderItem/RenderScene, Shadow Pass 및 Shadow VS는 변경하지 않았다.

## 파일과 책임

경로는 `Engine/DX11Renderer/Shader/` 기준이다.

| 파일 | 변경 | 책임 |
| --- | --- | --- |
| `MeshStaticVS.hlsl` | 추가 | `VSMain(StaticVertexInput)`: 입력을 `ReadSurface`로 읽고 `BuildVertexOutput` 호출 |
| `MeshSkinnedVS.hlsl` | 추가 | `VSMain(SkinnedVertexInput)`: `ReadSurface` → `ApplySkinning` → `BuildVertexOutput` |
| `MeshPS.hlsl` | 추가 | 기존 `PSMain` 본문을 그대로 이동한 공유 PS |
| `Common/MeshTypes.hlsli` | 수정 | 명시적인 Position/Static/Skinned 입력, 공통 `LocalSurface`, 기존 `VertexOutput` |
| `Common/MeshTransform.hlsli` | 추가 | `ReadSurface`, `BuildVertexOutput`: 로컬 데이터 생성과 공통 월드/클립 변환 |
| `Common/Skinning.hlsli` | 추가 | VS 전용 팔레트와 4개 영향의 Linear Blend Skinning |
| `Mesh.hlsl` | 수정 | 기존 C++ 컴파일 경로를 위한 호환 진입점; Static VS와 PS 구현을 include |

`Common/MeshSurface.hlsli`, `Material.hlsli`, `Lighting.hlsli`,
`SkyboxEnvironment.hlsli`, `Constants.hlsli`는 그대로 재사용한다.

## 기존 경로와 변환 규약

- 기존 `STATIC_VERTEX`는 **PositionOnly / 일반 StaticVertex** 구분이다.
  `Mesh.hlsl`에서 매크로가 있으면 `MeshStaticVS.hlsl`을 사용하고, 없으면
  PositionOnly 입력에 Normal=0, UV=0, Tangent=(0,0,0,1)을 채운다.
  Skinned 선택용 매크로로 재해석하지 않았다. 새 VS들은 매크로 없이 컴파일한다.
- 기존 `Shaders/Mesh.hlsl`의 `VSMain`, `PSMain` 호출을 그대로 사용할 수 있다.
  제품 C++ 코드의 컴파일 파일명/엔트리/플래그를 바꿀 필요가 없다.
- `ObjectBuffer b0`는 World와 WorldInverseTranspose, 128바이트이다.
  `ViewBuffer b2`는 View/Projection/ViewProjection/CameraPosition, 208바이트이다.
  레이아웃과 슬롯을 바꾸지 않았다.
- 엔진의 `D3DCOMPILE_PACK_MATRIX_ROW_MAJOR` (`fxc /Zpr`), CPU 무전치 업로드,
  `mul(vector, matrix)` 규약을 유지한다.
- `BuildVertexOutput`은 Position에 World → ViewProjection을 적용하고,
  Normal에는 WorldInverseTranspose, Tangent.xyz에는 World를 적용한다.
  Tangent.w는 그대로 전달한다. VS에서 추가 정규화를 하지 않는다.
- 공통 출력은 `SV_POSITION:float4`, `WORLDPOS:float3`, `NORMAL:float3`,
  `TEXCOORD0:float2`, `TANGENT:float4`로 기존과 같다.
- TBN은 기존 `GetShadingNormal`에서 Normal 정규화, Tangent 직교화/정규화,
  `cross(normal,tangent) * sign(Tangent.w)`로 구성한다. PositionOnly의
  미분 기반 geometric normal fallback도 그대로이다.
- PBR, Normal Mapping, ORM, AO, alpha mask/blend, specular IBL, HDR 출력 및
  기존 shadow sampling 코드는 변경하지 않았다.
- **Fog는 변경 전에도 상수와 CPU 업로드만 있고 mesh PS에 적용하는 코드가 없다.**
  이번 작업에서도 적용 코드를 새로 추가하지 않았다.
- Include는 각 부모 파일 기준 상대 경로이다. 실제 `ShaderFileIncludes`로
  중첩 include를 검증했다. Editor/Game의 재귀 `*.hlsl;*.hlsli` 복사 규칙에
  새 파일들이 포함되므로 복사 설정도 변경하지 않았다.

## 스키닝 계약과 지원 범위

- 입력은 기본 4개 속성 외에 `uint4 BoneIndices : BLENDINDICES0`,
  `float4 BoneWeights : BLENDWEIGHT0`이다. 인덱스는 이 draw 팔레트의 상대 인덱스다.
- `SkinningBuffer : register(b9)`는 **VS 전용**이며
  `row_major float4x4 SkinMatrices[256]`, 총 **16,384바이트**이다.
  기존 b0~b8 의미를 유지하기 위해 새 슬롯을 사용했다.
- 팔레트는 CPU가 계산한 최종 bind-pose mesh-local → posed mesh-local 변환이다.
  역바인드/본 계층/애니메이션 평가는 셰이더가 수행하지 않는다.
  이 팔레트에는 이후 적용할 World를 중복 포함하면 안 된다.
- 유한한 입력을 전제로 음수 가중치를 0으로 제한하고 합으로 정규화한다.
  합이 `1e-8` 이하이면 원본 표면을 반환한다. 양수 가중치의 인덱스는
  반드시 [0,255] 범위의 초기화된 항목을 가리켜야 한다. 활성 인덱스 오류를
  셰이더에서 보정하지 않는다. 가중치가 0인 항목은 읽지 않는다.
- 가중합 행렬로 Position(w=1), Normal/Tangent.xyz(w=0)를 변환한다.
  따라서 본 이동은 위치에만 적용되고 UV와 Tangent.w는 보존된다.
- 초기 지원 대상은 회전/이동 본 팔레트의 통상적인 LBS이다. 여러 회전을
  혼합해도 유효 행렬에 scale/shear가 생길 수 있어 Normal의 방향 가중합은
  근사이다. 혼합 행렬의 정확한 inverse-transpose를 계산하지 않는다.
- **본의 비균일 스케일, shear, 음수 determinant/반사는 올바른 Normal/TBN
  셰이딩 지원 대상이 아니다.** Tangent.w를 유지하므로 반사 부호도 보정하지 않는다.
  WorldInverseTranspose는 이후 World의 비균일 스케일만 처리하며, 본 변환의
  문제를 교정하지 않는다. 향후 지원 시 bone normal palette 또는 혼합 변환의
  inverse-transpose와 특이 행렬/handedness 처리 방식을 별도로 설계해야 한다.
  기존 Static 경로의 World 반사에 대한 handedness 처리 역시 변경하지 않았다.

## 검증

실행: 저장소 루트에서 `./Engine/DX11Renderer/Tests/ValidateMeshShaders.ps1`.
Visual Studio C++ 도구, Windows SDK FXC, D3D11 WARP가 필요하다.
산출물과 실행용 shader 사본은 `Intermediate/MeshShaderValidation/`에 생성한다.
테스트용 C++ 파일은 제품 프로젝트에 편입하지 않았다.

2026-11-17 수행 결과:

- FXC `/WX /Zpr`, `vs_5_0` / `ps_5_0`로 새 3개 진입점과 기존 Mesh의
  PositionOnly/Static VS, PS(매크로 유무)를 모두 컴파일했다.
- 기존 debug PS 5개, Shadow VS 두 변형과 masked PS, FinalOutput/Skybox
  VS·PS, CameraDepth PS도 컴파일했다. 전체 20개 컴파일 성공.
  Shadow 소스는 수정하지 않고 공통 헤더 사용의 회귀 여부만 확인했다.
- 수정 전에 저장한 FXC 어셈블리와 수정 후 PositionOnly VS, Static VS,
  PS 전체 텍스트의 SHA-256이 각각 동일했다. 호환 wrapper와 독립 파일의
  Static VS/PS 어셈블리도 동일했다. 이 비교 산출물은 로컬 Intermediate에만 있다.
- 실제 `CompileShaderFile`/`ShaderFileIncludes`로 새 파일과 기존 호환 파일을
  컴파일하고, reflection으로 두 VS 및 호환 VS의 출력과 PS 입력의
  semantic/index/register/type/mask 호환성을 확인했다.
- reflection으로 b9 슬롯, 팔레트 16,384바이트, row-major 256개 행렬을 확인했다.
- WARP에서 실제 VS를 실행하고 Geometry Shader stream output으로 16개 출력
  성분을 읽어 CPU 기준값과 비교했다(절대 오차 `< 1e-4`). 다음 6개 사례가 통과했다.
  1. Static: 비균일 World scale + 회전/이동, ViewProjection, Normal/Tangent, UV, Tangent.w
  2. Identity 팔레트: 4개 영향 혼합, 마지막 인덱스 255 포함
  3. 한 본에 가중치 1: 회전/이동, 비활성 인덱스 65535 제외
  4. 두 본 가중치 1:3을 0.25:0.75로 정규화한 혼합
  5. 가중치 합 0일 때 원본 유지
  6. 음수 가중치 제한 후 정규화

실제 애니메이션 장면의 엔진 내 렌더링, 하드웨어 GPU의 화면 비교는 수행하지 않았다.
Skinned 경로는 요청 범위대로 제품 C++ 바인딩에 연결하지 않았기 때문이다.
픽셀 셰이딩 보존은 기존과 동일한 PS 어셈블리 및 관련 셰이더 컴파일로 확인했다.

## 추후 C++ 연동 작업

1. 선택적으로 Static은 `Shaders/MeshStaticVS.hlsl:VSMain`, PS는
   `Shaders/MeshPS.hlsl:PSMain`으로 직접 컴파일한다. 기존 wrapper 사용도 가능하다.
2. `Shaders/MeshSkinnedVS.hlsl:VSMain`을 `vs_5_0`, row-major 플래그로 컴파일하고,
   기존 공통 PS와 조합하여 Skinned 전용 VS/InputLayout 선택 경로를 연결한다.
3. 실제 `SkinnedVertex`의 `offsetof`/stride를 확인하고 기존 속성 외에
   `Joints`를 `BLENDINDICES0 / DXGI_FORMAT_R16G16B16A16_UINT`,
   `Weights`를 `BLENDWEIGHT0 / DXGI_FORMAT_R32G32B32A32_FLOAT`로 연결한다.
   인덱스는 UNORM이 아닌 UINT로 읽어야 한다.
4. 16,384바이트 상수버퍼를 만들고 이 draw의 최종 팔레트를 VS b9에 업로드/바인딩한다.
   이번 HLSL에는 palette offset/count가 없으므로 기존 paletteOffset/count를
   CPU에서 적용해 로컬 팔레트로 전달하고, 활성 인덱스와 256개 용량을 검증한다.
5. 애니메이션/계층/역바인드 처리를 구현하거나 기존 결과를 연결한다.
   World 및 View/Object 바인딩은 기존 계약을 사용한다.
6. 실제 장면에서 normal map/미러 UV/애니메이션을 확인한다. Skinned shadow는
   이번 변경에 포함되지 않았으므로 별도 작업이 필요하다.
