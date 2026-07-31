#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <cstring>

namespace gargantuan {
	// 64-bit hash_combine, not FNV-1a; a collision can skip one redraw.
	inline void MixBits(uint64_t &hash, uint64_t value) {
		hash ^= value + 0x9E3779B97F4A7C15ull + (hash << 6) + (hash >> 2);
	}

	inline void MixFloat(uint64_t &hash, float value) {
		// Hash canonical bits so signed zero and NaN remain stable.
		uint32_t bits;
		std::memcpy(&bits, &value, sizeof(bits));
		MixBits(hash, bits);
	}

	inline void MixVec3(uint64_t &hash, const glm::vec3 &value) {
		MixFloat(hash, value.x);
		MixFloat(hash, value.y);
		MixFloat(hash, value.z);
	}

	inline void MixPointer(uint64_t &hash, const void *pointer) {
		MixBits(hash, (uint64_t)(uintptr_t)pointer);
	}
}
