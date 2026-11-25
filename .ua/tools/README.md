# .ua/tools

`/understand`(understand-anything 플러그인)로 이 저장소를 다시 분석할 때 쓰는 보조 스크립트다.
대시보드를 보기만 할 때는 필요 없다.

경로는 모두 이 폴더 위치에서 계산하므로 저장소를 어디에 받아도 그대로 동작한다.
플러그인 위치는 Claude 플러그인 캐시에서 최신 버전을 찾고, 다른 곳에 있으면 환경 변수 `UA_SKILL_DIR`에
`skills/understand` 폴더를 지정한다. Node.js만 있으면 된다.

## 왜 필요한가

- 플러그인의 C/C++ include 해석기는 MSBuild `AdditionalIncludeDirectories`를 모른다.
  보정하지 않으면 import 엣지가 68개만 잡힌다(보정 후 1,181개).
- 플러그인의 병합 스크립트는 `Tests/*Validation.cpp`를 테스트 파일로 인식하지 못해
  `tested_by` 엣지를 전부 버린다.
- 구조 추출기는 HLSL·PowerShell 파서가 없고, 포인터/참조를 반환하는 C++ 함수 정의,
  네임스페이스 수준 `#if` 블록 안의 정의, 가변 인자 템플릿을 놓친다.
  분석 에이전트에게 주는 지시문(`prep-batches.cjs`)에 이 보완 요구가 들어 있다.

## 실행 순서

저장소 루트에서 실행한다. 플러그인 단계 사이에 끼워 넣는 순서다.

| 시점 | 명령 | 하는 일 |
|---|---|---|
| 스캔 직후 | `node .ua/tools/augment-import-map.mjs .` | `scan-result.json`의 import 맵을 vcxproj include 경로 기준으로 보강 |
| 배치 계산 직후 | `node .ua/tools/prep-batches.cjs` | 배치별 입력·컨텍스트·지시문(`.ua/tmp/ua-dispatch-N.md`), 클래스 인덱스, 스캔 경로 목록 생성 |
| 배치가 끝날 때마다 | `node .ua/tools/verify-batch.cjs N` | 파일 노드 누락, 중복 ID, 필수 필드, import 엣지 수 검사 |
| 병합 직후 | `node .ua/tools/enrich-graph.cjs --write` | 끊긴 엣지 재연결, `tested_by` 복원, 선언 기반 `inherits` 보강 |
| 레이어·투어 단계 | `node .ua/tools/graph-tools.cjs <명령>` | `prep-arch`, `normalize-layers`, `prep-containers`, `apply-containers`, `prep-tour`, `prep-lessons`, `normalize-lessons`, `assemble-final <커밋 해시>` |
| 레슨이 끝날 때마다 | `node .ua/tools/verify-lessons.cjs N`, `node .ua/tools/lesson-alignment.cjs N` | 줄 범위와 설명이 실제 소스와 맞는지 검사 |

`--write` 없이 `enrich-graph.cjs`를 실행하거나 `augment-import-map.mjs`에 `--dry-run`을 붙이면 파일을 바꾸지 않고 결과만 출력한다.

## 알아둘 점

- 전체 분석(`--full`) 한 번으로 검증했다. 플러그인의 증분 업데이트 경로에서는 아직 써 보지 않았으니,
  증분 실행 뒤에는 import 엣지 수가 줄지 않았는지 확인한다.
- 파일 분석 배치와 코드 레슨은 Sonnet으로 돌려도 충분했다. 지시문의 "Quality bar"를 빼면 결과가 얕아진다.
- 컨테이너 이름과 코드 레슨용 지시문 생성은 스크립트로 남아 있지 않다.
