# Canape 빌드 환경 설정

## 첫 빌드

- 첫 빌드에서 의존성을 내려받아 Debug/Release 두 벌을 직접 컴파일한다. (인터넷 필요, 몇 분 걸림)

1. Visual Studio Installer에서 **vcpkg 패키지 관리자** 구성 요소를 설치한다.
2. **Developer PowerShell for VS**를 열고 아래 명령을 실행한다.
   ```powershell
   vcpkg integrate install
   ```
3. `Canape.slnx`를 열어 평소처럼 빌드한다.

## 파일 위치

| 항목 | 위치 |
|---|---|
| 의존성 목록 | 저장소 루트 `vcpkg.json` |
| 레지스트리/버전 고정 | 저장소 루트 `vcpkg-configuration.json` |
| 설치 결과 | 저장소 루트 `vcpkg_installed/` |

## 문제 해결

- `vcpkg` 관련 오류가 나면 위 2번(`vcpkg integrate install`)을 했는지 확인한다.
- 예전 빌드 결과가 꼬이면 `vcpkg_installed/`와 `Intermediate/`를 지우고 다시 빌드한다.
