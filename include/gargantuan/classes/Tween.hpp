#pragma once

#include "gargantuan/datatypes/CFrame.hpp"
#include "gargantuan/datatypes/Color3.hpp"
#include "gargantuan/datatypes/Color4.hpp"
#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/datatypes/Signal.hpp"
#include "gargantuan/datatypes/TweenInfo.hpp"
#include "gargantuan/datatypes/UDim.hpp"
#include "gargantuan/datatypes/UDim2.hpp"
#include "gargantuan/datatypes/Vector2.hpp"
#include "gargantuan/reflection/Enums.hpp"

#include <glm/glm.hpp>
#include <lua.h>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>

namespace gargantuan {
	G_ENUM(
		PlaybackState,

		Begin,
		Delayed,
		Playing,
		Paused,
		Completed,
		Cancelled
	)

	class Tween : public Instance {
	  public:
		static const ClassDefinition DEFINITION;

		using TweenableValue = std::variant<float, CFrame, Color3, Color4, UDim, UDim2, Vector2, glm::vec3>;
		using GoalPropertyMap = std::unordered_map<std::string, TweenableValue>;

		Instance::Pointer Target = nullptr;
		gargantuan::TweenInfo Info = {};
		GoalPropertyMap GoalProperties;
		Enums::PlaybackState PlaybackState = Enums::PlaybackState::Begin;

		Tween(lua_State *L, Instance::Pointer target, gargantuan::TweenInfo info, GoalPropertyMap goalProperties);

		void Play();
		void Pause();
		void Cancel();

		void Step(float deltaTime);
		bool IsFinished() const;

		static std::optional<TweenableValue> ReadTweenableValue(lua_State *L, int idx);

		G_SIGNAL(Completed, Enums::PlaybackState)

	  private:
		lua_State *L = nullptr;
		float Elapsed = 0.0f;
		float DelayElapsed = 0.0f;
		GoalPropertyMap InitialProperties;

		void CaptureInitialProperties();
		void ApplyAlpha(float alpha);
	};
}
