#include "Asset/Animation/AnimationClipAsset.h"
#include <stdexcept>

namespace Canape
{
    AnimationClipAssetId AnimationClipAsset::AllocateAssetId()
    {
        // 에셋 생성은 단일 스레드. Clear/reload 시에도 리셋하지 않는다.
        static uint64_t nextId = 1;
        if (nextId == 0) throw std::overflow_error("AnimationClip asset ID space exhausted");
        return AnimationClipAssetId{ nextId++ };
    }
}