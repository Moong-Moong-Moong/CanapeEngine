#pragma once
#include <cstdint>
#include <functional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace Canape::Debug
{
	enum class MainTag
	{
		Engine,
		DX11Renderer,
		Game,
		Count
	};

	void Init();
	void Shutdown();
	void Update();

	void Write(MainTag tag, std::string_view subTag, std::string message, const char* file, int line);

	void ShowAll();
	void ShowOnly(MainTag tag);
	void SetSubTagFilter(std::string_view subTag);
	void BeginSubTagInput();
	bool IsSubTagInputActive();
	void CycleSubTag();

	uint32_t RegisterKeyCommand(uint16_t virtualKey, std::string_view description, std::function<void()> command);
	void UnregisterKeyCommand(uint32_t commandId);

	template <typename... Args>
	void Log(MainTag tag, std::string_view subTag, const char* file, int line, Args&&... args) 
	{
		std::ostringstream stream; // 문자열 제작을 위한 스트림 객체
		(stream << ... << std::forward<Args>(args)); // Fold Expression, 여러개의 인자에 << 연산자를 반복 적용
		Write(tag, subTag, stream.str(), file, line); 
	}
}

#if defined(_DEBUG)
#define CN_LOG(mainTag, subTag, ...) \
		::Canape::Debug::Log(::Canape::Debug::MainTag::mainTag, subTag, __FILE__, __LINE__, __VA_ARGS__)
#else
#define CN_LOG(mainTag, subTag, ...) ((void)0)
#endif

// 모듈별 매크로
#define CN_LOG_ENGINE(subTag, ...)   CN_LOG(Engine, subTag, __VA_ARGS__)
#define CN_LOG_RENDERER(subTag, ...) CN_LOG(DX11Renderer, subTag, __VA_ARGS__)
#define CN_LOG_GAME(subTag, ...)     CN_LOG(Game, subTag, __VA_ARGS__)

///// 사용예

//#include "Debug/DebugLog.h"

//CN_LOG_ENGINE("Asset", "Loaded mesh '", path, "' (id ", id, ")");
//CN_LOG_RENDERER("Shader", "Compile failed: ", errorMsg);
//CN_LOG_GAME("", "Player spawned at (", x, ", ", y, ")");   // 하위태그 없음
//CN_LOG(Engine, "Input", "Key ", key, " pressed");