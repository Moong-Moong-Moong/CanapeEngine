#pragma once

#include <memory>
#include <string>

namespace Canape
{
	class AnimationClipAsset;

	class AnimationClipSerializer final
	{
	public:
		static bool Save(const AnimationClipAsset& clip, const std::string& path);
		static std::unique_ptr<AnimationClipAsset> Load(const std::string& path);
	};
}
