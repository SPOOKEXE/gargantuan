#include "gargantuan/render/GpuMesh.hpp"
#include "gargantuan/render/Mesh.hpp"
#include <cstring>

namespace gargantuan {
	GpuMesh::GpuMesh(Mesh mesh) {
		this->Vertices = mesh.Vertices;
		this->VertexCount = Vertices.size();
		this->VertexBufferBytes = VertexCount * sizeof(Vertex);

		this->Indices = mesh.Indices;
		this->IndexCount = Indices.size();
		this->IndexBufferBytes = IndexCount * sizeof(uint32_t);
	}

	SDL_GPUBuffer *GpuMesh::CreateVertexBuffer(SDL_GPUDevice *gpu) {
		if (VertexBuffer) {
			return VertexBuffer;
		}

		SDL_GPUBufferCreateInfo info = {.usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = VertexBufferBytes};
		VertexBuffer = SDL_CreateGPUBuffer(gpu, &info);

		return VertexBuffer;
	}

	SDL_GPUBuffer *GpuMesh::CreateIndexBuffer(SDL_GPUDevice *gpu) {
		if (IndexBuffer) {
			return IndexBuffer;
		}

		SDL_GPUBufferCreateInfo info = {.usage = SDL_GPU_BUFFERUSAGE_INDEX, .size = IndexBufferBytes};
		IndexBuffer = SDL_CreateGPUBuffer(gpu, &info);

		return IndexBuffer;
	}

	namespace {
		// Bytes one stream of `vertexCount` vertices occupies on its own.
		uint32_t StreamBytesForVertices(VertexStreams stream, uint32_t vertexCount) {
			return Vertex::BytesPerVertexForStream(stream) * vertexCount;
		}
	} // namespace

	SDL_GPUTransferBuffer *GpuMesh::CreateTransferBuffer(SDL_GPUDevice *gpu) {
		if (TransferBuffer) {
			return TransferBuffer;
		}

		uint32_t positionBytes = StreamBytesForVertices(VertexStreams::Position, VertexCount);
		uint32_t normalBytes = StreamBytesForVertices(VertexStreams::Normal, VertexCount);
		uint32_t uvBytes = StreamBytesForVertices(VertexStreams::UV, VertexCount);

		SDL_GPUTransferBufferCreateInfo info = {
			.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
			.size = VertexBufferBytes + IndexBufferBytes + positionBytes + normalBytes + uvBytes,
		};

		TransferBuffer = SDL_CreateGPUTransferBuffer(gpu, &info);

		auto *mappedTransfer = (uint8_t *)SDL_MapGPUTransferBuffer(gpu, TransferBuffer, false);
		if (!mappedTransfer) {
			return TransferBuffer;
		}

		std::memcpy(mappedTransfer, Vertices.data(), VertexBufferBytes);
		std::memcpy(mappedTransfer + VertexBufferBytes, Indices.data(), IndexBufferBytes);

		// De-interleaved, in the same order the regions are uploaded below. One
		// pass over the vertices writing three destinations, rather than three
		// passes -- the vertices are read once either way and this touches each
		// cache line of the source once.
		uint8_t *positions = mappedTransfer + VertexBufferBytes + IndexBufferBytes;
		uint8_t *normals = positions + positionBytes;
		uint8_t *uvs = normals + normalBytes;
		for (uint32_t index = 0; index < VertexCount; index++) {
			const Vertex &vertex = Vertices[index];
			std::memcpy(positions + index * sizeof(glm::vec3), &vertex.Position, sizeof(glm::vec3));
			std::memcpy(normals + index * sizeof(glm::vec3), &vertex.Normal, sizeof(glm::vec3));
			std::memcpy(uvs + index * sizeof(glm::vec2), &vertex.UV, sizeof(glm::vec2));
		}

		SDL_UnmapGPUTransferBuffer(gpu, TransferBuffer);

		return TransferBuffer;
	}

