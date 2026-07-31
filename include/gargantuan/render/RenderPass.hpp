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

		glm::vec3 LightDirection = glm::normalize(glm::vec3(0.75f, 1.0f, 0.5f));

		bool ShouldSkipRedraw = false;
	};

	inline constexpr float SHADOW_ORTHO_EXTENT = 30.0f;
	inline constexpr float SHADOW_ORTHO_NEAR = -50.0f;
	inline constexpr float SHADOW_ORTHO_FAR = 150.0f;
	inline constexpr float SHADOW_EYE_DISTANCE = 40.0f;

	namespace detail {
		constexpr float ConstexprSqrt(float value) {
			if (value <= 0.0f) {
				return 0.0f;
			}
			float guess = value > 1.0f ? value : 1.0f;
			for (int step = 0; step < 32; step++) {
				guess = 0.5f * (guess + value / guess);
			}
			return guess;
		}
	}

	inline constexpr float SHADOW_CAST_REACH = SHADOW_ORTHO_FAR - SHADOW_ORTHO_NEAR;
	inline constexpr float SHADOW_VOLUME_RADIUS = SHADOW_EYE_DISTANCE +
		detail::ConstexprSqrt(
			2.0f * SHADOW_ORTHO_EXTENT * SHADOW_ORTHO_EXTENT + SHADOW_ORTHO_FAR * SHADOW_ORTHO_FAR
		);

	inline constexpr uint32_t MAX_SURFACE_SLOTS = 256;
	inline constexpr uint32_t MAX_MESH_IDS = 32;

	struct PartSpan {
		BasePart *const *Parts = nullptr;
		size_t Count = 0;

		size_t size() const {
			return Count;
		}
		BasePart *const *begin() const {
			return Parts;
		}
		BasePart *const *end() const {
			return Parts + Count;
		}
	};

	struct VisibleSet {
		uint64_t VisiblePartsHash = 0;
		bool VisiblePartsHashValid = false;
		uint64_t SceneStamp = 0;
		uint64_t CameraStamp = 0;
		bool HasBeenBuilt = false;

		std::unordered_set<const BasePart *> InView;

		std::vector<BasePart *> InViewList;
		std::vector<BasePart *> ShadowCasterList;
		std::vector<uint32_t> InViewIndexList;
		std::vector<uint32_t> ShadowIndexList;
		size_t InViewCount = 0;
		size_t ShadowCount = 0;

		PartSpan InViewParts() const {
			return {InViewList.data(), InViewCount};
		}
		PartSpan ShadowParts() const {
			return {ShadowCasterList.data(), ShadowCount};
		}
		const uint32_t *ShadowSlots() const {
			return ShadowIndexList.size() >= ShadowCount ? ShadowIndexList.data() : nullptr;
		}

		bool IsInView(const BasePart *part) const {
			return InView.count(part) != 0;
		}
	};

	struct FrameContext : DrawContext {
		SDL_GPUCommandBuffer *Commands;

		SDL_GPUTexture *ColorTargetTexture;
		SDL_GPUTexture *DepthTexture;

		SDL_GPUTexture *ShadowMapTexture;
		SDL_GPUSampler *ShadowSampler;
		glm::mat4 LightViewProjectionMatrix;

		// Invariant: both optional measurement targets are null or both are set.
		SDL_GPUTexture *VelocityTarget = nullptr;
		SDL_GPUTexture *LinearViewDepthTexture = nullptr;

		uint32_t Width;
		uint32_t Height;

		SDL_GPUGraphicsPipeline *SurfacePipeline = nullptr;
		const void *SurfaceParameters = nullptr;
		uint32_t SurfaceParameterBytes = 0;
		const SDL_GPUTextureSamplerBinding *SurfaceSamplers = nullptr;
		uint32_t SurfaceSamplerCount = 0;

		const std::unordered_map<const BasePart *, SDL_GPUTexture *> *PartTextures = nullptr;
		// Indexed by stable grid slot, not draw order; null selects per-part building.
		const std::vector<PartInstance> *InstancesBySlot = nullptr;
		const std::vector<uint32_t> *DirtyInstanceSlots = nullptr;
		bool InstancesAllDirty = true;
		const std::vector<uint16_t> *MeshTextureBatchKeys = nullptr;
		const std::vector<SDL_GPUTexture *> *SurfaceTextures = nullptr;
		bool SurfaceSlotsComplete = false;
		SDL_GPUTexture *WhiteTexture = nullptr;
		SDL_GPUSampler *SurfaceTextureSampler = nullptr;

		const VisibleSet *VisibleParts = nullptr;
	};

	class RenderPass {
	  public:
		// Filled in by the subclass constructor, like Pipeline, and released by
		// RenderPass::Destroy. A pass that needs a second shader owns and
		// destroys that one itself.
		FileShader PassShader;
		SDL_GPUGraphicsPipeline *Pipeline = nullptr;

		virtual ~RenderPass() = default;
		virtual SDL_GPURenderPass *Draw(SDL_GPUDevice *gpu, FrameContext &context) = 0;
		virtual void Resize(SDL_GPUDevice *gpu, uint32_t width, uint32_t height) {};
		virtual void Destroy(SDL_GPUDevice *gpu);
	};
}
