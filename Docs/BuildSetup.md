# Canape 빌드 환경 설정

## 첫 빌드

- 첫 빌드에서 `vcpkg.json`의 의존성을 내려받아 Debug/Release 두 벌을 직접 컴파일한다. (인터넷 필요, 몇 분 걸림)

1. Visual Studio Installer에서 **vcpkg 패키지 관리자** 구성 요소를 설치한다.
2. **Developer PowerShell for VS**를 열고 아래 명령을 실행한다.
   ```powershell
   vcpkg integrate install
   ```
3. `Canape.slnx`를 열어 평소처럼 빌드한다.

## 외부 라이브러리

| 라이브러리 | 출처 | 쓰는 곳 |
|---|---|---|
| JoltPhysics | vcpkg `joltphysics` | JoltPhysics |
| RTTR | vcpkg `rttr` (overlay 포트) | Runtime, Game, Editor |
| nlohmann/json | vcpkg `nlohmann-json` | Runtime, Game |
| Dear ImGui | vcpkg `imgui[docking-experimental,dx11-binding,win32-binding]` | Editor |
| ufbx | 저장소 `Engine/Source/ThirdParty/ufbx` | Runtime |

- **RTTR은 overlay 포트로 받는다.** 공식 포트는 static 라이브러리 + 동적 CRT(`x64-windows-static-md`) 조합을 막아 두어, 그 제한만 푼 레시피를 `Tools/VcpkgOverlays/rttr`에 둔다.
- **ufbx는 소스로 둔다.** 고정한 baseline의 vcpkg 레지스트리에 포트가 없다.

## 파일 위치

| 항목 | 위치 |
|---|---|
| 의존성 목록 | 저장소 루트 `vcpkg.json` |
| 레지스트리/버전 고정, overlay 포트 경로 | 저장소 루트 `vcpkg-configuration.json` |
| 매니페스트 모드, triplet | 저장소 루트 `Directory.Build.props` |
| overlay 포트 | `Tools/VcpkgOverlays/` |
| 설치 결과 | 저장소 루트 `vcpkg_installed/` |

## 주의

- **overlay 포트는 baseline을 따라가지 않는다.** baseline을 올릴 때 레지스트리의 `ports/rttr`와 비교해 함께 맞춘다.

## 문제 해결

- `vcpkg` 관련 오류가 나면 위 2번(`vcpkg integrate install`)을 했는지 확인한다.
- 예전 빌드 결과가 꼬이면 `vcpkg_installed/`와 `Intermediate/`를 지우고 다시 빌드한다.
