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

		// Shaders run over this camera's output in order, each one reading what
		// the previous one produced
		std::vector<std::shared_ptr<ShaderScript>> Shaders;

		// Replaces how objects this camera draws are shaded. Null keeps the
		// engine's own lit-and-shadowed shading.
		std::shared_ptr<SurfaceShader> SurfaceShader = nullptr;

		// Runs the built-in antialias shader after this camera's own chain.
		// It leaves flat areas exactly as they were and only softens edges.
		bool Antialiasing = true;

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
