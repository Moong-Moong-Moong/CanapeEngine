#include "DebugLog.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cwctype>
#include <string>
#include <deque>
#include <vector>

namespace Canape::Debug
{
	namespace
	{
		// 로그 한개의 데이터 구조
		struct Entry
		{
			MainTag tag;
			std::string subTag;
			std::string message;
			std::string file;
			int line;
		};

		constexpr size_t kMaxHistory = 4096;
		constexpr size_t kTagCount = static_cast<size_t>(MainTag::Count);
		constexpr WORD kDefaultColor = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;

		std::deque<Entry> g_History;
		std::array<std::vector<std::string>, kTagCount> g_SubTags; // 상위태그별 하위태그 목록

		// 현재 필터
		bool g_ShowAll = true;
		MainTag g_SelectedTag = MainTag::Engine;
		std::string g_SelectedSubTag; // 비어 있으면 하위태그 전체

		HANDLE g_Out = nullptr;
		HANDLE g_In = nullptr;

		bool g_SubTagInputActive = false;
		std::wstring g_SubTagInput;
		HWND g_ReturnWindow = nullptr;

		struct KeyCommand
		{
			uint32_t id;
			uint16_t virtualKey;
			std::string description;
			std::function<void()> command;
		};

		std::vector<KeyCommand> g_KeyCommands;
		uint32_t g_NextCommandId = 1;

		std::string KeyName(uint16_t virtualKey)
		{
			if (virtualKey >= VK_F1 && virtualKey <= VK_F24)
			{
				return "F" + std::to_string(virtualKey - VK_F1 + 1);
			}
			return std::string(1, static_cast<char>(virtualKey));
		}

		const char* ToString(MainTag tag)
		{
			switch (tag)
			{
			case MainTag::Engine: return "Engine";
			case MainTag::DX11Renderer: return "DX11Renderer";
			case MainTag::Game: return "Game";
			default: return "Unknown";
			}
		}

		WORD ToColor(MainTag tag)
		{
			switch (tag)
			{
			case MainTag::Engine: return FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;      // 하늘색
			case MainTag::DX11Renderer: return  FOREGROUND_GREEN | FOREGROUND_INTENSITY;                 // 초록색
			case MainTag::Game: return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;         // 노란색
			default: return kDefaultColor;
			}
		}

		// "C:\...\Engine\Source\Asset\AssetManager.cpp" -> "Engine\Source\Asset\AssetManager.cpp"
		std::string ShortenPath(const char* file)
		{
			const std::string_view path(file);
			for (const std::string_view root : { std::string_view("Engine\\"), std::string_view("Game\\"), std::string_view("DX11Renderer\\") })
			{
				if (const size_t pos = path.rfind(root); pos != std::string_view::npos)
				{
					return std::string(path.substr(pos));
				}
			}
			return std::string(path);
		}

