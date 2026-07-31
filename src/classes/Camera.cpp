#include "gargantuan/classes/Camera.hpp"
#include "gargantuan/classes/EditableImage.hpp"
#include "gargantuan/datatypes/CFrame.hpp"
#include "gargantuan/datatypes/UDim2.hpp"
#include "gargantuan/datatypes/Vector2.hpp"
#include "gargantuan/render/RenderProvider.hpp"
#include "gargantuan/scripting/ThreadEngine.hpp"
#include "gargantuan/scripting/Userdata.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>
#include <algorithm>
#include <glm/trigonometric.hpp>
#include <lualib.h>
#include <memory>

namespace gargantuan {
	const Camera::ClassDefinition Camera::DEFINITION{
		.Name = "Camera",
		.Superclass = "Instance",
		.Constructor = ClassDefinition::WrapConstructor<Camera>(),
		.Properties = {
			G_UD_READWRITE_PROP(Camera, CameraType, Enums::CameraType),
			G_UD_READWRITE_PROP(Camera, CFrame, gargantuan::CFrame),
			G_UD_READWRITE_PROP(Camera, Enabled, bool),
			G_UD_READWRITE_PROP(Camera, Antialiasing, bool),
			{
				// Hand-written so a negative rate is normalised to exactly -1;
				// they all mean on demand and should read back the same
				"FPS",
				{
					[](lua_State *L, Instance *instance) -> int {
						StackValue<float>::Push(L, instance->Cast<Camera>()->GetFPS());
						return 1;
					},
					[](lua_State *L, Instance *instance) -> int {
						instance->Cast<Camera>()->SetFPS(CheckStackValue<float>(L, -1));
						return 0;
					},
					G_UD_REFLECT_TYPE(float),
				},
			},
			G_UD_READWRITE_PROP(Camera, DrawToWindow, bool),
			G_UD_READWRITE_PROP(Camera, WindowPosition, UDim2),
			G_UD_READWRITE_PROP(Camera, WindowSize, UDim2),
			G_UD_READWRITE_PROP(Camera, FieldOfView, float),
			{
				"SurfaceShader",
				{
					[](lua_State *L, Instance *instance) -> int {
						StackValue<Instance::Pointer>::Push(L, instance->Cast<Camera>()->SurfaceShader);
						return 1;
					},
					[](lua_State *L, Instance *instance) -> int {
						auto camera = instance->Cast<Camera>();
						if (lua_isnoneornil(L, -1)) {
							camera->SurfaceShader = nullptr;
							return 0;
						}

						auto shader = std::dynamic_pointer_cast<gargantuan::SurfaceShader>(
							StackValue<Instance::Pointer>::From(L, -1)
						);
						if (!shader) {
							luaL_error(L, "SurfaceShader must be a SurfaceShader");
							return 0;
						}

						camera->SurfaceShader = shader;
						return 0;
					},
					G_UD_REFLECT_TYPE(Instance::Pointer),
				},
			},
			G_UD_READWRITE_PROP(Camera, ViewportSize, gargantuan::Vector2),
			{
				"HorizontalFieldOfView",
				Property{
					Method::Wrap<&Camera::GetHorizontalFieldOfView>().Call,
					Method::Wrap<&Camera::SetHorizontalFieldOfView>().Call,
					G_UD_REFLECT_TYPE(float),
				},
			},
			{
				"DiagonalFieldOfView",
				Property{
					Method::Wrap<&Camera::GetDiagonalFieldOfView>().Call,
					Method::Wrap<&Camera::SetDiagonalFieldOfView>().Call,
					G_UD_REFLECT_TYPE(float),
				},
			},
		},
		.Methods = {
			{"Render",
			 {&Camera::LRender, []() -> std::string { return "(self): EditableImage"; }}},
			{"AddShader", Method::Wrap<&Camera::AddShader>()},
			{"RemoveShader",
			 {&Camera::LRemoveShader,
			  []() -> std::string { return "(self, shaderOrIndex: (ShaderScript | number)?): boolean"; }}},
			{"ListShaders", Method::Wrap<&Camera::ListShaders>()},
			{"ClearShaders", Method::Wrap<&Camera::ClearShaders>()},
		},
	};

