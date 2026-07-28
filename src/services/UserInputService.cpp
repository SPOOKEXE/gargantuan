#include "gargantuan/services/UserInputService.hpp"
#include "gargantuan/classes/InputObject.hpp"
#include "gargantuan/datatypes/Vector2.hpp"
#include "gargantuan/scripting/Userdata.hpp"
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>
#include <memory>
#include <variant>
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

				G_UD_READONLY_PROP(UserInputService, KeyboardEnabled, bool),
				G_UD_READONLY_PROP(UserInputService, OnScreenKeyboardVisible, bool),
				G_UD_READONLY_PROP(UserInputService, OnScreenKeyboardPosition, Vector2),
				G_UD_READONLY_PROP(UserInputService, OnScreenKeyboardSize, Vector2),

				G_UD_READONLY_PROP(UserInputService, TouchEnabled, bool),
				G_UD_READONLY_PROP(UserInputService, TouchScreenEnabled, bool),

				G_UD_READONLY_PROP(UserInputService, AccelerometerEnabled, bool),
				G_UD_READONLY_PROP(UserInputService, GamepadEnabled, bool),
				G_UD_READONLY_PROP(UserInputService, GyroscopeEnabled, bool),

				G_UD_READONLY_PROP(
					UserInputService,

					DeviceAccelerationChanged,
					Signal<std::shared_ptr<InputObject>>::Pointer
				),
				G_UD_READONLY_PROP(
					UserInputService,

					DeviceGravityChanged,
					Signal<std::shared_ptr<InputObject>>::Pointer
				),
				G_UD_READONLY_PROP(
					UserInputService,

					DeviceRotationChanged,
					Signal<DeviceRotationChangedSignalType>::Pointer
				),

				G_UD_READONLY_PROP(UserInputService, GamepadConnected, Signal<std::shared_ptr<InputObject>>::Pointer),
				G_UD_READONLY_PROP(
					UserInputService, GamepadDisconnected, Signal<std::shared_ptr<InputObject>>::Pointer
				),

				G_UD_READONLY_PROP(UserInputService, InputBegan, Signal<InputSignalType>::Pointer),
				G_UD_READONLY_PROP(UserInputService, InputChanged, Signal<InputSignalType>::Pointer),
				G_UD_READONLY_PROP(UserInputService, InputEnded, Signal<InputSignalType>::Pointer),

				G_UD_READONLY_PROP(UserInputService, JumpRequest, Signal<std::monostate>::Pointer),

				G_UD_READONLY_PROP(UserInputService, LastInputTypeChanged, Signal<Enums::UserInputType>::Pointer),
				G_UD_READONLY_PROP(UserInputService, PointerAction, Signal<PointerActionSignalType>::Pointer),

				G_UD_READONLY_PROP(UserInputService, TouchStarted, Signal<InputSignalType>::Pointer),
				G_UD_READONLY_PROP(UserInputService, TouchEnded, Signal<InputSignalType>::Pointer),
				G_UD_READONLY_PROP(UserInputService, TouchDrag, Signal<TouchDragSignalType>::Pointer),
				G_UD_READONLY_PROP(UserInputService, TouchLongPress, Signal<TouchLongPressSignalType>::Pointer),
				G_UD_READONLY_PROP(UserInputService, TouchMoved, Signal<InputSignalType>::Pointer),
				G_UD_READONLY_PROP(UserInputService, TouchPan, Signal<TouchPanSignalType>::Pointer),
				G_UD_READONLY_PROP(UserInputService, TouchPinch, Signal<TouchSwipeSignalType>::Pointer),
				G_UD_READONLY_PROP(UserInputService, TouchRotate, Signal<TouchSwipeSignalType>::Pointer),
				G_UD_READONLY_PROP(UserInputService, TouchTap, Signal<TouchTapSignalType>::Pointer),
				G_UD_READONLY_PROP(UserInputService, TouchTapInWorld, Signal<TouchTapInWorldSignalType>::Pointer),

				G_UD_READONLY_PROP(UserInputService, WindowFocused, Signal<std::monostate>::Pointer),
				G_UD_READONLY_PROP(UserInputService, WindowFocusReleased, Signal<std::monostate>::Pointer),
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
		}
	};

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
