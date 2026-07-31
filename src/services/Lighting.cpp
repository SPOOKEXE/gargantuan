#include "gargantuan/services/Lighting.hpp"
#include "gargantuan/datatypes/Color3.hpp"
#include "gargantuan/datatypes/Vector3.hpp"
#include "gargantuan/scripting/Userdata.hpp"

#include <cstdio>
#include <glm/gtc/constants.hpp>
#include <lualib.h>

namespace gargantuan {
	const Lighting::ClassDefinition Lighting::DEFINITION = {
		.Name = "Lighting",
		.Superclass = "Instance",
		.Constructor = ClassDefinition::WrapConstructor<Lighting>(),
		.Properties =
			{
				G_UD_READWRITE_PROP(Lighting, Ambient, Color3),
				G_UD_READWRITE_PROP(Lighting, OutdoorAmbient, Color3),
				G_UD_READWRITE_PROP(Lighting, Brightness, float),
				G_UD_READWRITE_PROP(Lighting, ExposureCompensation, float),
				G_UD_READWRITE_PROP(Lighting, GlobalShadows, bool),
				G_UD_READWRITE_PROP(Lighting, ShadowSoftness, float),
				G_UD_READWRITE_PROP(Lighting, FogColor, Color3),
				G_UD_READWRITE_PROP(Lighting, FogStart, float),
				G_UD_READWRITE_PROP(Lighting, FogEnd, float),
				G_UD_READWRITE_PROP(Lighting, GeographicLatitude, float),
				// Roblox spells these with an underscore, which is not a legal
				// C++ member name suffix here, so they are bound by hand
				{
					"ColorShift_Top",
					{
						[](lua_State *L, Instance *instance) -> int {
							StackValue<Color3>::Push(L, instance->Cast<Lighting>()->ColorShiftTop);
							return 1;
						},
						[](lua_State *L, Instance *instance) -> int {
							instance->Cast<Lighting>()->ColorShiftTop = CheckStackValue<Color3>(L, -1);
							return 0;
						},
						G_UD_REFLECT_TYPE(Color3),
					},
				},
				{
					"ColorShift_Bottom",
					{
						[](lua_State *L, Instance *instance) -> int {
							StackValue<Color3>::Push(L, instance->Cast<Lighting>()->ColorShiftBottom);
							return 1;
						},
						[](lua_State *L, Instance *instance) -> int {
							instance->Cast<Lighting>()->ColorShiftBottom = CheckStackValue<Color3>(L, -1);
							return 0;
						},
						G_UD_REFLECT_TYPE(Color3),
					},
				},
				{
					"ClockTime",
					{
						[](lua_State *L, Instance *instance) -> int {
							StackValue<float>::Push(L, instance->Cast<Lighting>()->GetClockTime());
							return 1;
						},
						[](lua_State *L, Instance *instance) -> int {
							instance->Cast<Lighting>()->SetClockTime(CheckStackValue<float>(L, -1));
							return 0;
						},
						G_UD_REFLECT_TYPE(float),
					},
				},
				{
					"TimeOfDay",
					{
						[](lua_State *L, Instance *instance) -> int {
							StackValue<std::string>::Push(L, instance->Cast<Lighting>()->GetTimeOfDay());
							return 1;
						},
						[](lua_State *L, Instance *instance) -> int {
							instance->Cast<Lighting>()->SetTimeOfDay(CheckStackValue<std::string>(L, -1));
							return 0;
						},
						G_UD_REFLECT_TYPE(std::string),
					},
				},
				G_UD_READONLY_PROP(Lighting, LightingChanged, Signal<bool>::Pointer),
			},
		.Methods = {
			{"GetMinutesAfterMidnight", Method::Wrap<&Lighting::GetMinutesAfterMidnight>()},
			{"SetMinutesAfterMidnight", Method::Wrap<&Lighting::SetMinutesAfterMidnight>()},
			{"GetSunDirection", Method::Wrap<&Lighting::GetSunDirection>()},
			{"GetMoonDirection", Method::Wrap<&Lighting::GetMoonDirection>()},
		}
	};

	float Lighting::GetClockTime() const {
		return ClockTime;
	}

	void Lighting::SetClockTime(float clockTime) {
		// Wrap into [0, 24) so 25 o'clock means one in the morning
		float wrapped = glm::mod(clockTime, 24.0f);
		if (wrapped < 0.0f) {
			wrapped += 24.0f;
		}

		if (wrapped == ClockTime) {
			return;
		}

		ClockTime = wrapped;
		// Roblox passes true when the change was a skybox-affecting one
		LightingChanged->Fire(true);
	}

	float Lighting::GetMinutesAfterMidnight() const {
		return ClockTime * 60.0f;
	}

	void Lighting::SetMinutesAfterMidnight(float minutes) {
		SetClockTime(minutes / 60.0f);
	}

	std::string Lighting::GetTimeOfDay() const {
		int totalSeconds = (int)glm::round(ClockTime * 3600.0f);
		int hours = (totalSeconds / 3600) % 24;
		int minutes = (totalSeconds / 60) % 60;
		int seconds = totalSeconds % 60;

		char buffer[16];
		std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", hours, minutes, seconds);
		return std::string(buffer);
	}

	void Lighting::SetTimeOfDay(std::string timeOfDay) {
		int hours = 0;
		int minutes = 0;
		int seconds = 0;

		// Roblox accepts "HH", "HH:MM" and "HH:MM:SS"
		int parsed = std::sscanf(timeOfDay.c_str(), "%d:%d:%d", &hours, &minutes, &seconds);
		if (parsed < 1) {
			return;
		}

		SetClockTime(hours + minutes / 60.0f + seconds / 3600.0f);
	}

	// The sun rises in the east at 06:00, peaks at noon and sets at 18:00. The
	// latitude tilts that arc away from straight overhead.
	glm::vec3 Lighting::GetSunDirection() const {
		float angle = ((ClockTime - 6.0f) / 24.0f) * glm::two_pi<float>();
		float latitude = glm::radians(GeographicLatitude);

		return glm::normalize(
			glm::vec3(glm::cos(angle), glm::sin(angle) * glm::cos(latitude), -glm::sin(angle) * glm::sin(latitude))
		);
	}

	glm::vec3 Lighting::GetMoonDirection() const {
		return -GetSunDirection();
	}
}
