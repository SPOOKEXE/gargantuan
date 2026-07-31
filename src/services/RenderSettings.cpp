#include "gargantuan/services/RenderSettings.hpp"
#include "gargantuan/classes/ShaderScript.hpp"
#include "gargantuan/scripting/Userdata.hpp"

#include <glm/common.hpp>
#include <limits>
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
					"BufferCount",
					{
						[](lua_State *L, Instance *instance) -> int {
							StackValue<int>::Push(L, instance->Cast<RenderSettings>()->GetBufferCount());
							return 1;
						},
						[](lua_State *L, Instance *instance) -> int {
							instance->Cast<RenderSettings>()->SetBufferCount(CheckStackValue<int>(L, -1));
							return 0;
						},
						G_UD_REFLECT_TYPE(int),
					},
				},
				{
					"MaxSimulationFPS",
					{
						[](lua_State *L, Instance *instance) -> int {
							StackValue<float>::Push(L, instance->Cast<RenderSettings>()->GetMaxSimulationFPS());
							return 1;
						},
						[](lua_State *L, Instance *instance) -> int {
							instance->Cast<RenderSettings>()->SetMaxSimulationFPS(CheckStackValue<float>(L, -1));
							return 0;
						},
						G_UD_REFLECT_TYPE(float),
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
					"IdleCameraFPS",
					{
						[](lua_State *L, Instance *instance) -> int {
							StackValue<float>::Push(L, instance->Cast<RenderSettings>()->GetIdleCameraFPS());
							return 1;
						},
						[](lua_State *L, Instance *instance) -> int {
							instance->Cast<RenderSettings>()->SetIdleCameraFPS(CheckStackValue<float>(L, -1));
							return 0;
						},
						G_UD_REFLECT_TYPE(float),
					},
				},
				{
					"CameraVisibilityMargin",
					{
						[](lua_State *L, Instance *instance) -> int {
							StackValue<float>::Push(L, instance->Cast<RenderSettings>()->GetCameraVisibilityMargin());
							return 1;
						},
						[](lua_State *L, Instance *instance) -> int {
							instance->Cast<RenderSettings>()->SetCameraVisibilityMargin(CheckStackValue<float>(L, -1));
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

	int RenderSettings::GetBufferCount() const {
		return BufferCount;
	}

	void RenderSettings::SetBufferCount(int buffers) {
		BufferCount = glm::clamp(buffers, MINIMUM_BUFFERS, MAXIMUM_BUFFERS);
	}

	float RenderSettings::GetMaxSimulationFPS() const {
		return MaxSimulationFPS;
	}

	void RenderSettings::SetMaxSimulationFPS(float framesPerSecond) {
		// NaN fails every comparison, so a plain clamp would leave it and the
		// interval below would come out NaN, which no sleep would ever satisfy
		if (!(framesPerSecond == framesPerSecond)) {
			framesPerSecond = DEFAULT_MAX_SIMULATION_FPS;
		}

		// Everything at or below zero reads back as zero, the one uncapped value
		MaxSimulationFPS = framesPerSecond <= 0.0f ? 0.0f : glm::max(framesPerSecond, MINIMUM_SIMULATION_FPS);
	}

	double RenderSettings::GetSimulationInterval() const {
		return MaxSimulationFPS > 0.0f ? 1.0 / (double)MaxSimulationFPS : 0.0;
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

	float RenderSettings::GetIdleCameraFPS() const {
		return IdleCameraFPS;
	}

	void RenderSettings::SetIdleCameraFPS(float framesPerSecond) {
		if (!(framesPerSecond == framesPerSecond)) {
			framesPerSecond = DEFAULT_IDLE_CAMERA_FPS;
		}

		IdleCameraFPS = framesPerSecond < 0.0f ? -1.0f : framesPerSecond;
	}

	double RenderSettings::GetIdleCameraInterval() const {
		if (IdleCameraFPS < 0.0f) {
			return -1.0;
		}
		if (IdleCameraFPS == 0.0f) {
			return std::numeric_limits<double>::infinity();
		}
		return 1.0 / (double)IdleCameraFPS;
	}

	float RenderSettings::GetCameraVisibilityMargin() const {
		return CameraVisibilityMargin;
	}

	void RenderSettings::SetCameraVisibilityMargin(float marginFraction) {
		if (!(marginFraction == marginFraction)) {
			marginFraction = DEFAULT_CAMERA_VISIBILITY_MARGIN;
		}

		CameraVisibilityMargin = glm::clamp(marginFraction, 0.0f, MAXIMUM_CAMERA_VISIBILITY_MARGIN);
	}
} // namespace gargantuan
