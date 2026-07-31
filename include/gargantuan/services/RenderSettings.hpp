#pragma once

#include "gargantuan/datatypes/Instance.hpp"

#include <memory>

namespace gargantuan {
	class ShaderScript;

	class RenderSettings : public Instance {
	  public:
		static const ClassDefinition DEFINITION;

		static constexpr int MINIMUM_FRAMES_IN_FLIGHT = 1;
		static constexpr int MAXIMUM_FRAMES_IN_FLIGHT = 8;
		static constexpr int DEFAULT_FRAMES_IN_FLIGHT = 2;

		int GetFramesInFlight() const;
		void SetFramesInFlight(int frames);

		// BufferCount controls display latency; FramesInFlight bounds CPU-side retained work.
		static constexpr int MINIMUM_BUFFERS = 1;
		static constexpr int MAXIMUM_BUFFERS = 3;
		static constexpr int N_BUFFERS = 2;

		int GetBufferCount() const;
		void SetBufferCount(int buffers);

		static constexpr float MINIMUM_SIMULATION_FPS = 1.0f;
		static constexpr float DEFAULT_MAX_SIMULATION_FPS = 240.0f;

		float GetMaxSimulationFPS() const;
		// Non-positive is uncapped, which busy-loops: nothing else paces the loop.
		void SetMaxSimulationFPS(float framesPerSecond);
		double GetSimulationInterval() const;

		static constexpr float MINIMUM_MAX_CAMERA_FPS = 1.0f;
		static constexpr float DEFAULT_MAX_CAMERA_FPS = 600.0f;

		float GetMaxCameraFPS() const;
		void SetMaxCameraFPS(float framesPerSecond);

		static constexpr float DEFAULT_IDLE_CAMERA_FPS = 2.0f;

		float GetIdleCameraFPS() const;
		void SetIdleCameraFPS(float framesPerSecond);
		double GetIdleCameraInterval() const;

		static constexpr float DEFAULT_CAMERA_VISIBILITY_MARGIN = 0.25f;
		static constexpr float MAXIMUM_CAMERA_VISIBILITY_MARGIN = 2.0f;

		float GetCameraVisibilityMargin() const;
		void SetCameraVisibilityMargin(float marginFraction);

		std::shared_ptr<ShaderScript> GetAntialiasShader() const;
		void SetAntialiasShader(std::shared_ptr<ShaderScript> shader);

	  private:
		int FramesInFlight = DEFAULT_FRAMES_IN_FLIGHT;
		int BufferCount = N_BUFFERS;
		float MaxSimulationFPS = DEFAULT_MAX_SIMULATION_FPS;
		float MaxCameraFPS = DEFAULT_MAX_CAMERA_FPS;
		float IdleCameraFPS = DEFAULT_IDLE_CAMERA_FPS;
		float CameraVisibilityMargin = DEFAULT_CAMERA_VISIBILITY_MARGIN;
		std::shared_ptr<ShaderScript> AntialiasShader;
	};
}
