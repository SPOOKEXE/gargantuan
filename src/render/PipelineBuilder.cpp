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

	PipelineBuilder &PipelineBuilder::SetVertexStreams(VertexStreams streams) {
		EnabledVertexStreams = streams;
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
		if (ExtraColorTargetCount + 1 >= MAXIMUM_COLOR_TARGETS) {
			SDL_Log("A pipeline cannot have more than %zu colour targets", MAXIMUM_COLOR_TARGETS);
			return *this;
		}

		ColorTargetDescriptions[++ExtraColorTargetCount].format = format;
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

	SDL_GPUGraphicsPipelineCreateInfo PipelineBuilder::BuildCreateInfo() {
		SDL_GPUGraphicsPipelineCreateInfo info{};
		info.vertex_shader = VertexShader;
		info.fragment_shader = FragmentShader;

		// gl_VertexIndex pipelines must declare no mesh input.
		if (VertexInputEnabled && EnabledVertexStreams == VertexStreams::All) {
			// The interleaved struct: one binding, three attributes at fixed
			// offsets. Kept for the stage that reads all three, where splitting
			// would trade one 32-byte fetch for three smaller ones and gain
			// nothing.
			info.vertex_input_state.vertex_attributes = Vertex::InterleavedAttributes->data();
			info.vertex_input_state.num_vertex_attributes = static_cast<Uint32>(Vertex::InterleavedAttributes->size());
			info.vertex_input_state.vertex_buffer_descriptions = Vertex::InterleavedBufferDescriptions->data();
			info.vertex_input_state.num_vertex_buffers = static_cast<Uint32>(Vertex::InterleavedBufferDescriptions->size());
		} else if (VertexInputEnabled) {
			// One tightly packed buffer per requested stream. The layout is held on
			// the builder because SDL reads these arrays when the pipeline is
			// created, which is after this function returns.
			SplitStreamVertexLayout = Vertex::LayoutFor(EnabledVertexStreams);
			info.vertex_input_state.vertex_attributes = SplitStreamVertexLayout.Attributes.data();
			info.vertex_input_state.num_vertex_attributes = SplitStreamVertexLayout.AttributeCount;
			info.vertex_input_state.vertex_buffer_descriptions = SplitStreamVertexLayout.BufferDescriptions.data();
			info.vertex_input_state.num_vertex_buffers = SplitStreamVertexLayout.BufferCount;
		}

		info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
		info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
		info.rasterizer_state.cull_mode = CullingEnabled ? SDL_GPU_CULLMODE_BACK : SDL_GPU_CULLMODE_NONE;
		info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;

		info.depth_stencil_state.enable_depth_test = DepthEnabled;
		info.depth_stencil_state.enable_depth_write = DepthEnabled;
		info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;

		ColorTargetDescriptions[0].format = ColorFormat;

		// All attachments share one blend state; measurement passes disable it.
		for (size_t index = 0; index <= ExtraColorTargetCount; index++) {
			auto &blend = ColorTargetDescriptions[index].blend_state;
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

		info.target_info.color_target_descriptions = ColorTargetDescriptions.data();

		if (ColorEnabled) {
			info.target_info.num_color_targets = (Uint32)(ExtraColorTargetCount + 1);
		} else {
			info.target_info.num_color_targets = 0;
		}

		info.target_info.depth_stencil_format = DepthFormat;
		info.target_info.has_depth_stencil_target = DepthEnabled;

		return info;
	}

	SDL_GPUGraphicsPipeline *PipelineBuilder::Build(SDL_GPUDevice *gpu) {
		auto info = BuildCreateInfo();
		return SDL_CreateGPUGraphicsPipeline(gpu, &info);
	}
} // namespace gargantuan
