# 기사단 게임 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 타이틀 → 게임 씬으로 넘어가 플레이어 1 + AI 4가 8라운드 "기사단 게임"을 2D UI로 플레이한다.

**Architecture:** 엔진과 무관한 순수 C++ 룰 코어(`Knights::Game`)와 AI(`Knights::Ai`)가 모든 판정을 맡고, 씬 스크립트(`KnightsBoardController`, `KnightsTitleMenu`)는 코어 상태를 읽어 UI를 그리고 입력·연출 타이밍만 다룬다. 룰과 AI는 엔진 링크 없이 `cl`로 빌드하는 단독 테스트로 검증한다.

**Tech Stack:** C++23 (MSVC `/std:c++23preview`), Canape 엔진 `ScriptComponent` + 게임 UI(`UICanvasComponent`, `UIButton`, `UIText`, `UIImage`, `UIHorizontalBox`/`UIVerticalBox`), RTTR 등록, `std::mt19937`.

**Spec:** `Docs/superpowers/specs/2026-11-27-knights-board-game-design.md` — 룰 확정본은 스펙 3절. 구현자는 스펙과 이 계획을 함께 읽는다.

## Global Constraints

- `Game/Source/KnightsGame/KnightsRules.*`, `KnightsAi.*`는 표준 라이브러리만 include한다. 엔진 헤더(`Engine.h` 등) 금지.
- `Engine/` 아래 코드는 수정하지 않는다.
- 빌드 경고 없이(`/W4`) 컴파일된다. 소스는 UTF-8.
- 코드 스타일은 기존 Game 코드를 따른다: 4칸 들여쓰기, 멤버 `m_`, 상수 `k`, 한국어 주석.
- 상수: 플레이어 5명(0 = 사람, 1~4 = AI), 지역 6곳, 8라운드, 깃발 2개, 시작 기사 5명.
- 지역 테이블(인덱스 순서 고정):
  0 고블린 둥지 Monster 1~3 / 1 오크 야영지 Monster 3~6 / 2 트롤 다리 Monster 5~9 / 3 드래곤 동굴 Monster 8~12 / 4 훈련소 TrainingGround / 5 용병 시장 MercenaryMarket
- AI 성향(플레이어 인덱스 순): 1 공격형, 2 신중형, 3 증원형, 4 균형형.
- 화면 문구: 제목 "기사단 게임", 버튼 "게임 시작", "종료", "확정", "다시 하기", "타이틀로". 플레이어 이름 "나", "AI 1"~"AI 4".
- 씬 이름: `KnightsTitle`, `KnightsGame`. `Game/Content/Game.json`의 `startupScene`은 `KnightsTitle`.
- 커밋은 `GamePlay` 브랜치에 태스크마다 한 번.

## Review Focus

1. 플레이어 기사가 0명이거나 깃발이 증원 지역에만 있을 때 기사 배치 단계에서 막히지 않아야 한다(전부 0 배치를 받아 주고, [확정]이 눌린다). → Task 2 테스트 `ZeroAllocationAccepted`, Task 6 UI 단계.
2. [확정] 연타·AI 차례 중 클릭: 같은 플레이어가 두 번 제출되면 거부되고 상태가 바뀌지 않아야 한다. → Task 1 테스트 `DoubleSubmitRejected`.
3. [다시 하기]는 점수·기사·라운드·공개 기록을 모두 처음 상태로 되돌려야 한다. → Task 3 테스트 `ResetRestoresStart`.
4. 매 라운드 몬스터 점수와 주사위가 항상 범위 안이어야 한다. → Task 4 자가 대전 불변식.
5. 한 플레이어가 여러 지역에서 동시에 전멸할 때 손실이 지역마다 정확히 빠지고 음수가 되지 않아야 한다. → Task 2 테스트 `WipeAcrossRegions`.

---

### Task 1: 룰 코어 골격 — 지역 테이블, 라운드 시작, 깃발 제출, 공개 뷰, 테스트 하니스

