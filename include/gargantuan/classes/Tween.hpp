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

		// NOTE: named Target rather than Instance so the member doesn't collide
		// with the Instance base class; it is exposed to Luau as "Instance"
		Instance::Pointer Target = nullptr;
		// NOTE: likewise Info, exposed to Luau as "TweenInfo"
		gargantuan::TweenInfo Info = {};
		GoalPropertyMap GoalProperties;
		Enums::PlaybackState PlaybackState = Enums::PlaybackState::Begin;

		Tween(lua_State *L, Instance::Pointer target, gargantuan::TweenInfo info, GoalPropertyMap goalProperties);

		void Play();
		void Pause();
		void Cancel();

		// Advances the tween and writes the interpolated properties back onto
		// the target. Driven by TweenService, not by scripts.
		void Step(float deltaTime);
		bool IsFinished() const;

		// Reads whichever tweenable datatype sits at `idx`, or nullopt when the
		// value is of a type Tween cannot interpolate.
		static std::optional<TweenableValue> ReadTweenableValue(lua_State *L, int idx);

		G_SIGNAL(Completed, Enums::PlaybackState)

	  private:
		// Property reads and writes go through the Luau stack, so a tween needs
		// a state to marshal values through even when no script is running
		lua_State *L = nullptr;
		float Elapsed = 0.0f;
		float DelayElapsed = 0.0f;
		GoalPropertyMap InitialProperties;

		void CaptureInitialProperties();
		void ApplyAlpha(float alpha);
	};
}
