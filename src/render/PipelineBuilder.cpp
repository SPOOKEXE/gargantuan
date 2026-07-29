#include "gargantuan/render/PipelineBuilder.hpp"
#include "gargantuan/render/Mesh.hpp"
#include <SDL3/SDL_gpu.h>

namespace gargantuan {
	PipelineBuilder &PipelineBuilder::SetVertexShader(SDL_GPUShader *shader) {
		VertexShader = shader;
		return *this;
	};

	PipelineBuilder &PipelineBuilder::SetFragmentShader(SDL_GPUShader *shader) {
		FragmentShader = shader;
		return *this;
	};

	PipelineBuilder &PipelineBuilder::SetVertexInputEnabled(bool enabled) {
		VertexInputEnabled = enabled;
		return *this;
	};

	PipelineBuilder &PipelineBuilder::SetCullingEnabled(bool enabled) {
		CullingEnabled = enabled;
		return *this;
	};

	PipelineBuilder &PipelineBuilder::SetColorFormat(SDL_GPUTextureFormat format) {
		ColorFormat = format;
		return *this;
	}

	PipelineBuilder &PipelineBuilder::AddColorFormat(SDL_GPUTextureFormat format) {
		if (ExtraColorTargets + 1 >= MAXIMUM_COLOR_TARGETS) {
			SDL_Log("A pipeline cannot have more than %zu colour targets", MAXIMUM_COLOR_TARGETS);
			return *this;
		}

		ColorTargets[++ExtraColorTargets].format = format;
		return *this;
	};

	PipelineBuilder &PipelineBuilder::SetColorEnabled(bool enabled) {
		ColorEnabled = enabled;
		return *this;
	};

	PipelineBuilder &PipelineBuilder::SetBlendingEnabled(bool enabled) {
		BlendingEnabled = enabled;
		return *this;
	};

	PipelineBuilder &PipelineBuilder::SetDepthFormat(SDL_GPUTextureFormat format) {
		DepthFormat = format;
		return *this;
	};

	PipelineBuilder &PipelineBuilder::SetDepthEnabled(bool enabled) {
		DepthEnabled = enabled;
		return *this;
	};

	SDL_GPUGraphicsPipelineCreateInfo PipelineBuilder::BuildInfo() {
		SDL_GPUGraphicsPipelineCreateInfo info{};
		info.vertex_shader = VertexShader;
		info.fragment_shader = FragmentShader;

		// A pipeline that declares mesh attributes expects a vertex buffer to be
		// bound; one generating geometry from gl_VertexIndex must declare none
		if (VertexInputEnabled) {
			info.vertex_input_state.vertex_attributes = Vertex::Attributes->data();
			info.vertex_input_state.num_vertex_attributes = static_cast<Uint32>(Vertex::Attributes->size());
			info.vertex_input_state.vertex_buffer_descriptions = Vertex::BufferDescriptions->data();
			info.vertex_input_state.num_vertex_buffers = static_cast<Uint32>(Vertex::BufferDescriptions->size());
		}

		info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
		info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
		info.rasterizer_state.cull_mode = CullingEnabled ? SDL_GPU_CULLMODE_BACK : SDL_GPU_CULLMODE_NONE;
		info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;

		info.depth_stencil_state.enable_depth_test = DepthEnabled;
		info.depth_stencil_state.enable_depth_write = DepthEnabled;
		info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;

		ColorTargets[0].format = ColorFormat;

		// The same blend state on every attachment. A pass writing more than
		// one writes measurements alongside its picture, and those are never
		// blended, so the two have not yet needed to differ.
		for (size_t index = 0; index <= ExtraColorTargets; index++) {
			auto &blend = ColorTargets[index].blend_state;
			blend.enable_blend = BlendingEnabled;

			if (BlendingEnabled) {
				blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
				blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
				blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
				blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
				blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
				blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
			} else {
				blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
				blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
				blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
				blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
				blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
				blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
			}
		}

		info.target_info.color_target_descriptions = ColorTargets.data();

		if (ColorEnabled) {
			info.target_info.num_color_targets = (Uint32)(ExtraColorTargets + 1);
		} else {
			info.target_info.num_color_targets = 0;
		}

		info.target_info.depth_stencil_format = DepthFormat;
		info.target_info.has_depth_stencil_target = DepthEnabled;

		return info;
	}

	SDL_GPUGraphicsPipeline *PipelineBuilder::Build(SDL_GPUDevice *gpu) {
		auto info = BuildInfo();
		return SDL_CreateGPUGraphicsPipeline(gpu, &info);
	}
} // namespace gargantuan