**Files:**
- Create: `Game/Source/KnightsGame/KnightsRules.h`, `Game/Source/KnightsGame/KnightsRules.cpp`
- Create: `Game/Tests/KnightsRulesValidation.cpp`, `Game/Tests/ValidateKnightsRules.ps1`
- Add (이미 작성됨, 커밋만): `Docs/superpowers/specs/2026-11-27-knights-board-game-design.md`, `Docs/superpowers/plans/2026-11-27-knights-board-game.md`

**Interfaces:**
- Produces (`namespace Knights`, `KnightsRules.h`):

```cpp
constexpr int kPlayerCount = 5;
constexpr int kRegionCount = 6;
constexpr int kRoundCount = 8;
constexpr int kFlagsPerPlayer = 2;
constexpr int kStartKnights = 5;
constexpr int kHumanPlayer = 0;

enum class RegionKind { Monster, TrainingGround, MercenaryMarket };
struct RegionDef { std::string_view name; RegionKind kind; int minScore; int maxScore; }; // 증원 지역은 0, 0
const std::array<RegionDef, kRegionCount>& GetRegionDefs();

enum class Phase { FlagPlacement, KnightPlacement, Resolution, GameOver };
// 공개는 단계 전환으로 표현한다: KnightPlacement 진입 = 깃발 공개, Resolution 진입 = 기사 공개 + 정산 완료.

enum class SubmitError { None, WrongPhase, AlreadySubmitted, InvalidPlayer, InvalidRegion, DuplicateRegion,
                         NoFlagThere, NotMonsterRegion, NegativeCount, ExceedsKnights };

using FlagChoice = std::array<int, kFlagsPerPlayer>;      // 지역 인덱스 2개
using KnightAllocation = std::array<int, kRegionCount>;   // 지역별 보낸 기사 수

struct PlayerState { int score = 0; int knights = kStartKnights; };

struct PublicView
{
    int viewer = 0;
    int round = 1;                                          // 1..8
    Phase phase = Phase::FlagPlacement;
    std::array<PlayerState, kPlayerCount> players{};
    std::array<int, kRegionCount> monsterScores{};          // 증원 지역은 0
    std::array<bool, kPlayerCount> submitted{};             // 현재 단계 제출 여부(모두 공개)
    std::array<std::array<bool, kRegionCount>, kPlayerCount> flags{};   // 공개 전에는 viewer 것만 채워짐
    std::array<KnightAllocation, kPlayerCount> knights{};               // 공개 전에는 viewer 것만 채워짐
};

class Game
{
public:
    explicit Game(uint32_t seed);
    void Reset(uint32_t seed);                 // 1라운드 FlagPlacement, 몬스터 점수 굴림
    Phase GetPhase() const;
    int GetRound() const;
    const PlayerState& GetPlayer(int player) const;
    int GetMonsterScore(int region) const;
    bool HasSubmitted(int player) const;
    SubmitError SubmitFlags(int player, const FlagChoice& regions);
    SubmitError SubmitKnights(int player, const KnightAllocation& allocation);   // Task 2
    const std::vector<RegionEvent>& GetResolution() const;                        // Task 2
    void AdvanceRound();                                                          // Task 3
    std::vector<int> GetWinners() const;                                          // Task 3
    PublicView GetView(int viewer) const;
};
```

- [ ] **Step 1: 테스트 하니스와 실패하는 테스트 작성**

`KnightsRulesValidation.cpp`: `main()`이 테스트 함수들을 차례로 부르고, `CHECK(expr)` 매크로가 실패 시 파일·줄·식을 출력하고 실패 수를 센다. 끝에 실패가 없으면 `ALL PASSED (N checks)`, 있으면 `FAILED (k)`를 출력하고 종료 코드 1.

