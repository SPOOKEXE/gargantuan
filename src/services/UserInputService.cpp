#include "gargantuan/services/UserInputService.hpp"
#include "gargantuan/classes/InputObject.hpp"
#include "gargantuan/datatypes/Vector2.hpp"
#include "gargantuan/scripting/Userdata.hpp"
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>
#include <algorithm>
#include <memory>
#include <vector>

namespace gargantuan {
	bool IsMouseButtonType(Enums::UserInputType inputType) {
		return inputType == Enums::UserInputType::MouseButton1 || inputType == Enums::UserInputType::MouseButton2 ||
			   inputType == Enums::UserInputType::MouseButton3;
	}

	const UserInputService::ClassDefinition UserInputService::DEFINITION =
		{.Name = "UserInputService",
		 .Superclass = "Instance",
		 .Constructor = ClassDefinition::WrapConstructor<UserInputService>(),
		 .Properties =
			 {
				 {"MouseBehavior", Property::fromSimple<&UserInputService::MouseBehavior>(true, false)},
				 {"MouseIcon", Property::fromSimple<&UserInputService::MouseIcon>(true, false)},
				 {"MouseIconContent", Property::fromSimple<&UserInputService::MouseIconContent>(true, false)},
				 {"MouseIconEnabled", Property::fromSimple<&UserInputService::MouseIconEnabled>(true, false)},

				 {"KeyboardEnabled", Property::fromSimple<&UserInputService::KeyboardEnabled>(true, false)},
				 {"OnScreenKeyboardVisible",
				  Property::fromSimple<&UserInputService::OnScreenKeyboardVisible>(true, false)},
				 {"OnScreenKeyboardPosition",
				  Property::fromSimple<&UserInputService::OnScreenKeyboardPosition>(true, false)},
				 {"OnScreenKeyboardSize", Property::fromSimple<&UserInputService::OnScreenKeyboardSize>(true, false)},

				 {"TouchEnabled", Property::fromSimple<&UserInputService::TouchEnabled>(true, false)},
				 {"TouchScreenEnabled", Property::fromSimple<&UserInputService::TouchScreenEnabled>(true, false)},

				 {"AccelerometerEnabled", Property::fromSimple<&UserInputService::AccelerometerEnabled>(true, false)},
				 {"GamepadEnabled", Property::fromSimple<&UserInputService::GamepadEnabled>(true, false)},
				 {"GyroscopeEnabled", Property::fromSimple<&UserInputService::GyroscopeEnabled>(true, false)},

				 {"DeviceAccelerationChanged",
				  Property::fromSimple<&UserInputService::DeviceAccelerationChanged>(true, false)},
				 {"DeviceGravityChanged", Property::fromSimple<&UserInputService::DeviceGravityChanged>(true, false)},
				 {"DeviceRotationChanged", Property::fromSimple<&UserInputService::DeviceRotationChanged>(true, false)},

				 {"GamepadConnected", Property::fromSimple<&UserInputService::GamepadConnected>(true, false)},
				 {"GamepadDisconnected", Property::fromSimple<&UserInputService::GamepadDisconnected>(true, false)},

				 {"InputBegan", Property::fromSimple<&UserInputService::InputBegan>(true, false)},
				 {"InputChanged", Property::fromSimple<&UserInputService::InputChanged>(true, false)},
				 {"InputEnded", Property::fromSimple<&UserInputService::InputEnded>(true, false)},

				 {"JumpRequest", Property::fromSimple<&UserInputService::JumpRequest>(true, false)},

				 {"LastInputTypeChanged", Property::fromSimple<&UserInputService::LastInputTypeChanged>(true, false)},
				 {"PointerAction", Property::fromSimple<&UserInputService::PointerAction>(true, false)},

				 {"TouchStarted", Property::fromSimple<&UserInputService::TouchStarted>(true, false)},
				 {"TouchEnded", Property::fromSimple<&UserInputService::TouchEnded>(true, false)},
				 {"TouchDrag", Property::fromSimple<&UserInputService::TouchDrag>(true, false)},
				 {"TouchLongPress", Property::fromSimple<&UserInputService::TouchLongPress>(true, false)},
				 {"TouchMoved", Property::fromSimple<&UserInputService::TouchMoved>(true, false)},
				 {"TouchPan", Property::fromSimple<&UserInputService::TouchPan>(true, false)},
				 {"TouchPinch", Property::fromSimple<&UserInputService::TouchPinch>(true, false)},
				 {"TouchRotate", Property::fromSimple<&UserInputService::TouchRotate>(true, false)},
				 {"TouchTap", Property::fromSimple<&UserInputService::TouchTap>(true, false)},
				 {"TouchTapInWorld", Property::fromSimple<&UserInputService::TouchTapInWorld>(true, false)},

				 {"WindowFocused", Property::fromSimple<&UserInputService::WindowFocused>(true, false)},
				 {"WindowFocusReleased", Property::fromSimple<&UserInputService::WindowFocusReleased>(true, false)},
			 },
		 .Methods = {
			 // G_UD_METHOD(UserInputService, GamepadSupports),
			 // G_UD_METHOD(UserInputService, GetConnectedGamepads),
			 // G_UD_METHOD(UserInputService, GetDeviceAcceleration),
			 // G_UD_METHOD(UserInputService, GetDeviceGravity),
			 // G_UD_METHOD(UserInputService, GetDeviceRotation),
			 // G_UD_METHOD(UserInputService, GetGamepadConnected),
			 // G_UD_METHOD(UserInputService, GetGamepadState),
			 // G_UD_METHOD(UserInputService, GetImageForKeyCode),
			 G_UD_METHOD(UserInputService, GetKeysPressed),
			 G_UD_METHOD(UserInputService, GetLastInputType),
			 G_UD_METHOD(UserInputService, GetMouseButtonsPressed),
			 G_UD_METHOD(UserInputService, GetMouseDelta),
			 G_UD_METHOD(UserInputService, GetMouseLocation),
			 // G_UD_METHOD(UserInputService, GetNavigationGamepads),
			 // G_UD_METHOD(UserInputService, GetStringForKeyCode),
			 // G_UD_METHOD(UserInputService, GetSupportedGamepadKeyCodes),
			 // G_UD_METHOD(UserInputService, IsGamepadButtonDown),
			 G_UD_METHOD(UserInputService, IsKeyDown),
			 G_UD_METHOD(UserInputService, IsMouseButtonPressed),
			 // G_UD_METHOD(UserInputService, IsNavigationGamepad),
			 // G_UD_METHOD(UserInputService, SetNavigationGamepad),
		 }};

