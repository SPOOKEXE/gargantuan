#pragma once

#include "gargantuan/datatypes/Color3.hpp"
#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/datatypes/Signal.hpp"

#include <glm/glm.hpp>
#include <string>

namespace gargantuan {
	class Lighting : public Instance {
	  public:
		static const ClassDefinition DEFINITION;

		Color3 Ambient = Color3(0, 0, 0);
		Color3 OutdoorAmbient = Color3(0.5f, 0.5f, 0.5f);
		float Brightness = 3.0f;
		float ExposureCompensation = 0.0f;
		bool GlobalShadows = true;
		float ShadowSoftness = 0.5f;

		Color3 ColorShiftTop = Color3(0, 0, 0);
		Color3 ColorShiftBottom = Color3(0, 0, 0);

		Color3 FogColor = Color3(0.75f, 0.75f, 0.75f);
		float FogStart = 0.0f;
		float FogEnd = 100000.0f;

		// Roblox's default is Sydney's latitude
		float GeographicLatitude = -33.87f;

		// Hours after midnight, in [0, 24)
		float GetClockTime() const;
		void SetClockTime(float clockTime);
		float GetMinutesAfterMidnight() const;
		void SetMinutesAfterMidnight(float minutes);
		// "HH:MM:SS"
		std::string GetTimeOfDay() const;
		void SetTimeOfDay(std::string timeOfDay);

		// Unit vectors pointing from the world towards the sun and the moon
		glm::vec3 GetSunDirection() const;
		glm::vec3 GetMoonDirection() const;

		G_SIGNAL(LightingChanged, bool);

	  private:
		float ClockTime = 14.0f;
	};
}
