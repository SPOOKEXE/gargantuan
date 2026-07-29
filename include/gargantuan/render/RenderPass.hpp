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

		// Direction TOWARDS the light. Defaults to a fixed afternoon sun so a
		// draw without a Lighting service still shades sensibly.
		glm::vec3 LightDirection = glm::normalize(glm::vec3(0.75f, 1.0f, 0.5f));

		// Reuse the existing target when this camera's cadence skips a frame.
		bool NotDueYet = false;
	};

	// What one camera's frustum walk found, kept rather than discarded so the
	// passes can submit what is on screen instead of the whole world.
	//
	// Two sets: the opaque pass wants what lands in the picture, the shadow
	// pass also wants what is off screen but throwing a shadow into it.
	struct VisibleSet {
		uint64_t Signature = 0;
		uint64_t SceneStamp = 0;
		uint64_t CameraStamp = 0;
		bool Walked = false;

		std::unordered_set<const BasePart *> InView;
		std::unordered_set<const BasePart *> ShadowsIntoView;

		// The same two answers as flat lists, so a pass walks what it draws
		// instead of walking the world and paying a hash lookup per part that
		// is not there. Kept as well as the sets, since the redraw check does
		// ask about one part at a time.
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

		// Where the opaque pass draws. Either the window's swapchain texture
		// or an offscreen camera target, so passes must not assume a window.
		SDL_GPUTexture *ColorTarget;
		SDL_GPUTexture *DepthTexture;

		SDL_GPUTexture *ShadowMapTexture;
		SDL_GPUSampler *ShadowSampler;
		glm::mat4 ShadowMatrix;

		// Where motion vectors and view distances go. Null unless the chain
		// asked for one of them, and the velocity pass is only recorded when
		// they are set: drawing the scene twice for buffers nothing reads is
		// not worth it. Set together, because one pass writes both.
		SDL_GPUTexture *VelocityTarget = nullptr;
		SDL_GPUTexture *ViewDepthTarget = nullptr;

		uint32_t Width;
		uint32_t Height;

		SDL_GPUGraphicsPipeline *SurfacePipeline = nullptr;
		const void *SurfaceParameters = nullptr;
		uint32_t SurfaceParameterBytes = 0;
		const SDL_GPUTextureSamplerBinding *SurfaceSamplers = nullptr;
		uint32_t SurfaceSamplerCount = 0;

		// Resolved once per frame: which texture, if any, each part shows on
		// its surface. Parts absent from the map use WhiteTexture.
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
