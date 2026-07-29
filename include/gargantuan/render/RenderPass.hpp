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
	};

	// What one camera's frustum walk found. The walk happens because the
	// redraw check needs a signature over what this camera can see; the sets
	// are the same answer kept rather than discarded, so the passes can submit
	// what is on screen instead of the whole world.
	//
	// Two sets because the passes ask different questions. The opaque pass
	// wants what lands in the picture. The shadow pass also wants what is off
	// screen but throwing a shadow into it, which is a longer reach and a
	// strictly wider set.
	struct VisibleSet {
		// The hash PlanRedraw compares against the camera's last frame
		uint64_t Signature = 0;
		// The state the walk was made at, so it can be reused until one of
		// them moves rather than repeated per pass
		uint64_t SceneStamp = 0;
		uint64_t CameraStamp = 0;
		bool Walked = false;

		std::unordered_set<const BasePart *> InView;
		std::unordered_set<const BasePart *> ShadowsIntoView;

		// The same two answers as flat lists. A pass wants to walk what it has
		// to draw; the sets answer about one part at a time, so walking the
		// world and asking about each part costs a hash lookup for every part
		// that turned out not to be there. At a few thousand parts that is the
		// larger half of what the pass was doing before it drew anything.
		//
		// Kept as well as the sets rather than instead of them, because the
		// redraw check does ask about one part at a time.
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

		// Where motion vectors and view distances go. Null on every camera but
		// the ones whose shader chain asked for one of them, and the velocity
		// pass is only recorded when they are set -- drawing the scene a second
		// time is not worth doing for buffers nothing reads. Set together,
		// because one pass writes both.
		SDL_GPUTexture *VelocityTarget = nullptr;
		SDL_GPUTexture *ViewDepthTarget = nullptr;

		uint32_t Width;
		uint32_t Height;

		// When a camera has a SurfaceShader, the opaque pass draws with this
		// pipeline instead of its own, and pushes the script's parameters as
		// the second fragment uniform buffer
		SDL_GPUGraphicsPipeline *SurfacePipeline = nullptr;
		const void *SurfaceParameters = nullptr;
		uint32_t SurfaceParameterBytes = 0;
		// Slot 0 is the shadow map; the script's images follow
		const SDL_GPUTextureSamplerBinding *SurfaceSamplers = nullptr;
		uint32_t SurfaceSamplerCount = 0;

		// Resolved once per frame: which texture, if any, each part shows on
		// its surface. Parts absent from the map use WhiteTexture.
		const std::unordered_map<const BasePart *, SDL_GPUTexture *> *PartTextures = nullptr;
		SDL_GPUTexture *WhiteTexture = nullptr;
		SDL_GPUSampler *SurfaceTextureSampler = nullptr;

		// What this camera can see, for the passes to cull against. Null means
		// no walk was made, and a pass then submits everything: wasteful, never
		// wrong, which is the right way round for a fallback.
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
