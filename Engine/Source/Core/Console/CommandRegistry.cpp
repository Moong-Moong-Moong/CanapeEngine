#include "Core/Console/CommandRegistry.h"
#include "CNDebug/DebugLog.h"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace Canape
{
	namespace
	{
		std::string ToLower(std::string_view text)
		{
			std::string result(text);
			std::transform(result.begin(), result.end(), result.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return result;
		}
	}

	const std::string& CommandArgs::GetString(size_t index) const
	{
		static const std::string empty;
		return index < m_Args.size() ? m_Args[index] : empty;
	}

	std::string CommandArgs::GetString(size_t index, std::string_view fallback) const
	{
		return index < m_Args.size() ? m_Args[index] : std::string(fallback);
	}

	int CommandArgs::GetInt(size_t index, int fallback) const
	{
		if (index >= m_Args.size())
		{
			return fallback;
		}

		const std::string& text = m_Args[index];
		int value = 0;
		const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
		return error == std::errc() && end == text.data() + text.size() ? value : fallback;
	}

	float CommandArgs::GetFloat(size_t index, float fallback) const
	{
		if (index >= m_Args.size())
		{
			return fallback;
		}

		const std::string& text = m_Args[index];
		float value = 0.0f;
		const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
		return error == std::errc() && end == text.data() + text.size() ? value : fallback;
	}

	bool CommandRegistry::Register(std::string_view name, std::string_view usage, std::string_view description, size_t minArgs, Handler handler)
	{
		const std::string key = ToLower(name);
		if (key.empty() || !handler || Find(key))
		{
			CN_LOG_RUNTIME("Console", "명령어 등록 실패 : ", name);
			return false;
		}

		m_Commands.push_back({ key, std::string(usage), std::string(description), minArgs, std::move(handler) });
		std::sort(m_Commands.begin(), m_Commands.end(),
			[](const Command& lhs, const Command& rhs) { return lhs.name < rhs.name; });
		return true;
	}

	void CommandRegistry::Unregister(std::string_view name)
	{
		const std::string key = ToLower(name);
		std::erase_if(m_Commands, [&key](const Command& command) { return command.name == key; });
	}

	const CommandRegistry::Command* CommandRegistry::Find(std::string_view name) const
	{
		const std::string key = ToLower(name);
		const auto found = std::find_if(m_Commands.begin(), m_Commands.end(),
			[&key](const Command& command) { return command.name == key; });
		return found != m_Commands.end() ? &*found : nullptr;
	}

	void CommandRegistry::Submit(std::string_view line)
	{
		m_Pending.emplace_back(line);
	}

	void CommandRegistry::ExecutePending()
	{
		std::vector<std::string> pending;
		pending.swap(m_Pending);
		for (const std::string& line : pending)
		{
			Execute(line);
		}
	}

	void CommandRegistry::Execute(std::string_view line)
	{
		std::vector<std::string> tokens = Tokenize(line);
		if (tokens.empty())
		{
			return;
		}

		CN_LOG_RUNTIME("Console", "> ", line);

		const Command* command = Find(tokens.front());
		if (!command)
		{
			CN_LOG_RUNTIME("Console", "알 수 없는 명령어 : ", tokens.front(), " (help 로 목록 확인)");
			return;
		}

		tokens.erase(tokens.begin());
		if (tokens.size() < command->minArgs)
		{
			CN_LOG_RUNTIME("Console", "사용법 : ", command->usage);
			return;
		}

		const Handler handler = command->handler;
		handler(CommandArgs(std::move(tokens)));
	}

	std::vector<std::string> CommandRegistry::Tokenize(std::string_view line)
	{
		std::vector<std::string> tokens;
		std::string current;
		bool quoted = false;
		bool hasToken = false;

		for (const char c : line)
		{
			if (c == '"')
			{
				quoted = !quoted;
				hasToken = true;
				continue;
			}

			if (!quoted && std::isspace(static_cast<unsigned char>(c)))
			{
				if (hasToken)
				{
					tokens.push_back(std::move(current));
					current.clear();
					hasToken = false;
				}
				continue;
			}

			current.push_back(c);
			hasToken = true;
		}

		if (hasToken)
		{
			tokens.push_back(std::move(current));
		}
		return tokens;
	}
}