```cpp
void RegionTable()        // GetRegionDefs(): 이름·종류·범위가 Global Constraints 표와 정확히 같다
void StartState()         // Game g(1): round 1, FlagPlacement, 모든 플레이어 score 0 knights 5,
                          //  몬스터 0~3 점수가 범위 안, 4·5는 0
void FlagValidation()     // SubmitFlags: {0,0} → DuplicateRegion, {0,6} → InvalidRegion, {-1,2} → InvalidRegion,
                          //  player 5 → InvalidPlayer, {0,4} → None
void DoubleSubmitRejected() // 같은 플레이어 두 번째 SubmitFlags → AlreadySubmitted, 첫 선택 유지
void FlagsHiddenUntilAll()  // 0이 {0,4} 제출 후 GetView(1).flags[0] 전부 false, submitted[0] == true,
                            //  GetView(0).flags[0][0] == true. 5명 모두 제출하면 phase == KnightPlacement,
                            //  GetView(1).flags[0][0] == true
void KnightsSubmitWrongPhase() // FlagPlacement 중 SubmitKnights → WrongPhase
```

- [ ] **Step 2: `ValidateKnightsRules.ps1` 작성**

`ValidatePlayground.ps1`처럼 vswhere로 VS를 찾아 `vcvars64.bat`를 부르는 배치를 `Intermediate/KnightsRulesValidation/`에 쓰고 실행한다. 컴파일 명령:

```
cl /nologo /std:c++23preview /EHsc /MD /utf-8 /W4 /WX /IGame\Source Game\Tests\KnightsRulesValidation.cpp Game\Source\KnightsGame\KnightsRules.cpp Game\Source\KnightsGame\KnightsAi.cpp /FoIntermediate\KnightsRulesValidation\ /FeIntermediate\KnightsRulesValidation\KnightsRulesValidation.exe
```

`KnightsAi.cpp`가 아직 없으면(Task 4 전) 그 파일은 목록에서 빼는 조건을 넣는다(`Test-Path`). 빌드 실패·테스트 실패 시 `throw`, 성공 시 실행 출력 그대로 보여 준다. 저장소 루트에서 `powershell -ExecutionPolicy Bypass -File Game/Tests/ValidateKnightsRules.ps1`로 돈다.

- [ ] **Step 3: 실행해 실패 확인**

Run: `powershell -ExecutionPolicy Bypass -File Game/Tests/ValidateKnightsRules.ps1`
Expected: 컴파일 실패(`KnightsRules.h` 없음).

- [ ] **Step 4: `KnightsRules.h/.cpp` 구현 (Task 1 범위)**

`Game`은 `std::mt19937`을 소유한다. 몬스터 점수는 `std::uniform_int_distribution<int>(min, max)`. `SubmitKnights`/`GetResolution`/`AdvanceRound`/`GetWinners`는 선언하고 Task 2·3에서 채운다(그때까지 `SubmitKnights`는 KnightPlacement가 아니면 `WrongPhase`). `RegionEvent`는 Task 2에서 정의하므로 이 태스크에서 같이 선언만 해 둔다(Task 2 Interfaces 참고). 마지막 깃발 제출 시 phase를 `KnightPlacement`로 바꾸고 `submitted`를 비운다.

- [ ] **Step 5: 실행해 통과 확인**

Run: 위와 같음. Expected: `ALL PASSED`.

- [ ] **Step 6: Commit**

```bash
git add Game/Source/KnightsGame/KnightsRules.h Game/Source/KnightsGame/KnightsRules.cpp Game/Tests/KnightsRulesValidation.cpp Game/Tests/ValidateKnightsRules.ps1 Docs/superpowers
git commit -m "feat: 기사단 게임 룰 코어 골격과 깃발 단계"
```

---

### Task 2: 기사 배치와 몬스터 정산 (스펙 3.4)

**Files:**
- Modify: `Game/Source/KnightsGame/KnightsRules.h/.cpp`
- Modify: `Game/Tests/KnightsRulesValidation.cpp`

**Interfaces:**
- Consumes: Task 1의 `Game`, `KnightAllocation`, `SubmitError`.
- Produces:

