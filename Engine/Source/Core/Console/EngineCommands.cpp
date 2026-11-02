#include "Core/Console/EngineCommands.h"
#include "CNDebug/DebugLog.h"
#include "Core/Application.h"

#include <string>

namespace Canape
{
	namespace
	{
		std::string ResolveScenePath(const SceneManager& sceneManager, const std::string& nameOrPath)
		{
			if (nameOrPath.find_first_of("/\\.") != std::string::npos)
			{
				return nameOrPath;
			}
			return sceneManager.GetSceneDirectory() + nameOrPath + ".json";
		}

		void RegisterHelpCommand(CommandRegistry& commands)
		{
			commands.Register("help", "help [command]", "명령어 목록 또는 특정 명령어의 사용법을 출력", 0,
				[&commands](const CommandArgs& args)
				{
					if (args.Has(0))
					{
						const CommandRegistry::Command* command = commands.Find(args.GetString(0));
						if (!command)
						{
							CN_LOG_RUNTIME("Console", "알 수 없는 명령어 : ", args.GetString(0));
							return;
						}
						CN_LOG_RUNTIME("Console", command->usage, " - ", command->description);
						return;
					}

					for (const CommandRegistry::Command& command : commands.GetCommands())
					{
						CN_LOG_RUNTIME("Console", command.usage, " - ", command.description);
					}
				});
		}

		void RegisterSceneCommands(CommandRegistry& commands, Application& application)
		{
			commands.Register("scene.save", "scene.save [name|path]", "활성 씬을 JSON으로 저장 (기본 경로: <씬 폴더>/<씬이름>.json)", 0,
				[&application](const CommandArgs& args)
				{
					SceneManager& sceneManager = application.GetSceneManager();
					Scene* scene = sceneManager.GetActiveScene();
					if (!scene)
					{
						CN_LOG_RUNTIME("Console", "활성 씬이 없음");
						return;
					}

					sceneManager.SaveSceneToFile(*scene, ResolveScenePath(sceneManager, args.GetString(0, scene->GetName())));
				});

			commands.Register("scene.load", "scene.load <name|path>", "JSON 씬을 불러와 활성 씬과 교체", 1,
				[&application](const CommandArgs& args)
				{
					SceneManager& sceneManager = application.GetSceneManager();
					Scene* previous = sceneManager.GetActiveScene();
					if (sceneManager.LoadSceneFromFile(ResolveScenePath(sceneManager, args.GetString(0))) && previous)
					{
						sceneManager.UnloadScene(previous);
					}
				});

			commands.Register("scene.open", "scene.open <name>", "등록된 씬 팩토리로 씬을 생성해 활성 씬과 교체", 1,
				[&application](const CommandArgs& args)
				{
					SceneManager& sceneManager = application.GetSceneManager();
					Scene* previous = sceneManager.GetActiveScene();
					Scene* opened = sceneManager.OpenScene(args.GetString(0));
					if (!opened)
					{
						CN_LOG_RUNTIME("Console", "등록되지 않은 씬 : ", args.GetString(0));
						return;
					}

					if (previous && previous != opened)
					{
						sceneManager.UnloadScene(previous);
					}
				});

			commands.Register("scene.list", "scene.list", "로드된 씬 목록 출력", 0,
				[&application](const CommandArgs&)
				{
					const SceneManager& sceneManager = application.GetSceneManager();
					for (const std::unique_ptr<Scene>& scene : sceneManager.GetScenes())
					{
						const bool active = scene.get() == sceneManager.GetActiveScene();
						CN_LOG_RUNTIME("Console", active ? "* " : "  ", scene->GetName());
					}
				});
		}
	}

	void RegisterEngineCommands(Application& application)
	{
		CommandRegistry& commands = application.GetCommands();
		RegisterHelpCommand(commands);
		RegisterSceneCommands(commands, application);
	}
}
