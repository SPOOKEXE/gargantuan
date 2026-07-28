#pragma once

#include "gargantuan/datatypes/CFrame.hpp"
#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/datatypes/Vector2.hpp"
#include "gargantuan/reflection/Enums.hpp"

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
		CFrame CFrame;
		float Pitch = 0.0f, Yaw = -90.0f, Roll;
		// Vertical field of view in degrees.
		float FieldOfView = 70.0f;
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
