#pragma once

#include "Math/Matrix4.h"
#include "Math/Vector2.h"
#include "Math/Vector3.h"

#include <algorithm>
#include <cmath>

namespace Canape
{
	//***** 월드 공간 캔버스 계산(렌더러·씬과 무관한 순수 함수)

	//*** 캔버스 픽셀(좌상단 원점, +Y 아래) → 오브젝트 로컬 좌표(+X 오른쪽, +Y 위, Z=0 평면).
	//*** pivot(0~1)이 로컬 원점에 온다. 앞면은 로컬 -Z 쪽에서 +Z 방향으로 볼 때 글자가 바로 읽힌다.
	inline Matrix4 MakeCanvasToLocal(const Vector2& canvasSize, const Vector2& pivot, float unitsPerPixel)
	{
		Matrix4 m;
		m.M[0][0] = unitsPerPixel;
		m.M[1][1] = -unitsPerPixel;
		m.M[3][0] = -canvasSize.X * pivot.X * unitsPerPixel;
		m.M[3][1] = canvasSize.Y * pivot.Y * unitsPerPixel;
		return m;
	}

	//*** 캔버스 픽셀 하나가 화면에서 몇 픽셀로 보이는지를 2의 거듭제곱 계단(1/4~4)으로 올려 맞춘다.
	//*** 올림이라 화면 밀도보다 낮게 굽지 않으므로 흐려지지 않고(축소만 일어남),
	//*** 계단이라 카메라가 움직여도 아틀라스에 글자 크기가 마구 늘지 않는다.
	//*** canvasToWorld의 첫 행 길이 = 캔버스 1픽셀의 월드 길이(오브젝트 스케일 포함).
	inline float ComputeRasterFactor(const Matrix4& canvasToWorld, const Vector3& worldPoint, const Matrix4& view,
		const Matrix4& projection, float targetHeight)
	{
		const float viewDepth = view.TransformPoint(worldPoint).Z;
		if (viewDepth <= 1e-3f || targetHeight <= 0.0f)
		{
			return 1.0f;
		}
		const float pixelWorld = std::hypot(canvasToWorld.M[0][0], canvasToWorld.M[0][1], canvasToWorld.M[0][2]);
		const float screenPerWorld = projection.M[1][1] * targetHeight * 0.5f / viewDepth;
		const float density = pixelWorld * screenPerWorld;
		if (!(density > 0.0f))
		{
			return 1.0f;
		}
		// 정확한 2의 거듭제곱이 부동소수 오차로 한 계단 올라가지 않게 조금 깎고 올린다.
		return std::clamp(std::exp2(std::ceil(std::log2(density) - 1e-3f)), 0.25f, 4.0f);
	}
}
