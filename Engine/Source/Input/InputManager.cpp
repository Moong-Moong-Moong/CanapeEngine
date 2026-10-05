#include "Input/InputManager.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <windowsx.h>
#include <Xinput.h>

#pragma comment(lib, "xinput.lib")

namespace Canape
{
	namespace
	{
		constexpr uint64_t kGamepadRetryIntervalMs = 1000;

		Vector2 NormalizeStick(SHORT x, SHORT y, SHORT deadZone)
		{
			const Vector2 value{
				Math::Clamp(x / 32767.0f, -1.0f, 1.0f),
				Math::Clamp(y / 32767.0f, -1.0f, 1.0f)
			};

			const float length = value.Length();
			const float threshold = deadZone / 32767.0f;
			if (length <= threshold)
			{
				return Vector2::Zero;
			}

			const float scaled = (Math::Min(length, 1.0f) - threshold) / (1.0f - threshold);
			return value * (scaled / length);
		}

		float NormalizeTrigger(BYTE value)
		{
			if (value <= XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
			{
				return 0.0f;
			}
			return (value - XINPUT_GAMEPAD_TRIGGER_THRESHOLD) / static_cast<float>(255 - XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
		}
	}

	void InputManager::InitInput(void* windowHandle)
	{
		m_WindowHandle = windowHandle;
		m_Input = {};
		m_Previous = {};
		m_NextGamepadPollTime = 0;

		RAWINPUTDEVICE device{};
		device.usUsagePage = 0x01;
		device.usUsage = 0x02;
		device.dwFlags = 0;
		device.hwndTarget = static_cast<HWND>(windowHandle);
		RegisterRawInputDevices(&device, 1, sizeof(device));
	}

	void InputManager::ObserveInput(const MSG& msg)
	{
		switch (msg.message)
		{
		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
			m_Input.Keys[msg.wParam & 0xFF] = true;
			break;

		case WM_KEYUP:
		case WM_SYSKEYUP:
			m_Input.Keys[msg.wParam & 0xFF] = false;
			break;

		case WM_LBUTTONDOWN: SetMouseButton(MouseButton::Left, true, msg.hwnd); break;
		case WM_LBUTTONUP: SetMouseButton(MouseButton::Left, false, msg.hwnd); break;
		case WM_RBUTTONDOWN: SetMouseButton(MouseButton::Right, true, msg.hwnd); break;
		case WM_RBUTTONUP: SetMouseButton(MouseButton::Right, false, msg.hwnd); break;
		case WM_MBUTTONDOWN: SetMouseButton(MouseButton::Middle, true, msg.hwnd); break;
		case WM_MBUTTONUP: SetMouseButton(MouseButton::Middle, false, msg.hwnd); break;

		case WM_MOUSEMOVE:
			m_Input.MousePosition = { GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam) };
			break;

		case WM_MOUSEWHEEL:
			m_Input.MouseWheel += static_cast<float>(GET_WHEEL_DELTA_WPARAM(msg.wParam)) / WHEEL_DELTA;
			break;

		case WM_INPUT:
		{
			RAWINPUT raw{};
			UINT size = sizeof(raw);
			if (GetRawInputData(reinterpret_cast<HRAWINPUT>(msg.lParam), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1))
			{
				break;
			}

			if (raw.header.dwType == RIM_TYPEMOUSE && !(raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE))
			{
				m_Input.MouseDelta += { raw.data.mouse.lLastX, raw.data.mouse.lLastY };
			}
			break;
		}
		}
	}

	void InputManager::Update()
	{
		PollGamepad();

		if (GetFocus() != static_cast<HWND>(m_WindowHandle))
		{
			Clear();
		}
	}

	void InputManager::EndFrame()
	{
		m_Previous = m_Input;
		m_Input.MouseDelta = IntVector2::Zero;
		m_Input.MouseWheel = 0.0f;
	}

	void InputManager::SetMouseButton(MouseButton button, bool down, void* windowHandle)
	{
		m_Input.MouseButtons[Index(button)] = down;

		if (down)
		{
			SetCapture(static_cast<HWND>(windowHandle));
			return;
		}

		for (bool pressed : m_Input.MouseButtons)
		{
			if (pressed)
			{
				return;
			}
		}
		ReleaseCapture();
	}

	void InputManager::PollGamepad()
	{
		GamepadState& gamepad = m_Input.Gamepad;
		const uint64_t now = GetTickCount64();

		if (!gamepad.Connected && now < m_NextGamepadPollTime)
		{
			return;
		}

		XINPUT_STATE state{};
		if (XInputGetState(0, &state) != ERROR_SUCCESS)
		{
			gamepad = {};
			m_NextGamepadPollTime = now + kGamepadRetryIntervalMs;
			return;
		}

		static constexpr WORD kButtonMasks[] = {
			XINPUT_GAMEPAD_A,
			XINPUT_GAMEPAD_B,
			XINPUT_GAMEPAD_X,
			XINPUT_GAMEPAD_Y,
			XINPUT_GAMEPAD_LEFT_SHOULDER,
			XINPUT_GAMEPAD_RIGHT_SHOULDER,
			XINPUT_GAMEPAD_LEFT_THUMB,
			XINPUT_GAMEPAD_RIGHT_THUMB,
			XINPUT_GAMEPAD_BACK,
			XINPUT_GAMEPAD_START,
			XINPUT_GAMEPAD_DPAD_UP,
			XINPUT_GAMEPAD_DPAD_DOWN,
			XINPUT_GAMEPAD_DPAD_LEFT,
			XINPUT_GAMEPAD_DPAD_RIGHT
		};

		const XINPUT_GAMEPAD& pad = state.Gamepad;
		gamepad.Connected = true;
		for (std::size_t i = 0; i < gamepad.Buttons.size(); ++i)
		{
			gamepad.Buttons[i] = (pad.wButtons & kButtonMasks[i]) != 0;
		}

		gamepad.LeftStick = NormalizeStick(pad.sThumbLX, pad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
		gamepad.RightStick = NormalizeStick(pad.sThumbRX, pad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
		gamepad.LeftTrigger = NormalizeTrigger(pad.bLeftTrigger);
		gamepad.RightTrigger = NormalizeTrigger(pad.bRightTrigger);
	}

	void InputManager::Clear()
	{
		m_Input.Keys = {};
		m_Input.MouseButtons = {};
		m_Input.MouseDelta = IntVector2::Zero;
		m_Input.MouseWheel = 0.0f;

		const bool connected = m_Input.Gamepad.Connected;
		m_Input.Gamepad = {};
		m_Input.Gamepad.Connected = connected;
	}
}