		bool EqualsIgnoreCase(std::string_view a, std::string_view b)
		{
			return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char lhs, char rhs)
			{
				return std::tolower(static_cast<unsigned char>(lhs)) == std::tolower(static_cast<unsigned char>(rhs));
			});
		}

		std::string ToUtf8(const std::wstring& text)
		{
			if (text.empty())
			{
				return {};
			}
			const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
			std::string result(static_cast<size_t>(size), '\0');
			WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
			return result;
		}

		bool Passes(const Entry& entry)
		{
			if (!g_ShowAll && entry.tag != g_SelectedTag)
			{
				return false;
			}
			return g_SelectedSubTag.empty() || EqualsIgnoreCase(entry.subTag, g_SelectedSubTag);
		}

		// [Engine][Asset] 메세지  (Engine\Source\...\File.cpp:123)
		std::string Format(const Entry& entry)
		{
			std::ostringstream stream;
			stream << '[' << ToString(entry.tag) << ']';
			if (!entry.subTag.empty())
			{
				stream << '[' << entry.subTag << ']';
			}
			const std::string_view message(entry.message);
			const size_t firstLineEnd = message.find('\n');
			stream << ' ' << message.substr(0, firstLineEnd) << "  (" << entry.file << ':' << entry.line << ")\n";
			if (firstLineEnd != std::string_view::npos && firstLineEnd + 1 < message.size())
			{
				stream << message.substr(firstLineEnd + 1);
				if (message.back() != '\n')
				{
					stream << '\n';
				}
			}
			return stream.str();
		}

		void WriteConsole(const std::string& text, WORD color)
		{
			if (!g_Out)
			{
				return;
			}
			DWORD written = 0;
			SetConsoleTextAttribute(g_Out, color);
			WriteConsoleA(g_Out, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
			SetConsoleTextAttribute(g_Out, kDefaultColor);
		}

		void ClearConsole()
		{
			CONSOLE_SCREEN_BUFFER_INFO info{};
			if (!g_Out || !GetConsoleScreenBufferInfo(g_Out, &info))
			{
				return;
			}
			const DWORD size = static_cast<DWORD>(info.dwSize.X) * info.dwSize.Y;
			DWORD written = 0;
			const COORD origin{ 0, 0 };
			FillConsoleOutputCharacterA(g_Out, ' ', size, origin, &written);
			FillConsoleOutputAttribute(g_Out, info.wAttributes, size, origin, &written);
			SetConsoleCursorPosition(g_Out, origin);
		}

		// 필터가 바뀌면 화면을 지우고 기록에서 조건에 맞는 것만 다시 출력
		void RefreshLocked()
		{
			ClearConsole();

			std::ostringstream header;
			header << "===== Filter: " << (g_ShowAll ? "ALL" : ToString(g_SelectedTag))
				<< " / " << (g_SelectedSubTag.empty() ? "*" : g_SelectedSubTag)
				<< "  (F5 All | F6 Engine | F7 DX11Renderer | F8 Game | F11 SubTag Input";
			for (const KeyCommand& keyCommand : g_KeyCommands)
			{
				header << " | " << KeyName(keyCommand.virtualKey) << ' ' << keyCommand.description;
			}
			header << ") =====\n";

			header << '\n';
			WriteConsole(header.str(), kDefaultColor | FOREGROUND_INTENSITY);

			for (const Entry& entry : g_History)
			{
				if (Passes(entry))
				{
					WriteConsole(Format(entry), ToColor(entry.tag));
				}
			}
		}

		void DrawSubTagPrompt()
		{
			CONSOLE_SCREEN_BUFFER_INFO info{};
			if (!g_Out || !GetConsoleScreenBufferInfo(g_Out, &info))
			{
				return;
			}
			const COORD lineStart{ 0, info.dwCursorPosition.Y };
			DWORD written = 0;
			FillConsoleOutputCharacterA(g_Out, ' ', static_cast<DWORD>(info.dwSize.X), lineStart, &written);
			SetConsoleCursorPosition(g_Out, lineStart);
			WriteConsole("SubTag (Enter: apply, Esc: cancel)> " + ToUtf8(g_SubTagInput), FOREGROUND_GREEN | FOREGROUND_INTENSITY);
		}

		void EndSubTagInput(bool apply)
		{
			g_SubTagInputActive = false;
			if (apply)
			{
				g_SelectedSubTag = ToUtf8(g_SubTagInput);
			}
			g_SubTagInput.clear();
			RefreshLocked();

			if (g_ReturnWindow)
			{
				SetForegroundWindow(g_ReturnWindow);
				g_ReturnWindow = nullptr;
			}
		}

		void HandleKey(const KEY_EVENT_RECORD& key)
		{
			if (!g_SubTagInputActive)
			{
				switch (key.wVirtualKeyCode)
				{
				case VK_F5: ShowAll(); break;
				case VK_F6: ShowOnly(MainTag::Engine); break;
				case VK_F7: ShowOnly(MainTag::DX11Renderer); break;
				case VK_F8: ShowOnly(MainTag::Game); break;
				case VK_F11: BeginSubTagInput(); break;
				default:
				{
					const std::vector<KeyCommand> keyCommands = g_KeyCommands;
					for (const KeyCommand& keyCommand : keyCommands)
					{
						if (keyCommand.virtualKey == key.wVirtualKeyCode && keyCommand.command)
						{
							keyCommand.command();
						}
					}
					break;
				}
				}
				return;
			}

			switch (key.wVirtualKeyCode)
			{
			case VK_RETURN:
				EndSubTagInput(true);
				return;
			case VK_ESCAPE:
				EndSubTagInput(false);
				return;
			case VK_BACK:
				if (!g_SubTagInput.empty())
				{
					g_SubTagInput.pop_back();
					DrawSubTagPrompt();
				}
				return;
			default:
				break;
			}

			const wchar_t character = key.uChar.UnicodeChar;
			if (character >= L' ' && !std::iswspace(character))
			{
				g_SubTagInput.push_back(character);
				DrawSubTagPrompt();
			}
		}
	}

	void Init()
	{
		if (!AllocConsole())
		{
			return; // 이미 콘솔이 있으면 그대로 사용
		}
		SetConsoleOutputCP(CP_UTF8);
		SetConsoleTitleW(L"Canape Debug Console");
		g_Out = GetStdHandle(STD_OUTPUT_HANDLE);
		g_In = GetStdHandle(STD_INPUT_HANDLE);
		SetConsoleMode(g_In, ENABLE_EXTENDED_FLAGS);

		// printf / std::cout 도 콘솔로
		FILE* stream = nullptr;
		freopen_s(&stream, "CONOUT$", "w", stdout);
		freopen_s(&stream, "CONOUT$", "w", stderr);

		RefreshLocked();
	}

	void Shutdown()
	{
		g_History.clear();
		g_Out = nullptr;
		g_In = nullptr;
		g_SubTagInputActive = false;
		g_SubTagInput.clear();
		g_ReturnWindow = nullptr;
		g_KeyCommands.clear();
		FreeConsole();
	}

	uint32_t RegisterKeyCommand(uint16_t virtualKey, std::string_view description, std::function<void()> command)
	{
		const uint32_t commandId = g_NextCommandId++;
		g_KeyCommands.push_back({ commandId, virtualKey, std::string(description), std::move(command) });
		if (!g_SubTagInputActive)
		{
			RefreshLocked();
		}
		return commandId;
	}

	void UnregisterKeyCommand(uint32_t commandId)
	{
		g_KeyCommands.erase(std::remove_if(g_KeyCommands.begin(), g_KeyCommands.end(),
			[commandId](const KeyCommand& keyCommand) { return keyCommand.id == commandId; }), g_KeyCommands.end());
	}

	void Update()
	{
		if (!g_In)
		{
			return;
		}

		DWORD count = 0;
		while (GetNumberOfConsoleInputEvents(g_In, &count) && count > 0)
		{
			INPUT_RECORD record{};
			DWORD read = 0;
			if (!ReadConsoleInputW(g_In, &record, 1, &read) || read == 0)
			{
				break;
			}
			if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown)
			{
				HandleKey(record.Event.KeyEvent);
			}
		}
	}

	void Write(MainTag tag, std::string_view subTag, std::string message, const char* file, int line)
	{
		Entry entry{ tag, std::string(subTag), std::move(message), ShortenPath(file), line };
		const std::string text = Format(entry);

		// VS 출력 창에는 필터와 상관없이 항상 출력
		OutputDebugStringA(text.c_str());

		// 하위태그 자동 등록
		std::vector<std::string>& subTags = g_SubTags[static_cast<size_t>(tag)];
		if (!entry.subTag.empty() && std::find(subTags.begin(), subTags.end(), entry.subTag) == subTags.end())
		{
			subTags.push_back(entry.subTag);
		}

		if (!g_SubTagInputActive && Passes(entry))
		{
			WriteConsole(text, ToColor(tag));
		}

		g_History.push_back(std::move(entry));
		if (g_History.size() > kMaxHistory)
		{
			g_History.pop_front();
		}
	}

	void ShowAll()
	{
		if (g_SubTagInputActive)
		{
			return;
		}
		if (!g_ShowAll)
		{
			g_SelectedSubTag.clear();
		}
		g_ShowAll = true;
		RefreshLocked();
	}

	void ShowOnly(MainTag tag)
	{
		if (g_SubTagInputActive)
		{
			return;
		}
		if (g_ShowAll || g_SelectedTag != tag)
		{
			g_SelectedSubTag.clear();
		}
		g_ShowAll = false;
		g_SelectedTag = tag;
		RefreshLocked();
	}

	void SetSubTagFilter(std::string_view subTag)
	{
		g_SelectedSubTag = std::string(subTag);
		RefreshLocked();
	}

	void BeginSubTagInput()
	{
		if (!g_Out || g_SubTagInputActive)
		{
			return;
		}
		g_SubTagInputActive = true;
		g_SubTagInput.clear();

		const HWND consoleWindow = GetConsoleWindow();
		const HWND foregroundWindow = GetForegroundWindow();
		g_ReturnWindow = foregroundWindow != consoleWindow ? foregroundWindow : nullptr;
		if (consoleWindow)
		{
			SetForegroundWindow(consoleWindow);
		}

		FlushConsoleInputBuffer(g_In);
		DrawSubTagPrompt();
	}

	bool IsSubTagInputActive()
	{
		return g_SubTagInputActive;
	}

	void CycleSubTag()
	{
		if (g_ShowAll)
		{
			return;
		}
		// "" -> sub1 -> sub2 -> ... -> ""
		const std::vector<std::string>& subTags = g_SubTags[static_cast<size_t>(g_SelectedTag)];
		if (subTags.empty())
		{
			return;
		}
		if (g_SelectedSubTag.empty())
		{
			g_SelectedSubTag = subTags.front();
		}
		else
		{
			auto it = std::find(subTags.begin(), subTags.end(), g_SelectedSubTag);
			if (it == subTags.end() || ++it == subTags.end())
			{
				g_SelectedSubTag.clear();
			}
			else
			{
				g_SelectedSubTag = *it;
			}
		}
		RefreshLocked();
	}
}