```cpp
struct RegionEvent
{
    int region = 0;
    RegionKind kind = RegionKind::Monster;
    int monsterScore = 0;        // Monster만
    int totalKnights = 0;        // Monster만
    bool wiped = false;          // Monster만
    int dice = 0;                // MercenaryMarket만, 깃발이 없으면 0(굴리지 않음)
    int discarded = 0;           // 폐기된 점수(Monster) 또는 기사(MercenaryMarket)
    std::array<int, kPlayerCount> sent{};          // Monster: 보낸 기사
    std::array<bool, kPlayerCount> flagged{};
    std::array<int, kPlayerCount> scoreGained{};
    std::array<int, kPlayerCount> knightsLost{};
    std::array<int, kPlayerCount> knightsGained{};
};

// 순수 함수: 정산 규칙만. Game이 사용하고 테스트가 직접 부른다.
RegionEvent ResolveMonster(int region, int monsterScore, const std::array<int, kPlayerCount>& sent,
                           const std::array<bool, kPlayerCount>& flagged);
```

- [ ] **Step 1: 실패하는 테스트 작성**

```cpp
void MonsterTable()   // ResolveMonster, 스펙 3.4 표 그대로 (A=0, B=1, C=2):
  // S1 {1,1,0}: gained {0,0,0}, discarded 1, wiped false
  // S5 {2,5,0}: gained {2,3,0}, discarded 0
  // S4 {1,3,0}: gained {1,3,0}
  // S5 {2,2,3}: gained {2,2,1}
  // S5 {3,3,0}: gained {2,2,0}, discarded 1
  // S7 {2,3,0}: wiped true, knightsLost {2,3,0}, gained 0
  // S3 {3,0,0}: gained {3,0,0}
  // S3 {0,0,0} (A만 flagged): totalKnights 0, wiped false, 아무 변화 없음
void KnightValidation() // KnightPlacement에서: 깃발 없는 지역 배치 → NoFlagThere, 증원 지역 배치 → NotMonsterRegion,
                        //  음수 → NegativeCount, 합 > 보유 → ExceedsKnights
void ZeroAllocationAccepted() // 깃발이 {4,5}(증원만)인 플레이어의 전부 0 배치 → None
void KnightsHiddenUntilAll()  // 1명 제출 후 다른 뷰에서 knights[그 플레이어] 전부 0, 전원 제출 후 phase == Resolution,
                              //  뷰에서 모두 보임
void WipeAcrossRegions() // 몬스터 점수를 알 수 없으므로: 플레이어 0이 지역 0·3에 깃발, 1·4명 배치 시
                         //  GetResolution()의 해당 이벤트들의 knightsLost/scoreGained 합만큼 GetPlayer(0) 변화가 일치하고
                         //  knights >= 0
```

- [ ] **Step 2: 실행해 실패 확인** — Expected: 컴파일 실패(`ResolveMonster` 없음).

- [ ] **Step 3: `ResolveMonster` 구현과 `SubmitKnights` 연결**

분배 알고리즘(스펙 3.4, 사용자 확정 (가)안):

```
T = sum(sent); if T == 0 → 이벤트만 반환
if S > T → wiped, knightsLost = sent
else R = S; 보낸 수 c>0 인 플레이어를 c 오름차순으로 묶음 처리
     묶음 인원 n: if R >= c*n → 각자 c, R -= c*n
                  else → 각자 R/n (정수 나눗셈), discarded = R - (R/n)*n, R = 0, 종료
     모든 묶음 처리 후 남은 R은 discarded에 더함
```

마지막 기사 제출 시 지역 0~5 순서로 이벤트를 만들어 `GetResolution()`에 담고, 점수·기사 변화를 `PlayerState`에 반영한 뒤 phase = `Resolution`. 증원 지역 이벤트는 Task 3에서 채운다(이 태스크에서는 `flagged`만 채운 빈 이벤트).

- [ ] **Step 4: 실행해 통과 확인** — Expected: `ALL PASSED`.

- [ ] **Step 5: Commit** — `git commit -m "feat: 기사단 게임 기사 배치와 몬스터 정산"`

---

### Task 3: 증원 지역, 라운드 진행, 승패

**Files:**
- Modify: `Game/Source/KnightsGame/KnightsRules.h/.cpp`, `Game/Tests/KnightsRulesValidation.cpp`

