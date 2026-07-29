#pragma once

#include "gargantuan/classes/ShaderScript.hpp"
#include "gargantuan/classes/SurfaceShader.hpp"
#include "gargantuan/datatypes/CFrame.hpp"
#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/datatypes/UDim2.hpp"
#include "gargantuan/datatypes/Vector2.hpp"
#include "gargantuan/reflection/Enums.hpp"

#include <memory>

#include <SDL3/SDL.h>
#include <lua.h>
#include <vector>

namespace gargantuan {
	G_ENUM(
		CameraType,
		/// Camera is stationary.
		Fixed,
		/// Camera moves with the subject at a fixed offset and will rotate as the subject rotates.
		Attach,
		/// Camera is stationary but will rotate to keep the subject in the center of the screen.
		Watch,
		/// Camera moves with the subject but does not rotate automatically.
		Track,
		/// Camera moves with the subject and rotates to keep the subject in the center.
		Follow,
		/// Default mode used by Gargantuan.
		Custom,
		/// No default behavior. Used when developers need to script custom behavior.
		Scriptable,
		/// The camera has a fixed Y position, but can be rotated around the player.
		Orbital,
		/// Camera has omnidirectional movement.
		Freecam,
	);

	class Camera : public Instance {
	  public:
		static const ClassDefinition DEFINITION;

		Enums::CameraType CameraType = Enums::CameraType::Freecam;
		// A disabled camera is skipped by the renderer entirely. The camera
		// Workspace.CurrentCamera points at draws to the window; every other
		// enabled camera draws into its own offscreen target instead.
		bool Enabled = true;
		// Draws this camera into the window alongside the others, rather than
		// only into its own offscreen target. Workspace.CurrentCamera behaves
		// as though this were on.
		bool DrawToWindow = false;
		// Where in the window it lands, as a fraction of the window plus a
		// pixel offset. The defaults cover the whole thing.
		UDim2 WindowPosition = UDim2(0.0f, 0, 0.0f, 0);
		UDim2 WindowSize = UDim2(1.0f, 0, 1.0f, 0);
		CFrame CFrame;
		// Degrees. Yaw starts at zero so it agrees with an identity CFrame,
		// which looks down -Z; starting elsewhere made the first mouse movement
		// snap the view round. Roll was left uninitialised.
		float Pitch = 0.0f, Yaw = 0.0f, Roll = 0.0f;
		// Vertical field of view in degrees.
		float FieldOfView = 70.0f;

		// How near and how far a camera can see, in studs. Named rather than
		// written into the projection, because the depth a camera hands to a
		// shader is measured in the same units and the two agreeing is the
		// whole point of handing it over.
		static constexpr float NEAR_PLANE = 0.1f;
		static constexpr float FAR_PLANE = 100000.0f;
		Vector2 ViewportSize = gargantuan::Vector2(0.0f, 0.0f);

		float AccumulatedDeltaX = 0.0f;
		float AccumulatedDeltaY = 0.0f;
		float FreecamSpeed = 10.0f;
		float FreecamSensitivity = 0.2f;

		float GetAspectRatio();
		float GetHorizontalFieldOfView();
		float GetDiagonalFieldOfView();
		void SetHorizontalFieldOfView(float fovy);
		void SetDiagonalFieldOfView(float fovy);
		glm::mat4 GetProjectionMatrix();
		glm::mat4 GetViewMatrix();
		// The projection with this frame's sub-pixel offset folded in, which is
		// what the world is actually drawn through. Identical to
		// GetProjectionMatrix on a camera that is not jittering, and that one
		// stays the truthful description of the frustum: culling, motion
		// vectors and anything else comparing one frame against another want
		// the camera's real shape, not the wobble laid over it.
		glm::mat4 GetJitteredProjectionMatrix();

		// How many sub-pixel offsets the camera cycles through before repeating.
		// Eight is the usual choice: enough positions to cover the pixel
		// convincingly, few enough that a pass blending frames together has seen
		// all of them again before its history has decayed away.
		static constexpr uint32_t JITTER_SEQUENCE_LENGTH = 8;

		// Where inside the pixel this frame was sampled, and where the frame
		// before it was, both in pixels and both zero on a camera with no pass
		// asking for the offset. Bookkeeping, not properties: the renderer moves
		// them on exactly when it redraws the world.
		glm::vec2 Jitter = glm::vec2(0.0f);
		glm::vec2 PreviousJitter = glm::vec2(0.0f);
		uint32_t JitterIndex = 0;
		// Steps to the next offset in the sequence, or clears it back to none.
		// Only ever called from the point the scene is about to be redrawn, so a
		// camera the engine skipped keeps the offset its picture was drawn with.
		void AdvanceJitter(bool jittering);

