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
		Fixed,
		Attach,
		Watch,
		Track,
		Follow,
		Custom,
		Scriptable,
		Orbital,
		Freecam,
	);

	class Camera : public Instance {
	  public:
		static const ClassDefinition DEFINITION;

		Enums::CameraType CameraType = Enums::CameraType::Freecam;
		bool Enabled = true;
		bool DrawToWindow = false;
		UDim2 WindowPosition = UDim2(0.0f, 0, 0.0f, 0);
		UDim2 WindowSize = UDim2(1.0f, 0, 1.0f, 0);
		CFrame CFrame;
		float Pitch = 0.0f, Yaw = 0.0f, Roll = 0.0f;
		float FieldOfView = 70.0f;

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
		glm::mat4 GetJitteredProjectionMatrix();

		static constexpr uint32_t JITTER_SEQUENCE_LENGTH = 8;

		glm::vec2 Jitter = glm::vec2(0.0f);
		glm::vec2 PreviousJitter = glm::vec2(0.0f);
		uint32_t JitterIndex = 0;
		void AdvanceJitter(bool jittering);

		glm::mat4 PreviousViewProjection = glm::mat4(1.0f);
		bool HasPreviousViewProjection = false;

		std::vector<std::shared_ptr<ShaderScript>> Shaders;

		std::shared_ptr<SurfaceShader> SurfaceShader = nullptr;

		bool Antialiasing = true;

		static constexpr float ON_DEMAND_FPS = -1.0f;
		static constexpr float DEFAULT_FPS = 24.0f;

		float FPS = DEFAULT_FPS;

		float GetFPS() const;
		void SetFPS(float framesPerSecond);
		bool IsOnDemand() const;
		double GetRenderInterval(float maximumFps) const;

		double LastOffscreenDraw = -1.0;

		uint64_t LastSceneSignature = 0;
		uint64_t LastVisiblePartsHash = 0;
		bool LastVisiblePartsHashValid = false;
		uint64_t LastCameraSignature = 0;
		bool HasDrawn = false;
		uint32_t StillFrames = 0;

		void AddShader(std::shared_ptr<ShaderScript> shader);
		void RemoveShader(std::shared_ptr<ShaderScript> shader);
		static int LRemoveShader(lua_State *L, Instance *instance);
		std::vector<std::shared_ptr<Instance>> ListShaders();
		void ClearShaders();

		void OnEvent(SDL_Window *window, SDL_Event &event);
		void Step(float deltaTime);

		static int LRender(lua_State *L, Instance *instance);

		static const std::vector<Camera *> &GetAllCameras();

		Camera();
		~Camera() override;
	};
}