**Interfaces:**
- Produces:

```cpp
RegionEvent ResolveTraining(int region, const std::array<bool, kPlayerCount>& flagged);           // 깃발 1개당 +1
RegionEvent ResolveMarket(int region, int dice, const std::array<bool, kPlayerCount>& flagged);   // floor(d/n), 나머지 discarded
void Game::AdvanceRound();               // Resolution에서만: round < 8 → round+1, FlagPlacement, 점수 재굴림, 깃발·배치·제출 초기화
                                          //                    round == 8 → GameOver
std::vector<int> Game::GetWinners() const; // GameOver에서만: 최고 점수, 동점이면 기사 많은 쪽, 그래도 같으면 모두
```

- [ ] **Step 1: 실패하는 테스트 작성**

```cpp
void TrainingGround()  // flagged {1,0,1,0,0} → knightsGained {1,0,1,0,0}
void Market()          // d=1, 2명 → 각 0, discarded 1 / d=6, 4명 → 각 1, discarded 2 / d=5, 1명 → 5 / 깃발 0명 → dice 0
void ReinforceNextRound() // 정산 직후 GetPlayer의 knights에 증원이 더해져 있고, AdvanceRound 후 그 수만큼 배치 가능
                          //  (보유 = 5 + 1 인 플레이어가 6명 배치 → None)
void RoundsAndGameOver()  // 모든 플레이어가 {4,5} 깃발 + 0 배치로 8라운드 진행: 라운드 1..8 증가, 8 이후 GameOver,
                          //  GameOver에서 SubmitFlags → WrongPhase
void Winners()            // 점수·기사 직접 세팅이 불가하므로 공개 API로 만들 수 없는 경우는 GetWinners 내부 로직을
                          //  free function `std::vector<int> RankWinners(const std::array<PlayerState,kPlayerCount>&)`로
                          //  빼서 테스트: 점수 {3,5,5,1,0}/기사 {9,2,4,9,9} → {2}; 점수 {5,5,..}/기사 {3,3,..} → {0,1}
void ResetRestoresStart() // 몇 라운드 진행 후 Reset(7): round 1, FlagPlacement, 모두 0점 5명, 제출·공개 초기화
```

`GetWinners()`는 `RankWinners(m_Players)`를 부른다(헤더에 `RankWinners` 선언).

- [ ] **Step 2: 실행해 실패 확인**
- [ ] **Step 3: 구현** — 주사위는 `Game`의 RNG로 `uniform_int_distribution(1, 6)`, 깃발이 하나도 없으면 굴리지 않는다. 증원 기사는 정산 때 `knights`에 바로 더한다(다음 라운드 배치에서 쓰임 = 스펙 "다음 라운드부터").
- [ ] **Step 4: 실행해 통과 확인** — Expected: `ALL PASSED`.
- [ ] **Step 5: Commit** — `git commit -m "feat: 기사단 게임 증원 지역과 라운드·승패"`

---

### Task 4: AI

**Files:**
- Create: `Game/Source/KnightsGame/KnightsAi.h`, `Game/Source/KnightsGame/KnightsAi.cpp`
- Modify: `Game/Tests/KnightsRulesValidation.cpp`

**Interfaces:**
- Consumes: `PublicView`, `FlagChoice`, `KnightAllocation`, `GetRegionDefs()`.
- Produces:

```cpp
enum class AiPersonality { Aggressive, Cautious, Reinforcer, Balanced };
AiPersonality GetAiPersonality(int player);   // 1 Aggressive, 2 Cautious, 3 Reinforcer, 4 Balanced
class Ai
{
public:
    Ai(AiPersonality personality, uint32_t seed);
    FlagChoice ChooseFlags(const PublicView& view);           // 항상 서로 다른 유효 지역 2개
    KnightAllocation ChooseKnights(const PublicView& view);   // 항상 SubmitKnights가 None을 돌려줄 배치
};
```

AI는 `PublicView`만 받으므로 숨긴 정보에 접근할 길이 없다(치팅 방지는 구조로 보장).