		// The unjittered view-projection this camera drew with last time, so a
		// motion vector can say where a point on screen was then. Meaningless
		// until the camera has drawn once, which is what the flag is for.
		glm::mat4 PreviousViewProjection = glm::mat4(1.0f);
		bool HasPreviousViewProjection = false;

		// Shaders run over this camera's output in order, each one reading what
		// the previous one produced
		std::vector<std::shared_ptr<ShaderScript>> Shaders;

		// Replaces how objects this camera draws are shaded. Null keeps the
		// engine's own lit-and-shadowed shading.
		std::shared_ptr<SurfaceShader> SurfaceShader = nullptr;

		// Runs the antialias shader after this camera's own chain: the engine's
		// own, which leaves flat areas exactly as they were and only softens
		// edges, or whatever RenderSettings.AntialiasShader was given in its
		// place. A temporal pass put there also decides, through what it binds,
		// whether this camera jitters its projection and keeps the buffers such
		// a pass needs.
		bool Antialiasing = true;

		// How often this camera redraws its own offscreen target. A camera
		// drawing to the window ignores it and draws every frame; this is for
		// the ones feeding textures -- security monitors, mirrors, the picture
		// on a part's surface -- which rarely need to be as current as the view
		// the player is looking at.
		//
		//   -1  on demand: the engine never draws it, only Camera:Render()
		//    0  no limit, every frame, as it behaved before this existed
		//   >0  that many times a second
		//
		// RenderSettings.MaxCameraFPS caps whatever is asked for here, so one
		// camera cannot opt the whole scene into more work than the engine
		// allows. On demand is not free of consequence: until the first
		// Render() the target is blank, so anything sampling it live sees
		// black. A Render() still draws whatever the camera samples.
		static constexpr float ON_DEMAND_FPS = -1.0f;
		static constexpr float DEFAULT_FPS = 24.0f;

		float FPS = DEFAULT_FPS;

		float GetFPS() const;
		void SetFPS(float framesPerSecond);
		// True when nothing but an explicit Camera:Render() draws this camera
		bool IsOnDemand() const;
		// Seconds this camera must wait between redraws, under the engine-wide
		// ceiling. Negative when on demand, zero when uncapped.
		double GetRenderInterval(float maximumFps) const;

		// DistributedGameTime when this camera last redrew its offscreen
		// target, for FPS to throttle against. Negative until it has drawn
		// once, so the first frame is never skipped. Not scriptable; it is
		// bookkeeping, not a property.
		double LastOffscreenDraw = -1.0;

		// The scene and the camera as they were when this camera last drew.
		// Redrawing is skipped while both still match and the cached image is
		// good. Bookkeeping, not properties.
		//
		// LastSceneSignature covers the whole world and LastVisibleSignature
		// only the parts this camera can see. The wide one is the cheap check:
		// when nothing anywhere moved and the camera did not either, the subset
		// cannot have changed and there is no reason to work out what it is.
		// The narrow one is what decides it when something did move, so a part
		// walking about behind the camera costs it nothing.
		uint64_t LastSceneSignature = 0;
		uint64_t LastVisibleSignature = 0;
		uint64_t LastCameraSignature = 0;
		// False until a full draw has filled the target, so the first frame is
		// never answered out of an empty cache
		bool HasDrawn = false;
		// Consecutive frames the scene and the camera have both been identical.
		// Caching costs a copy, so it is not worth paying while the picture is
		// still changing; the engine waits for this to settle first.
		uint32_t StillFrames = 0;

		void AddShader(std::shared_ptr<ShaderScript> shader);
		void RemoveShader(std::shared_ptr<ShaderScript> shader);
		// RemoveShader() drops the last one, RemoveShader(n) the nth counting
		// from one, RemoveShader(shader) that particular one
		static int LRemoveShader(lua_State *L, Instance *instance);
		std::vector<std::shared_ptr<Instance>> ListShaders();
		void ClearShaders();

		void OnEvent(SDL_Window *window, SDL_Event &event);
		void Step(float deltaTime);

		// Renders this camera and yields the calling thread until the GPU
		// hands the pixels back, then resumes it with an EditableImage
		static int LRender(lua_State *L, Instance *instance);

		// Every Camera ever constructed and not yet destroyed. The renderer
		// walks this rather than the instance tree so an offscreen camera does
		// not have to be parented to anything to be useful.
		static const std::vector<Camera *> &GetAllCameras();

		Camera();
		~Camera() override;
	};
} // namespace gargantuan
