# 라이트맵 슬롯과 UV 규칙

일반 머티리얼 텍스처(BaseColor, Normal, Metallic/Roughness, AO, Emissive)는 UV0을 사용하고, 라이트맵은 UV1을 사용한다. 기존 `MaterialDesc::uvIndex`는 이전 `.mat` 파일과의 호환성을 위해 보존하며 렌더링에는 사용하지 않는다. 노멀맵과 탄젠트 생성도 UV0 기준을 유지한다.

## 에셋 지정

`.mat`의 최상위 `lightmapTexture`에 기존 텍스처 에셋 경로를 지정한다. 인스펙터에서도 같은 필드를 편집할 수 있다.

```json
{
    "baseColorTexture": "Content/Textures/BaseColor.png",
    "lightmapTexture": "Content/Textures/Lightmap.png"
}
```

필드 생략, 빈 문자열 또는 텍스처 로드 실패 시 라이트맵 없는 머티리얼로 처리한다. 기존 텍스처 핸들 소유권과 머티리얼 갱신·해제 경로를 재사용한다. glTF의 AO·Emissive를 라이트맵으로 자동 변환하지 않는다.

현재 슬롯은 기존 **RGBA8_UNORM 선형 2D 텍스처 경로**를 사용한다. HDR/EXR 로드, 베이크 데이터의 정규화·색 공간 변환은 포함하지 않는다. 입력 파일의 데이터가 이 경로에 맞게 준비되어 있어야 한다.

## 셰이더 인터페이스

- `VertexOutput.UV` / `TEXCOORD0`: 일반 텍스처용.
- `VertexOutput.UV1` / `TEXCOORD1`: 라이트맵용. 정적·스킨드 메시에서 전달하며 위치 전용 정점은 `(0, 0)`으로 초기화한다.
- `LightmapTexture`: PS `t10`, CPU 상수 `ShaderConstants::LightmapTextureSlot`.
- `HasLightmap`: PS `b1`의 `MaterialBuffer`에 포함. 유효한 텍스처가 있을 때 1, 없으면 0.
- CPU/HLSL 머티리얼 상수 크기는 64바이트이며 `HasLightmap`은 바이트 오프셋 48에 있다.

메인 드로우마다 라이트맵 SRV를 바인딩하며 없는 경우 null을 바인딩해 이전 머티리얼의 텍스처가 남지 않게 한다. `HasLightmap`은 텍스처 유효성만 나타내며 원본 메시의 UV1 존재 여부는 검증하지 않는다. UV1이 없는 메시에는 기본 `(0, 0)`이 들어 있다.

**현재 최종 조명은 라이트맵을 샘플링하거나 합성하지 않는다.** 슬롯과 UV1 전달만 준비되어 있어 기존 화면 결과가 유지된다. 합성은 베이크 데이터 계약을 확정한 뒤 별도 작업으로 연결한다.

## 검증

```powershell
./Engine/Source/Asset/Material/Tests/ValidateMaterials.ps1
./Engine/DX11Renderer/Tests/ValidateMeshShaders.ps1
./Engine/DX11Renderer/Tests/ValidatePreparedScene.ps1
./Engine/DX11Renderer/Tests/ValidateIBLRuntime.ps1
```

`.mat` 호환성과 저장·로드, 두 UV 세트의 정적·스킨드 VS 출력, 라이트맵 리소스 수명, 실제 GPU 슬롯·상수의 전달과 초기화, 기존 조명 결과 유지를 검사한다.
