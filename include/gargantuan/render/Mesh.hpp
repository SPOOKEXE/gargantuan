#pragma once

#include <SDL3/SDL.h>
#include <array>
#include <glm/glm.hpp>

namespace gargantuan {
	enum class VertexStreams : uint8_t {
		None = 0,
		Position = 1 << 0,
		Normal = 1 << 1,
		UV = 1 << 2,

		All = Position | Normal | UV,
	};

	constexpr VertexStreams operator|(VertexStreams a, VertexStreams b) {
		return (VertexStreams)((uint8_t)a | (uint8_t)b);
	}
	constexpr bool Contains(VertexStreams set, VertexStreams stream) {
		return ((uint8_t)set & (uint8_t)stream) != 0;
	}

	struct Vertex {
	  public:
		glm::vec3 Position;
		glm::vec3 Normal;
		glm::vec2 UV;

		static std::array<SDL_GPUVertexBufferDescription, 1> InterleavedBufferDescriptions[];
		static std::array<SDL_GPUVertexAttribute, 3> InterleavedAttributes[];

		struct Layout {
			std::array<SDL_GPUVertexBufferDescription, 3> BufferDescriptions{};
			std::array<SDL_GPUVertexAttribute, 3> Attributes{};
			uint32_t BufferCount = 0;
			uint32_t AttributeCount = 0;
			uint32_t BytesPerVertex = 0;
		};

		static Layout LayoutFor(VertexStreams streams);
		static uint32_t BytesPerVertexForStream(VertexStreams stream);
	};

	struct Mesh {
	  public:
		std::vector<Vertex> Vertices;
		std::vector<uint32_t> Indices;
	};
};