	void Camera::AddShader(std::shared_ptr<ShaderScript> shader) {
		if (!shader) {
			return;
		}

		// Adding the same shader twice would run it twice, which is almost
		// never what was meant
		auto existing = std::find(Shaders.begin(), Shaders.end(), shader);
		if (existing != Shaders.end()) {
			return;
		}

		Shaders.push_back(std::move(shader));
	}

	void Camera::RemoveShader(std::shared_ptr<ShaderScript> shader) {
		auto existing = std::find(Shaders.begin(), Shaders.end(), shader);
		if (existing != Shaders.end()) {
			Shaders.erase(existing);
		}
	}

	int Camera::LRemoveShader(lua_State *L, Instance *instance) {
		auto *camera = instance->Cast<Camera>();
		if (!camera) {
			luaL_error(L, "RemoveShader must be called on a Camera");
			return 0;
		}

		// No argument means the one on the end
		if (lua_isnoneornil(L, 2)) {
			if (camera->Shaders.empty()) {
				lua_pushboolean(L, false);
				return 1;
			}

			camera->Shaders.pop_back();
			lua_pushboolean(L, true);
			return 1;
		}

		if (lua_isnumber(L, 2)) {
			// One-based, to match ListShaders and the rest of Luau
			int index = (int)lua_tointeger(L, 2);
			if (index < 1 || index > (int)camera->Shaders.size()) {
				lua_pushboolean(L, false);
				return 1;
			}

			camera->Shaders.erase(camera->Shaders.begin() + (index - 1));
			lua_pushboolean(L, true);
			return 1;
		}

		auto shader = std::dynamic_pointer_cast<ShaderScript>(StackValue<Instance::Pointer>::From(L, 2));
		if (!shader) {
			luaL_typeerror(L, 2, "ShaderScript or number");
			return 0;
		}

		size_t before = camera->Shaders.size();
		camera->RemoveShader(shader);
		lua_pushboolean(L, camera->Shaders.size() != before);
		return 1;
	}

	std::vector<std::shared_ptr<Instance>> Camera::ListShaders() {
		std::vector<std::shared_ptr<Instance>> result;
		result.reserve(Shaders.size());
		for (auto &shader : Shaders) {
			result.push_back(shader);
		}
		return result;
	}

	void Camera::ClearShaders() {
		Shaders.clear();
	}

	// Cameras register themselves so the renderer can find every one of them
	// without walking the instance tree each frame
	static std::vector<Camera *> ALL_CAMERAS;

	Camera::Camera() {
		ALL_CAMERAS.push_back(this);
	}

	Camera::~Camera() {
		if (auto *provider = RenderProvider::GetCurrent()) {
			provider->ReleaseCameraTarget(this);
		}

		auto it = std::find(ALL_CAMERAS.begin(), ALL_CAMERAS.end(), this);
		if (it != ALL_CAMERAS.end()) {
			ALL_CAMERAS.erase(it);
		}
	}

	const std::vector<Camera *> &Camera::GetAllCameras() {
		return ALL_CAMERAS;
	}

	int Camera::LRender(lua_State *L, Instance *instance) {
		auto *camera = instance->Cast<Camera>();
		if (!camera) {
			luaL_error(L, "Render must be called on a Camera");
			return 0;
		}

		auto *provider = RenderProvider::GetCurrent();
		if (!provider) {
			luaL_error(L, "Cannot render a Camera before the renderer is running");
			return 0;
		}

		if (camera->ViewportSize.GetX() < 1.0f || camera->ViewportSize.GetY() < 1.0f) {
			luaL_error(L, "Cannot render a Camera whose ViewportSize is empty; set it first");
			return 0;
		}

		auto owned = camera->weak_from_this().lock();
		if (!owned) {
			luaL_error(L, "Cannot render a Camera that is not owned by the engine");
			return 0;
		}

		auto *engine = ThreadEngine::Get(L);

		if (!provider->Scene.WorldRoot) {
			luaL_error(L, "Cannot render a Camera before the Workspace exists");
			return 0;
		}

		DrawContext drawContext{
			.WorldRoot = provider->Scene.WorldRoot,
			.Camera = std::static_pointer_cast<Camera>(owned),
			.LightDirection = provider->Scene.LightDirection,
		};

		if (!provider->BeginRenderReadback(drawContext, L, engine)) {
			luaL_error(L, "Failed to start a Camera render");
			return 0;
		}

		// The thread resumes with the EditableImage once the download lands
		return lua_yield(L, 0);
	}

