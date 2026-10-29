#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>
#include <vector>

#include "DX11ResourceId.h"

namespace Canape
{
    struct DX11SubMesh
    {
        uint32_t startIndex = 0;   // 인덱스 버퍼에서 이 구간이 시작하는 위치
        uint32_t indexCount = 0;   // 이 구간의 인덱스 개수 (삼각형이므로 3의 배수 )
        int32_t baseVertex = 0;    // 여러 메쉬들을 한 메쉬 에셋에 저장하고 싶을때, 버텍스 버퍼를 공유하고 싶을 때 시작점을 등록
        uint32_t materialSlot = 0; // 사용할 머터리얼 번호
    };

    struct DX11MeshResource
    {
        MeshResourceId Id;

        Microsoft::WRL::ComPtr<ID3D11Buffer> VertexBuffer; // 정점 데이터
        Microsoft::WRL::ComPtr<ID3D11Buffer> IndexBuffer; // 인덱스 데이터

        uint32_t VertexStride = 0; // 정점 하나의 크기
        DXGI_FORMAT IndexFormat = DXGI_FORMAT_R32_UINT;

        std::vector<DX11SubMesh> SubMeshes; //서브 메시마다 그리는 범위와 사용 머터리얼 정의
    };
}
