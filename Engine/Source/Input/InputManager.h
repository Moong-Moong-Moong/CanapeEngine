#pragma once

#include "Math/IntVector.h"
#include "Math/Vector2.h"

#include <array>
#include <cstddef>
#include <cstdint>

typedef struct tagMSG MSG;

namespace Canape
{
	enum class Key : uint8_t
	{
		None = 0x00,
		Backspace = 0x08,
		Tab = 0x09,
		Enter = 0x0D,
		Shift = 0x10,
		Ctrl = 0x11,
		Alt = 0x12,
		Escape = 0x1B,
		Space = 0x20,
		Left = 0x25,
		Up = 0x26,
		Right = 0x27,
		Down = 0x28,
		Num0 = 0x30, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
		A = 0x41, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
		F1 = 0x70, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12
	};

	enum class MouseButton : uint8_t
	{
		Left,
		Right,
		Middle,
		Count
	};

	enum class GamepadButton : uint8_t
	{
		A,
		B,
		X,
		Y,
		LeftShoulder,
		RightShoulder,
		LeftThumb,
		RightThumb,
		Back,
		Start,
		DPadUp,
		DPadDown,
		DPadLeft,
		DPadRight,
		Count
	};

	struct GamepadState
	{
		bool Connected = false;
		std::array<bool, static_cast<std::size_t>(GamepadButton::Count)> Buttons{};
		Vector2 LeftStick;
		Vector2 RightStick;
		float LeftTrigger = 0.0f;
		float RightTrigger = 0.0f;
	};

	struct Input
	{
		std::array<bool, 256> Keys{};
		std::array<bool, static_cast<std::size_t>(MouseButton::Count)> MouseButtons{};
		IntVector2 MousePosition;
		IntVector2 MouseDelta;
		float MouseWheel = 0.0f;
		GamepadState Gamepad;
	};

	class InputManager
	{
	public:
		void InitInput(void* windowHandle);
		void ObserveInput(const MSG& msg);
		void Update();
		void EndFrame();

		const Input& GetInput() const { return m_Input; }

		bool IsKeyDown(Key key) const { return m_Input.Keys[Index(key)]; }
		bool IsKeyPressed(Key key) const { return m_Input.Keys[Index(key)] && !m_Previous.Keys[Index(key)]; }
		bool IsKeyReleased(Key key) const { return !m_Input.Keys[Index(key)] && m_Previous.Keys[Index(key)]; }

		bool IsMouseDown(MouseButton button) const { return m_Input.MouseButtons[Index(button)]; }
		bool IsMousePressed(MouseButton button) const { return m_Input.MouseButtons[Index(button)] && !m_Previous.MouseButtons[Index(button)]; }
		bool IsMouseReleased(MouseButton button) const { return !m_Input.MouseButtons[Index(button)] && m_Previous.MouseButtons[Index(button)]; }

		bool IsGamepadDown(GamepadButton button) const { return m_Input.Gamepad.Buttons[Index(button)]; }
		bool IsGamepadPressed(GamepadButton button) const { return m_Input.Gamepad.Buttons[Index(button)] && !m_Previous.Gamepad.Buttons[Index(button)]; }
		bool IsGamepadReleased(GamepadButton button) const { return !m_Input.Gamepad.Buttons[Index(button)] && m_Previous.Gamepad.Buttons[Index(button)]; }

		const IntVector2& GetMousePosition() const { return m_Input.MousePosition; }
		const IntVector2& GetMouseDelta() const { return m_Input.MouseDelta; }
		float GetMouseWheel() const { return m_Input.MouseWheel; }
		const GamepadState& GetGamepad() const { return m_Input.Gamepad; }

	private:
		template<typename T>
		static constexpr std::size_t Index(T value) { return static_cast<std::size_t>(value); }

		void SetMouseButton(MouseButton button, bool down, void* windowHandle);
		void PollGamepad();
		void Clear();

		Input m_Input;
		Input m_Previous;
		void* m_WindowHandle = nullptr;
		uint64_t m_NextGamepadPollTime = 0;
	};
}