- [ ] **Step 1: 실패하는 테스트 작성**

```cpp
void AiAlwaysValid()   // 시드 1..1000 게임 자가 대전: 플레이어 0도 Ai(Balanced)로 대체, 매 단계 모든 Submit이 None.
                       //  불변식: 몬스터 점수 ∈ [min,max], 주사위 ∈ [0,6], knights >= 0, 각 게임이 GameOver로 끝남
void AiNoHopelessSolo() // 깃발 공개 뷰를 직접 구성: viewer 2, 지역 3(점수 12)에 viewer만 깃발, viewer 기사 5명,
                        //  다른 플레이어 깃발 없음 → 모든 성향에서 ChooseKnights()[3] == 0
void AiVaries()         // 같은 뷰로 시드 1..50의 Ai(Balanced).ChooseFlags 결과가 2가지 이상
```

- [ ] **Step 2: 실행해 실패 확인** — Expected: 컴파일 실패(`KnightsAi.h` 없음).

- [ ] **Step 3: 구현**

지역마다 점수(기대 이득)를 매겨 가중 무작위로 고른다. 공식·가중치는 빌더 재량(스펙 4.4). 지켜야 할 것:
- 다른 플레이어가 보낼 기사 추정은 그 플레이어의 공개 보유 기사·공개 깃발 수로만 한다.
- 성향은 눈에 띄게 다르게: 공격형은 높은 점수 지역과 많은 기사, 신중형은 남보다 적게 보내기, 증원형은 보유 기사가 적을수록(예: 4명 이하) 증원 지역 우선, 균형형은 기대값.
- `AiNoHopelessSolo` 조건: 그 지역에 나 말고 깃발이 없고 내 보유 기사 < 몬스터 점수면 0명.

- [ ] **Step 4: 실행해 통과 확인** — Expected: `ALL PASSED`, 1000게임 완주.
- [ ] **Step 5: Commit** — `git commit -m "feat: 기사단 게임 AI 4종"`

---

### Task 5: 타이틀 씬, 게임 씬 파일, 프로젝트 등록

**Files:**
- Create: `Game/Source/KnightsGame/KnightsTitleMenu.h/.cpp`
- Create: `Game/Source/KnightsGame/KnightsBoardController.h/.cpp` (이 태스크에서는 빈 화면 + 커서 처리만, Task 6에서 채움)
- Create: `Game/Content/Scenes/KnightsTitle.json`, `Game/Content/Scenes/KnightsGame.json`
- Modify: `Game/Game.vcxproj`, `Game/Game.vcxproj.filters`, `Game/Content/Game.json`

**Interfaces:**
- Consumes: `Canape::ScriptComponent`(`Start/Update/OnDestroy`), `Application::Get().GetSceneManager().OpenScene/UnloadScene`, `Application::Quit()`, `InputManager::SetCursorRequested(bool)`, `UIDemoTextures::MakeRounded()`/`kRoundedSize`/`kRoundedRadius` (`Game/Source/Scripts/UIDemoTextures.h`).
- Produces: RTTR 이름 `"KnightsTitleMenu"`, `"KnightsBoardController"`(프로퍼티 없음), 씬 `KnightsTitle`, `KnightsGame`.

- [ ] **Step 1: 씬 JSON 작성**

각 씬은 `UISample.json` 형식(`version` 1, `name`, `mainCamera` = 카메라 컴포넌트 id, `gameObjects`)으로 오브젝트 2개:
- "Main Camera": `TransformComponent`(원점) + `CameraComponent`(fieldOfView 60, nearClip 0.1, farClip 150).
- "Knights Title" / "Knights Board": `TransformComponent` + 각 스크립트(`{"enabled": true}`).

- [ ] **Step 2: `KnightsTitleMenu` 구현**

