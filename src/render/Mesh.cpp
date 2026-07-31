#include "gargantuan/render/Mesh.hpp"

namespace gargantuan {
	std::array<SDL_GPUVertexBufferDescription, 1> Vertex::InterleavedBufferDescriptions[]{
		SDL_GPUVertexBufferDescription{
			.slot = 0,
			.pitch = sizeof(Vertex),
			.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
		},
	};

	std::array<SDL_GPUVertexAttribute, 3> Vertex::InterleavedAttributes[]{
		SDL_GPUVertexAttribute{
			.location = 0,
			.buffer_slot = 0,
			.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
			.offset = offsetof(Vertex, Position),
		},
		SDL_GPUVertexAttribute{
			.location = 1,
			.buffer_slot = 0,
			.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
			.offset = offsetof(Vertex, Normal),
		},
		SDL_GPUVertexAttribute{
			.location = 2,
			.buffer_slot = 0,
			.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
			.offset = offsetof(Vertex, UV),
		},
	};

	uint32_t Vertex::BytesPerVertexForStream(VertexStreams stream) {
		switch (stream) {
		case VertexStreams::Position:
		case VertexStreams::Normal:
			return sizeof(glm::vec3);
		case VertexStreams::UV:
			return sizeof(glm::vec2);
		default:
			return 0;
		}
	}

	Vertex::Layout Vertex::LayoutFor(VertexStreams streams) {
		Layout layout;

		// Slots are assigned in stream order rather than being fixed per stream,
		// so a position-only layout uses slot 0 and nothing else -- a gap in the
		// slot numbering would mean binding a buffer the shader never reads just
		// to fill it.
		struct Stream {
			VertexStreams Bit;
			uint32_t ShaderLocation;
			SDL_GPUVertexElementFormat Format;
		};
		constexpr Stream STREAMS[] = {
			{VertexStreams::Position, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3},
			{VertexStreams::Normal, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3},
			{VertexStreams::UV, 2, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2},
		};

		for (const Stream &stream : STREAMS) {
			if (!Contains(streams, stream.Bit)) {
				continue;
			}

			uint32_t slot = layout.BufferCount;
			uint32_t bytesPerVertex = BytesPerVertexForStream(stream.Bit);

			layout.BufferDescriptions[slot] = SDL_GPUVertexBufferDescription{
				.slot = slot,
				// Tightly packed: the whole point is that the stride is the
				// stream's own width and not the interleaved struct's.
				.pitch = bytesPerVertex,
				.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
			};
			layout.Attributes[layout.AttributeCount] = SDL_GPUVertexAttribute{
				.location = stream.ShaderLocation,
				.buffer_slot = slot,
				.format = stream.Format,
				.offset = 0,
			};

			layout.BufferCount++;
			layout.AttributeCount++;
			layout.BytesPerVertex += bytesPerVertex;
		}

		return layout;
	}
} // namespace gargantuan