	float Camera::GetFPS() const {
		return FPS;
	}

	void Camera::SetFPS(float framesPerSecond) {
		// NaN fails every comparison, so it would slip through untouched and
		// make the interval NaN, which no comparison would ever satisfy
		if (!(framesPerSecond == framesPerSecond)) {
			framesPerSecond = DEFAULT_FPS;
		}

		// Every negative rate means on demand, so they all read back as -1
		FPS = framesPerSecond < 0.0f ? ON_DEMAND_FPS : framesPerSecond;
	}

	bool Camera::IsOnDemand() const {
		return FPS < 0.0f;
	}

	double Camera::GetRenderInterval(float maximumFps) const {
		if (IsOnDemand()) {
			return -1.0;
		}
		if (FPS <= 0.0f) {
			return 0.0;
		}

		// The ceiling only ever lowers a rate; a camera asking for less than
		// the limit keeps what it asked for
		float effective = maximumFps > 0.0f ? glm::min(FPS, maximumFps) : FPS;
		return 1.0 / (double)effective;
	}

	float Camera::GetAspectRatio() {
		return ViewportSize.GetY() > 0.0f ? ViewportSize.GetX() / ViewportSize.GetY() : 1.0f;
	}

	float Camera::GetHorizontalFieldOfView() {
		return glm::degrees(2 * glm::atan(GetAspectRatio() * glm::tan(glm::radians(FieldOfView) / 2)));
	}

	void Camera::SetHorizontalFieldOfView(float fovy) {
		FieldOfView = glm::degrees(2 * glm::atan(1 / GetAspectRatio() * glm::tan(glm::radians(fovy) / 2)));
	}

	float Camera::GetDiagonalFieldOfView() {
		return glm::degrees(
			2 * glm::atan(glm::sqrt(1 + glm::pow(GetAspectRatio(), 2)) * glm::tan(glm::radians(FieldOfView) / 2))
		);
	}

	void Camera::SetDiagonalFieldOfView(float fovy) {
		FieldOfView = glm::degrees(
			2 * glm::atan(1 / glm::sqrt(1 + glm::pow(GetAspectRatio(), 2)) * glm::tan(glm::radians(fovy) / 2))
		);
	}

	glm::mat4 Camera::GetProjectionMatrix() {
		return glm::perspective(glm::radians(FieldOfView), GetAspectRatio(), NEAR_PLANE, FAR_PLANE);
	}

	glm::mat4 Camera::GetViewMatrix() {
		glm::vec3 position = CFrame.Position;
		return glm::lookAt(position, position + CFrame.GetLookVector(), CFrame.GetUpVector());
	}

	namespace {
		// Radical inverse: write the index in `base`, mirror its digits about
		// the point. Halton is that in a different base per axis, so every new
		// sample lands in the largest gap the ones before it left.
		float RadicalInverse(uint32_t index, uint32_t base) {
			float result = 0.0f;
			float fraction = 1.0f / (float)base;
			while (index > 0) {
				result += (float)(index % base) * fraction;
				index /= base;
				fraction /= (float)base;
			}
			return result;
		}
	} // namespace

	void Camera::AdvanceJitter(bool jittering) {
		PreviousJitter = Jitter;

		if (!jittering) {
			Jitter = glm::vec2(0.0f);
			JitterIndex = 0;
			return;
		}

		// One-based: Halton at zero is the pixel centre, where an unjittered
		// camera already samples
		JitterIndex = JitterIndex % JITTER_SEQUENCE_LENGTH + 1;
		Jitter = glm::vec2(RadicalInverse(JitterIndex, 2) - 0.5f, RadicalInverse(JitterIndex, 3) - 0.5f);
	}