	uint32_t GpuMesh::CollectStreamBuffers(VertexStreams streams, SDL_GPUBuffer **out, uint32_t outBufferCapacity) const {
		// Slot order has to match Vertex::LayoutFor, or an attribute reads the
		// wrong buffer and the mesh comes out inside out.
		struct Entry {
			VertexStreams Bit;
			SDL_GPUBuffer *Buffer;
		};
		const Entry entries[] = {
			{VertexStreams::Position, PositionBuffer},
			{VertexStreams::Normal, NormalBuffer},
			{VertexStreams::UV, UVBuffer},
		};

		uint32_t count = 0;
		for (const Entry &entry : entries) {
			if (!Contains(streams, entry.Bit)) {
				continue;
			}
			if (!entry.Buffer || count >= outBufferCapacity) {
				// A missing buffer means the split upload did not happen. Reporting
				// none is what makes the caller bind the interleaved buffer instead
				// of drawing from a partial set.
				return 0;
			}
			out[count++] = entry.Buffer;
		}
		return count;
	}

	void GpuMesh::DestroyTransferBuffer(SDL_GPUDevice *gpu) {
		if (TransferBuffer) {
			SDL_ReleaseGPUTransferBuffer(gpu, TransferBuffer);
			TransferBuffer = nullptr;
		}
	}

	void GpuMesh::Upload(SDL_GPUDevice *gpu, SDL_GPUCopyPass *copyPass) {
		auto transferBuffer = CreateTransferBuffer(gpu);

		SDL_GPUTransferBufferLocation vertexSource{.transfer_buffer = transferBuffer, .offset = 0};
		SDL_GPUBufferRegion vertexDestination{.buffer = CreateVertexBuffer(gpu), .offset = 0, .size = VertexBufferBytes};
		SDL_UploadToGPUBuffer(copyPass, &vertexSource, &vertexDestination, false);

		SDL_GPUTransferBufferLocation indexSource{.transfer_buffer = transferBuffer, .offset = VertexBufferBytes};
		SDL_GPUBufferRegion indexDestination{.buffer = CreateIndexBuffer(gpu), .offset = 0, .size = IndexBufferBytes};
		SDL_UploadToGPUBuffer(copyPass, &indexSource, &indexDestination, false);

		// The split streams, laid out after the indices by CreateTransferBuffer in
		// this same order.
		uint32_t transferOffsetBytes = VertexBufferBytes + IndexBufferBytes;
		struct Split {
			VertexStreams Bit;
			SDL_GPUBuffer **Buffer;
		};
		const Split splits[] = {
			{VertexStreams::Position, &PositionBuffer},
			{VertexStreams::Normal, &NormalBuffer},
			{VertexStreams::UV, &UVBuffer},
		};

		for (const Split &split : splits) {
			uint32_t streamBytes = StreamBytesForVertices(split.Bit, VertexCount);
			if (streamBytes == 0) {
				continue;
			}

			if (!*split.Buffer) {
				SDL_GPUBufferCreateInfo info = {.usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = streamBytes};
				*split.Buffer = SDL_CreateGPUBuffer(gpu, &info);
			}
			if (!*split.Buffer) {
				// Not fatal: CollectStreamBuffers reports none, and every stage falls back
				// to the interleaved buffer it used before this existed.
				transferOffsetBytes += streamBytes;
				continue;
			}

			SDL_GPUTransferBufferLocation source{.transfer_buffer = transferBuffer, .offset = transferOffsetBytes};
			SDL_GPUBufferRegion destination{.buffer = *split.Buffer, .offset = 0, .size = streamBytes};
			SDL_UploadToGPUBuffer(copyPass, &source, &destination, false);
			transferOffsetBytes += streamBytes;
		}

		DestroyTransferBuffer(gpu);
	}

	void GpuMesh::Destroy(SDL_GPUDevice *gpu) {
		DestroyTransferBuffer(gpu);

		if (VertexBuffer) {
			SDL_ReleaseGPUBuffer(gpu, VertexBuffer);
			VertexBuffer = nullptr;
		}

		if (IndexBuffer) {
			SDL_ReleaseGPUBuffer(gpu, IndexBuffer);
			IndexBuffer = nullptr;
		}

		SDL_GPUBuffer **streams[] = {&PositionBuffer, &NormalBuffer, &UVBuffer};
		for (SDL_GPUBuffer **buffer : streams) {
			if (*buffer) {
				SDL_ReleaseGPUBuffer(gpu, *buffer);
				*buffer = nullptr;
			}
		}
	}
} // namespace gargantuan
