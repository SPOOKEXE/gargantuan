#pragma once

#include "gargantuan/classes/Camera.hpp"
#include "gargantuan/classes/WorldRoot.hpp"
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

	// One camera's cached frustum result. Shadows include offscreen casters.
	struct VisibleSet {
		uint64_t Signature = 0;
		uint64_t SceneStamp = 0;
		uint64_t CameraStamp = 0;
		bool Walked = false;

		std::unordered_set<const BasePart *> InView;
		std::unordered_set<const BasePart *> ShadowsIntoView;

		// Lists serve passes; sets serve per-part redraw checks.
		std::vector<BasePart *> InViewList;
		std::vector<BasePart *> ShadowList;

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