	std::shared_ptr<InputObject> InputObjectPool::Acquire() {
		if (!Free.empty()) {
			auto object = Free.back();
			Free.pop_back();
			object->Reset();
			return object;
		}
		return std::make_shared<InputObject>();
	}

	void InputObjectPool::Recycle(const std::shared_ptr<InputObject> &object) {
		// use_count of 1 means the caller's reference is the only one left, so
		// nothing in Luau is still looking at it.
		if (!object || object.use_count() != 1) return;
		Free.push_back(object);
	}

	void UserInputService::SetKeyState(Enums::KeyCode keyCode, bool down) {
		auto index = (size_t)keyCode;
		if (index >= KeyCodeCount) return;

		bool wasDown = KeysDown[index] != 0;
		if (wasDown == down) return;
		KeysDown[index] = down ? 1 : 0;

		if (down) {
			PressedKeys.push_back(keyCode);
		} else if (auto it = std::find(PressedKeys.begin(), PressedKeys.end(), keyCode); it != PressedKeys.end()) {
			*it = PressedKeys.back();
			PressedKeys.pop_back();
		}
	}

	void UserInputService::SetMouseButtonState(Enums::UserInputType button, bool down, glm::vec3 position) {
		auto index = (size_t)button;
		if (index >= MouseButtonCount) return;

		InputState &state = MouseButtons[index];
		bool wasDown = state.State == Enums::UserInputState::Begin;
		if (wasDown == down) return;

		state.State = down ? Enums::UserInputState::Begin : Enums::UserInputState::None;
		state.Position = position;

		if (down) {
			PressedMouseButtons.push_back(button);
		} else if (auto it = std::find(PressedMouseButtons.begin(), PressedMouseButtons.end(), button);
				   it != PressedMouseButtons.end()) {
			*it = PressedMouseButtons.back();
			PressedMouseButtons.pop_back();
		}
	}

