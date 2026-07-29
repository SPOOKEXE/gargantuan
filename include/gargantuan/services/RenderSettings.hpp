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

		// The rate a camera drops to while nothing is reading it: not drawing
		// to the window, not sampled by a camera that is, and not the surface
		// of a part any of them can see. Four security cameras nobody is
		// looking at are four whole passes over the world a frame, and the
		// picture they produce is thrown away.
		//
		//   >0  that many times a second while idle
		//    0  not at all, until something reads it again
		//   <0  off; every camera keeps its own rate, read or not
		//
		// Like MaxCameraFPS this only ever lowers a rate, so a camera asking
		// for less than it keeps what it asked for, and it never touches one
		// set to -1 (on demand). The first draw is never skipped whatever this
		// says, or a target nothing has read yet would be black at the moment
		// something finally does.
		static constexpr float DEFAULT_IDLE_CAMERA_FPS = 2.0f;

		float GetIdleCameraFPS() const;
		void SetIdleCameraFPS(float framesPerSecond);
		// Seconds an idle camera waits between redraws. Negative when idle
		// throttling is off, infinite when an idle camera never redraws.
		double GetIdleCameraInterval() const;

		// How much wider than what a camera really sees the surface check is,
		// as a fraction of its field of view. A camera feeding a surface starts
		// redrawing at its full rate while that surface is still off the side
		// of the screen, so it has caught up by the time it arrives rather than
		// showing a stale picture for the first few frames it is visible.
		//
		// Zero tests the frustum as it is, which is exact and pops. Wider costs
		// redraws for surfaces that never come into view at all.
		static constexpr float DEFAULT_CAMERA_VISIBILITY_MARGIN = 0.25f;
		static constexpr float MAXIMUM_CAMERA_VISIBILITY_MARGIN = 2.0f;

		float GetCameraVisibilityMargin() const;
		void SetCameraVisibilityMargin(float margin);

		// What Camera.Antialiasing runs. Null is the engine's own edge-softening
		// pass; setting one puts that shader in its place for every camera with
		// Antialiasing on, which is the whole point -- the built-in is one
		// shared pass, so replacing it is one decision rather than one per
		// camera. A camera wanting something of its own already has Shaders.
		//
		// A pass here runs on every camera at once, so it cannot name the camera
		// it wants anything from. Properties:SetRenderTexture is how it asks
		// for one of the reader's own buffers instead:
		//
		//   Enum.RenderTexture.History   that camera's finished picture, last
		//                                frame, which is also what makes the
		//                                engine keep the copy
		//   Enum.RenderTexture.Velocity  where each of its pixels was on that
		//                                frame, which makes the engine draw the
		//                                scene a second time to work them out
		//   Enum.RenderTexture.Depth     how far away each one is, in studs,
		//                                which that same second pass produces
		//   .DepthHistory                and how far away whatever stood there
		//                                was, which is how a pass tells a
		//                                surface that has merely moved from one
		//                                that has just been uncovered
		//
		// and reading builtin.Jitter in the shader is what puts that camera's
		// projection on a sub-pixel wander, so successive frames have something
		// new to average rather than the same sample over again. Nothing is
		// produced for a camera whose passes ask for none of it.
		//
		// Those three are what separate a real temporal pass from a blur, and
		// assets/shaders/taa.frag is the one the engine ships built on them --
		// Enum.PresetShaders.TemporalAntialias, with examples/
		// TemporalAntialiasing.luau showing the swap.
		//
		// A pass wanting some other camera's previous frame can still bind that
		// camera with Properties:SetCameraTexture: a camera reading itself is a
		// cycle, and the engine resolves a cycle by handing over the previous
		// frame's copy.
		std::shared_ptr<ShaderScript> GetAntialiasShader() const;
		void SetAntialiasShader(std::shared_ptr<ShaderScript> shader);

	  private:
		int FramesInFlight = DEFAULT_FRAMES_IN_FLIGHT;
		float MaxCameraFPS = DEFAULT_MAX_CAMERA_FPS;
		float IdleCameraFPS = DEFAULT_IDLE_CAMERA_FPS;
		float CameraVisibilityMargin = DEFAULT_CAMERA_VISIBILITY_MARGIN;
		std::shared_ptr<ShaderScript> AntialiasShader;
	};
} // namespace gargantuan
