# Canape 좌표계 · 단위 · 수학 규약

## 좌표계
| 항목 | 규약 |
|---|---|
| 손 좌표계 | **왼손 좌표계** |
| Right | **+X** |
| Up | **+Y** |
| Forward | **+Z** (화면 안쪽) |
| 외적 | `Cross(Right, Up) = Forward` |

```
        +Y (Up)
         |
         |   +Z (Forward)
         |  /
         | /
         +------ +X (Right)
```

## 단위
| 항목 | 단위 |
|---|---|
| 거리 | **1 unit = 1 m** (`Math::Meter`, `Math::Centimeter`) |
| 시간 | 초 (s) |
| 질량 | kg |
| 중력 | `Math::Gravity` = 9.81 m/s² (−Y 방향) |
| 각도 (함수 인자) | **라디안** |
| 각도 (`Rotator`) | **도(degree)** |

## 회전 방향
양(+)의 각도 = 회전축의 + 방향에서 원점을 바라볼 때 **시계 방향** (왼손 법칙).

| 회전 | 축 | 양수일 때 |
|---|---|---|
| Yaw | +Y | 오른쪽으로 돈다 (Forward → Right) |
| Pitch | +X | 아래를 본다 (Forward → Down) |
| Roll | +Z | Right 축이 위로 올라간다 |

- 카메라 마우스 입력처럼 "위로 올리면 위를 본다"가 필요하면 게임 코드에서 Pitch 부호를 뒤집는다.
- `Vector3::SignedAngle(Forward, Right, Up)` = +90°

### Rotator (오일러 각)
- `Rotator{ Pitch, Yaw, Roll }`, 단위는 도.
- 적용 순서: **Roll(Z) → Pitch(X) → Yaw(Y)**
- `ToQuaternion() = Yaw * Pitch * Roll`

## 쿼터니언
- `(X, Y, Z, W)`, 기본값은 Identity `(0, 0, 0, 1)`.
- `a * b` = **b를 먼저 적용한 뒤 a를 적용**한다.
- `q * v` 또는 `q.Rotate(v)` = 벡터 회전
- `GetForward()` / `GetRight()` / `GetUp()` = 회전된 로컬 축

## 행렬
| 항목 | 규약 |
|---|---|
| 벡터 | **행 벡터** (`v' = v * M`) |
| 저장 | **Row-major** (`M[row][column]`) |
| 결합 순서 | 왼쪽부터 적용: `World = Scale * Rotation * Translation` |
| 계층 | `World = Local * Parent` |
| WVP | `World * View * Projection` |
| 축 위치 | Row 0 = Right, Row 1 = Up, Row 2 = Forward, Row 3 = Translation |

- `TransformPoint`는 w = 1(이동 포함), `TransformVector`는 w = 0(이동 제외)이다.
- `ProjectPoint`는 w로 나누는 투영 변환이다.

### 투영
| 함수 | 깊이 범위 |
|---|---|
| `Perspective` | near = 0, far = 1 (D3D 규약) |
| `PerspectiveReversedZ` | near = 1, far = 0 (원거리 정밀도용) |
| `Orthographic` / `OrthographicOffCenter` | near = 0, far = 1 |

- FOV는 **세로 FOV, 라디안**이다.

### HLSL 연동
- 셰이더 컴파일 시 `D3DCOMPILE_PACK_MATRIX_ROW_MAJOR`(`/Zpr`)를 사용하고 `mul(v, M)` 형태로 곱한다.
- 그러면 CPU 행렬을 **전치 없이 그대로** 상수 버퍼에 올릴 수 있다.

## Transform
- `Position`, `Rotation(Quaternion)`, `Scale`
- `child * parent` = 월드 Transform
- 비균등 스케일 + 회전이 섞인 부모의 `Inverse()`는 근사값이다(균등 스케일에서는 정확하다).

## 삼각형 · 평면
| 항목 | 규약 |
|---|---|
| 앞면 | **시계 방향(CW)** 감김 (D3D 기본값) |
| 평면 노멀 | `Plane::FromPoints(a, b, c)` = `Cross(b - a, c - a)`. 시계 방향으로 보이는 쪽을 향한다. |
| 평면 식 | `Dot(Normal, p) + D = 0`, 양수 = 앞쪽 |
| Frustum | 평면 노멀이 안쪽을 향한다 |

## 색상
- `Color` = **선형(Linear)** float RGBA. 셰이딩 계산과 상수 버퍼에 사용한다.
- `Color32` = **sRGB** 8bit RGBA. 텍스처, UI, 헥스 값에 사용한다.
- 변환: `Color::FromSRGB(Color32)`, `Color::ToSRGB()`

## 파일 구성
| 파일 | 내용 |
|---|---|
| `Math/MathUtility.h` | 상수, Clamp / Lerp / SmoothStep / Wrap / SmoothDamp / InterpTo 등 |
| `Math/Vector2.h`, `Vector3.h`, `Vector4.h` | 실수 벡터 |
| `Math/IntVector.h` | 정수 벡터 (해상도, 그리드) |
| `Math/Quaternion.h` | 쿼터니언, Slerp, LookRotation, FromToRotation |
| `Math/Rotator.h` | 오일러 각(도) |
| `Math/Matrix4.h` | 4x4 행렬, 역행렬, 분해, 뷰 · 투영 |
| `Math/Transform.h` | 위치 · 회전 · 스케일 |
| `Math/Geometry.h` | Ray, Plane, Sphere, AABB, Frustum |
| `Math/Intersection.h` | 레이캐스트, 겹침 판정, 최근접점 |
| `Math/Color.h` | Color(선형), Color32(sRGB), HSV |
| `Math/Random.h` | PCG32 난수, 단위 구 · 원 샘플링, 랜덤 회전 |
| `Math/Math.h` | 전체 include |
