#pragma once

#include "gargantuan/datatypes/Instance.hpp"

#include <memory>

namespace gargantuan {
	class ShaderScript;

	// Knobs that belong to the renderer rather than to the place. Roblox keeps
	// its RenderSettings in Studio, where a game cannot reach it; a standalone
	// engine has nowhere else to put them, so this is a service like any other.
	class RenderSettings : public Instance {
	  public:
		static const ClassDefinition DEFINITION;

		// How many frames of GPU work the engine lets pile up before it waits.
		//
		// One frame serialises the CPU behind the GPU: lowest latency, lowest
		// frame rate, and the least memory tied up in work that has not
		// finished. More hides GPU stalls, at the cost of input lag and of
		// everything those submissions hold alive.
		//
		// Zero would mean nothing ever waits, which is what caused the leak
		// this setting exists to bound, so it is not allowed.
		static constexpr int MINIMUM_FRAMES_IN_FLIGHT = 1;
		// Past a handful the latency is worse than the stall it hides
		static constexpr int MAXIMUM_FRAMES_IN_FLIGHT = 8;
		static constexpr int DEFAULT_FRAMES_IN_FLIGHT = 2;

		int GetFramesInFlight() const;
		// Clamped rather than rejected, so a script cannot stall the engine or
		// turn the bound off by writing a silly number
		void SetFramesInFlight(int frames);

		// The ceiling on Camera.FPS. Each camera sets its own rate; this is the
		// one number that caps the lot, so a scene full of cameras cannot ask
		// for more work than the engine is willing to do however they are
		// configured individually.
		//
		// It only ever lowers a camera's rate. A camera asking for less than
		// this keeps what it asked for, and a camera set to 0 (uncapped) or -1
		// (on demand) is not touched by it at all.
		static constexpr float MINIMUM_MAX_CAMERA_FPS = 1.0f;
		static constexpr float DEFAULT_MAX_CAMERA_FPS = 600.0f;

		float GetMaxCameraFPS() const;
		void SetMaxCameraFPS(float framesPerSecond);

		// What Camera.Antialiasing runs. Null is the engine's own edge-softening
		// pass; setting one puts that shader in its place for every camera with
		// Antialiasing on, which is the whole point -- the built-in is one
		// shared pass, so replacing it is one decision rather than one per
		// camera. A camera wanting something of its own already has Shaders.
		//
		// A pass that wants last frame's picture can bind its own camera with
		// SetCameraTexture: a camera reading itself is a cycle, and the engine
		// resolves a cycle by handing over the previous frame's copy. That is
		// the input a reprojecting pass needs, without the engine keeping a
		// history for cameras that never ask for one.
		std::shared_ptr<ShaderScript> GetAntialiasShader() const;
		void SetAntialiasShader(std::shared_ptr<ShaderScript> shader);

	  private:
		int FramesInFlight = DEFAULT_FRAMES_IN_FLIGHT;
		float MaxCameraFPS = DEFAULT_MAX_CAMERA_FPS;
		std::shared_ptr<ShaderScript> AntialiasShader;
	};
} // namespace gargantuan