`Start`: 둥근 텍스처 생성, 1920×1080 캔버스, 전체 배경 패널, 가운데 제목 "기사단 게임"(약 96px), 그 아래 [게임 시작], [종료] 버튼. [게임 시작] → `OpenScene("KnightsGame")` 성공 시 `UnloadScene(GetOwnerObject()->GetScene())`. [종료] → `Application::Get().Quit()`.
`Start`와 `Update`에서 `SetCursorRequested(true)`, `OnDestroy`에서 `false`(스펙 4.7: 지연 언로드 대응으로 Update에서 매 프레임 다시 건다). `OnDestroy`에서 둥근 텍스처를 해제하는 API가 렌더러에 있으면 해제한다(`UIDemoHud::OnDestroy` 방식 따름).

- [ ] **Step 3: `KnightsBoardController` 뼈대** — 같은 커서 처리, 배경 패널과 "기사단 게임" 텍스트만.

- [ ] **Step 4: 프로젝트 등록** — `Game.vcxproj`에 4개 `.cpp` `ClCompile`, 6개 `.h` `ClInclude`, 두 씬 `None Include="Content\Scenes\..."`. `.filters`에 `Source\KnightsGame` 필터(새 GUID)와 각 파일 배치. `Game.json` → `"startupScene": "KnightsTitle"`.

- [ ] **Step 5: 빌드**

Run (저장소 루트, VS 개발자 PowerShell 또는 vswhere로 찾은 MSBuild): `MSBuild Game\Game.vcxproj /p:Configuration=Debug /p:Platform=x64 /m /v:minimal`
Expected: 오류·경고 0.

- [ ] **Step 6: 실행 확인**

Run: `Binaries\x64\Debug\Game\Game.exe` (실제 출력 경로는 빌드 로그 확인)
Expected: 타이틀 화면과 커서가 보이고, [게임 시작] → 게임 씬 텍스트, 커서 계속 보임. [종료] → 종료. 룰 테스트도 여전히 `ALL PASSED`.

- [ ] **Step 7: Commit** — `git commit -m "feat: 기사단 게임 타이틀·게임 씬 추가"`

---

### Task 6: 보드 화면과 진행 연출 (KnightsBoardController)

**Files:**
- Modify: `Game/Source/KnightsGame/KnightsBoardController.h/.cpp`

**Interfaces:**
- Consumes: Task 1~4의 `Knights::Game`, `Knights::Ai`, `GetAiPersonality`, `PublicView`, `RegionEvent`, `GetWinners`; Task 5 뼈대.
- Produces: 없음(최종 화면).

컨트롤러는 코어 위에 화면 단계를 둔다:

```cpp
enum class Stage { PlayerFlags, AiFlags, FlagReveal, PlayerKnights, AiKnights, KnightReveal, Resolving, RoundEnd, GameOver };
constexpr float kAiTurnDelay = 0.5f;     // AI 한 명 차례 표시
constexpr float kRevealHold = 1.0f;      // 공개 후 잠깐 보여 주기
constexpr float kResolveStep = 1.2f;     // 정산 지역 하나
```

멤버: `Knights::Game m_Game`, `std::array<std::optional<Knights::Ai>, kPlayerCount> m_Ais`(1~4), `Stage m_Stage`, `float m_Timer`, 진행 인덱스(현재 AI, 현재 정산 지역), 플레이어 선택 상태(깃발 선택 집합, 지역별 배치 수). 시드는 `std::random_device` 또는 시간. `Time::DeltaTime()`으로 타이머를 진행한다.

- [ ] **Step 1: 화면 구성** (스펙 4.5 배치도)
  - 상단 바: "라운드 n / 8", 단계 안내 문구.
  - 지역 카드 6장(3×2, `UIButton`): 이름, 범위 또는 "깃발 1개당 기사 +1"/"주사위 → 깃발 수로 나눔", 몬스터 점수(크게), 깃발 칩 줄, 기사 칩 줄(플레이어 색 사각형 + 숫자).
  - 오른쪽 점수판: 5행(색 사각형, 이름, 점수, 기사), 현재 차례 행 강조, 제출 완료 표시 "✓" 대신 "완료" 텍스트(폰트 글리프 확실).
  - 하단 행동 패널: 진행 문구 한 줄, 기사 배치용 내 깃발 지역별 [-] n [+] 행, 남은 기사 수, [확정].
  - 플레이어 색: 나 금색, AI 1~4 빨강·파랑·초록·보라 계열(값은 재량).
