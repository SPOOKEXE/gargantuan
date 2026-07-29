#include "gargantuan/services/RenderSettings.hpp"
#include "gargantuan/classes/ShaderScript.hpp"
#include "gargantuan/scripting/Userdata.hpp"

#include <glm/common.hpp>
#include <lualib.h>

namespace gargantuan {
	const RenderSettings::ClassDefinition RenderSettings::DEFINITION = {
		.Name = "RenderSettings",
		.Superclass = "Instance",
		.Constructor = ClassDefinition::WrapConstructor<RenderSettings>(),
		.Properties =
			{
				{
					"FramesInFlight",
					{
						[](lua_State *L, Instance *instance) -> int {
							StackValue<int>::Push(L, instance->Cast<RenderSettings>()->GetFramesInFlight());
							return 1;
						},
						[](lua_State *L, Instance *instance) -> int {
							instance->Cast<RenderSettings>()->SetFramesInFlight(CheckStackValue<int>(L, -1));
							return 0;
						},
						G_UD_REFLECT_TYPE(int),
					},
				},
				{
					"MaxCameraFPS",
					{
						[](lua_State *L, Instance *instance) -> int {
							StackValue<float>::Push(L, instance->Cast<RenderSettings>()->GetMaxCameraFPS());
							return 1;
						},
						[](lua_State *L, Instance *instance) -> int {
							instance->Cast<RenderSettings>()->SetMaxCameraFPS(CheckStackValue<float>(L, -1));
							return 0;
						},
						G_UD_REFLECT_TYPE(float),
					},
				},
				{
					"AntialiasShader",
					{
						[](lua_State *L, Instance *instance) -> int {
							StackValue<std::shared_ptr<ShaderScript>>::Push(
								L, instance->Cast<RenderSettings>()->GetAntialiasShader()
							);
							return 1;
						},
						[](lua_State *L, Instance *instance) -> int {
							auto *settings = instance->Cast<RenderSettings>();
							if (lua_isnoneornil(L, -1)) {
								settings->SetAntialiasShader(nullptr);
								return 0;
							}

							settings->SetAntialiasShader(CheckStackValue<std::shared_ptr<ShaderScript>>(L, -1));
							return 0;
						},
						G_UD_REFLECT_TYPE(std::shared_ptr<ShaderScript>),
					},
				},
			},
	};

	int RenderSettings::GetFramesInFlight() const {
		return FramesInFlight;
	}

	void RenderSettings::SetFramesInFlight(int frames) {
		FramesInFlight = glm::clamp(frames, MINIMUM_FRAMES_IN_FLIGHT, MAXIMUM_FRAMES_IN_FLIGHT);
	}

	std::shared_ptr<ShaderScript> RenderSettings::GetAntialiasShader() const {
		return AntialiasShader;
	}

	void RenderSettings::SetAntialiasShader(std::shared_ptr<ShaderScript> shader) {
		AntialiasShader = std::move(shader);
	}

	float RenderSettings::GetMaxCameraFPS() const {
		return MaxCameraFPS;
	}

	void RenderSettings::SetMaxCameraFPS(float framesPerSecond) {
		// NaN fails every comparison, so a plain clamp would pass it through
		// and every camera's interval would come out NaN
		if (!(framesPerSecond == framesPerSecond)) {
			framesPerSecond = DEFAULT_MAX_CAMERA_FPS;
		}

		// A ceiling of zero would stop every camera, which is what Camera.FPS
		// is for; the ceiling only ever lowers a rate, never silences one
		MaxCameraFPS = glm::max(framesPerSecond, MINIMUM_MAX_CAMERA_FPS);
	}
} // namespace gargantuan
