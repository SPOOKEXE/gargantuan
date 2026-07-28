#include "gargantuan/services/UserInputService.hpp"
#include "gargantuan/classes/InputObject.hpp"
#include "gargantuan/datatypes/Vector2.hpp"
#include "gargantuan/scripting/Userdata.hpp"
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>
#include <memory>
#include <vector>

namespace gargantuan {
	bool IsMouseButtonType(Enums::UserInputType inputType) {
		return inputType == Enums::UserInputType::MouseButton1 || inputType == Enums::UserInputType::MouseButton2 ||
			   inputType == Enums::UserInputType::MouseButton3;
	}

	const UserInputService::ClassDefinition UserInputService::DEFINITION = {
		.Name = "UserInputService",
		.Superclass = "Instance",
		.Constructor = ClassDefinition::WrapConstructor<UserInputService>(),
		.Properties =
			{
				G_UD_READWRITE_PROP(UserInputService, MouseBehavior, Enums::MouseBehavior),
				G_UD_READWRITE_PROP(UserInputService, MouseIcon, ContentId),
				G_UD_READWRITE_PROP(UserInputService, MouseIconContent, Content),
				G_UD_READWRITE_PROP(UserInputService, MouseIconEnabled, bool),

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

	std::vector<std::shared_ptr<InputObject>> UserInputService::GetKeysPressed() {
		std::vector<std::shared_ptr<InputObject>> result;
		result.reserve(ActiveKeys.size());
		for (const auto &[_, inputObject] : ActiveKeys) {
			result.push_back(inputObject);
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
		result.reserve(ActiveMouseButtons.size());
		for (const auto &[_, inputObject] : ActiveMouseButtons) {
			result.push_back(inputObject);
		}
		return result;
	}

	Vector2 UserInputService::GetMouseLocation() {
		return MouseLocation;
	}

	bool UserInputService::IsKeyDown(Enums::KeyCode keyCode) {
		return ActiveKeys.contains(keyCode);
	}

	bool UserInputService::IsMouseButtonPressed(Enums::UserInputType mouseType) {
		return ActiveMouseButtons.contains(mouseType);
	}

	void UserInputService::ProcessEvent(SDL_Event &event) {
		if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) return WindowFocused->Fire({});
		if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
			ActiveKeys.clear();
			ActiveMouseButtons.clear();
			return WindowFocusReleased->Fire({});
		};

		auto input = InputObject::fromEvent(event);
		if (!input) return;

		auto inputType = input->UserInputType;
		auto inputState = input->UserInputState;

		if (LastInputType != inputType) {
			LastInputType = inputType;
			LastInputTypeChanged->Fire(inputType);
		}

		if (inputState == Enums::UserInputState::Begin) {
			if (inputType == Enums::UserInputType::Keyboard) {
				if (!ActiveKeys.contains(input->KeyCode)) ActiveKeys.emplace(input->KeyCode, input);
				if (input->KeyCode == Enums::KeyCode::Space) JumpRequest->Fire({});
			} else if (IsMouseButtonType(inputType)) {
				if (!ActiveMouseButtons.contains(inputType)) ActiveMouseButtons.emplace(inputType, input);
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
				ActiveKeys.erase(input->KeyCode);
			} else if (IsMouseButtonType(inputType)) {
				ActiveMouseButtons.erase(inputType);
			}
			InputEnded->Fire({input, false});
		}
	}
}