	glm::mat4 Camera::GetJitteredProjectionMatrix() {
		glm::mat4 projection = GetProjectionMatrix();

		float width = ViewportSize.GetX();
		float height = ViewportSize.GetY();
		if (width <= 0.0f || height <= 0.0f || (Jitter.x == 0.0f && Jitter.y == 0.0f)) {
			return projection;
		}

		// Scaled by w, so it is the same sub-pixel step at any depth. Two
		// pixels span clip space, hence the factor of two; y is subtracted
		// because SDL flips the viewport to a top-left origin.
		projection[2][0] += 2.0f * Jitter.x / width;
		projection[2][1] -= 2.0f * Jitter.y / height;
		return projection;
	}

	void Camera::OnEvent(SDL_Window *window, SDL_Event &event) {
		if (CameraType != Enums::CameraType::Freecam) {
			return;
		}

		if (event.type == SDL_EVENT_WINDOW_RESIZED) {
			int width, height;
			SDL_GetWindowSizeInPixels(window, &width, &height);
			ViewportSize = Vector2(width, height);
		} else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_RIGHT) {
			SDL_SetWindowRelativeMouseMode(window, true);
		} else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_RIGHT) {
			SDL_SetWindowRelativeMouseMode(window, false);
		} else if (event.type == SDL_EVENT_MOUSE_MOTION && SDL_GetWindowRelativeMouseMode(window)) {
			AccumulatedDeltaX += event.motion.xrel;
			AccumulatedDeltaY += event.motion.yrel;
		}
	}

	void Camera::Step(float deltaTime) {
		if (CameraType != Enums::CameraType::Freecam) {
			return;
		}

		if (AccumulatedDeltaX != 0.0f || AccumulatedDeltaY != 0.0f) {
			// SDL reports rightwards and downwards as positive, and both turn
			// the camera the other way round
			Yaw -= AccumulatedDeltaX * FreecamSensitivity;

			Pitch -= AccumulatedDeltaY * FreecamSensitivity;
			Pitch = glm::clamp(Pitch, -89.0f, 89.0f);

			AccumulatedDeltaX = 0.0f;
			AccumulatedDeltaY = 0.0f;

			// Yaw about the world's up, then pitch about the camera's own
			// right: Ry * Rx. Composed the other way, as CFrame::Angles does,
			// the yaw happens inside the already-pitched frame, which rolls the
			// horizon and makes a sideways mouse movement curve.
			auto rotation = CFrame::fromEulerAnglesYXZ(glm::radians(Pitch), glm::radians(Yaw), 0.0f);
			CFrame = gargantuan::CFrame(CFrame.Position, rotation.Rotation);
		}

		auto keys = SDL_GetKeyboardState(nullptr);
		// Null before SDL's video subsystem is up, which the tests rely on
		if (!keys) {
			return;
		}

		auto lookVector = CFrame.GetLookVector();
		auto rightVector = CFrame.GetRightVector();
		auto upVector = CFrame.GetUpVector();

		if (keys[SDL_SCANCODE_W]) {
			CFrame.Position += lookVector * FreecamSpeed * deltaTime;
		}

		if (keys[SDL_SCANCODE_S]) {
			CFrame.Position -= lookVector * FreecamSpeed * deltaTime;
		}

		if (keys[SDL_SCANCODE_A]) {
			CFrame.Position -= rightVector * FreecamSpeed * deltaTime;
		}

		if (keys[SDL_SCANCODE_D]) {
			CFrame.Position += rightVector * FreecamSpeed * deltaTime;
		}

		if (keys[SDL_SCANCODE_SPACE]) {
			CFrame.Position += glm::vec3(0, FreecamSpeed * deltaTime, 0);
		}

		// complex and volatile so i can screenshot on macos
		bool shiftPressed = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
		bool guiPressed = (SDL_GetModState() & SDL_KMOD_GUI) != 0;
		if (shiftPressed && !guiPressed) {
			CFrame.Position -= glm::vec3(0, FreecamSpeed * deltaTime, 0);
		}
	}
} // namespace gargantuan
