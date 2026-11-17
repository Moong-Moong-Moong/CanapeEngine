#pragma once

#include <memory>
#include <string>

namespace Canape
{
	class JoltRuntime;
	class Scene;
	class Session;

	class SceneSerializer final
	{
	public:
		static bool Save(Scene& scene, const std::string& path);
		static std::unique_ptr<Scene> Load(const std::string& path, JoltRuntime* physicsRuntime = nullptr, Session* simulationSession = nullptr);
	};
}