- [ ] **Step 2: 매 프레임 갱신** — `m_Game.GetView(kHumanPlayer)`로 모든 텍스트·칩·버튼 상태를 다시 그린다(값이 바뀐 것만 SetText 해도 되고 매번 해도 된다).
- [ ] **Step 3: 단계 진행**
  - `PlayerFlags`: 카드 클릭으로 최대 2개 선택 토글(증원 포함). 정확히 2개일 때만 [확정] 활성. 확정 → `SubmitFlags(0, …)` → `AiFlags`.
  - `AiFlags`: `kAiTurnDelay`마다 AI 1→4 한 명씩 `SubmitFlags(i, m_Ais[i]->ChooseFlags(m_Game.GetView(i)))`, 점수판에 "완료". 끝나면 `FlagReveal`(코어는 이미 KnightPlacement), `kRevealHold` 후 `PlayerKnights`.
  - `PlayerKnights`: 내 깃발이 있는 몬스터 지역마다 [-]/[+]. 합 ≤ 보유. [확정]은 항상 활성(0명 배치 허용, Review Focus 1). 내 몬스터 깃발이 없으면 "보낼 지역이 없습니다" 표시.
  - `AiKnights`: AiFlags와 같은 방식으로 `SubmitKnights`. 끝나면 `KnightReveal` → `kRevealHold` 후 `Resolving`.
  - `Resolving`: `GetResolution()`의 이벤트를 `kResolveStep`마다 하나씩: 해당 카드 강조 + 결과 문구. 문구 예: "트롤 다리 승리! 나 +2, AI 3 +5", "드래곤 동굴 전멸 — AI 1 기사 3명 잃음", "용병 시장 주사위 4 → 2명이 2명씩", "훈련소 — 나, AI 2 기사 +1", 깃발만 있고 기사가 없으면 "고블린 둥지 — 아무도 싸우지 않음". 폐기 점수가 있으면 "(1점 폐기)". 점수판 숫자는 해당 이벤트를 보여 줄 때까지 이전 값으로 보이는 것이 이상적이지만 재량. 화면 아무 곳 클릭(`GetInputManager().GetInput()` 원본 마우스) 시 다음 이벤트로 즉시.
  - `RoundEnd`: 짧게 "라운드 n 종료" 후 `AdvanceRound()`; GameOver면 `GameOver`, 아니면 `PlayerFlags`.
  - `GameOver`: 가운데 패널에 순위표(점수 내림차순, 동점은 기사), 승자 강조("나의 승리!" / "AI 2 승리" / "공동 우승: …"), [다시 하기] → `m_Game.Reset(새 시드)`, AI 재생성, 선택 상태·화면 초기화, `PlayerFlags`. [타이틀로] → `OpenScene("KnightsTitle")` + 현재 씬 `UnloadScene`.
  - 플레이어 입력 단계가 아닐 때 카드·[확정]·[-]/[+]는 `SetInteractable(false)`.
- [ ] **Step 4: 빌드** — Task 5 Step 5와 같음. Expected: 오류·경고 0.
- [ ] **Step 5: 수동 플레이 확인**
  Expected: 타이틀 → 게임 시작 → 8라운드를 끝까지 진행. 확인 목록:
  - 깃발 2개 아니면 [확정] 비활성, AI 깃발은 공개 전까지 안 보임.
  - 기사 0명 배치·증원만 깃발인 라운드에서 막히지 않음.
  - 정산 문구와 점수판 변화가 일치.
  - 종료 화면 → [다시 하기]로 1라운드 0점 5명부터, [타이틀로] 이동 후 다시 시작 가능, 모든 씬에서 커서 보임.
- [ ] **Step 6: 룰 테스트 재실행** — `ALL PASSED`.
- [ ] **Step 7: Commit** — `git commit -m "feat: 기사단 게임 보드 화면과 진행 연출"`
