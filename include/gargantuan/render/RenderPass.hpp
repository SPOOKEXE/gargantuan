#pragma once

#include "gargantuan/classes/Camera.hpp"
#include "gargantuan/classes/WorldRoot.hpp"
#include "gargantuan/render/InstanceData.hpp"
#include "gargantuan/render/Shader.hpp"

#include <SDL3/SDL.h>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gargantuan {
	struct DrawContext {
		std::shared_ptr<WorldRoot> WorldRoot;
		std::shared_ptr<Camera> Camera;

		// Points toward the light; default preserves lighting without a service.
		glm::vec3 LightDirection = glm::normalize(glm::vec3(0.75f, 1.0f, 0.5f));

		// Reuse the target when camera cadence skips this frame.
		bool NotDueYet = false;
	};

	// The shadow map covers one fixed box at the world origin. The walk needs
	// the same numbers to reject casters that could never land in it, so they
	// live here rather than inside the pass.
	inline constexpr float SHADOW_ORTHO_EXTENT = 30.0f;
	inline constexpr float SHADOW_ORTHO_NEAR = -50.0f;
	inline constexpr float SHADOW_ORTHO_FAR = 150.0f;
	inline constexpr float SHADOW_EYE_DISTANCE = 40.0f;
	// Bounds that box from the origin: eye distance plus its own half diagonal,
	// 40 + sqrt(30^2 + 30^2 + 150^2). Loose is the safe way round.
	inline constexpr float SHADOW_VOLUME_RADIUS = 196.0f;

	// Slot 0 is no picture. A world with more distinct pictures than this keeps
	// working: the pass falls back to the map, which has no cap.
	inline constexpr uint32_t MAX_SURFACE_SLOTS = 256;
	inline constexpr uint32_t MAX_MESH_IDS = 32;

	struct PartSpan {
		BasePart *const *Data = nullptr;
		size_t Count = 0;

		size_t size() const {
			return Count;
		}
		BasePart *const *begin() const {
			return Data;
		}
		BasePart *const *end() const {
			return Data + Count;
		}
	};

	struct VisibleSet {
		uint64_t Signature = 0;
		bool SignatureValid = false;
		uint64_t SceneStamp = 0;
		uint64_t CameraStamp = 0;
		bool Walked = false;

		std::unordered_set<const BasePart *> InView;
		std::unordered_set<const BasePart *> ShadowsIntoView;

		// Lists serve passes; sets serve per-part redraw checks.
		std::vector<BasePart *> InViewList;
		std::vector<BasePart *> ShadowList;
		// Where each InViewList entry sits in the world, so the opaque pass
		// reaches its built instance without asking the part anything
		std::vector<uint32_t> InViewIndexList;
		size_t InViewCount = 0;
		size_t ShadowCount = 0;

		PartSpan InViewParts() const {
			return {InViewList.data(), InViewCount};
		}
		PartSpan ShadowParts() const {
			return {ShadowList.data(), ShadowCount};
		}

		bool IsInView(const BasePart *part) const {
			return InView.count(part) != 0;
		}
		bool CastsIntoView(const BasePart *part) const {
			return ShadowsIntoView.count(part) != 0;
		}
	};

	struct FrameContext : DrawContext {
		SDL_GPUCommandBuffer *Commands;

		// Swapchain or offscreen target; passes must not assume a window.
		SDL_GPUTexture *ColorTarget;
		SDL_GPUTexture *DepthTexture;

		SDL_GPUTexture *ShadowMapTexture;
		SDL_GPUSampler *ShadowSampler;
		glm::mat4 ShadowMatrix;

		// Both null or both set; one optional pass writes both measurements.
		SDL_GPUTexture *VelocityTarget = nullptr;
		SDL_GPUTexture *ViewDepthTarget = nullptr;

		uint32_t Width;
		uint32_t Height;

		SDL_GPUGraphicsPipeline *SurfacePipeline = nullptr;
		const void *SurfaceParameters = nullptr;
		uint32_t SurfaceParameterBytes = 0;
		const SDL_GPUTextureSamplerBinding *SurfaceSamplers = nullptr;
		uint32_t SurfaceSamplerCount = 0;

		// Resolved once per frame; missing parts use WhiteTexture.
		const std::unordered_map<const BasePart *, SDL_GPUTexture *> *PartTextures = nullptr;
		// Built instances in draw order, or null to build them per part
		const std::vector<InstanceData> *PartInstances = nullptr;
		// Mesh and surface slot in one number, same order as PartInstances
		const std::vector<uint16_t> *DrawKeys = nullptr;
		const std::vector<SDL_GPUTexture *> *SurfaceTextures = nullptr;
		bool SurfaceSlotsComplete = false;
		SDL_GPUTexture *WhiteTexture = nullptr;
		SDL_GPUSampler *SurfaceTextureSampler = nullptr;

		// Null falls back to submitting everything.
		const VisibleSet *Visible = nullptr;
	};

	class RenderPass {
	  public:
		Shader Shader;
		SDL_GPUGraphicsPipeline *Pipeline = nullptr;

		virtual ~RenderPass() = default;
		virtual SDL_GPURenderPass *Draw(SDL_GPUDevice *gpu, FrameContext &context) = 0;
		virtual void Resize(SDL_GPUDevice *gpu, uint32_t width, uint32_t height) {};
		virtual void Destroy(SDL_GPUDevice *gpu);
	};
} // namespace gargantuan