	// Materialised from the dense state only when something asks for it. Held
	// keys carry no instance of their own.
	std::shared_ptr<InputObject>
	UserInputService::MakeInputObject(Enums::UserInputType type, Enums::KeyCode keyCode, glm::vec3 position) {
		auto object = std::make_shared<InputObject>();
		object->UserInputType = type;
		object->KeyCode = keyCode;
		object->Position = position;
		object->UserInputState = Enums::UserInputState::Begin;
		return object;
	}

	std::vector<std::shared_ptr<InputObject>> UserInputService::GetKeysPressed() {
		std::vector<std::shared_ptr<InputObject>> result;
		result.reserve(PressedKeys.size());
		for (auto keyCode : PressedKeys) {
			result.push_back(MakeInputObject(Enums::UserInputType::Keyboard, keyCode, glm::vec3(0.0f)));
		}
		return result;
	}

	Enums::UserInputType UserInputService::GetLastInputType() {
		return LastInputType;
	}

	Vector2 UserInputService::GetMouseDelta() {
		return MouseDelta;
	}

	std::vector<std::shared_ptr<InputObject>> UserInputService::GetMouseButtonsPressed() {
		std::vector<std::shared_ptr<InputObject>> result;
		result.reserve(PressedMouseButtons.size());
		for (auto button : PressedMouseButtons) {
			result.push_back(
				MakeInputObject(button, Enums::KeyCode::None, MouseButtons[(size_t)button].Position)
			);
		}
		return result;
	}

	Vector2 UserInputService::GetMouseLocation() {
		return MouseLocation;
	}

	bool UserInputService::IsKeyDown(Enums::KeyCode keyCode) {
		auto index = (size_t)keyCode;
		return index < KeyCodeCount && KeysDown[index] != 0;
	}

	bool UserInputService::IsMouseButtonPressed(Enums::UserInputType mouseType) {
		auto index = (size_t)mouseType;
		return index < MouseButtonCount && MouseButtons[index].State == Enums::UserInputState::Begin;
	}

	void UserInputService::ProcessEvent(SDL_Event &event) {
		if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) return WindowFocused->Fire({});
		if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
			KeysDown.fill(0);
			MouseButtons.fill({});
			PressedKeys.clear();
			PressedMouseButtons.clear();
			return WindowFocusReleased->Fire({});
		};

		auto input = InputObjects.Acquire();
		if (!InputObject::FillFromEvent(*input, event)) {
			InputObjects.Recycle(input);
			return;
		}

		auto inputType = input->UserInputType;
		auto inputState = input->UserInputState;

		if (LastInputType != inputType) {
			LastInputType = inputType;
			LastInputTypeChanged->Fire(inputType);
		}

		if (inputState == Enums::UserInputState::Begin) {
			if (inputType == Enums::UserInputType::Keyboard) {
				SetKeyState(input->KeyCode, true);
				if (input->KeyCode == Enums::KeyCode::Space) JumpRequest->Fire({});
			} else if (IsMouseButtonType(inputType)) {
				SetMouseButtonState(inputType, true, input->Position);
			}
			InputBegan->Fire({input, false});
		} else if (inputState == Enums::UserInputState::Change) {
			if (inputType == Enums::UserInputType::MouseMovement) {
				MouseDelta = Vector2(input->Delta);
				MouseLocation = Vector2(input->Position);
			}
			InputChanged->Fire({input, false});
		} else if (inputState == Enums::UserInputState::End) {
			if (inputType == Enums::UserInputType::Keyboard) {
				SetKeyState(input->KeyCode, false);
			} else if (IsMouseButtonType(inputType)) {
				SetMouseButtonState(inputType, false, input->Position);
			}
			InputEnded->Fire({input, false});
		}

		// Back to the pool unless a script kept hold of it.
		InputObjects.Recycle(input);
	}
}
