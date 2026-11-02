#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Canape
{
	class CommandArgs
	{
	public:
		explicit CommandArgs(std::vector<std::string> args) : m_Args(std::move(args)) {}

		size_t Count() const { return m_Args.size(); }
		bool Has(size_t index) const { return index < m_Args.size(); }
		const std::string& GetString(size_t index) const;
		std::string GetString(size_t index, std::string_view fallback) const;
		int GetInt(size_t index, int fallback) const;
		float GetFloat(size_t index, float fallback) const;

	private:
		std::vector<std::string> m_Args;
	};

	class CommandRegistry final
	{
	public:
		using Handler = std::function<void(const CommandArgs&)>;

		struct Command
		{
			std::string name;
			std::string usage;
			std::string description;
			size_t minArgs = 0;
			Handler handler;
		};

		CommandRegistry() = default;
		CommandRegistry(const CommandRegistry&) = delete;
		CommandRegistry& operator=(const CommandRegistry&) = delete;

		bool Register(std::string_view name, std::string_view usage, std::string_view description, size_t minArgs, Handler handler);
		void Unregister(std::string_view name);

		const Command* Find(std::string_view name) const;
		const std::vector<Command>& GetCommands() const { return m_Commands; }

		void Submit(std::string_view line);
		void ExecutePending();
		void Execute(std::string_view line);

		static std::vector<std::string> Tokenize(std::string_view line);

	private:
		std::vector<Command> m_Commands;
		std::vector<std::string> m_Pending;
	};
}
